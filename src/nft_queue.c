/*******************************************************************************
 * (C) Xenadyne Inc, 2001-2013.	All Rights Reserved
 *
 * Permission to use, copy, modify and distribute this software for
 * any purpose and without fee is hereby granted, provided that the
 * above copyright notice appears in all copies.
 *
 * XENADYNE INC DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL XENADYNE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM THE
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 ************************************************************************
 *
 * File: nft_queue.c
 *
 * Description:
 *
 * Provides synchronized message queues for use in threaded applications.
 * Please refer to nft_queue.h for a detailed description of the package.
 * Usage is illustrated by the unit test below (see #ifdef MAIN).
 *
 *******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#include <unistd.h>
#endif

#include <nft_queue.h>

NFT_DEFINE_WRAPPERS(nft_queue,);

/* Here are some macros to help us manage the circular array.
 *
 * An empty queue is indicated by setting q->first = -1,
 * and this also requires that q->next = 0. The NEXT macro
 * will work correctly on an empty queue, but the PREV macro
 * must not be used on an empty queue.
 *
 * Also note the distinction between FULL(q) and LIMIT(q).
 */
#define EMPTY(q) (q->first < 0)
#define FULL(q)  (q->next == q->first)
#define NEXT(i)  ((i + 1) % q->size)
#define PREV(i)  ((i + q->size - 1) % q->size)
#define COUNT(q) (EMPTY(q) ? 0 : ((q->next + ((q->next <= q->first) ? q->size : 0)) - q->first))
#define LIMIT(q) ((q->limit > 0) && (COUNT(q) >= q->limit))

/* The queue initially points array to minarray[], grows by doubling,
 * and shrinks by halving. The array pointer is redirected to malloc
 * memory when it grows beyond NFT_QUEUE_MIN_SIZE.
 *
 * These macros define when to grow and shrink the queue's array.
 * We shrink the array by halves, but only when the count
 * has dropped to a quarter of the size, to reduce thrashing.
 */
#define GROW(q)   (FULL(q) && (!q->limit || (q->size < q->limit)))
#define SHRINK(q) ((COUNT(q) < (q->size/4)) && (NFT_QUEUE_MIN_SIZE <= (q->size/2)))

// When the queue has been shutdown, the shutdown flag is nonzero:
//    0 => queue is active
//    1 => queue is shutting down
//    2 => queue will be destroyed
//
#define SHUTDOWN(q) (0 != q->shutdown)

#define VALIDATE(q) assert(q->first < q->size);\
                    assert(q->next  < q->size);\
                    assert((q->first != -1) || (q->next == 0))

/*----------------------------------------------------------------------
 *  queue_grow() - Allocate more space for q->array.
 *		   Returns 0 on success, else ENOMEM on malloc failure.
 *----------------------------------------------------------------------
 */
static int
queue_grow(nft_queue * q)
{
    VALIDATE(q);
    assert(GROW(q));
#ifndef NDEBUG
    int count = COUNT(q);
#endif
    // Double the size of the array - the logic below assumes this.
    // Don't let new size exceed max signed integer.
    int nsize = 2 * q->size;
    if (nsize < 0) return ENOMEM;

    // This has to be void** for pointer arithmetic to work!
    void ** new;
    if (q->array == q->minarray) {
        if ((new = malloc(nsize * sizeof(void*)))) {
	    memset(new,        0,   nsize * sizeof(void*));
	    memcpy(new, q->array, q->size * sizeof(void*));
	}
    }
    else {
	new = realloc(q->array, nsize * sizeof(void*));
    }
    if (!new) return ENOMEM;

    /* If the items are "wrapped" we need to move the tail (the part of the
     * queue that wrapped around to the start of array) to the end of the head.
     * We are sure the new queue won't wrap, since we have doubled the array size.
     */
    if (q->next <= q->first)
    {
	// Copy the items in new[0...next-1] to new[size..(size+next-1)].
	memcpy(new + q->size, new, q->next * sizeof(void*));
	q->next += q->size;
    }
    q->array = new;
    q->size  = nsize;
    assert(COUNT(q) == count);
    VALIDATE(q);

    return 0;
}


/*----------------------------------------------------------------------
 *  queue_shrink() - Reduce the space allocated for the array.
 *----------------------------------------------------------------------
 */
