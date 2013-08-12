/*******************************************************************************
 * (C) Xenadyne Inc. 2002-2013.  All rights reserved.
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
 * File: nft_pool.c
 *
 * Description:	Thread pool package.
 *
 * This package provides thread pools, also known as "work queues".
 * It illustrates Nifty's object-oriented style of development,
 * using the nft_queue package as the base class. The unit test
 * at the bottom of this file (see #ifdef MAIN) demonstrates how
 * to use this package.
 *
 *******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include <nft_gettime.h>
#include <nft_queue.h>
#include <nft_pool.h>

typedef struct work_item	// Work items are queued by the pool
{
    void (*function)(void *);
    void  *argument;
} work_item;

typedef struct nft_pool		// Structure describing a thread pool.
{
    nft_queue           queue;		// Inherit from nft_queue.

    int			num_threads;
    int			max_threads;
    int			idle_threads;
    pthread_attr_t	attr;		// Create detached threads.
} nft_pool;

// Define nft_pool_class, showing derivation from nft_queue.
#define nft_pool_class nft_queue_class ":nft_pool"

// Define helper functions nft_pool_cast, _handle, _lookup, and _discard.
NFT_DEFINE_HELPERS(nft_pool,static)

// When the pool has been shutdown, its handle is deleted.
#define SHUTDOWN(pool) (nft_pool_handle(pool) == NULL)

/*------------------------------------------------------------------------------
 * pool_thread_cleanup	- Function for use with pthread_cleanup_push/pop.
 *
 * This cleanup handler is used by pool threads, which cannot be cancelled,
 * but which may call pthread_exit from the work function.
 *------------------------------------------------------------------------------
 */
static void
pool_thread_cleanup(void * arg)
{
    nft_pool * pool = nft_pool_cast(arg); assert(pool);

    int rc = pthread_mutex_lock(&pool->queue.mutex); assert(rc == 0);    

    // If the pool is shutting down and we are the last pool thread
    // to finish, signal the thread that is waiting in nft_pool_shutdown.
    if (--pool->num_threads == 0 && SHUTDOWN(pool))
	pthread_cond_signal(&pool->queue.cond);

    rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);

    // The nft_pool_thread holds a pool reference, which we must discard.
    nft_pool_discard(pool);
}

/*------------------------------------------------------------------------------
 * nft_pool_thread	- Thread start function to serve the pool's work queue.
 *
 * nft_pool_add generates a fresh protected reference to the pool,
 * while holding the pool mutex, and passes that reference to us.
 *------------------------------------------------------------------------------
 */
static void *
nft_pool_thread(void * arg)
{
    nft_pool * pool = nft_pool_cast(arg);

    int rc = pthread_mutex_lock(&pool->queue.mutex); assert(rc == 0);

    // Note that the pool has a new, idle worker.
    pool->num_threads++;
    pool->idle_threads++;
    
    // Unless shutting down, wait for up to one second for work to arrive, then exit.
    int         timeout = SHUTDOWN(pool) ? 0 : 1 ;
    work_item * item;
    while ((item = nft_queue_dequeue(&pool->queue, timeout)))
    {
	pool->idle_threads--;

	// We must release the mutex while the work function executes.
	rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);

	/* The nft_pool_threads are private threads, so they cannot be cancelled,
	 * but the work function could call pthread_exit(), so we need a cleanup
	 * function to decrement pool->num_threads and discard the reference.
	 */
	pthread_cleanup_push(pool_thread_cleanup, pool);

	void (* function)(void *) = item->function;
	void  * argument          = item->argument;

	// Free the item first, in case function calls pthread_exit().
	free(item);
	function(argument);

	pthread_cleanup_pop(0); // do not execute pool_thread_cleanup
	rc = pthread_mutex_lock(&pool->queue.mutex); assert(0 == rc);

	// If the pool is shutting down, exit once the queue is empty.
	if (SHUTDOWN(pool)) timeout = 0;
	pool->idle_threads++;
    }
    pool->idle_threads--;
    pool->num_threads--;
    
    // If the pool is shutting down and we are the last pool thread
    // to finish, signal the thread that is waiting in nft_pool_shutdown.
    if (pool->num_threads == 0 && SHUTDOWN(pool))
	pthread_cond_signal(&pool->queue.cond);

    rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);
    nft_pool_discard(pool);
    return NULL;
}

