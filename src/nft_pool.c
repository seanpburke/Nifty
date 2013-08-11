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

    pthread_attr_t	attr;		// Create detached threads.
    int			quit;		// pool should quit.
    int			num_threads;
    int			max_threads;
    int			idle_threads;
} nft_pool;

// Define nft_pool_class, showing derivation from nft_queue.
#define nft_pool_class nft_queue_class ":nft_pool"

// Define helper functions nft_pool_cast, _handle, _lookup, and _discard.
NFT_DEFINE_HELPERS(nft_pool,static)

#define MIN_STACK_SIZE	16*1024

/*------------------------------------------------------------------------------
 * nft_pool_cleanup	- Function for use with pthread_cleanup_push/pop.
 *------------------------------------------------------------------------------
 */
static void
pool_cleanup(void * arg)
{
    nft_pool * pool = nft_pool_cast(arg); assert(pool);

    int rc = pthread_mutex_lock(&pool->queue.mutex); assert(0 == rc);
    pool->num_threads--;
    rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);

    // The nft_pool_thread holds its own pool reference,
    // so we must discard it on exit.
    nft_pool_discard(pool);
}

/*------------------------------------------------------------------------------
 * nft_pool_thread	- Thread start function to serve the pool's work queue.
 *------------------------------------------------------------------------------
 */
static void *
nft_pool_thread(void * arg)
{
    nft_pool * pool = nft_pool_lookup(arg);
    if (!pool) return NULL; // The pool must have been shutdown.

    /* The nft_pool_threads are private threads, so they cannot be cancelled,
     * but the work function could call pthread_exit(), so we need a cleanup
     * function to decrement pool->num_threads and discard the reference.
     */
    pthread_cleanup_push(pool_cleanup, pool);

    // Wait for up to one second for work to arrive, then exit.
    work_item * item;
    while ((item = nft_queue_pop_wait(nft_queue_handle(&pool->queue), 1)))
    {
	int rc = pthread_mutex_lock(&pool->queue.mutex); assert(0 == rc);
	pool->idle_threads--;
	rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);

	void (* function)(void *) = item->function;
	void  * argument          = item->argument;

	// Free the item first, in case function calls pthread_exit().
	free(item);
	function(argument);

	rc = pthread_mutex_lock(&pool->queue.mutex); assert(0 == rc);
	pool->idle_threads++;
	rc = pthread_mutex_unlock(&pool->queue.mutex); assert(0 == rc);
    }
    pthread_cleanup_pop(1); // executes pool_cleanup
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
    if (stack_size != 0 && stack_size < MIN_STACK_SIZE) stack_size = MIN_STACK_SIZE;
    if (stack_size  > 0) {
	rc = pthread_attr_setstacksize(&pool->attr, stack_size); assert(0 == rc);
    }
    pool->max_threads  = (max_threads >= 1) ? max_threads : 1;
    pool->num_threads  = 0;
    pool->idle_threads = 0;
    pool->quit         = 0;

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
 * This function may also return an error code from pthread_create,
 * if a new pool thread cannot be created.
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

    // Enqueue the work item. This may block if the queue is at its limit.
    int result = nft_queue_enqueue(&pool->queue, item, timeout, 'L');
    if ((result == 0) &&
	(pool->idle_threads == 0) &&
	(pool->num_threads   < pool->max_threads))
    {
	// Spawn a new thread, passing the pool's handle to nft_pool_thread.
	pthread_t id;
	result = pthread_create(&id, &pool->attr, nft_pool_thread, nft_pool_handle(pool));
	if (result == 0) {
	    pool->num_threads++;
	    pool->idle_threads++;
	}
    }
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
 * nft_pool_shutdown	- Free resources associated with thread pool
 *
 * There are a number of caveats associated with shutdown, all having
 * to do with how to dispose of work items enqueued for processing,
 * and pool threads that are currently processing work items.
 *
 * The shutdown call may return before all pool threads have exited.
 * This call will wait for idle threads to detach from the queue,
 * but pool threads that are busy will exit only after they have
 * completed their current work item.
 *
 * In some cases, it might be desirable to block until busy threads
 * have finished, and in others one might wish to cancel busy threads.
 * This is an area for future development.
 *
 * This call has the potential to leak resources, when the queue holds
 * unprocessed work items. While the memory occupied by the struct
 * work_item itself will be freed, any resources associated with the
 * work_item's arg will be leaked. For example, if the work item's arg
 * is a file descriptor, there is no means to ensure that it would be
 * closed if the pool were shut down.
 *
 * This is not completely satisfactory, but I am not certain how best
 * to remedy it. One option is to have the shutdown wait for every
 * currently queued work item to be processed, but that would not be
 * suitable in all cases. Another aproach would be to provide an API
 * option to add a destroyer function to the work item, to be invoked
 * when the item must be freed without processing.
 *------------------------------------------------------------------------------
 */
