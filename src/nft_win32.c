/***********************************************************************
 * (C) Xenadyne Inc. 2002-2013.	ALL RIGHTS RESERVED
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
 * File:  nft_win32.c
 *
 * Description: Emulates certain pthread calls using the WIN32 API.
 *
 *********************************************************************** 
 */
#ifdef _WIN32

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#include <nft_gettime.h>
#include <nft_win32.h>


#define PError(funcName) fprintf(stderr, "Error: %s returned error %d\n", funcName, GetLastError());

// This struct allows us to pass the user's (start_func, arg) to thread_top.
struct thread_parms
{
    void * (*start_func)(void *);
    void * user_arg;
};

/* thread_top - base function for a user thread.
 * 
 * This function is actually spawned by pthread_create(),
 * which passes the user start function and args to it,
 * via the (struct thread_args *). That enables us to handle
 * cleanup here when the start_func returns without calling
 * pthread_exit().
 */ 
static unsigned __stdcall thread_top(void * arg)
{
    struct thread_parms * parms = arg;
    void * (*start_func)(void *) = parms->start_func;
    void *              user_arg = parms->user_arg;

    // Free the thread_parms block, before running the thread function.
    free(parms);
    _endthreadex((unsigned) start_func(user_arg));
    return 0;
}

int pthread_create (pthread_t *h_thread_p,
		    const pthread_attr_t *attr,
		    void * (*start_func)(void *),
		    void * user_arg)
{
    struct thread_parms * parms;
    unsigned stack_size = 0;
	HANDLE   h_thread;
    unsigned threadid;

    if (NULL == h_thread_p || NULL == start_func)
	return EINVAL;

    if ((parms = malloc(sizeof(struct thread_parms))) != NULL) {
	parms->start_func = start_func;
	parms->user_arg   = user_arg;
    }
    else
	return ENOMEM;

    // Take stack size from thread attributes.
    if (NULL != attr)
	stack_size = attr->stack_size;

    // Spawn the thread. 
    if ((h_thread = (HANDLE)_beginthreadex(NULL, stack_size, thread_top, parms, 0, &threadid)) < 0)
	return errno;

    // Detached thread?
    if (attr && (attr->detach_state == PTHREAD_CREATE_DETACHED))
	CloseHandle(h_thread);

    *h_thread_p = h_thread;
    return 0;
}

int pthread_join(pthread_t h_thread, void ** val_p)
{
    // We are not sure that the thread does not exist, so return EINVAL rather than ESRCH.
    if (WaitForSingleObject(h_thread, INFINITE) == WAIT_FAILED)
	return EINVAL;

    if (val_p != NULL)
	GetExitCodeThread(h_thread, (LPDWORD) val_p);

    CloseHandle(h_thread);
    return 0;
}

int pthread_detach (pthread_t h_thread)
{
    if (CloseHandle(h_thread) == 0)
	return EINVAL;
    return 0;
}

void pthread_exit (void * value)
{
    _endthreadex((unsigned) value);
}