/*------------------------------------------------------------------------------
 * nft_pool_destroy
 *
 * This can only be called when all references to the pool have been discarded,
 * following a nft_pool_shutdown.
 *------------------------------------------------------------------------------
 */
static void
nft_pool_destroy(nft_core * p)
{
    nft_pool * pool = nft_pool_cast(p); assert(pool);
    if (!pool) return;
    int rc = pthread_attr_destroy(&pool->attr); assert(0 == rc);
    nft_queue_destroy(p);
}

/*------------------------------------------------------------------------------
 * nft_pool_create
 *
 * The queue_limit parameter sets the nft_queue limit parameter. If queue_limit
 * is negative, the pool's queue will be limited to the minimum size defined
 * in nft_queue.h. If queue_limit is zero, there will be no limit upon the size
 * of the queue. Otherwise, nft_queue_add will block if the limit is reached.
 *
 * The max_threads parameter will be forced to one if non-positive.
 *
 * If the stack_size is zero, it will be set to the default stack size.
 * If stack_size is nonzero, and it is less than the default stack size,
 * then the default stack size will be used.
 *
 * Since this constructor does not accept class and size parameters,
 * it will not be practical to create a subclass of nft_pool.
 *
 * Returns NULL on malloc failure.
 *------------------------------------------------------------------------------
 */
nft_pool_h
nft_pool_create(int queue_limit, int max_threads, int stack_size)
{
    nft_queue * q = nft_queue_create_f(nft_pool_class, sizeof(nft_pool), queue_limit, free);
    if (!q) return NULL;

    // Override the nft_core destructor with our own dtor.
    q->core.destroy = nft_pool_destroy;

    nft_pool  * pool = nft_pool_cast(q);
    int rc = pthread_attr_init(&pool->attr); assert(0 == rc);
    rc     = pthread_attr_setdetachstate(&pool->attr, PTHREAD_CREATE_DETACHED);  assert(0 == rc);
    if (stack_size != 0 && stack_size < NFT_POOL_MIN_STACK_SIZE) stack_size = NFT_POOL_MIN_STACK_SIZE;
    if (stack_size  > 0) {
	rc = pthread_attr_setstacksize(&pool->attr, stack_size); assert(0 == rc);
    }
    pool->max_threads  = (max_threads >= 1) ? max_threads : 1;
    pool->num_threads  = 0;
    pool->idle_threads = 0;

    return nft_pool_handle(pool);
}

/*------------------------------------------------------------------------------
 * nft_pool_add_wait	- Add a work item to the pool's queue.
 *
 * The wait parameter is defined as in nft_queue_timed_wait.
 * If the queue is at its limit:
 *
 *	timeout  < 0	will wait indefinitely
 *      timeout == 0	will return ETIMEDOUT immediately
 *      timeout  > 0	will return ETIMEDOUT after timeout seconds
 *
 * Returns: 0		Success
 *          ENOMEM      malloc failed, memory exhausted.
 *	    EINVAL	Invalid handle, or pool has been shutdown.
 *          ESHUTDOWN   Pool has been shutdown.
 *          various     pthread_create error codes
 *
 *------------------------------------------------------------------------------
 */
int
nft_pool_add_wait(nft_pool_h handle, int timeout, void (*function)(void *),  void * argument)
{
    nft_pool * pool = nft_pool_lookup(handle);
    if (!pool) return EINVAL;

    work_item * item = malloc(sizeof(work_item));
    if (item) {
	item->function = function;
	item->argument = argument;
    }
    else {
	// Be sure not to return without discarding this reference.
	nft_pool_discard(pool);
	return ENOMEM;
    }

    // We must hold the mutex when calling nft_queue_enqueue.
    int rc = pthread_mutex_lock(&pool->queue.mutex); assert(0 == rc);

    if (SHUTDOWN(pool)) {
	rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);
	nft_pool_discard(pool);
	free(item);
	return ESHUTDOWN;
    }
    // Enqueue the work item. This may block if the queue is at its limit
    int result = nft_queue_enqueue(&pool->queue, item, timeout, 'L');
    if (result == 0)
    {
	// The item was queued successfully, so make sure there is a thread to process it.
	if ((pool->idle_threads == 0) &&
	    (pool->num_threads  <  pool->max_threads))
	{
	    // Create a fresh reference to the pool, which we will pass to the thread.
	    nft_pool * newref = nft_pool_lookup(nft_pool_handle(pool)); assert(newref);

	    // Discard the new reference if pthread_create fails.
	    pthread_t  id;
	    if ((result = pthread_create(&id, &pool->attr, nft_pool_thread, newref)))
		nft_pool_discard(newref);
	}
    }
    else free(item); // The item was not queued.

    rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);
    nft_pool_discard(pool);
    return result;
}