int
nft_pool_shutdown(nft_pool_h handle)
{
    nft_pool * pool = nft_pool_lookup(handle);
    if (!pool) return EINVAL;

    nft_queue_shutdown(nft_queue_handle(&pool->queue));
    nft_pool_discard(pool);
    return 0;
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

/*
 * This is an obnoxious function that exits the pool thread.
 */
void test_exit(void * arg)
{
    pthread_exit(0);
}

int
main(int argc, char *argv[])
{
    nft_pool_h pool;
    int	       i, n;

    // Do a basic smoke test.
    pool = nft_pool_create( 32,  // queue_limit
			    10,  // max_threads
		      128*1024); // stack size
    assert(NULL != pool);
    nft_pool_add(pool, (void(*)(void*)) puts, "Foo");
    nft_pool_add(pool, (void(*)(void*)) puts, "Bar");
    nft_pool_add(pool, (void(*)(void*)) puts, "Faz");
    nft_pool_shutdown(pool);
    assert(nft_pool_lookup(pool) == NULL);
    fputs("Test 1 passed.\n\n", stderr);

    // Test that destroy blocks until work is finished.
    pool = nft_pool_create(-1,  // queue_limit is NFT_QUEUE_MIN_SIZE
			    2,  // max_threads
			    0); // default stack size
    nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 2);
    sleep(1);
    nft_pool_shutdown(pool);
    fputs("Test 2 passed.\n\n", stderr);

    // Stress/Performance test - push many work items through the queue.
    n = 100000;
    fprintf(stderr, "Test 3: processing %d tasks...", n);
    pool = nft_pool_create(-1, 2, 0);
    for (i = 0; i < n; i++)
	nft_pool_add(pool, (void(*)(void*)) random, NULL);
    nft_pool_shutdown(pool);
    fputs("passed.\n\n", stderr);

#ifndef WIN32
    /* The following tests fail on WIN32, either because
     * cancellation is not supported, or the cleanup handlers
     * aren't run on thread exit.
     */

    /* Test a function that exits the pool thread.
     */
    pool = nft_pool_create(-1, 0, 0);
    nft_pool_add(pool, test_exit, 0);
    sleep(1);
    // assert(0 == pool->num_threads);

    // Verify that the pool is still functional.
    nft_pool_add(pool, (void(*)(void*)) puts, "Test 4 passed.\n\n");
    sleep(1);

    /* Test cancellation in nft_pool_shutdown.
     *
     * We submit a 4-second sleep to keep the worker thread busy,
     * then spawn nft_pool_shutdown in a thread. That thread will
     * block waiting for the sleep to finish, whereupon we cancel
     * it. The canceled shutdown must increment pool->quit to 2,
     * and the worker thread must free the pool when it exits.
     */
    // This test has been disabled, because nft_pool_shutdown
    // does not wait for busy pool threads to finish. -SEan
    if (0) {
	pthread_t thread;
	int       rc;
	nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 4);
	sleep(1);
	rc = pthread_create(&thread, NULL, (void* (*)(void*)) nft_pool_shutdown, pool);
	assert(0 == rc);
	sleep(1);
	rc = pthread_cancel(thread);
	assert(0 == rc);
	sleep(1);
	// assert(2 == pool->quit);
	// assert(1 == pool->num_threads);
	sleep(1);
	// assert(0 == pool->num_threads);
	fputs("Test 5 passed.\n\n", stderr);
    }
#endif /* WIN32 */

#ifdef NDEBUG
    printf("You must recompile this test driver without NDEBUG!\n");
#else
    printf("All tests passed.\n");
#endif
    exit(0);
}


#endif /* MAIN */


