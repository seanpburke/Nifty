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
#ifndef WIN32
#include <strings.h>
#include <unistd.h>
#endif
#include <pthread.h>

#include <nft_core.h>
#include <nft_gettime.h>
#include <nft_queue.h>

NFT_DEFINE_HELPERS(nft_queue,);

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
#define VALID(q) (nft_queue_handle(q) != NULL)

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

/*----------------------------------------------------------------------
 * queue_validate - Validate the consistency of the queue.
 *----------------------------------------------------------------------
 */
static int
queue_validate( nft_queue * q)
{
    assert(q->first < q->size);
    assert(q->next  < q->size);
    assert((q->first != -1) || (q->next == 0));
    return 1;
}

/*----------------------------------------------------------------------
 *  queue_grow() - Allocate more space for q->array.
 *		   Returns 0 on success, else ENOMEM on malloc failure.
 *----------------------------------------------------------------------
 */
static int
queue_grow(nft_queue * q)
{
    assert(queue_validate(q));
    assert(GROW(q));

    int count = COUNT(q);

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

    return 0;
}


/*----------------------------------------------------------------------
 *  queue_shrink() - Reduce the space allocated for the array.
 *----------------------------------------------------------------------
 */
static void
queue_shrink(nft_queue * q)
{
    assert(queue_validate(q));
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
}


/*----------------------------------------------------------------------
 *  queue_cleanup() - cancellation cleanup handler.
 *
 *  This handler is called when a thread is cancelled while blocked
 *  on the queue condition variable. It is used in queue_wait() and
 *  nft_queue_shutdown.
 *----------------------------------------------------------------------
 */
static void
queue_cleanup(void * arg)
{
    nft_queue * q = nft_queue_cast(arg);
    assert(q);

    /* If queue had been shutdown while we were waiting, and we
     * are the last waiter, signal the nft_queue_shutdown() thread,
     * which waits until all remaining waiters have left the queue.
     */
    q->nwait--;
    if (!VALID(q) && (q->nwait == 0))
	pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    nft_queue_discard(q);
}

/*----------------------------------------------------------------------
 *  queue_wait() - Wait to enqueue or dequeue an item.
 *
 *  This function is only called when dequeuing from an empty queue,
 *  or enqueueing to a queue that has reached its limit.
 *
 *  This function may block in pthread_cond_wait or _timedwait,
 *  which are thread-cancellation points. It is cancellation-safe,
 *  by virtue of the queue_cleanup function defined above.
 *
 *  Returns zero 	- on success.
 *  	    ETIMEDOUT	- the timeout period expired
 *----------------------------------------------------------------------
 */
static int
queue_wait(nft_queue * q, int timeout)
{
    // If the queue is empty, we'll wait for it to become non-EMPTY.
    // Otherwise, we assume it is full, and wait to become non-LIMIT.
    //
    assert(EMPTY(q) || LIMIT(q));
    int empty = EMPTY(q);
    int result;

    q->nwait++; // We are waiting for an item to be queued.

    // Push a cancellation cleanup handler in case we get cancelled.
    pthread_cleanup_push(queue_cleanup, q);

    // If timeout is positive, do a timed wait, else wait indefinitely.
    if (timeout > 0) {
	struct timespec abstime = nft_gettime();
	abstime.tv_sec += timeout;

	// pthread_cond_timed_wait returns ETIMEDOUT on timeout.
	while (VALID(q) && (empty ? EMPTY(q) : LIMIT(q)))
	    if ((result = pthread_cond_timedwait(&q->cond, &q->mutex, &abstime)) != 0)
		break;
	assert(result == 0 || result == ETIMEDOUT);
    }
    else if (timeout < 0) { // Wait indefinitely.
	while (VALID(q) && (empty ? EMPTY(q) : LIMIT(q)))
	    if ((result = pthread_cond_wait(&q->cond, &q->mutex)) != 0)
		break;
	assert(result == 0);
    }
    pthread_cleanup_pop(0); // Pop cleanup without executing it.

    q->nwait--; // We are no longer waiting to dequeue.

    // If queue was shut down while we were waiting, and we are
    // the last waiter, signal the nft_queue_shutdown() thread.
    //
    if (!VALID(q) && (q->nwait == 0)) {
	int rc = pthread_cond_signal(&q->cond); assert(rc == 0);
    }
    return result;
}

