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
 * File:  nft_win32.h
 *
 * PURPOSE
 *
 * This package provides a subset of the pthread APIs using WIN32 primitives.
 *
 * CAVEATS
 *
 * This is a minimal emulation of pthreads to allow the 'Nifty' package 
 * to be used on WIN32 platforms. You can use this package to port other
 * pthreads code to Windows, but before you do so, you must ensure that
 * the code conforms to the following limitations:
 *
 * - No static initialization of mutexes, condition variables or rwlocks.
 *   If your code has things like:
 *
 *	pthread_mutex_t	MyMutex = PTHREAD_MUTEX_INITIALIZER;
 *
 *   you are going to have to add:
 *
 *	pthread_once_t	MyMutexOnce = PTHREAD_ONCE_INIT;
 *	static void
 *	MyMutex_Init(void)  { pthread_mutex_init(&MyMutex, PTHREAD_MUTEXATTR_DEFAULT); }
 *
 *   and insert calls to pthread_once to dynamically initialize the mutex:
 *
 *	pthread_once(&MyMutexOnce, MyMutex_Init);
 *
 *   The same applies to condition variables and rwlocks.
 *
 * - No pthread_cancel() or related calls.
 *
 * - The pthread_cleanup_push() and _pop() calls won't call the cleanup
 *   handlers if the thread calls pthread_exit(). This could be fixed.
 *
 *****************************************************************************
 */
#ifndef nft_win32_h
#define nft_win32_h

#ifdef WIN32
/*
 * Set various WIN32 flags
 */
#define NOGDICAPMASKS
#define NOVIRTUALKEYCODES
#define NOWINMESSAGES
#define NOWINSTYLES
#define NOSYSMETRICS
#define NOMENUS
#define NOICONS
#define NOKEYSTATES
#define NOSYSCOMMANDS
#define NORASTEROPS
#define NOSHOWWINDOW
#define OEMRESOURCE
#define NOATOM
#define NOCLIPBOARD
#define NOCOLOR
#define NOCTLMGR
#define NODRAWTEXT
#define NOGDI
#define NOKERNEL
#define NOUSER
#define NONLS
#define NOMB
#define NOMEMMGR
#define NOMETAFILE
/* #define NOMINMAX         */
#define NOMSG
#define NOOPENFILE
#define NOSCROLL
#define NOSERVICE
#define NOSOUND
#define NOTEXTMETRIC
#define NOWH
#define NOWINOFFSETS
#define NOCOMM
#define NOKANJI
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX

/*
 * Enable NT-4.0 specific API's.
 */
#define _WIN32_WINNT	0x0400

#include <windows.h>
#include <process.h> /* Thread creation */
#include <winbase.h> /* synchronization methods */

#ifndef ETIMEDOUT
#define ETIMEDOUT	200		/* not in MSVC errno.h, nor equivalent JEB last err ~50 */
#endif


/*
 * The following definitions provide a pthread subset on WIN32.
 */

/*
 * Threads
 */
typedef HANDLE pthread_t;

typedef struct
{
    unsigned detach_state;
    unsigned stack_size;
} pthread_attr_t;


int pthread_create(pthread_t            * h_thread_p,
		   const pthread_attr_t * attr,
		   void                 * (*start_func)(void *),
		   void                 * arg);
int  pthread_detach(pthread_t h_thread);
int  pthread_join  (pthread_t h_thread, void ** val_p);
void pthread_exit  (void    * value);


/*
 * Thread attributes.
 */
#define PTHREAD_CREATE_DETACHED	1
#define PTHREAD_CREATE_JOINABLE	2

#define PTHREAD_SCOPE_PROCESS	1
#define PTHREAD_SCOPE_SYSTEM	2

int pthread_attr_init		(pthread_attr_t *attr);
int pthread_attr_setscope	(pthread_attr_t *attr, unsigned scope);
int pthread_attr_setdetachstate	(pthread_attr_t *attr, unsigned detach_state);
int pthread_attr_setstacksize	(pthread_attr_t *attr, unsigned stack_size);
int pthread_attr_destroy  (const pthread_attr_t *attr);


/*
 * Mutexes
 */
typedef HANDLE pthread_mutex_t;

typedef struct
{
	int scope;
} pthread_mutexattr_t;

int pthread_mutex_init(pthread_mutex_t *mp, const pthread_mutexattr_t *attr);
int pthread_mutex_lock(pthread_mutex_t *mp);
int pthread_mutex_trylock(pthread_mutex_t *mp);
int pthread_mutex_unlock(pthread_mutex_t *mp);
int pthread_mutex_destroy(pthread_mutex_t *mp);

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_MUTEXATTR_DEFAULT ((pthread_mutexattr_t *) 0)


/*
 * Condition variables.
 */
typedef struct
{
    int waiters_count_;
    /* Number of waiting threads. */

    CRITICAL_SECTION waiters_count_lock_;
    /* Serialize access to <waiters_count_>. */

    HANDLE sema_;
    /* Semaphore used to queue up threads waiting for the condition */

} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER { 0 }

typedef struct
{
	int scope;
} pthread_condattr_t;

#define PTHREAD_CONDATTR_DEFAULT ((pthread_condattr_t *) 0)

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_destroy(pthread_cond_t *cond);


/*
 * pthread_oonce()
 *
 * The pthread_once_t records whether an init_routine has been run.
 */
typedef void * pthread_once_t;

#define PTHREAD_ONCE_INIT 0

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));


/* pthread_cleanup_push/pop()
 *
 * This is not a real implementation of the cleanup_push/pop calls.
 * The pthread_cleanup_pop() macro will run the cleanup function if
 * called in the normal flow and the flag is true, but the cleanup
 * handler is not called if the thread calls pthread_exit().
 * We don't support pthread_cancel(), so that isn't an issue.
 */
typedef struct _cleanup {
    void (*function)(void *);
    void  *argument;
} _cleanup_t;


#define	pthread_cleanup_push(fun, arg) { \
	_cleanup_t _cleanup_info; 	 \
	_cleanup_info.function = (fun);	 \
	_cleanup_info.argument = (arg);
 
#define	pthread_cleanup_pop(ex) \
	if (ex) (_cleanup_info.function(_cleanup_info.argument)); }


/*
 * Semaphores
 */
typedef HANDLE sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_destroy(sem_t *sem);


/*
 * Readers/Writer locks use the nft_rwlock implementation.
 */
#include <nft_rwlock.h>
typedef nft_rwlock_t pthread_rwlock_t;
#define pthread_rwlock_init(lock, attr)		nft_rwlock_init(lock)
#define pthread_rwlock_destroy(lock)		nft_rwlock_destroy(lock)
#define pthread_rwlock_rdlock(lock)		nft_rw_rdlock(lock)
#define pthread_rwlock_wrlock(lock)		nft_rw_wrlock(lock)
#define pthread_rwlock_trywrlock		nft_rw_wrtrylock(lock)
#define pthread_rwlock_unlock(lock)		nft_rw_unlock(lock)


#endif /* WIN32 */

#endif /* nft_win32_h */
