/*******************************************************************************
 * (C) Copyright Xenadyne, Inc. 2002-2013  All rights reserved.
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
 * File:  nft_queue.h
 *
 * Synchronized FIFO and LIFO message queues for use in threaded applications.
 *
 * This package provides functions that enable threads to exchange messages.
 * You can set a limit on the number of items in the queue, and you can
 * shut down the queue gracefully when you wish to.
 *
 * The nft_queue_pop_wait() call is provided to enable a thread to block
 * while waiting for an item to be placed on the queue.
 *
 * The nft_queue_add_wait() call enables the thread to block for a limited time
 * when the queue is full. The blocked thread will complete if a pop operation
 * creates space to queue a new item before the timeout expires. There is also
 * a nft_queue_push_wait() call, to operate the queue in LIFO (stack) mode.
 *
 * These calls are cancellation-safe (but NOT async-cancel safe!) so you
 * can safely cancel a thread that is blocked in one of the _wait() calls.
 *
 *******************************************************************************
 */
#ifndef _NFT_QUEUE_H_
#define _NFT_QUEUE_H_

/* The client APIs below refer the queue object by its handle.
 * The handle is just an integer, but we define it as a pointer
 * in order to gain strict static type checking.
 */
typedef struct nft_queue_h * nft_queue_h;

/*  Create an empty queue.
 *
 *  limit	When the number of items in the queue reaches this limit,
 *  		further enqueues will block until items are dequeued.
 *      	This limit is an upper bound - the initial size is small,
 *              and queue grows by doubling as needed. If limit is zero,
 *      	the queue may grow until memory is exhausted. If limit
 *      	is negative, the queue cannot grow from its initial size.
 *
 *  Returns	NULL on malloc failure.
 */
nft_queue_h nft_queue_create(int limit);


/*  Append an item to the tail of the queue.
 *  If the queue limit had been reached, this function
 *  will block until items are removed, creating free space.
 *
 *  Returns 	zero 	  - on success.
 *		EINVAL	  - not a valid queue.
 *		ENOMEM	  - malloc failed
 *              ESHUTDOWN - the queue has been shut down.
 */
int	nft_queue_add(nft_queue_h queue, void * item);


/*  Like nft_queue_add(), but if the queue limit is reached,
 *  may wait until the queue shrinks, depending on the timeout:
 *
 *  	timeout >   0 - wait up to timeout seconds.
 *  	timeout ==  0 - return ETIMEDOUT immediately.
 *  	timeout == -1 - wait indefinitely.
 *
 *  Returns:	zero      - on success.
 *		EINVAL    - not a valid queue.
 *		ENOMEM    - malloc failed.
 *		ETIMEDOUT - operation timed out.
 *              ESHUTDOWN - the queue has been shut down.
 */
int	nft_queue_add_wait(nft_queue_h queue, void * item, int timeout);


/*  Prepend an item to the head of the queue.
 *  If the queue limit had been reached, this function
 *  will block until items are removed, creating free space.
 *
 *  Returns	zero      - on success.
 *		EINVAL    - not a valid queue.
 *		ENOMEM    - malloc failed
 *              ESHUTDOWN - the queue has been shut down.
 */
int	nft_queue_push(nft_queue_h queue, void * item);


/*  Like nft_queue_push(), but if the queue limit is reached,
 *  may wait until the queue shrinks, depending on the timeout:
 *
 *	timeout >   0 - wait up to timeout seconds.
 *	timeout ==  0 - return ETIMEDOUT immediately.
 *	timeout == -1 - wait indefinitely.
 *
 *  Returns	zero 	  - on success.
 *		EINVAL	  - not a valid queue.
 *		ENOMEM	  - malloc failed.
 *		ETIMEDOUT - operation timed out.
 *              ESHUTDOWN - the queue has been shut down.
 */
int nft_queue_push_wait(nft_queue_h queue, void * item, int timeout);


/*  Return the first item in the queue, or block indefinitely
 *  until an item is enqueued. This call can return NULL
 *  if the queue handle is invalid, if the queue is shut down,
 *  or if a NULL was entered onto the queue.
 *
 *  If this call returns NULL, you can determine if the queue
 *  has been destroyed or shutdown by calling nft_queue_state.
 *  If you also need to distinguish the cases where the call
 *  timed out, or where a queue had been enqueued, then you
 *  can use nft_queue_pop_wait_ex (see below).
 *
 */
void * nft_queue_pop(nft_queue_h queue);


/*  Like nft_queue_pop(), but if the queue remains empty,
 *  this call may return NULL, depending on the timeout:
 *
 *  	timeout >   0 - Return NULL after timeout seconds.
 *  	timeout ==  0 - Return NULL immediately.
 *  	timeout == -1 - Wait indefinitely.
 */
void * nft_queue_pop_wait(nft_queue_h queue, int timeout);