/*----------------------------------------------------------------------
 * nft_queue_enqueue()	- Enqueue an item.
 *
 * This function can append an item to the end of the list,
 * or push the item onto the front of the list, depending on the
 * 'which' parameter, which can be 'L' for LIFO, or 'F' for FIFO.
 *
 * Do NOT use this function unless you know what you are doing.
 * The caller MUST hold the queue mutex while calling _enqueue.
 * This would be a static function, except that it is needed by
 * subclasses, such as nft_pool.
 *
 * This function calls queue_wait, which is a cancellation point.
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
    if (VALID(q))
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
	    }
	    else if ('L' == which) {
		q->array[q->next] = item;
		q->next = NEXT(q->next);
	    }
	    else if ('F' == which) {
		q->first = PREV(q->first);
		q->array[q->first] = item;
	    }
	    /* If threads are waiting in nft_queue_pop_wait, wake them.
	     * Since the condition is shared between append and pop threads,
	     * we need to do a broadcast.
	     */
	    if (q->nwait > 0) {
		int rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);
	    }
	}
	else return ETIMEDOUT;
    }
    else return EINVAL;

    return result;
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

    /* If the destroyer is set, apply it to each item in the queue.
     * The iteration logic is funky because when the queue is full,
     * q->first == q->next.
     */
    if (q->destroyer && !EMPTY(q))
	for (int idx = q->first; idx != q->next; idx = NEXT(idx))
	    q->destroyer(q->array[idx]);

    int rc = pthread_mutex_destroy(&q->mutex); assert(rc == 0);
    rc     = pthread_cond_destroy (&q->cond);  assert(rc == 0);

    // Free array only if it points to malloced memory.
    if (q->array != q->minarray) free(q->array);

    nft_core_destroy(p);
}

/*----------------------------------------------------------------------
 * nft_queue_create_f()
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
nft_queue_create_f(const char * class,
		   size_t       size,
		   int          limit,
		   void      (* destroyer)(void *))
{
    nft_queue * q = nft_queue_cast(nft_core_create(class, size));
    if (!q) return NULL;

    // Override the nft_core destructor with our own dtor.
    q->core.destroy = nft_queue_destroy;
    q->destroyer = destroyer;
    q->limit = (limit < 0) ? NFT_QUEUE_MIN_SIZE : limit ;
    q->array = q->minarray;
    q->size  = NFT_QUEUE_MIN_SIZE;
    q->first = -1;
    q->next  = 0;
    q->nwait = 0;

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
 *  nft_queue_add_wait() - Add one item to the end of the queue.
 *
 *  This function will wait for up to timeout seconds if the queue
 *  limit has been reached, or indefinitely when timeout is -1.
 *  The queue limit is ignored if timeout is zero.
 *
 *  Returns zero on success, otherwise:
 *  EINVAL	- not a valid queue.
 *  ENOMEM	- malloc failed
 *  ETIMEDOUT	- timeout reached
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
 *  nft_queue_pop_wait() - Remove and return the head item on the queue.
 *
 *  If the queue is empty, this call will block for up to timeout
 *  seconds, and return NULL if no item is queued in that time.
 *  Blocks indefinitely if the timeout is -1.
 *----------------------------------------------------------------------
 */
