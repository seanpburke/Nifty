/******************************************************************************
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
 * File:  nft_task.h
 *
 * This package provides a facility to schedule a task to execute asyncronously
 * at a specified time in the future. The caller is given a handle that can be
 * used to cancel the task prior to execution. The APIs are documented in this
 * header file, but you should also review the examples in nft_task.c, in the
 * unit test code (see #ifdef MAIN).
 *
 * CAVEATS
 *
 * Tasks are executed in a scheduler thread that is dedicated for this purpose.
 * Task code must therefore synchronize access to shared data structures.
 * The task must not block for any length of time, as that will disrupt the
 * timely execution of other scheduled tasks. If the task will perform any
 * blocking operations, such as gethostbyname_r(), the task should submit the
 * work to a thread pool (see nft_pool.h), so that the task scheduler thread
 * doesn't block. There is an example in nft_task.c unit-test section, which
 * demonstrates a subclass of nft_task that does this.
 *
 ******************************************************************************
 */
#ifndef _NFT_TASK_H_
#define _NFT_TASK_H_

#include "nft_gettime.h" // for struct timespec

/* All of the client APIs refer the task object by its handle.
 * The handle is just an integer, but we define it as a pointer
 * in order to gain strict static type checking.
 */
typedef struct nft_task_h * nft_task_h;

/*
 * nft_task_schedule - Schedule a task for future execution by the scheduler.
 *
 * The abstime argument is defined as in pthread_cond_timedwait().
 * The interval argument is defined as in timer_settimer().
 * When the task comes due, the scheduler thread calls *function(argument).
 *
 * Either of the abstime or interval arguments may be null. If abstime
 * is null, the task will begin executing after one interval from the
 * time of the schedule call. If interval is null, the task will execute
 * once at abstime.
 *
 * The function must free any resources associated with arg, if necessary.
 * If the task is cancelled, the arg is returned to the cancelling thread
 * so that it can be freed there.
 */
nft_task_h  nft_task_schedule(struct timespec abstime,
			      struct timespec interval,
			      void	   (* function)(void *),
			      void          * argument);

/*
 * nft_task_cancel - Cancel a scheduler task.
 *
 * Returns the task->arg to the caller, in case the caller needs to free it.
 * Returns NULL if the task was not found, as with a one-shot that has already
 * executed.
 */
void *	    nft_task_cancel(nft_task_h taskh);


/*
 * nft_task_this - Return the handle of the current task.
 *
 * This convenience function allows your task function to get its own
 * task handle, making it easy for repeating tasks to cancel themselves.
 * It should only be called within the task code.
 */
nft_task_h  nft_task_this(void);



/******************************************************************************
 *
 * The nft_task package is completely functional, using only the APIs
 * that are declared above this point. The declarations that follow,
 * are _only_ needed if you wish to implement a subclass based on nft_task.
 * You will find a demonstration how to do this in src/nft_task.c.
 * Look for the section on nft_task_pool.
 *
 ******************************************************************************
 */
#include <nft_core.h>

/* The nft_task is a subclass of nft_core.
 * For more information, consult the Nifty README.txt.
 */
typedef struct nft_task
{
    nft_core        core;
    long            index;		// position in heap

    struct timespec abstime;		// absolute time to perform task
    struct timespec interval;		// period to repeat task
    void         (* action)(struct nft_task *);
    void	 (* function)(void *);	// task user function
    void	  * argument;		// argument to user function
} nft_task;

nft_task *
nft_task_create( const char    * class,
		 size_t          size,
		 struct timespec abstime,
		 struct timespec interval,
		 void         (* function)(void *),
		 void          * argument );
void nft_task_destroy(nft_core * p);
int  nft_task_schedule_task(nft_task * task);
int  nft_task_cancel_task(nft_task * task);

// Declare helper functions nft_task_cast, _handle, _lookup, _discard
#define nft_task_class nft_core_class ":nft_task"
NFT_DECLARE_CAST(nft_task)
NFT_DECLARE_HANDLE(nft_task)
NFT_DECLARE_LOOKUP(nft_task)
NFT_DECLARE_DISCARD(nft_task)

#endif // _NFT_TASK_H_