int pthread_attr_init (pthread_attr_t *attr)
{
    attr->stack_size = 0;
    attr->detach_state = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_setdetachstate  (pthread_attr_t *attr, unsigned detach_state)
{
    if ((detach_state == PTHREAD_CREATE_DETACHED) ||
	(detach_state == PTHREAD_CREATE_JOINABLE))
	attr->detach_state = detach_state;
    else
	return EINVAL;
    return 0;
}

int pthread_attr_setscope  (pthread_attr_t *attr, unsigned scope)
{
    // Unless scope is invalid, this thread attribute is ignored.
    if ((scope != PTHREAD_SCOPE_PROCESS) &&
	(scope != PTHREAD_SCOPE_SYSTEM))
	return EINVAL;
    return 0;
}

int pthread_attr_setstacksize  (pthread_attr_t *attr, unsigned stack_size)
{
    attr->stack_size = stack_size;
    return 0;
}

int pthread_attr_destroy(const pthread_attr_t *attr)
{
    return 0;
}

//________________________________________________________________________________________
//
// Mutex implementation
//
int pthread_mutex_init (pthread_mutex_t *mp, const pthread_mutexattr_t *attr)
{
    if (NULL == mp)
	return EINVAL;

    *mp = CreateMutex(NULL, FALSE, NULL);
    if (!*mp) {
	PError("CreateMutex");
	return -1;
    }
    return 0;
}

int pthread_mutex_lock (pthread_mutex_t *mp)
{
    if (mp == NULL)
	return EINVAL;
	
    /* Ensure that the lock has been initialized.
     * Use an assert to ensure that uninitialized locks are detected
     * during  development. Compile with /D "NDEBUG" to suppress asserts.
     */
    assert(*mp != NULL);
    if (*mp == NULL)
	return EFAULT;

    switch(WaitForSingleObject(*mp, INFINITE)) {
    case WAIT_OBJECT_0:	return 0;	/* Success */
    case WAIT_TIMEOUT:	return EBUSY;	/* Should not happen */
    default:		PError("WaitForSingleObject");
			return EFAULT;
    }
}

int pthread_mutex_trylock (pthread_mutex_t *mp)
{
    if (mp == NULL)
	return EINVAL;
	
    /* Ensure that the lock has been initialized.
     * Use an assert to ensure that uninitialized locks are detected
     * during  development. Compile with /D "NDEBUG" to suppress asserts.
     */
    assert(*mp != NULL);
    if (*mp == NULL)
	return EFAULT;

    switch(WaitForSingleObject(*mp, 0)) {
    case WAIT_OBJECT_0:	return 0;	/* Lock acquired */
    case WAIT_TIMEOUT:	return EBUSY;	/* Lock already held */
    default:		PError("WaitForSingleObject");
			return EFAULT;
    }
}

int pthread_mutex_unlock (pthread_mutex_t *mp)
{
    if (NULL == mp)
	return EINVAL;

    /* Ensure that the lock has been initialized.
     * Use an assert to ensure that uninitialized locks are detected
     * during  development. Compile with /D "NDEBUG" to suppress asserts.
     */
    assert(*mp != NULL);
    if (*mp == NULL)
	return EFAULT;

    if (0 == ReleaseMutex(*mp)) {
	PError("ReleaseMutex");
	return EFAULT;
    }
    return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *mp)
{
    if (NULL == mp)
	return EINVAL;

    if (*mp && !CloseHandle(*mp)) {
	PError("CloseHandle");
	return EFAULT;
    }
    return 0;
}

//________________________________________________________________________________________
//
// Condition variables
//
//
// Implementing condition variables under WIN32 is tricky.
// This approach is based on information in
//	Strategies for Implementing POSIX Condition Variables on Win32
//	Douglas C. Schmidt and Irfan Pyarali
//	Department of Computer Science Washington University, St. Louis, Missouri
//
// This approach is utilized in ACE (//www.cs.wustl.edu/~schmidt/ACE.html).
// tho I've simplified it at the cost of eliminating any attempt at "fairness".
//
// We utilize the Windows NT 4.0 SignalObjectAndWait function, 
// which is not available on Windows CE, 95 or NT3.51, so this
// code will only work an NT4, W2K, WXP, etc.
//
int
pthread_cond_init (pthread_cond_t *cv,
		   const pthread_condattr_t * attr)
{
    cv->waiters_count_ = 0;
    cv->sema_ = CreateSemaphore (NULL,	     /* no security */
				 0,          /* initially 0 */
				 0x7fffffff, /* max count */
				 NULL);      /* unnamed */
    InitializeCriticalSection (&cv->waiters_count_lock_);

    return 0;
}



// The pthread_cond_wait function uses waits for the sema_ semaphore to be signaled,
// which indicates that a pthread_cond_broadcast or pthread_cond_signal has occurred.
//
// I'm concerned whether spurious or missed wakeups could result because the decrement
// of cv_waiters doesn't happen under the mutex. I've adopted the practice of calling
// pthread_cond_signal() and _broadcast() under the mutex, until I can resolve this.
//
int
pthread_cond_wait (pthread_cond_t *cv,
                   pthread_mutex_t *external_mutex)
{
    return pthread_cond_timedwait(cv, external_mutex, NULL);
}


// The pthread_cond_timedwait function is similar to cond_wait.
//
// The return values are listed below. Note that a POSIX implementation would
// not return EINTR on spurious wakeups, so perhaps this call should return 0
// when SignalObjectAndWait() returns WAIT_IO_COMPLETION.
//
// RETURNS
//	0		Success
//	EINVAL		Bad cv, mutex or abstime argument.
//	EINTR		Interrupted by an 'asynchronous procedure call' APC
//	ETIMEDOUT	Condition was not signaled during timeout interval.
//
int
pthread_cond_timedwait (pthread_cond_t *cv,
			pthread_mutex_t *external_mutex,
			const struct timespec *abstime)
{
    int errcode = 0;
    int wait;		/* wait interval in milliseconds */
    
    // If an abstime is given, compute the wait time, which is a relative 
    // interval in milliseconds, from the absolute time parameter.
    if (abstime != NULL) {
	struct timeb currtm;

	ftime(&currtm);
	wait  = (abstime->tv_sec - currtm.time) * 1000;
	wait += (abstime->tv_nsec/1000000 - currtm.millitm);

	if (wait < 10) wait = 10;
    }
    else {
	// Otherwise wait forever.
	wait = INFINITE;
    }

    // Avoid race conditions.
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_++;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    // This call atomically releases the mutex and waits on the  semaphore until
    // pthread_cond_signal() or pthread_cond_broadcast() are called by another thread.
    switch(SignalObjectAndWait (*external_mutex, cv->sema_, wait, FALSE))
    {
    case WAIT_FAILED:		errcode = EINVAL;	break;	/* Assume bad arguments */
    case WAIT_ABANDONED:				break;	/* Should not happen */
    case WAIT_IO_COMPLETION:	errcode = EINTR;	break;  /* Interrupted by an APC */
    case WAIT_OBJECT_0:					break;  /* The object was signaled */
    case WAIT_TIMEOUT:		errcode = ETIMEDOUT;	break;  /* Time-out interval elapsed. */
    }

    // Reacquire lock to avoid race conditions.
    EnterCriticalSection (&cv->waiters_count_lock_);

    // We're no longer waiting...
    cv->waiters_count_--;

    LeaveCriticalSection (&cv->waiters_count_lock_);

    // Always regain the external mutex before returning.
    WaitForSingleObject (*external_mutex, INFINITE);

    return errcode;
}


// The pthread_cond_signal function releases one waiting thread,
// by incrementing the sema_ semaphore by 1.
int
pthread_cond_signal (pthread_cond_t *cv)
{
    EnterCriticalSection (&cv->waiters_count_lock_);

    if (cv->waiters_count_ > 0)
	ReleaseSemaphore (cv->sema_, 1, 0);

    LeaveCriticalSection (&cv->waiters_count_lock_);

    return 0;
}


// The pthread_cond_broadcast function releases all waiting threads,
// which can be done atomically by passing the waiters_count_ to ReleaseSemaphore.
int
pthread_cond_broadcast (pthread_cond_t *cv)
{
    EnterCriticalSection (&cv->waiters_count_lock_);
    if (cv->waiters_count_ > 0)
	ReleaseSemaphore (cv->sema_, cv->waiters_count_, 0);
    LeaveCriticalSection (&cv->waiters_count_lock_);
    return 0;
}

// pthread_cond_destroy - Close handles associated with condition.
int
pthread_cond_destroy(pthread_cond_t *cv)
{
    if (NULL == cv)
	return EINVAL;
    if (!CloseHandle(cv->sema_)) {
	PError("CloseHandle");
	return EFAULT;
    }
    return 0;
}


//________________________________________________________________________________________
//
// Semaphores
//
int sem_init (sem_t *sem, int pshared, unsigned int value)
{
    *sem = CreateSemaphore(NULL, value, value, NULL);
    if (!*sem) {
	PError("CreateSemaphore");
	return -1;
    }
    return 0;
}

int sem_wait (sem_t *sem)
{
    switch(WaitForSingleObject(*sem, INFINITE))
    {
    case WAIT_FAILED:	PError("WaitForSingleObject");
			return EFAULT;
    case WAIT_TIMEOUT:	return EBUSY;
    default:		return 0; 
    }
}

int sem_post (sem_t *sem)
{
    if (ReleaseSemaphore(*sem, 1, NULL))
	return 0;
    else
	return 1;
}

int sem_destroy(sem_t *sem)
{
    if (NULL == sem)
	return EINVAL;

    if (!CloseHandle(*sem)) {
	PError("CloseHandle");
	return EFAULT;
    }
    return 0;
}

//________________________________________________________________________________________
//
int pthread_once (pthread_once_t  *once_control,
		  void		 (*init_routine)(void))
{
    if (!once_control || !init_routine)
	return EINVAL;

    /* The values for *once_control are:
     *
     *   0 - init_routine not yet performed.
     *   1 - init_routine currently executing.
     *   2 - init_routine has completed.
     */
    if (*once_control != (void*) 2)
    {
	/* InterlockedCompareExchange atomically compares *once_control to 0,
	 * and if they are equal sets *once_control to 1, returning the
	 * original value of *once_control. This ensures that init_routine()
	 * is only run once for a given once_control.
	 */
	if (InterlockedCompareExchange(once_control, (void*) 1, (void*) 0) == 0)
	{
	    (*init_routine)();

	    // When the init routine completes, increment control to 2.
	    InterlockedIncrement(once_control);
	}
	else {
	    // Spin-yield while other thread is executing init_routine.
	    while (*once_control != (void*) 2)
		Sleep(0);
	}
    }
    return 0;
}

#endif // _WIN32



