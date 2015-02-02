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
 * The APIs are documented in nft_pool.h. The unit test at the bottom
 * of this file (see #ifdef MAIN) demonstrates how to use this package.
 *
 * This package illustrates Nifty's object-oriented style of development,
 * using the nft_queue package as the base class. The README.txt section
 * on object-oriented development explains how this works.
 *
 *******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include <nft_pool.h>

// Define helper functions nft_pool_cast, _handle, _lookup, and _discard.
NFT_DEFINE_WRAPPERS(nft_pool,)

typedef struct work_item	// Work items are queued by the pool
{
    void (*function)(void *);
    void  *argument;
} work_item;

// The nft_pool_create stack_size parameter is forced to this minimum.
#define  NFT_POOL_MIN_STACK_SIZE 16*1024

// When the queue has been shutdown, the shutdown flag is true.
#define SHUTDOWN(q) (0 != pool->queue.shutdown)


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
 * and passes that reference to us.
 * pool->num_threads, but it has not incremented pool->idle_threads.
 *------------------------------------------------------------------------------
 */
static void *
nft_pool_thread(void * arg)
{
    nft_pool * pool = nft_pool_cast(arg);
    int rc          = pthread_mutex_lock(&pool->queue.mutex); assert(rc == 0);

    // Increment pool->idle_threads before we block in nft_queue_dequeue.
    // nft_pool_add has already incremented num_threads, after spawning this thread.
    pool->idle_threads++;

    // nft_queue_dequeue will not block when the queue is shutting down.
    // It will continue to dequeue items, and return ESHUTDOWN when the queue is empty.
    work_item * item;
    while ((0 == nft_queue_dequeue(&pool->queue, 1, (void**) &item)))
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
 * The max_threads parameter will be forced to four if non-positive.
 *
 * If the stack_size is zero, it will be set to the default stack size.
 * If stack_size is nonzero, and it is less than the default stack size,
 * then the default stack size will be used.
 *
 * Returns NULL on malloc failure.
 *------------------------------------------------------------------------------
 */
nft_pool *
nft_pool_create(const char * class, size_t size,
		int    queue_limit, int max_threads, int stack_size)
{
    nft_queue * q = nft_queue_create(class, size, queue_limit);
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
    pool->max_threads  = (max_threads > 0) ? max_threads : 4 ;
    pool->num_threads  = 0;
    pool->idle_threads = 0;

    return pool;
}

/*----------------------------------------------------------------------
 * nft_pool_new()
 *
 * Like nft_pool_create, with simpler parameters,
 * and returning a nft_pool_h handle instead of nft_pool *.
 *----------------------------------------------------------------------
 */
nft_pool_h
nft_pool_new(int queue_limit, int max_threads, int stack_size)
{
    return nft_pool_handle(nft_pool_create(nft_pool_class, sizeof(nft_pool),
					   queue_limit, max_threads, stack_size));
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
 *	    EINVAL	Invalid handle
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
	    // Discard the clone reference if pthread_create fails.
	    nft_pool * clone = nft_pool_lookup(nft_pool_handle(pool)); assert(clone);
	    pthread_t  id;
	    if (!(result = pthread_create(&id, &pool->attr, nft_pool_thread, clone)))
		pool->num_threads++;
	    else
		nft_pool_discard(clone);
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
 *  	    EINVAL	- Invalid pool.
 *          ETIMEDOUT   - Timed out while waiting.
 *          ESHUTDOWN   - Pool has been shutdown.
 *------------------------------------------------------------------------------
 */
int
nft_pool_shutdown(nft_pool_h handle, int timeout)
{
    nft_pool * pool = nft_pool_lookup(handle);
    if (!pool) return EINVAL;

    // nft_queue_shutdown will wake any idle threads that are waiting,
    // either to enqueue or dequeue, and wait for them to detach,
    // but busy pool threads will continue to dequeue and process items.
    //
    int result  = nft_queue_shutdown((nft_queue_h) handle, timeout);
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
#ifdef NDEBUG
#undef NDEBUG  // Enable asserts for test code.
#endif
#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif

volatile int flags[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

// Clear the indicated flag immediately.
void clear_flag(void * arg) {
    flags[(long)arg] = 0;
}
// Clear the indicated flag after sleeping
void sleeper(void * arg) {
    long flag = (long) arg;
    fprintf(stderr,"\n\tsleeping for %ld seconds...", flag);
    sleep(flag);
    flags[flag] = 0;
}
void test_exit(void * arg) { // An obnoxious function that calls pthread_exit.
    pthread_exit(0);
}
void test_shutdown(void * arg) { // A function that shutdowns down the pool.
    int rc = nft_pool_shutdown(arg, -1); assert(rc == 0);
}

void
basic_tests(void)
{
    int rc;
    fputs("Test 1: basic test", stderr);

    // Test shutdown on idle queue
    nft_pool_h pool = nft_pool_new(-1,  // queue_limit is NFT_QUEUE_MIN_SIZE
				    1,  // max_threads
				    0); // default stack size
    assert(NULL != pool);
    flags[0] = 1, flags[1] = 1, flags[2] = 1;
    rc = nft_pool_add(pool, clear_flag, (void*) 0); assert(rc == 0);
    rc = nft_pool_add(pool, clear_flag, (void*) 1); assert(rc == 0);
    rc = nft_pool_add(pool, clear_flag, (void*) 2); assert(rc == 0);

    // After a short sleep, pool should be idle, and shutdown with no timeout succeeds.
    sleep(1);
    assert(0 == nft_pool_shutdown(pool, 0));
    assert(flags[0] == 0 && flags[1] == 0 && flags[2] == 0);
    fputs("passed.\n", stderr);

    fputs("Test 2: shutdown while busy ", stderr);

    // Test that shutdown blocks until work is finished.
    // Create a pool with sufficient threads that three can be blocked at once.
    pool = nft_pool_new(-1,  // queue_limit is NFT_QUEUE_MIN_SIZE
			 3,  // max_threads
			 0); // default stack size
    flags[0] = 1, flags[1] = 1, flags[2] = 1;
    rc = nft_pool_add(pool, sleeper, (void*) 1); assert(rc == 0);
    rc = nft_pool_add(pool, sleeper, (void*) 2); assert(rc == 0);
    rc = nft_pool_add(pool, sleeper, (void*) 3); assert(rc == 0);
    rc = nft_pool_shutdown(pool, -1);            assert(rc == 0);
    assert(flags[1] == 0 && flags[2] == 0 && flags[3] == 0);
    fputs("passed.\n", stderr);

    /* The following tests fail on WIN32, either because cancellation
     * is not supported, or the cleanup handlers aren't run on thread exit.
     */
#ifndef _WIN32
    // The test_exit function will call pthread_exit from the pool thread.
    fputs("Test 3: Call pthread_exit from pool thread ", stderr);
    pool = nft_pool_new(-1, 0, 0); assert(pool != NULL);
    rc   = nft_pool_add(pool, test_exit, 0);
    assert(0 == rc);
    sleep(1);
    // Verify that the pool is still functional afterward.
    flags[0] = 1;
    rc = nft_pool_add(pool, clear_flag, (void*) 0); assert(0 == rc);
    rc = nft_pool_shutdown(pool, -1); assert(0 == rc);
    assert(flags[0] == 0);
    fputs("passed\n", stderr);

    /* Test cancellation in nft_pool_shutdown.
     *
     * We submit a 4-second sleep to keep the worker thread busy,
     * then spawn nft_pool_shutdown in a thread. That thread will
     * block waiting for the sleep to finish, whereupon we cancel it.
     */
    fputs("Test 4: cancel thread in nft_pool_shutdown ", stderr);
    pool = nft_pool_new(-1, 0, 0); assert(pool != NULL);

    // First, get our own ref to the pool, so we can check its state.
    nft_pool * pref = nft_pool_lookup(pool);
    assert(pref != NULL);

    // Add a four-second sleep to the pool.
    rc = nft_pool_add(pool, (void(*)(void*)) sleep, (void*) 4);
    assert(0 == rc);
    sleep(1);
    assert(pref->num_threads  == 1);
    assert(pref->idle_threads == 0);

    // Spawn a thread that shuts down the queue, but waits for sleep to finish.
    pthread_t thread;
    rc = pthread_create(&thread, NULL, (void* (*)(void*)) test_shutdown, pool);
    assert(0 == rc);
    sleep(1);
    assert(pref->num_threads  == 1);
    assert(pref->idle_threads == 0);

    // Take advantage of the base-class APIs.
    assert(ESHUTDOWN == nft_queue_state((nft_queue_h) pool));

    // Cancel the test_shutdown thread, confirm the sleeper is still there.
    assert(pthread_cancel(thread) == 0);
    sleep(1);
    assert(pref->num_threads  == 1);
    assert(pref->idle_threads == 0);

    // Wait for sleeper to finish.
    sleep(2);
    assert(pref->num_threads  == 0);
    assert(pref->idle_threads == 0);

    // Discard our handle
    nft_pool_discard(pref);
    fputs("passed.\n", stderr);
#endif // _WIN32

    // Stress/Performance test - push many work items through the queue.
    int n = 100000;
    fprintf(stderr, "Test 5: processing %d tasks...", n);
    pool = nft_pool_new(-1, 2, 0);
    for (int i = 0; i < n; i++) {
	rc = nft_pool_add(pool, (void(*)(void*)) rand, NULL); assert(rc == 0);
    }
    rc = nft_pool_shutdown(pool, -1);
    assert(rc == 0);
    fputs("passed.\n", stderr);
}


/*****************************************************************************************
 * nft_action_pool	- Demonstrate a subclass based on nft_pool
 *
 * The nft_pool requires to pass both a function and its argument,
 * when adding a task to the pool. This is very flexible, but you
 * might want to create a simpler pool, that applies the same action
 * to every item that is passed to the pool.
 *
 * Here we demonstrate a subclass that extends nft_pool with a
 * function attribute, named 'action'. You specify the action function
 * when the pool is created, and this action is applied to every
 * item that is added to the queue:
 */
typedef struct nft_action_pool {
    nft_pool pool;
    void  (* action)(void *);
} nft_action_pool;

// The class string must properly reflect inheritance from nft_pool_class.
#define nft_action_pool_class nft_pool_class ":nft_action_pool"

// This macro expands to declare the nft_action_pool_cast, _handle, _lookup, and _discard methods.
NFT_DECLARE_WRAPPERS(nft_action_pool,static)

// This macro expands to define the _cast, _handle, _lookup, and _discard methods.
NFT_DEFINE_WRAPPERS(nft_action_pool,static)

// Our constructor adds one parameter to initialize the new attribute.
nft_action_pool_h
nft_action_pool_new(int     queue_limit,
		    int     max_threads,
		    int     stack_size,
		    void (* action)(void *))
{
    // Invoke the base class "private" constructor, passing our class string and size.
    nft_pool * pool = nft_pool_create(nft_action_pool_class, sizeof(nft_action_pool), queue_limit, max_threads, stack_size);
    if (!pool) return NULL;

    nft_action_pool * action_pool = nft_action_pool_cast(pool);
    action_pool->action = action;

    // Return a handle to the caller, not the pointer.
    return nft_action_pool_handle(action_pool);
}

// The _add operation has no function parameter:
int
nft_action_pool_add(nft_action_pool_h handle, void * argument)
{
    nft_action_pool * pool = nft_action_pool_lookup(handle);
    if (!pool) return EINVAL;

    // Here we pass pool->action and argument to the base class
    // method nft_pool_add. Nifty APIs have static type-checking,
    // so we have to typecast the handle:
    int result = nft_pool_add((nft_pool_h) handle, pool->action, argument);

    nft_action_pool_discard(pool);
    return result;
}
int
nft_action_pool_shutdown(nft_action_pool_h handle, int timeout)
{
    // We can pass our handle to parent-class methods,
    // but we do have to type-cast the handle to avoid
    // compilation warnings.
    return nft_pool_shutdown((nft_pool_h) handle, timeout);
}

void
test_nft_action_pool(void)
{
    int rc;
    fputs("\nTesting nft_action_pool ", stderr);

    // Create a nft_action_pool that applies clear_flag to each item.
    // With this subclass, we set the pool's action function when we
    // create it.
    nft_action_pool_h action_pool =
	nft_action_pool_new(-1, // default queue_limit
			     0, // default max_threads
			     0, // default stack size
			     clear_flag );
    assert(NULL != action_pool);

    for (int i = 0; i < 10; i++) flags[i] = 1;
    rc = nft_action_pool_add(action_pool, (void*) 0); assert(rc == 0);
    rc = nft_action_pool_add(action_pool, (void*) 2); assert(rc == 0);
    rc = nft_action_pool_add(action_pool, (void*) 3); assert(rc == 0);

    // Note that we can use methods of the object's parent class,
    // in object-oriented style, though we have to type-cast the handle:
    //
    rc = nft_pool_add((nft_pool_h) action_pool, sleeper, (void*) 1); assert(rc == 0);

    // Wait for all threads to finish, and check the flags.
    rc = nft_action_pool_shutdown(action_pool, -1);   assert(rc == 0);

    for (int i = 0; i < 4; i++) assert(0 == flags[i]);

    // Nifty won't let you do an invalid cast. Here we try to
    // use a subclass method with an instance of the parent class.
    // First, we create a nft_pool:
    nft_pool_h pool = nft_pool_new( 0, 0, 0); assert(pool != NULL);

    // Now we attempt to use the nft_pool in a call to nft_action_pool_add.
    // We have to typecast the handle. Although the compiler is fooled
    // by the typecast, Nifty's run-time type-checking is not fooled:
    //
    assert(EINVAL == nft_action_pool_add((nft_action_pool_h) pool, (void*) 1));

    fputs("passed.\n", stderr);
}

int
main(int argc, char *argv[])
{
    basic_tests();

    test_nft_action_pool();

    printf("nft_pool: All tests passed.\n");
    exit(0);
}
#endif // MAIN