static void
queue_shrink(nft_queue * q)
{
    VALIDATE(q);
    assert(SHRINK(q));

    int count = COUNT(q);

    if (!EMPTY(q))
    {
	/* Rearrange the queue items to fit in the smaller area.
	 * If the items aren't wrapped, move them to the start of array.
	 */
	if (q->first < q->next)	{
	    memmove(q->array, q->array + q->first, count * sizeof(void*));
	}
	else {
	    /* When wrapped, shift the tail back, and the head to the front of array.
	     * This is safe because the queue is less than one quarter full.
	     */
	    memmove(q->array + (q->size - q->first), q->array, q->next   * sizeof(void*));
	    memmove(q->array,  q->array + q->first, (q->size - q->first) * sizeof(void*));
	}
	q->first = 0;
	q->next  = count;
    }
    int nsize = q->size / 2;
    if (nsize == NFT_QUEUE_MIN_SIZE) {
	memcpy(q->minarray, q->array, nsize * sizeof(void*));
	free(q->array);
	q->array = q->minarray;
    }
    else {
	q->array = realloc(q->array, nsize * sizeof(void*));
    }
    q->size = nsize;

    assert(COUNT(q) == count);
    VALIDATE(q);
}

/*----------------------------------------------------------------------
 *  queue_cleanup() 	- cancellation cleanup handler.
 *
 *  This handler is called when a pool thread is cancelled
 *  while blocked in queue_wait() or nft_queue_shutdown().
 *----------------------------------------------------------------------
 */
static void
queue_cleanup(void * arg)
{
    nft_queue * q = nft_queue_cast(arg); assert(q);
    pthread_mutex_unlock(&q->mutex);
    nft_queue_discard(q);
}

/*----------------------------------------------------------------------
 *  queue_wait() - Wait to enqueue or dequeue an item.
 *
 *  This function is only called when dequeuing from an empty queue,
 *  or enqueueing to a queue that has reached its limit. This call
 *  will not block if the queue has been shutdown.
 *
 *  The caller MUST hold the queue mutex while calling queue_wait.
 *  This function may block in pthread_cond_wait or _timedwait,
 *  which are thread-cancellation points. It is cancellation-safe,
 *  by virtue of the queue_cleanup function defined above.
 *
 *  Returns zero 	- on success.
 *  	    ETIMEDOUT	- the timeout period expired
 *          ESHUTDOWN   - the queue was shut down.
 *----------------------------------------------------------------------
 */
static int
queue_wait(nft_queue * q, int timeout)
{
    if (SHUTDOWN(q)) return ESHUTDOWN;

    // If the queue is empty, we'll wait for it to become non-EMPTY.
    // Otherwise, we assume it is full, and wait to become non-LIMIT.
    //
    assert(EMPTY(q) || LIMIT(q));
    int empty  = EMPTY(q);
    int result = 0;

    // Push a cancellation cleanup handler in case we get cancelled.
    pthread_cleanup_push(queue_cleanup, q);

    // If timeout is positive, do a timed wait, else wait indefinitely.
    if (timeout > 0) {
	struct timespec abstime = nft_gettime();
	abstime.tv_sec += timeout;

	// pthread_cond_timed_wait returns ETIMEDOUT on timeout.
	while (!SHUTDOWN(q) && (empty ? EMPTY(q) : LIMIT(q)))
	    if ((result = pthread_cond_timedwait(&q->cond, &q->mutex, &abstime)) != 0)
		break;
	assert(result == 0 || result == ETIMEDOUT);
    }
    else if (timeout < 0) { // Wait indefinitely.
	while (!SHUTDOWN(q) && (empty ? EMPTY(q) : LIMIT(q)))
	    if ((result = pthread_cond_wait(&q->cond, &q->mutex)) != 0)
		break;
	assert(result == 0);
    }
    pthread_cleanup_pop(0); // Pop cleanup without executing it.

    if (SHUTDOWN(q)) result = ESHUTDOWN;
    return result;
}

/*----------------------------------------------------------------------
 *  nft_queue_enqueue()	- Enqueue an item.
 *
 *  This function can append an item to the end of the list,
 *  or push the item onto the front of the list, depending on the
 *  'which' parameter, which can be 'L' for LIFO, or 'F' for FIFO.
 *
 *  Do NOT use this function unless you know what you are doing.
 *  The caller MUST hold the queue mutex while calling _enqueue.
 *  This call should only be used in subclasses, such as nft_pool.
 *
 *  This function calls queue_wait, which is a cancellation point.
 *
 *  Returns:	zero		On success
 *		ENOMEM  	Memory exhausted
 *		ETIMEDOUT	Operation timed out
 *		ESHUTDOWN	Queue has been shutdown
 *
 *----------------------------------------------------------------------
 */
