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
 * PURPOSE - Synchronized FIFO message queues for use in threaded applications.
 *
 * This package provides functions that enable threads to exchange messages.
 * You can set a limit on the number of items in the queue, and you can
 * assign a destructor function that will free any remaining items when the
 * queue is destroyed.
 *
 * The nft_queue_pop_wait() call is provided to enable a thread to block while
 * waiting for an item to be placed on the queue.
 *
 * The queue_append_wait() call enables the thread to block for a limited time
 * when the queue is full. The blocked append will complete if a pop operation
 * creates space to queue a new item before the timeout expires.
 *
 * These calls are cancellation-safe (but NOT async-cancel safe!) so you
 * can safely cancel a thread that is blocked in one of the _wait() calls.
 *
 * CAVEATS
 *
 * Note that the pop operations return NULL on timeout, which means that
 * you cannot distinguish timeouts if null pointers are present in the queue.
 * 
 *******************************************************************************
 */
#ifndef nft_queue_header
#define nft_queue_header

/* All of the client APIs refer the queue object by its handle.
 * The handle is just an integer, but we define it as a pointer
 * in order to gain strict static type checking.
 */
typedef struct nft_queue_h * nft_queue_h;

/*	Create an empty queue.
 *
 * 	Arguments:
 *
 *	limit   When the number of items in the queue reaches this limit,
 *		further additions will block until space is freed.
 *      	The queue grows by doubling, so setting the limit to a
 *      	power of two will be most efficient. If limit is zero,
 *      	the queue may grow until memory is exhausted. If limit
 *      	is negative, the queue cannot grow from its initial size.
 *
 *	destroyer  Frees queued items when the queue is destroyed.
 *
 *	Returns	NULL on malloc failure.
 */
nft_queue_h nft_queue_create(int limit,  void  (*destroyer)(void *));


/*	Append an item to the tail of the queue.
 *  	If the queue limit had been reached, this function
 *	 will block until items are removed, creating free space.
 *
 *  	Returns zero 	- on success.
 *		EINVAL	- not a valid queue.
 *		ENOMEM	- malloc failed
 */
int	nft_queue_add(nft_queue_h queue, void * item);


/*	Like nft_queue_add(), but if the queue limit is reached,
 *	may wait until the queue shrinks, depending on the timeout:
 *
 *		timeout >   0 - wait up to timeout seconds.
 *		timeout ==  0 - return ETIMEDOUT immediately.
 *		timeout == -1 - wait indefinitely.
 *	
 *  	Returns zero 	  - on success.
 *		EINVAL	  - not a valid queue.
 *		ENOMEM	  - malloc failed.
 *		ETIMEDOUT - operation timed out.
 */
int	nft_queue_add_wait(nft_queue_h queue, void * item, int timeout);


/*	Return the first item in the queue, or block indefinitely
 *	until an item is enqueued.
 */
void  * nft_queue_pop(nft_queue_h queue);



/*	Like nft_queue_pop(), but if the queue remains empty, 
 *	this call may return NULL, depending on the timeout:
 *
 *		timeout >   0 - return NULL after timeout seconds.
 *		timeout ==  0 - return NULL immediately.
 *		timeout == -1 - wait indefinitely.
 */
void  * nft_queue_pop_wait(nft_queue_h queue, int timeout);


/*	Shutdown an active queue.
 *
 *      This call invalidates the queue and destroys it.
 *
 *	Shutdown invalidates this queue, so that attempts to
 *      invoke a nft_queue API with this queue handle will fail.
 *
 *      Shutdown awakens threads that are blocked on this queue.
 *      Threads that were blocked in a call to nft_queue_pop
 *      will receive a return value of NULL, and threads blocked
 *      on nft_queue_add or _push will receive EINVAL.
 *     
 *      Once the queue has been released, the destroyer function
 *	is applied to all the items remaining in the queue,
 *      and then the queue itself is freed.
 *
 *	nft_queue_shutdown returns when this process is complete.
 *
 *	Returns zero 	- on success.
 *  		EINVAL	- not a valid queue.
 */
int	nft_queue_shutdown( nft_queue_h q);


/*	How many items are currently in the queue?
 *	Returns -1 if the queue is invalid.
 */
int	nft_queue_count(nft_queue_h queue);

/*	Return the first item on the queue, without popping it.
 *	Returns NULL if the queue is empty or invalid.
 */
void  * nft_queue_peek(nft_queue_h q);

/*	Replaces the queue's current destroyer function.
 *	Returns the previous destroyer function.
 */
void (*nft_queue_set_destroyer(nft_queue_h h, void (*destroyer)(void *)))(void *);


#endif // nft_queue_header
