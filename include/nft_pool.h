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
 * PURPOSE
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
 * is used, otherwise it must be 16K or more.
 *
 * If either argument does not satisfy the required minimum, it will be
 * silently increased to the minimum value.
 * 
 * Returns NULL on a malloc failure.
 */
nft_pool_h nft_pool_create(int queue_limit, int max_threads, int stack_size);


/* nft_pool_add:  Submit a work item to the pool.
 *
 * This function enqueues a work item that consists of a function and
 * an argument to be passed to it. This call will block indefinitely
 * while the pool's queued work items are at its queue_limit.
 * See nft_pool_add_wait below for a timeout option.
 *
 * Returns zero on success, otherwise:
 *	EINVAL The pool argument is not a valid thread pool.
 *	ENOMEM malloc() failed.
 */
int nft_pool_add(nft_pool_h pool, void (*function)(void *), void * argument);

/* nft_pool_add_wait: Enqueue a work item, with a timeout.
 *
 * This function work like nft_pool_add, except that when the pool's
 * work item queue is at its limit:
 *
 *	timeout  < 0 :	will wait indefinitely
 *      timeout == 0 :	will return ETIMEDOUT immediately
 *      timeout  > 0 :	will return ETIMEDOUT after timeout seconds
 *------------------------------------------------------------------------------
 */
int
nft_pool_add_wait(nft_pool_h handle, int timeout, void (*function)(void *),  void * argument);


/* nft_pool_shutdown: Free resources associated with thread pool.
 *
 * If work items are in the pool, the calling thread blocks until all
 * of the tasks have been performed. While waiting, the calling thread
 * may be cancelled, in which case the pool will be freed when and
 * if the last worker thread exits.
 *
 * Returns zero on success, otherwise:
 *	EINVAL The pool argument is not a valid thread pool.
 * 
 */
int nft_pool_shutdown(nft_pool_h pool);

#endif // nft_pool_header