int
nft_queue_enqueue(nft_queue * q,  void * item,  int timeout, char which)
{
    assert((which ==  'L') || (which == 'F')); // L for LastInFirstOut, F for FirstInFirstOut
    int result = 0;

    /* If a limit is set, and the limit has been reached,
     * and timeout is nonzero, wait for an item to be popped.
     */
    if (LIMIT(q) && timeout) queue_wait(q, timeout);

    // The queue may have been shutdown while we were waiting.
    // Do not permit enqueues after the queue has been shutdown.
    if (!SHUTDOWN(q))
    {
	// If the wait timed out, the list may still be at limit.
	if (!LIMIT(q))
	{
	    // Attempt to grow the queue if necessary.
	    if (GROW(q) && ((result = queue_grow(q)) != 0)) return result;

	    assert((q->first != -1) || (q->next == 0)); // Are first and next consistent?

	    // LIFO and FIFO work the same when the queue is empty.
	    if (EMPTY(q)) {
		q->array[0] = item;
		q->first    = 0;
		q->next     = 1;

		/* Threads may be waiting in nft_queue_dequeue, so wake them.
		 * Since the condition is shared between _enqueue and _dequeue
		 * threads, we need to do a broadcast. You might think it not
		 * possible for threads to be blocked in _enqueue and _dequeue
		 * simultaneously, but it can happen, and if our signal is not
		 * given to a thread waiting to dequeue, the signal is lost.
		 */
		int rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);
	    }
	    else if ('L' == which) {
		q->array[q->next] = item;
		q->next = NEXT(q->next);
	    }
	    else if ('F' == which) {
		q->first = PREV(q->first);
		q->array[q->first] = item;
	    }
	}
	else return ETIMEDOUT;
    }
    else return ESHUTDOWN;

    return result;
}

/*----------------------------------------------------------------------
 *  nft_queue_dequeue()	- Dequeue an item.
 *
 *  This function removes an item from the head of the list.
 *
 *  Do NOT use this function unless you know what you are doing.
 *  The caller MUST hold the queue mutex while calling _dequeue.
 *  This call should only be used in subclasses, such as nft_pool.
 *  This function calls queue_wait, which is a cancellation point.
 *
 *  The first item in the queue is popped and written to *item,
 *  and a NULL will be written to *item if the queue is empty.
 *
 *  Returns:	zero		On success
 *		EINVAL  	Queue or itemp is invalid.
 *		ETIMEDOUT	Operation timed out
 *		ESHUTDOWN	Queue has been shutdown
 *----------------------------------------------------------------------
 */
int
nft_queue_dequeue(nft_queue * q, int timeout, void ** itemp)
{
    if (!q || !itemp) return EINVAL;

    assert((q->first != -1) || (q->next == 0));

    *itemp = NULL;

    // If the queue is empty and timeout is set, wait for an enqueue.
    if (EMPTY(q) && timeout) queue_wait(q, timeout);

    // If the wait timed out, or the queue was shut down, the list may be empty.
    if (!EMPTY(q))
    {
	/* If there could be threads blocked in nft_queue_enqueue,
	 * wake them now. Since the condition is shared between _enqueue
	 * and _dequeue threads, we need to do a broadcast.
	 */
	if (LIMIT(q)) {
	    int rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);
	}
	// Pop the first item in the queue.
	*itemp   = q->array[q->first];
	q->first = NEXT(q->first);

	// If the queue appears to be FULL, set queue to empty state.
	if (FULL(q)) {
	    q->first = -1;
	    q->next  =  0;

	    // If the queue is being shutdown, wake waiting threads.
	    if (SHUTDOWN(q)) {
		int rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);
	    }
	}
	// If the queue is less than one quarter full, shrink it by half.
	if (!SHUTDOWN(q) && SHRINK(q)) queue_shrink(q);

	return 0;
    }
    else if (SHUTDOWN(q))
	return ESHUTDOWN;
    else
	return ETIMEDOUT;
}

/*----------------------------------------------------------------------
 *  nft_queue_destroy()
 *
 *  Destroy the list and any queued items if the destroyer is set.
 *----------------------------------------------------------------------
 */
void
nft_queue_destroy(nft_core * p)
{
    nft_queue * q = nft_queue_cast(p); assert(q);
    if (!q) return;

    int rc = pthread_mutex_destroy(&q->mutex); assert(rc == 0);
    rc     = pthread_cond_destroy (&q->cond);  //FIXME assert(rc == 0);

    // Free array only if it points to malloced memory.
    if (q->array != q->minarray) free(q->array);

    nft_core_destroy(p);
}

/*----------------------------------------------------------------------
 * nft_queue_create()
 *
 * Allocate the base struct which contains head and tail list pointers,
 * and various other data. Returns queue pointer to the caller,
 * or NULL if malloc fails.
 *
 * If limit is negative, it will be set to NFT_QUEUE_MIN_SIZE.
 * This will cause attempts to enqueue more than NFT_QUEUE_MIN_SIZE items
 * to block, rather than call malloc() to expand the queue.
 * So, if you wish to avoid having the queue malloc or realloc,
 * simply pass -1 as size, and adjust NFT_QUEUE_MIN_SIZE to suit your needs.
 * If limit is zero, the queue may grow without limit.
 *
 * This returns the queue handle, but does not discard the reference
 * that was returned from nifty_core_create, so the queue reference
 * count will remain nonzero, until nft_queue_shutdown is called.
 *----------------------------------------------------------------------
 */