void *
nft_queue_pop_wait(nft_queue_h h, int timeout)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) return NULL;

    void * item = NULL;
    int rc = pthread_mutex_lock(&q->mutex); assert(rc == 0);

    assert((q->first != -1) || (q->next == 0));

    // If the queue is empty and timeout is set, Wait for an item to be queued.
    if (EMPTY(q) && timeout) queue_wait(q, timeout);

    // The queue may have been shutdown while we were waiting.
    if (VALID(q))
    {
	// If the wait timed out, the list may still be empty.
	if (!EMPTY(q))
	{
	    item     = q->array[q->first];
	    q->first = NEXT(q->first);

	    // If the queue appears to be FULL after popping this item, set queue to empty state.
	    if (FULL(q)) {
		q->first = -1;
		q->next  =  0;
	    }

	    /* If there could be threads blocked in nft_queue_add_wait(),
	     * wake them now. Since the condition is shared between append
	     * and pop threads, we need to do a broadcast.
	     */
	    if (q->nwait && q->limit) {
		rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);
	    }

	    // If the queue is less than one quarter full, shrink it by half.
	    if (SHRINK(q)) queue_shrink(q);
	}
    }
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
 *  Invalidate the queue and awaken blocked threads.
 *  Returns when all blocked threads have released the queue.
 *
 *  Returns zero 	- on success.
 *  	    EINVAL	- not a valid queue.
 *----------------------------------------------------------------------
 */