/*------------------------------------------------------------------------------
 * nft_pool_add		- Add a work item to the pool's queue, with no timeout.
 *------------------------------------------------------------------------------
 */
int
nft_pool_add(nft_pool_h handle, void (*function)(void *),  void * argument)
{
    return nft_pool_add_wait(handle, -1, function, argument);
}


/*------------------------------------------------------------------------------
 * pool_shutdown_cleanup - Function for use with pthread_cleanup_push/pop.
 *
 * This cleanup is pushed by nft_pool_shutdown, in case it is cancelled
 * while waiting for shutdown to finish.
 *------------------------------------------------------------------------------
 */
static void
pool_shutdown_cleanup(void * arg)
{
    nft_pool * pool = nft_pool_cast(arg); assert(pool);
    int rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);
    nft_pool_discard(pool);
}

/*------------------------------------------------------------------------------
 * nft_pool_shutdown	- Free resources associated with thread pool
 *
 *  Returns zero 	- On success.
 *  	    EINVAL	- Invalid pool, or pool has been shut down.
 *          ESHUTDOWN   - Pool has been shutdown.
 *------------------------------------------------------------------------------
 */
int
nft_pool_shutdown(nft_pool_h handle, int timeout)
{
    nft_pool * pool = nft_pool_lookup(handle);
    if (!pool) return EINVAL;

    // nft_queue_shutdown will delete our handle, so that no new
    // work items can be enqueued. If the queue is empty, it will
    // wake any idle threads that are waiting, and wait for them
    // to detach, but busy pool threads will continue to dequeue
    // and process work items.
    //
    int result  = nft_queue_shutdown(nft_queue_handle(&pool->queue));
    if (result != 0) {
	nft_pool_discard(pool);
	return result;
    }
    int rc = pthread_mutex_lock(&pool->queue.mutex); assert(0 == rc);

    // Push a cleanup handler before calling pthread_cond_(timed_)wait,
    // since those calls are cancellation points.
    pthread_cleanup_push(pool_shutdown_cleanup, pool);

    // Did the caller ask to wait until shutdown is complete?
    // If timeout is positive, do a timed wait, else wait indefinitely.
    nft_queue * q = &pool->queue;
    if (timeout > 0) {
	struct timespec abstime = nft_gettime(); abstime.tv_sec += timeout;
	while (pool->num_threads)
	    if ((result = pthread_cond_timedwait(&q->cond, &q->mutex, &abstime)) != 0)
		break;
	// pthread_cond_timed_wait returns ETIMEDOUT on timeout.
	assert(result == 0 || result == ETIMEDOUT);
    }
    else if (timeout < 0) {
	while (pool->num_threads)
	    if ((result = pthread_cond_wait(&q->cond, &q->mutex)) != 0)
		break;
	assert(result == 0);
    }
    pthread_cleanup_pop(1); // execute pool_shutdown_cleanup
    return result;
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		POOL PACKAGE UNIT TEST				*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#include <stdio.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <winbase.h>
#define sleep(n) _sleep(n*1000)
#endif

void clear_flag(void * arg) { // Simply zeroes an int*.
    *(int*) arg = 0;
}
void test_exit(void * arg) { // An obnoxious function that calls pthread_exit.
    pthread_exit(0);
}
void test_shutdown(void * arg) { // A function that shutdowns down the pool.
    int rc = nft_pool_shutdown(arg, -1); assert(rc == 0);
}

int
main(int argc, char *argv[])
{
    int rc;

    // Do a basic smoke test.
    fputs("Test 1: ", stderr);
    nft_pool_h pool = nft_pool_create( 32,  // queue_limit
				       10,  // max_threads
				 128*1024); // stack size
    assert(NULL != pool);
    rc = nft_pool_add(pool, (void(*)(void*)) puts, "Foo"); assert(rc == 0);
    rc = nft_pool_add(pool, (void(*)(void*)) puts, "Bar"); assert(rc == 0);
    rc = nft_pool_add(pool, (void(*)(void*)) puts, "Faz"); assert(rc == 0);
    rc = nft_pool_shutdown(pool, 1);
    assert(nft_pool_lookup(pool) == NULL);
    
    // We asked shutdown to wait, but it should not time out.
    assert(rc != ETIMEDOUT || !"shutdown timed out");
    assert(rc == 0);

    fputs("passed.\n", stderr);

    // Test that shutdown blocks until work is finished.
    fputs("Test 2: shutdown while busy ", stderr);
    pool = nft_pool_create(-1,  // queue_limit is NFT_QUEUE_MIN_SIZE
			    2,  // max_threads
			    0); // default stack size
    rc = nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 2); assert(rc == 0);
    rc = nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 2); assert(rc == 0);
    rc = nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 2); assert(rc == 0);

    // Note that you can pass nft_pool handles to nft_queue APIs,
    // since that is nft_pool's superclass, but you have to cast it.
    // Invalid casts will be detected - review nft_core lookup.
    assert(nft_queue_count((nft_queue_h) pool) == 3);

    rc = nft_pool_shutdown(pool, -1);
    assert(rc == 0);
    
    fputs("passed.\n", stderr);