nft_queue *
nft_queue_create(const char * class,
		 size_t       size,
		 int          limit)
{
    nft_queue * q = nft_queue_cast(nft_core_create(class, size));
    if (!q) return NULL;

    // Override the nft_core destructor with our own dtor.
    q->core.destroy = nft_queue_destroy;
    q->limit = (limit < 0) ? NFT_QUEUE_MIN_SIZE : limit ;
    q->array = q->minarray;
    q->size  = NFT_QUEUE_MIN_SIZE;
    q->first = -1;
    q->next  = 0;
    q->shutdown = 0;

    int rc;
    if ((rc = pthread_mutex_init(&q->mutex, NULL)) ||
	(rc = pthread_cond_init (&q->cond,  NULL))  )
    {	// mutex or cond initialization failed.
	assert(rc == 0);
	nft_queue_discard(q);
	return NULL;
    }
    return q;
}

/*----------------------------------------------------------------------
 * nft_queue_new()
 *
 * Like nft_queue_create, with simpler parameters,
 * and returning a nft_queue_h handle instead of nft_queue *.
 *----------------------------------------------------------------------
 */
nft_queue_h
nft_queue_new(int limit)
{
    return nft_queue_handle(nft_queue_create(nft_queue_class, sizeof(nft_queue), limit));
}

/*----------------------------------------------------------------------
 *  nft_queue_add_wait() - Add one item to the end of the queue.
 *
 *  This function will wait for up to timeout seconds if the queue
 *  limit has been reached, or indefinitely when timeout is -1.
 *  The queue limit is ignored if timeout is zero.
 *
 *  Returns zero on success, otherwise:
 *  EINVAL	- not a valid queue
 *  ENOMEM	- malloc failed
 *  ETIMEDOUT	- timeout reached
 *  ESHUTDOWN	- queue has been shutdown
 *----------------------------------------------------------------------
 */
int
nft_queue_add_wait(nft_queue_h h,  void * item,  int timeout)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) return EINVAL;

    int rc     = pthread_mutex_lock(&q->mutex); assert(rc == 0);
    int result = nft_queue_enqueue(q, item, timeout, 'L');
    rc = pthread_mutex_unlock(&q->mutex); assert(rc == 0);

    nft_queue_discard(q);
    return result;
}
int
nft_queue_add(nft_queue_h h, void * item)
{
    return nft_queue_add_wait(h, item, -1);
}

/*----------------------------------------------------------------------
 *  nft_queue_push_wait() - Add one item to the front of the queue.
 *
 *  This function will wait for up to timeout seconds if the queue
 *  limit has been reached, or indefinitely when timeout is -1.
 *  The queue limit is ignored if timeout is zero.
 *
 *  Returns zero on success, otherwise:
 *  EINVAL	- not a valid queue.
 *  ENOMEM	- malloc failed
 *  ETIMEDOUT	- timeout reached
 *  ESHUTDOWN	- queue has been shutdown
 *----------------------------------------------------------------------
 */
int
nft_queue_push_wait(nft_queue_h h,  void * item,  int timeout)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) return EINVAL;

    int rc     = pthread_mutex_lock(&q->mutex); assert(rc == 0);
    int result = nft_queue_enqueue(q, item, timeout, 'F');
    rc = pthread_mutex_unlock(&q->mutex); assert(rc == 0);

    nft_queue_discard(q);
    return result;
}
int
nft_queue_push(nft_queue_h h, void * item)
{
    return nft_queue_push_wait(h, item, -1);
}

/*----------------------------------------------------------------------
 *  nft_queue_pop_wait_ex() - Remove and return the head item on the queue.
 *
 *  If the queue is empty, this call will block for up to timeout
 *  seconds, and return NULL if no item is queued in that time,
 *  or if the queue shuts down while waiting.  Blocks indefinitely
 *  if the timeout is -1.
 *----------------------------------------------------------------------
 */
int
nft_queue_pop_wait_ex(nft_queue_h h, int timeout, void ** itemp)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) {
	*itemp = NULL;
	return EINVAL;
    }
    int rc     = pthread_mutex_lock(&q->mutex); assert(rc == 0);
    int result = nft_queue_dequeue(q, timeout, itemp);
    rc = pthread_mutex_unlock(&q->mutex); assert(rc == 0);

    nft_queue_discard(q);
    return result;
}

/*----------------------------------------------------------------------
 *  nft_queue_pop_wait() - Remove and return the head item on the queue.
 *
 *  If the queue is empty, this call will block for up to timeout
 *  seconds, and return NULL if no item is queued in that time,
 *  or if the queue shuts down while waiting.  Blocks indefinitely
 *  if the timeout is -1.
 *----------------------------------------------------------------------
 */