int
nft_queue_shutdown(nft_queue_h h)
{
    nft_queue * q = nft_queue_lookup(h);
    if (!q) return EINVAL;

    // nft_core_destroy() will delete the object's handle, so that no
    // new references to this queue can be obtained via nft_queue_lookup.
    // If the reference count is nonzero, it will decrement it, and
    // free the object if the reference count becomes zero as a result.
    //
    // But, there should be at least two references to the queue:
    // the initial reference from nft_queue_create, and the reference
    // that we just created via _lookup. So this will not actually
    // destroy the queue (and it/ would be very bad if it did).
    // The queue will be destroyed after we discard our reference,
    // after waiting for all waiters to detach from the queue.
    //
    assert(q->core.reference_count >= 2);
    nft_core_destroy(&q->core);

    int rc = pthread_mutex_lock(&q->mutex); assert(rc == 0);

    if (q->nwait > 0) {
	pthread_cleanup_push(queue_cleanup, q);

	// Waken any threads that are blocked in queue_wait().
	rc = pthread_cond_broadcast(&q->cond); assert(rc == 0);

	// Wait for all waiting threads to detach from the queue.
	// queue_wait will signal us when the last waiter leaves.
	//
	while (q->nwait > 0) {
	    rc = pthread_cond_wait(&q->cond, &q->mutex); assert(rc == 0);
	}
	pthread_cleanup_pop(0); // Pop cleanup without executing it.
    }
    rc = pthread_mutex_unlock(&q->mutex); assert(rc == 0);

    nft_queue_discard(q);
    return 0;
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
 *  nft_queue_set_destroyer
 *
 *  This function replaces the queue's current destroyer function.
 *  It returns the previous destroyer function.
 *----------------------------------------------------------------------
 */
void (*
nft_queue_set_destroyer(nft_queue_h h, void (*destroyer)(void *)))(void *)
{
    void (*previous)(void *) = NULL;
    nft_queue * q = nft_queue_lookup(h);
    if (q) {
	pthread_mutex_lock(&q->mutex);
	previous = q->destroyer;
	q->destroyer = destroyer;
	nft_queue_discard(q);
    }
    return previous;
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		QUEUE PACKAGE UNIT TEST				*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * These strings are used for the simple tests.
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
 * standard input, and passes them to the raw message queue.
 */
static void *
poll_input(void *arg)
{
    while (1) {
	char * buff = malloc(BUFFSZ);
	if (fgets(buff, BUFFSZ, stdin) == NULL)  break;
	if (nft_queue_add(input_Q, buff)  != 0)  break;
	countin++;
    }
    fputs("input thread done\n", stderr);
    return NULL;
}

/* Multiple worker threads are spawned that read messages
 * from the raw message queue, convert them to upper case,
 * and append them to the output queue.
 */
static void *
worker_thread(void * index)
{
    char  * msg;
    while ((msg = nft_queue_pop(input_Q))) {
	for (char * s = msg; (*s = toupper(*s));  s++);
	if (nft_queue_add(output_Q, msg) != 0) break;
    }
    fprintf(stderr, "worker_thread[%ld] done\n", (long) index);
    return NULL;
}

/* This function is spawned as a single thread that reads messages
 * from the output queue and prints them to standard output.
 */
static void *
poll_output(void * arg)
{
    char  * msg;
    while ((msg = nft_queue_pop(output_Q))) {
	free(msg);
	countout++;
    }
    fputs("output thread done\n", stderr);
    return NULL;
}

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

    /* Multithreaded test - best run on a multprocessor machine.
     *
     * In this test, we create a work pipeline consisting of two queues.
     * The input thread reads lines of text from standard input,
     * and appends these strings to the input queue.
     *
     * A number of worker threads pop strings from the input queue,
     * convert them to upper case, and append them to the output queue.
     *
     * An output thread pops strings from the output queue and
     * prints them to standard output. The test concludes when
     * the user presses Ctrl-C, at which time the queues are
     * shutdown, and the program exits.
     */
    fputs("multithread test, reading from stdin...\n", stderr);

    // Create the input and output queues.
    input_Q  = nft_queue_create(Q_LIMIT, free);
    output_Q = nft_queue_create(Q_LIMIT, free);

    // Create the input, output, and worker threads.
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create(&input_thread,  NULL, poll_input,  0);
    pthread_create(&output_thread, NULL, poll_output, 0);
    for (i = 0; i < NUM_WORKERS; i++)
	pthread_create(&worker_threads[i], NULL, worker_thread, (void *) i);

    // Wait for the input thread to finish.
    pthread_join(input_thread, NULL);

    // Shutdown pipeline.
    fputs("shutdown pipeline\n", stderr);
    rc = nft_queue_shutdown(input_Q);    assert(rc == 0);
    assert(nft_queue_count(input_Q)  == -1);
    rc = nft_queue_shutdown(output_Q);   assert(rc == 0);
    assert(nft_queue_count(output_Q) == -1);

    // Now join with the workers and output thread.
    for (i = 0; i < NUM_WORKERS; i++) {
	rc = pthread_join(worker_threads[i], NULL);  assert(rc == 0);
    }
    rc = pthread_join(output_thread, NULL);  assert(rc == 0);

    // This test may fail, because we did not ensure that
    // the queues had been emptied before we called nft_shutdown.
    //
    assert(countin == countout);
    fprintf(stderr, "words in: %d	words out: %d\n", countin, countout);

#ifdef NDEBUG
    fprintf(stderr, "You must recompile this test driver without NDEBUG!\n");
#else
    fprintf(stderr, "All tests passed.\n");
#endif
    exit(0);
}

/*
 * t1 - Test basic API calls.
 */
static void
t1( void)
{
    nft_queue_h  q;
    int i;

    fprintf(stderr, "t1 (create/destroy):");

    // Create an unlimited queue.
    q = nft_queue_create(0, free);

    // With the queue empty, test the pop timeout.
    nft_queue_pop_wait(q, 1);

    // Test the _append(), _count() and _peek() operations.
    for ( i = 0 ; Strings[ i] != NULL ; i++)
    {
	nft_queue_add( q, strdup( Strings[ i]));
	assert(nft_queue_count(q) == (i + 1));
    }
    assert(strcmp(nft_queue_peek(q), Strings[0]) == 0);

    // Set the queue limit, and test the append timeout.
    // FIXME q->limit = i;
    // assert(nft_queue_add_wait(q, "", 0) == ETIMEDOUT);

    // Shutdown the queue, and verify invalid queue returns.
    nft_queue_shutdown(q);
    assert(nft_queue_add(q, 0) == EINVAL);
    assert(nft_queue_pop(q)       == NULL);
    assert(nft_queue_peek(q)      == NULL);
    assert(nft_queue_count(q)     == -1);

    nft_queue_shutdown(q);

    fprintf(stderr, "passed.\n");
}

/*
 * t2 - Test append/pop operations.
 */
static void
t2( void)
{
    nft_queue_h  q;
    int i;
    void * ss;

    fprintf(stderr, "t2 (append/pop): ");

    q = nft_queue_create(0, free);

    for ( i = 0 ; Strings[ i] != NULL ; i++)
	nft_queue_add( q, strdup(Strings[i]));

    i = 0;
    while ((ss = nft_queue_pop_wait(q, 0)) != NULL)
    {
	if (strcmp(ss, Strings[i++]))
	{
	    fprintf(stderr, " failed.\n");
	    return;
	}
	free(ss);
    }
    nft_queue_shutdown( q);

    fprintf(stderr, " passed.\n");
}

/*
 * t3 - Test destroy on nonempty queue.
 */
static void
t3( void)
{
    nft_queue_h  q;
    int i;

    fprintf(stderr, "t3 (append/destroy): ");

    q = nft_queue_create(0, free);

    for ( i = 0 ; Strings[ i] != NULL ; i++)
	nft_queue_add( q, strdup( Strings[ i]));

    nft_queue_shutdown( q);

    fprintf(stderr, "passed.\n");
}


static void *
append_thread(void * arg)
{
    nft_queue_h  q = arg;
    return (void*) (intptr_t) nft_queue_add(q, "second");
}

static void *
pop_thread(void * arg)
{
    nft_queue_h  q = arg;
    return nft_queue_pop_wait(q, -1);
}


/*
 * t4 - Test queue shutdown during append.
 */
static void
t4( void)
{
    pthread_t     th;
    nft_queue_h  q;
    void	* value;
    int           rc;

    fprintf(stderr, "t4 (append/shutdown): ");

    q = nft_queue_create(1, NULL);

    // The queue limit is one, so the thread will block.
    nft_queue_add(q, "first");
    pthread_create(&th, 0, append_thread, q);
    sleep(1);

    // Shut down the queue, and join the append_thread.
    nft_queue_shutdown(q);
    rc = pthread_join(th, &value);
    assert(0      == rc);
    assert(EINVAL == (long) value);
    fprintf(stderr, "passed.\n");
}


/*
 * t5 - Test queue shutdown during pop.
 */
static void
t5( void)
{
    pthread_t     th;
    nft_queue_h  q;
    void	* value;
    int           rc;

    fprintf(stderr, "t5 (pop/shutdown): ");

    q = nft_queue_create(1, NULL);

    // The queue is empty, so the thread will block.
    pthread_create(&th, 0, pop_thread, q);
    sleep(1);

    // Shut down the queue, and join the pop_thread.
    nft_queue_shutdown(q);
    rc = pthread_join(th, &value);
    assert(0    == rc);
    assert(NULL == value);
    fprintf(stderr, "passed.\n");
}


/*
 * t6 - Test cancellation during append.
 */
static void
t6( void)
{
#ifndef WIN32	// no pthread_cancel() on WIN32
    pthread_t     th;
    nft_queue_h  q;

    fprintf(stderr, "t6 (append/cancel): ");

    q = nft_queue_create(1, NULL);

    // The queue limit is one, so the thread will block.
    nft_queue_add(q, "first");
    pthread_create(&th, 0, append_thread, q);
    sleep(1);
    pthread_cancel(th);
    sleep(1);
    assert(strcmp("first", (char*) nft_queue_pop(q)) == 0);

    nft_queue_shutdown(q);

    fprintf(stderr, "passed.\n");
#endif // WIN32
}


/*
 * t7 - Test cancellation during pop.
 */
static void
t7( void)
{
#ifndef WIN32	// no pthread_cancel() on WIN32
    pthread_t    th;
    nft_queue_h  q;

    fprintf(stderr, "t7 (pop/cancel): ");

    q = nft_queue_create(1, NULL);

    // The queue is empty, so the thread will block.
    pthread_create(&th, 0, pop_thread, q);
    sleep(1);
    pthread_cancel(th);
    sleep(1);
    assert(nft_queue_pop_wait(q, 0) == NULL);

    nft_queue_shutdown(q);

    fprintf(stderr, "passed.\n");
#endif // WIN32
}

#endif	// MAIN
