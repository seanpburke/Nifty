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
 * PURPOSE
 *
 * This package provides a facility to schedule a task to execute asyncronously
 * at a specified time in the future. The caller is given a handle that can be
 * used to cancel the task prior to execution.
 *
 * CAVEATS
 *
 * Tasks are executed in a scheduler thread that is dedicated for this purpose.
 * Task code must therefore synchronize access to shared data structures.
 * The task must not block for any length of time, as that will disrupt the
 * timely execution of other scheduled tasks. If the task will perform any
 * blocking operations, such as gethostbyname_r(), the task should submit the
 * work to a thread pool (see nft_pool.h), so that the task scheduler thread
 * doesn't block.
 *
 ******************************************************************************
 */
#ifndef nft_task_header
#define nft_task_header

#include <time.h>
#include <nft_core.h>

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
 * When the task comes due, the scheduler thread calls *function(arg).
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
nft_task_h  nft_task_schedule(const struct timespec * abstime,
			      const struct timespec * interval,
			      void		   (* function)(void *),
			      void		    * arg);

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


#endif // nft_task_header