void *
nft_queue_pop_wait(nft_queue_h h, int timeout)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) return NULL;

    void * item = NULL;
    int    rc   = pthread_mutex_lock(&q->mutex); assert(rc == 0);
    nft_queue_dequeue(q, timeout, &item);
    rc = pthread_mutex_unlock(&q->mutex); assert(rc == 0);

    nft_queue_discard(q);
    return item;
}
void *
nft_queue_pop(nft_queue_h h)
{
    return nft_queue_pop_wait(h, -1);
}

/*----------------------------------------------------------------------
 *  nft_queue_shutdown()
 *
 *  Increment the shutdown flag and awaken all blocked threads.
 *  Returns when the queue is empty, or the timeout expires.
 *  On ETIMEDOUT, the queue is not empty, and the original reference
 *  has not been discarded, so the queue will not be freed.
 *
 *  Returns zero 	- on success.
 *  	    EINVAL	- not a valid queue.
 *          ETIMEDOUT   - queue has already been shutdown.
 *----------------------------------------------------------------------
 */
int
nft_queue_shutdown(nft_queue_h h, int timeout)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) return EINVAL;

    int result = 0;
    int rc     = pthread_mutex_lock(&q->mutex); assert(rc == 0);

    // This cleanup handler frees the queue mutex and discards the queue reference.
    pthread_cleanup_push(queue_cleanup, q);

    // Note that shutdown may be called more than once.
    if (!SHUTDOWN(q))
    {
	// Flag that the queue is being shutdown,
	// and waken any threads that are blocked in queue_wait().
	q->shutdown = 1;
	rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);
    }
    if (timeout && !EMPTY(q))
    {
	// Did the caller ask to wait until shutdown is complete?
	// If timeout is positive, do a timed wait, else wait indefinitely.
	if (timeout > 0) {
	    struct timespec abstime = nft_gettime(); abstime.tv_sec += timeout;
	    while (!EMPTY(q))
		if ((result = pthread_cond_timedwait(&q->cond, &q->mutex, &abstime)) != 0)
		    break;
	    // pthread_cond_timed_wait returns ETIMEDOUT on timeout.
	    assert(result == 0 || result == ETIMEDOUT);
	}
	else if (timeout < 0) {
	    while (!EMPTY(q))
		if ((result = pthread_cond_wait(&q->cond, &q->mutex)) != 0)
		    break;
	    assert(result == 0);
	}
    }
    if (EMPTY(q)) {
	if (q->shutdown++ == 1) {
	    // This is an "extra" discard, to cancel the initial reference from
	    // when the queue was created, which enables the queue to be destroyed,
	    // when the last reference is discarded.
	    nft_queue_discard(q);
	}
	result = 0;
    }
    else {
	// Return this even if timeout was zero, to indicate that the queue is not empty.
	result = ETIMEDOUT;
    }
    pthread_cleanup_pop(1); // executes queue_cleanup

    return result;
}

/*----------------------------------------------------------------------
 *  nft_queue_count  -	Return the number of items in the queue.
 *			Returns -1 for an invalid handle.
 *----------------------------------------------------------------------
 */
int
nft_queue_count( nft_queue_h h)
{
    int    result = -1;
    nft_queue * q = nft_queue_lookup(h);
    if (q) {
	pthread_mutex_lock(&q->mutex);
	result = COUNT(q);
	pthread_mutex_unlock(&q->mutex);
	nft_queue_discard(q);
    }
    return result;
}

/*----------------------------------------------------------------------
 *  nft_queue_peek  -	Return the first item on the queue.
 *			Returns NULL on empty queue or invalid handle.
 *----------------------------------------------------------------------
 */
void *
nft_queue_peek( nft_queue_h h)
{
    void * result = NULL;
    nft_queue * q = nft_queue_lookup(h);
    if (q) {
	pthread_mutex_lock(&q->mutex);
	if (!EMPTY(q)) result = q->array[q->first];
	pthread_mutex_unlock(&q->mutex);
	nft_queue_discard(q);
    }
    return result;
}

/*----------------------------------------------------------------------
 *  nft_queue_state
 *
 *  Returns:	zero		Queue is in operation.
 *		EINVAL  	Queue handle is invalid.
 *		ESHUTDOWN	Queue has been shutdown
 *----------------------------------------------------------------------
 */