/*  Like nft_queue_pop_wait, but returns an error code as the
 *  function result, and returns any dequeued item via itemp.
 *  Note that ETIMEDOUT is returned if the queue was empty,
 *  even when the timeout parameter is zero.
 *
 *  Returns:	zero		An item was dequeued.
 *		EINVAL  	Queue handle is invalid.
 *		ETIMEDOUT	Queue empty and wait timed out.
 *		ESHUTDOWN	Queue empty and has been shutdown.
 */
int nft_queue_pop_wait_ex(nft_queue_h h, int timeout, void ** itemp);


/*  Shutdown an active queue.
 *
 *  This call prevents any more items being enqueued,
 *  and optionally waits for the queue to become empty.
 *  The queue will be destroyed if it is empty when
 *  _shutdown returns.
 *
 *  Shutdown awakens threads that are blocked on this queue.
 *  Threads that were blocked in a call to nft_queue_pop_wait
 *  will receive the return value NULL, and threads blocked
 *  in nft_queue_add, _push or _pop_wait_ex will receive the
 *  error code ESHUTDOWN.
 *
 *  If the queue is not empty, pop operations will continue
 *  to succeed, while the queue is shutting down, but attempts
 *  to _add or _push items will fail with the error ESHUTDOWN.
 *  When the queue is empty, nft_queue_pop(_wait) will return
 *  NULL without waiting, and nft_queue_pop_wait_ex will
 *  return ESHUTDOWN, or EINVAL if the queue has been destroyed.
 *
 *  Depending on the timeout parameter, nft_queue_shutdown
 *  will return immediately, or wait for the queue to empty.
 *  If ETIMEDOUT is returned, the queue handle is still valid.
 *  The caller should pop and destroy the remaining queue items,
 *  and then call nft_queue_shutdown a second time to destroy it.
 *
 *  timeout < 0 Wait for queue to empty, queue will be destroyed.
 *  timeout = 0 Immediately return zero or ETIMEDOUT (see below).
 *  timeout > 0 Wait for queue to empty, returning zero or ETIMEDOUT.
 *
 *  Returns 	zero 	  - Queue is empty, and has been destroyed.
 *  		EINVAL	  - not a valid queue.
 *		ETIMEDOUT - Queue not empty, and will not be destroyed.
 */
int	nft_queue_shutdown( nft_queue_h q, int timeout );


/*  How many items are currently in the queue?
 *  Returns -1 if the queue is invalid.
 */
int	nft_queue_count(nft_queue_h queue);


/*  Return the first item on the queue, without popping it.
 *  Returns NULL if the queue is empty or invalid.
 */
void  * nft_queue_peek(nft_queue_h q);


/*  Returns:	zero		Queue is in operation.
 *		EINVAL  	Queue handle is invalid.
 *		ESHUTDOWN	Queue has been shutdown.
 */
int	nft_queue_state( nft_queue_h h);


/******************************************************************************
 *
 * The nft_queue package is completely functional, using only the APIs that
 * are declared above this point. But, you may wish to implement a subclass
 * based on nft_queue. For example, the nft_pool package derives from nft_queue.
 * The declarations that follow, are _only_ needed to author subclasses,
 * and they are generally not safe to use, unless you understand the risks.
 *
 ******************************************************************************
 */
#include <nft_core.h>

/* struct nft_queue
 *
 * The queued items are stored in a "circular array", meaning that
 * we access it using modulo arithmetic. This array is allocated
 * initially to the minimum size defined below, and grows by doubling.
 * Whenever the queue is less than one quarter full, the array size
 * is halved to free memory.
 */
#define NFT_QUEUE_MIN_SIZE 32
typedef struct nft_queue
{
    nft_core            core;
    int                 shutdown;// has queue been shutdown?
    int                 first;   // First item in array.
    int                 next;    // Next free item in array.
    int                 size;    // Size of array.
    int                 limit;   // Maximum number of items.
    pthread_mutex_t     mutex;   // Lock to protect queue.
    pthread_cond_t      cond;    // Signals waiting threads.
    void             ** array;   // Array holding queued items.
    void              * minarray[NFT_QUEUE_MIN_SIZE]; // Initial array
} nft_queue;

nft_queue *
nft_queue_create_ex(const char * class, size_t size, int limit);
int    nft_queue_enqueue(nft_queue * q, void * item, int timeout, char which);
int    nft_queue_dequeue(nft_queue * q, int timeout, void ** item);
void   nft_queue_destroy(nft_core * p);

// Declare helper functions nft_queue_cast, _handle, _lookup, _discard, _gather.
#define nft_queue_class nft_core_class ":nft_queue"
NFT_DECLARE_CAST(nft_queue)
NFT_DECLARE_HANDLE(nft_queue)
NFT_DECLARE_LOOKUP(nft_queue)
NFT_DECLARE_DISCARD(nft_queue)
NFT_DECLARE_GATHER(nft_queue)

#endif // _NFT_QUEUE_H_