#ifndef WIN32
    /* The following tests fail on WIN32, either because cancellation
     * is not supported, or the cleanup handlers aren't run on thread exit.
     */

    // The test_exit function will call pthread_exit from the pool thread.
    fputs("Test 3: Call pthread_exit from pool thread ", stderr);
    pool = nft_pool_create(-1, 0, 0); assert(pool != NULL);
    rc = nft_pool_add(pool, test_exit, 0);
    assert(0 == rc);
    sleep(1);
    // Verify that the pool is still functional afterward.
    int flag = -1;
    rc = nft_pool_add(pool, clear_flag, &flag); assert(0 == rc);
    sleep(1);  // FIXME - fails without this sleep.
    rc = nft_pool_shutdown(pool, -1); assert(0 == rc);
    assert(flag == 0);
    fputs("passed\n", stderr);

    /* Test cancellation in nft_pool_shutdown.
     *
     * We submit a 4-second sleep to keep the worker thread busy,
     * then spawn nft_pool_shutdown in a thread. That thread will
     * block waiting for the sleep to finish, whereupon we cancel it.
     */
    fputs("Test 4: cancel thread in nft_pool_shutdown ", stderr);
    pool = nft_pool_create(-1, 0, 0); assert(pool != NULL);

    // First, get our own ref to the pool, so we can check its state.
    nft_pool * pref = nft_pool_lookup(pool);
    assert(pref != NULL);
    assert(pref->queue.core.reference_count == 2);

    // Add a four-second sleep to the pool.
    rc = nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 4);
    assert(0 == rc);
    sleep(1);
    assert(pref->num_threads  == 1);
    assert(pref->idle_threads == 0);
    assert(pref->queue.core.reference_count == 3);

    // Spawn a thread that shuts down the queue, but waits for sleep to finish.
    pthread_t thread;
    rc = pthread_create(&thread, NULL, (void* (*)(void*)) test_shutdown, pool);
    assert(0 == rc);
    sleep(1);
    assert(pref->queue.core.handle == NULL);
    assert(pref->num_threads  == 1);
    assert(pref->idle_threads == 0);
    assert(pref->queue.core.reference_count == 3);

    // Cancel the test_shutdown thread, confirm the sleeper is still there.
    rc = pthread_cancel(thread);
    assert(0 == rc);
    sleep(1);
    assert(pref->num_threads  == 1);
    assert(pref->idle_threads == 0);
    assert(pref->queue.core.reference_count == 2);

    // Wait for sleeper to finish.
    sleep(2);
    assert(pref->num_threads  == 0);
    assert(pref->idle_threads == 0);
    assert(pref->queue.core.reference_count == 1);

    // Discard our handle
    nft_pool_discard(pref);
    fputs("passed.\n", stderr);
#endif // WIN32

    // Stress/Performance test - push many work items through the queue.
    int n = 10000;
    fprintf(stderr, "Test 5: processing %d tasks...", n);
    pool = nft_pool_create(-1, 2, 0);
    for (int i = 0; i < n; i++) {
	rc = nft_pool_add(pool, (void(*)(void*)) random, NULL); assert(rc == 0);
    }
    rc = nft_pool_shutdown(pool, -1);
    assert(rc == 0);
    fputs("passed.\n", stderr);

#ifdef NDEBUG
    printf("You must recompile this test driver without NDEBUG!\n");
#else
    printf("All tests passed.\n");
#endif
    exit(0);
}
#endif // MAIN