int
nft_queue_state( nft_queue_h h)
{
    nft_queue * q = nft_queue_lookup(h);
    int    result = EINVAL;
    if (q) {
	result = SHUTDOWN(q) ? ESHUTDOWN : 0 ;
	nft_queue_discard(q);
    }
    return result;
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		QUEUE PACKAGE UNIT TEST				*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Assertions must be active in test code.
#endif
#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#include <nft_string.h>

/* Strings are used for the simple tests.
 */
static char * Strings[] =
{
    "and",	"the",	"shorter",	"of",	"the",	"porter's",	"daughters",
    "dips",	"her",	"hand",		"in",	"the",	"deadly",	"waters",
    NULL
};

static void t1( void);
static void t2( void);
static void t3( void);
static void t4( void);
static void t5( void);
static void t6( void);
static void t7( void);


#define BUFFSZ		120
#define NUM_WORKERS	3
#define Q_LIMIT		32

nft_queue_h	input_Q  = NULL;
nft_queue_h	output_Q = NULL;

int		countin   = 0;
int		countout  = 0;

pthread_t	input_thread  = 0;
pthread_t	output_thread = 0;
pthread_t	worker_threads[NUM_WORKERS];

/* This is spawned as a single thread that reads lines from
 * standard input, and passes them to the message queue.
 */
static void *
poll_input(void * arg)
{
    nft_queue_h q  = arg;
    long        rc = -1;
    char        buff[BUFFSZ];
    while (fgets(buff, sizeof(buff), stdin))
    {
	// Newly-created objects have a reference count of one,
	// so we could safely place this object on the queue.
	// The only reason for placing its handle into the queue
	// instead, is to add stress to the handle subsystem,
	// by making the workers call _lookup/_discard.
	//
	nft_string * object = nft_string_new(buff);
	if ((rc = nft_queue_add(q, object->core.handle))) break;
	countin++;
    }
    fputs("input thread done\n", stderr);
    return (void*) rc;
}

/* Multiple worker threads are spawned that read messages
 * from the raw message queue, convert them to upper case,
 * and add them to the output queue.
 */
static void *
worker_thread(void * index)
{
    long         rc = -1;
    nft_string_h handle;
    while ((handle = nft_queue_pop(input_Q))) {
	nft_string * object = nft_string_lookup(handle);
	if (object) {
	    for (char * s = object->string; (*s = toupper(*s));  s++);
	    nft_string_discard(object);
	}
	if ((rc = nft_queue_add(output_Q, handle))) break;
    }
    fprintf(stderr, "worker_thread[%ld] done\n", (long) index);
    return (void*) rc;
}

/* This function is spawned as a single thread that reads messages
 * from the output queue and prints them to standard output.
 */
static void *
poll_output(void * arg)
{
    nft_queue_h  q  = arg;
    long         rc = -1;
    nft_string_h handle;

    // Illustrate use of the _pop_wait_ex call, to pop items until the queue is shut down.
    while (!(rc = nft_queue_pop_wait_ex(q, -1, (void**) &handle))) {
	nft_string * object = nft_string_lookup(handle);
	if (object) {
	    // We call discard twice - first to balance the _lookup we just did,
	    // and again to discard the original reference created by poll_input().
	    nft_string_discard(object);
	    nft_string_discard(object);
	}
	countout++;
    }
    assert(ESHUTDOWN == rc);
    fputs("output thread done\n", stderr);
    return (void*) rc;
}

/* Timing stuff.
 */
struct timespec mark, done;
#define MARK	mark = nft_gettime()
#define TIME	done = nft_gettime()
#define ELAPSED ((done.tv_sec * 1.0 + done.tv_nsec * 0.000000001) - \
                 (mark.tv_sec * 1.0 + mark.tv_nsec * 0.000000001)   )

