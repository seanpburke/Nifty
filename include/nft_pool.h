/******************************************************************************
 * (C) Copyright Xenadyne, Inc. 2002  All rights reserved.
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
 * File:  nft_pool.h
 *
 * This package provides a facility to submit tasks to a thread pool.
 * The thread pool is associated with one or more threads that execute
 * the tasks asynchronously.
 *
 * The API calls are documented in this file, but you should also
 * review the unit test driver in src/nft_pool.c (at #ifdef MAIN)
 * to see examples of this package in use.
 *
 * This package is inspired by Butenhof, section 7.2, first edition.
 * It also demonstrates Nifty's object-oriented style of development,
 * because it uses the nft_queue package as a base class.
 *
 * The Nifty style of "inheritance" is explained in the README.txt.
 * You may notice that this header file does not expose the structures
 * and APIs that would allow you to derive a subclass from this package.
 * That is simply laziness on my part.
 *
 ******************************************************************************
 */
#ifndef nft_pool_header
#define nft_pool_header

#include <pthread.h>

typedef struct nft_pool_h * nft_pool_h;

/* nft_pool_create: Initialize a thread pool.
 *
 * The max_threads argument sets the maximum number of worker threads
 * that will be created to service work items. It must be one or higher.
 *
 * The stack_size argument sets the maximum size in bytes of the worker
 * thread's stack region. It can be zero, in which case platform default
 * is used. If nonzero but less than NFT_POOL_MIN_STACK_SIZE, it will be
 * forced to the minimum.
 *
 * If either argument does not satisfy the required minimum, it will be
 * silently increased to the minimum value.
 * 
 * Returns NULL on a malloc failure.
 *
 *  Note that nft_pool_create is actually a convenience macro.
 *  The function nft_pool_create_f accepts class and size arguments,
 *  to enable subclasses to be authored, based on nft_pool.
 */
#define		nft_pool_create(queue_limit,max_threads,stack_size)     \
nft_pool_handle(nft_pool_create_f(nft_pool_class, sizeof(nft_pool),     \
                                  queue_limit, max_threads, stack_size));

/* nft_pool_add:  Submit a work item to the pool.
 *
 * This function enqueues a work item that consists of a function and
 * an argument to be passed to it. This call will block indefinitely
 * while the pool's queued work items are at its queue_limit.
 * See nft_pool_add_wait below for a timeout option.
 *
 * Returns zero on success, otherwise:
 *	EINVAL    - The pool handle is not valid.
 *	ENOMEM    - malloc() failed.
 *      ESHUTDOWN - the queue has been shut down.
 *
 * Note that you will usually get EINVAL if the pool has been
 * shut down. ESHUTDOWN is only returned when the shutdown
 * occurs while this call is waiting to queue the item.
 */
int nft_pool_add(nft_pool_h pool, void (*function)(void *), void * argument);

/* nft_pool_add_wait: Enqueue a work item, with a timeout.
 *
 * This function work like nft_pool_add, except that 
 * when the pool's queue has reached its limit:
 *
 *	timeout  < 0 :	will wait indefinitely
 *      timeout == 0 :	will return ETIMEDOUT immediately
 *      timeout  > 0 :	will return ETIMEDOUT after timeout seconds
 */
int
nft_pool_add_wait(nft_pool_h handle, int timeout, void (*function)(void *),  void * argument);


/* nft_pool_shutdown: Free resources associated with thread pool.
 *
 * After the call to shutdown, no new work items may be enqueued.
 * Pool threads will continue to process enqueued items that remain,
 * and the pool will be destroyed when the last pool thread exits.
 * Depending on the timeout parameter, the caller can elect to:
 *
 *	timeout  < 0	Wait indefinitely for processing to finish.
 *      timeout == 0	Return immediately.
 *      timeout  > 0	Wait up to timeout seconds.
 *
 * Returns zero on success, otherwise:
 *	EINVAL     The pool argument is not a valid handle.
 *	ETIMEDOUT  The timeout interval expired.
 *      ESHUTDOWN  The pool has already been shut down.
 *
 * Note that, as with nft_pool_add_wait, you will usually
 * get EINVAL if the queue has been shut down. It is not
 * often possible to distinguish the handle of a queue
 * that has been shutdown, from an invalid handle.
 */
int nft_pool_shutdown(nft_pool_h pool, int timeout);

/******************************************************************************
 *
 * The nft_pool package is completely functional, using only the APIs that
 * are declared above this point. But, you may wish to implement a subclass
 * based on nft_pool. The declarations that follow, are _only_ needed to 
 * author subclasses, and they are generally not safe to use, unless you
 * understand the risks.
 *
 ******************************************************************************
 */
#include <pthread.h>
#include <nft_queue.h>

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
NFT_DECLARE_CAST(nft_pool)
NFT_DECLARE_HANDLE(nft_pool)
NFT_DECLARE_LOOKUP(nft_pool)
NFT_DECLARE_DISCARD(nft_pool)

nft_pool *
nft_pool_create_f(const char * class,
		  size_t       size,
		  int queue_limit, int max_threads, int stack_size);

#endif // nft_pool_header