int
main()
{
    pthread_attr_t attr;
    long rc, i;

    // First, do the basic single-threaded tests.
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();

    /* Multithreaded test - best run on a multi-core host.
     *
     * In this test, we create a work pipeline consisting of two queues.
     * The input thread reads lines of text from standard input,
     * and adds these strings to the input queue.
     *
     * A number of worker threads pop strings from the input queue,
     * convert them to upper case, and add them to the output queue.
     * An output thread pops strings from the output queue and
     * prints them to standard output.
     *
     * Note that if you want to set up something like this,
     * you should look at the nft_pool package. nft_pool
     * implements the input_Q-plus-worker-threads part of
     * this demo.
     */
    fputs("multithread test, reading from stdin...\n", stderr);
    MARK; // record start time

    // Create the input and output queues.
    input_Q  = nft_queue_new(Q_LIMIT);
    output_Q = nft_queue_new(Q_LIMIT);

    // Create the input, output, and worker threads.
    rc = pthread_attr_init(&attr); assert(!rc);
    rc = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); assert(!rc);
    rc = pthread_create(&input_thread,  NULL, poll_input,  input_Q); assert(!rc);
    rc = pthread_create(&output_thread, NULL, poll_output, output_Q); assert(!rc);
    for (i = 0; i < NUM_WORKERS; i++)
	pthread_create(&worker_threads[i], NULL, worker_thread, (void *) i);
    rc = pthread_attr_destroy(&attr); assert(!rc);
    assert(0 == nft_queue_state(input_Q));
    assert(0 == nft_queue_state(output_Q));

    // Join with the input thread.
    void * thread_return;
    rc = pthread_join(input_thread, &thread_return); assert(!rc);
    assert(thread_return == NULL);

    // Shutdown pipeline - there may still be enqueued items.
    // Worker threads will continue to pop and process items,
    // and this call waits until the input queue is empty.
    rc = nft_queue_shutdown(input_Q, -1); assert(rc == 0);

    // Now join with the worker threads.
    for (i = 0; i < NUM_WORKERS; i++) {
	rc = pthread_join(worker_threads[i], &thread_return);  assert(rc == 0);
	assert(thread_return == NULL);
    }
    // The input queue should have been destroyed now.
    assert(EINVAL == nft_queue_state(input_Q));

    // Shutdown the output queue without waiting. This could succeed
    // or return ETIMEDOUT, depending on thread scheduling.
    rc = nft_queue_shutdown(output_Q, 0); assert(rc == 0 || rc == ETIMEDOUT);

    // The output queue may be in the process of shutting down, or destroyed.
    rc = nft_queue_state(output_Q); assert(rc == ESHUTDOWN || rc == EINVAL);

    // Wait for the output thread to finish emptying the output queue.
    rc = pthread_join(output_thread, &thread_return);  assert(rc == 0);
    assert((long)thread_return == ESHUTDOWN);

    // Now shut it down again. If it failed the first time, this should succeed,
    // but if it succeeded on the first try, this should return EINVAL.
    rc = nft_queue_shutdown(output_Q, 0); assert(rc == 0 || rc == EINVAL);

    TIME; // compute elapsed time
    fprintf(stderr, "words in: %d	words out: %d	elapsed: %.3f\n", countin, countout, ELAPSED);
    assert(countin == countout);

    fprintf(stderr, "nft_queue: All tests passed.\n");
    exit(0);
}

/*
 * t1 - Test _create, _shutdown.
 */
static void
t1( void)
{
    fprintf(stderr, "t1 (create/shutdown): ");

    nft_queue_h q = nft_queue_new(0);
    assert(q);

    for (int i = 0 ; Strings[ i] != NULL ; i++)
	assert(0 == nft_queue_add( q, strdup(Strings[i])));

    // You should always follow this pattern when shutting down a queue.
    //
    if (ETIMEDOUT == nft_queue_shutdown(q, 1)) {
	void * str;
	while ((str = nft_queue_pop_wait(q, 0))) free(str);
	nft_queue_shutdown(q, 0);
    }
    assert(EINVAL == nft_queue_shutdown(q, 0));

    fprintf(stderr, "passed.\n");
}


/*
 * t2 - Test add/push/pop operations.
 */
static void
t2( void)
{
    void * ss;
    fprintf(stderr, "t2 (add/push/pop): ");

    // Create an unlimited queue
    nft_queue_h q = nft_queue_new(0);

    // With the queue empty, test the pop timeout.
    assert(ETIMEDOUT == nft_queue_pop_wait_ex(q, 0, &ss));
    assert(ETIMEDOUT == nft_queue_pop_wait_ex(q, 1, &ss));

    // Test add/pop.
    for (int i = 0 ; Strings[i] != NULL ; i++) {
	int rc = nft_queue_add( q, strdup(Strings[i])); assert(rc == 0);
    }
    for (int i = 0; ((ss = nft_queue_pop_wait(q, 0)) != NULL); i++) {
	assert(!strcmp(ss, Strings[i]));
	free(ss);
    }

    // Test push/pop.
    for (int i = 0 ; Strings[i] != NULL ; i++) {
	int rc = nft_queue_push( q, strdup(Strings[i])); assert(rc == 0);
    }
    for (int i = nft_queue_count(q); i > 0; i--) {
	int rc = nft_queue_pop_wait_ex(q, 0, &ss);
	assert(0    == rc);
	assert(NULL != ss);
	assert(!strcmp(ss, Strings[i-1]));
	free(ss);
    }
    assert(0 == nft_queue_shutdown(q,0));
    fprintf(stderr, " passed.\n");
}

/*
 * t3 - Test more API calls.
 */
static void
t3( void)
{
    void * ss;
    int    rc;
    fprintf(stderr, "t3 (count/peek/state):");

    // Create an unlimited queue.
    nft_queue_h q = nft_queue_new(0);

    // Test the _add(), _count() and _peek() operations.
    int   i;
    for ( i = 0 ; Strings[ i] != NULL ; i++) {
	rc = nft_queue_add( q, strdup( Strings[ i]));
	assert(rc == 0);
	assert(nft_queue_count(q) == (i + 1));
    }
    assert(strcmp(nft_queue_peek(q), Strings[0]) == 0);
    assert(0 == nft_queue_state(q));

    // Shutdown returns ETIMEDOUT to indicate the the queue was not empty.
    assert(ETIMEDOUT == nft_queue_shutdown(q, 0));
    assert(ESHUTDOWN == nft_queue_state(q));

    // The queue handle is still valid, so clear the queue.
    // Once the queue has been shutdown, _pop will not block.
    while ((ss = nft_queue_pop(q))) free(ss);

    // Shutdown on the empty queue will return success.
    assert(0 == nft_queue_shutdown(q, 0));

    // The queue has been destroyed.
    assert(EINVAL == nft_queue_state(q));

    fprintf(stderr, "passed.\n");
}

static void *
add_thread(void * arg)
{
    nft_queue_h  q = arg;
    return (void*)(intptr_t) nft_queue_add(q, "second");
}

static void *
pop_thread(void * arg)
{
    nft_queue_h  q = arg;
    void * item;
    return (void*)(intptr_t) nft_queue_pop_wait_ex(q, -1, &item);
}

/*
 * t4 - Test queue shutdown during add.
 */
static void
t4( void)
{
    fprintf(stderr, "t4 (add/shutdown): ");

    // Create a queue that is limited to one item.
    nft_queue_h q = nft_queue_new(1);
    nft_queue_add(q, "first");

    // The queue limit is one, so this add should timeout immediately.
    assert(ETIMEDOUT == nft_queue_add_wait(q, "", 0));

    // The queue limit is one, so this thread will block.
    pthread_t th;
    int rc = pthread_create(&th, 0, add_thread, q); assert(0 == rc);
    sleep(1);

    // Shut down the queue, and join the add_thread.
    assert(ETIMEDOUT == nft_queue_shutdown(q, 0));

    // Join with add_thread.
    // nft_queue_add or _push return ESHUTDOWN
    // if the queue shuts down while they are waiting.
    void * value;
    rc = pthread_join(th, &value); assert(0 == rc);
    assert(ESHUTDOWN == (long) value);

    // Shutdown succeeds after we pop the item.
    assert(strcmp("first",nft_queue_pop_wait(q, 0)) == 0);
    assert(0       == nft_queue_shutdown(q, 0));

    fprintf(stderr, "passed.\n");
}

/*
 * t5 - Test queue shutdown during pop.
 */
static void
t5( void)
{
    fprintf(stderr, "t5 (pop/shutdown): ");

    nft_queue_h q = nft_queue_new(1); assert(q);

    // The queue is empty, so the thread will block.
    pthread_t th;
    int rc = pthread_create(&th, 0, pop_thread, q); assert(0 == rc);
    sleep(1);

    // Shut down the queue.
    assert(0 == nft_queue_shutdown(q, 0));

    // Join the pop_thread.
    // nft_queue_pop returns NULL on timeout, invalid queue, etc.
    void * value;
    rc = pthread_join(th, &value); assert(0 == rc);
    assert(ESHUTDOWN == (long) value);

    fprintf(stderr, "passed.\n");
}


/*
 * t6 - Test cancellation during add.
 */
static void
t6( void)
{
#ifndef _WIN32	// no pthread_cancel() on WIN32

    fprintf(stderr, "t6 (add/cancel): ");

    nft_queue_h q = nft_queue_new(1);
    nft_queue_add(q, "first");

    // The queue limit is one, so the add_thread will block.
    pthread_t th;
    int rc = pthread_create(&th, 0, add_thread, q); assert(0 == rc);
    sleep(1);

    // Cancel and join the add_thread.
    void * value;
    rc = pthread_cancel(th); assert(0 == rc);
    rc = pthread_join(th, &value); assert(0 == rc);
    assert(PTHREAD_CANCELED == value);

    // We should still be able to pop the first item.
    assert(strcmp("first", nft_queue_pop(q)) == 0);

    // The queue should be empty.
    assert(0 == nft_queue_shutdown(q,0));

    fprintf(stderr, "passed.\n");
#endif // _WIN32
}


/*
 * t7 - Test cancellation during pop.
 */
static void
t7( void)
{
#ifndef _WIN32	// no pthread_cancel() on WIN32

    fprintf(stderr, "t7 (pop/cancel): ");

    nft_queue_h q = nft_queue_new(1);

    // The queue is empty, so pop_thread will block.
    pthread_t    th;
    int rc = pthread_create(&th, 0, pop_thread, q); assert(0 == rc);
    sleep(1);
    assert(nft_queue_pop_wait(q, 0) == NULL);

    // Cancel and join the pop_thread.
    void * value;
    rc = pthread_cancel(th); assert(0 == rc);
    rc = pthread_join(th, &value); assert(0 == rc);
    assert(PTHREAD_CANCELED == value);

    // Another pop should timeout immediately.
    void * item;
    assert(ETIMEDOUT == nft_queue_pop_wait_ex(q, 0, &item));

    // Shutdown should succeed immediately.
    assert(0 == nft_queue_shutdown(q, 1));

    fprintf(stderr, "passed.\n");
#endif // _WIN32
}

#endif // MAIN
