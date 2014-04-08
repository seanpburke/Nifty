/******************************************************************************
 * (C) Copyright Xenadyne Inc, 2002 - 2013 ALL RIGHTS RESERVED.
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
 * File: nft_task.c
 *
 * Description: Threaded task scheduler for pthreads.
 *
 * This module defines a set of APIs that let you schedule functions
 * to be called asynchronously by another thread at a specified time.
 *
 * The API for this package is documented in nft_task.h.
 * The unit test below provides examples of usage (see #ifdef MAIN).
 *
 *
 * REALTIME SCHEDULING
 * ~~~~~~~~~~~~~~~~~~~
 * There are reasons you may wish to run the TaskThread at an elevated
 * realtime priority. You may want tasks to execute as nearly as possible
 * to their scheduled time, to minimize "jitter". When tasks are used
 * to implement time-outs, they may need an elevated priority in order
 * to preempt a thread that is performing a lengthy computation.
 *
 * Using realtime scheduling will often cause more problems than it solves,
 * but in this case, since scheduled tasks should be brief and nonblocking,
 * there is no reason to expect problems. Note that if you use realtime
 * scheduling, and your task spawns a thread, the spawned thread will by
 * default inherit the scheduling priority of the TaskThread. You should
 * explicitly override this, or use nft_pool instead of spawning a thread.
 *
 * With these caveats in mind, if you wish to use realtime scheduling,
 * uncomment the #define USE_REALTIME_SCHEDULING symbol below.
 *
 ******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <nft_task.h>

// Uncomment this definition to run the TaskThread at elevated priority.
// #define USE_REALTIME_SCHEDULING

#if    defined(USE_REALTIME_SCHEDULING) && !defined(_WIN32)
#include <unistd.h>
#if    defined(_POSIX_THREAD_PRIORITY_SCHEDULING)
#include <sched.h>
#endif
#endif

// Define the minimum size of the queue's task array.
#define MIN_SIZE   32

/* All of the pending tasks are stored in a queue that is structured as a heap.
 * The tasks array is 1-based, so min[] must be sized to MIN_SIZE + 1.
 */
typedef struct heap {
    unsigned    size;		 // size of heap (number of nodes)
    unsigned    count;		 // number of nodes in use
    nft_task ** tasks;		 // array of task pointers
    nft_task  * min[MIN_SIZE+1]; // initial minimal array
} heap_t;


// Forward prototypes for the heap functions.
static void		heap_init  (heap_t *heap);
static int		heap_insert(heap_t *heap, nft_task *  item);
static unsigned int	heap_top   (heap_t *heap, nft_task ** item);
static int		heap_pop   (heap_t *heap, nft_task ** item);
static void		heap_delete(heap_t *heap, unsigned    index);


// Local static data.
static heap_t		Queue;		// This heap holds the task queue.
static nft_task_h	CurrentTask;	// Handle to executing task.


/* The Queue and other local static vars are protected from concurrent access
 * by QueueMutex. The scheduler thread uses QueueCond to control its behavior.
 * All of them are initialized under QueueOnce.
 */
static pthread_once_t   QueueOnce  = PTHREAD_ONCE_INIT;
static pthread_mutex_t	QueueMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	QueueCond  = PTHREAD_COND_INITIALIZER;
static pthread_t	TaskThread;  // The task scheduler thread id.

// Define the helper functions nft_task_cast, _handle, _lookup, and _discard.
NFT_DEFINE_WRAPPERS(nft_task,)


/*-----------------------------------------------------------------------------
 * task_thread - The scheduler thread.
 *-----------------------------------------------------------------------------
 */
static void *
task_thread(void * ignore)
{
    int rc = pthread_mutex_lock(&QueueMutex); assert(rc == 0);

    while (1) // This loop never exits.
    {
	nft_task * task = NULL;

	// If there is a task in the queue...
	if (heap_top(&Queue, &task))
	{
	    // Wait for the topmost task to come due, or for a new task to be scheduled.
	    rc = pthread_cond_timedwait(&QueueCond, &QueueMutex, &task->abstime); assert((rc == 0) || (rc == ETIMEDOUT));
	}
	else {
	    // There is no pending task - wait for condition to be signaled by nft_task_schedule().
	    rc = pthread_cond_wait(&QueueCond, &QueueMutex); assert(rc == 0);
	}

	// Get the time that we woke up.
	struct timespec curr = nft_gettime();

	/* While there are tasks on the queue, check the topmost task
	 * to see if its abstime has been reached. If so, pop the queue
	 * and execute the task.
	 *
	 * If it's a repeated task, compute the new abstime and insert
	 * back into the queue. Otherwise, free the task.
	 *
	 * Repeat until the queue is empty, or the top task is not yet due.
	 */
	while (heap_top(&Queue, &task))
	{
	    if ((task->abstime.tv_sec >   curr.tv_sec) ||
		((task->abstime.tv_sec == curr.tv_sec) &&
		 (task->abstime.tv_nsec > curr.tv_nsec)))
		break; // Task not yet due to execute.

	    // Pop the top task from the queue.
	    rc = heap_pop(&Queue, &task); assert(rc == 1);

	    /* If the task is a repeated task, reinsert it now, otherwise free it.
	     * This is important to allow a task to cancel itself - the task must
	     * be enqueued in order to be cancelable.
	     */
	    int discard = 1;
	    if (task->interval.tv_sec || task->interval.tv_nsec)
	    {
		// Periodic task. Increment abstime (rather than current time)
		// by the task interval, to prevent cumulative drift.
		task->abstime.tv_sec  += task->interval.tv_sec;
		task->abstime.tv_nsec += task->interval.tv_nsec;

		// Normalize abstime.
		task->abstime.tv_sec += task->abstime.tv_nsec / NANOSEC;
		task->abstime.tv_nsec = task->abstime.tv_nsec % NANOSEC;

		// Re-insert task into queue, and remember not to discard it.
		heap_insert(&Queue, task);
		discard = 0;
	    }
	    /* Yield the queue mutex while the task action executes,
	     * in case the action needs to add or cancel a task.
	     */
	    rc = pthread_mutex_unlock(&QueueMutex); assert(rc == 0);

	    CurrentTask = nft_task_handle(task);
	    task->action(task);
	    CurrentTask = NULL;
	    if (discard) nft_task_discard(task);

	    rc = pthread_mutex_lock(&QueueMutex); assert(rc == 0);
	}
    }
    // Unlock the scheduler queue.
    // Currently, the loop above will never exit, so this code is somewhat superfluous. -SEan
    rc = pthread_mutex_unlock(&QueueMutex); assert(rc == 0);
    return NULL;
}


/*-----------------------------------------------------------------------------
 * task_init  - Initialize the task scheduler. Called by pthread_once().
 *-----------------------------------------------------------------------------
 */
static void
task_init(void)
{
    pthread_attr_t	attr;
    int		   	rc;

    // Initialize the task queue.
    heap_init(&Queue);

    // Initialize the condition and mutex that guard the task queue.
    rc = pthread_cond_init (&QueueCond,  NULL); assert(rc == 0);
    rc = pthread_mutex_init(&QueueMutex, NULL); assert(rc == 0);

    // Initialize thread attributes to defaults.
    rc = pthread_attr_init(&attr); assert(rc == 0);

    rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); assert(rc == 0);

#if defined(USE_REALTIME_SCHEDULING) && defined(_POSIX_THREAD_PRIORITY_SCHEDULING)
    /*
     * Attempt to set realtime scheduling policy and priority.
     * Any failures that occur should be due to lack of support for
     * realtime scheduling on this platform, so we quietly ignore them.
     */
    if ((pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) == 0) &&
	(pthread_attr_setschedpolicy (&attr, SCHED_RR) == 0))
    {
	struct sched_param param;
	int                policy;
	rc = pthread_getschedparam(pthread_self(), &policy, &param); assert(rc == 0);
	param.sched_priority += 1;
	rc = pthread_attr_setschedparam(&attr, &param); assert(rc == 0);
    }
#endif

    // Create the task thread.
    rc = pthread_create(&TaskThread, &attr, task_thread, NULL); assert(rc == 0);

    // Free the thread attribute structure.
    pthread_attr_destroy(&attr);

    return;
}


/*-----------------------------------------------------------------------------
 * Privileged APIs - these should only be used by subclasses
 *-----------------------------------------------------------------------------
 */
void
nft_task_destroy(nft_core * p)
{
    // This destructor exists only so that subclasses derived from nft_task
    // can invoke it, rather than call nft_core_destroy directly.
    nft_core_destroy(p);
}

static void
nft_task_action(nft_task * t)
{
    // The default task action is to execute the user function,
    // but subclasses may override it.
    t->function(t->argument);
}

nft_task *
nft_task_create(const char    * class,
                size_t          size,
                struct timespec abstime,
                struct timespec interval,
                void         (* function)(void *),
                void          * argument)
{
    // Validate inputs.
    if (!(abstime.tv_sec || interval.tv_sec || interval.tv_nsec) || !function) return NULL;

    nft_task * task = nft_task_cast(nft_core_create(class, size));
    if (!task) return NULL;

    // Override the nft_core destructor with our own.
    task->core.destroy = nft_task_destroy;
    task->action       = nft_task_action;
    task->interval     = interval;
    task->function     = function;
    task->argument     = argument;

    if (abstime.tv_sec) {
	task->abstime  = abstime;
    }
    else {
	// If abstime isn't given, compute it as now plus one interval.
	task->abstime = nft_gettime();
	task->abstime.tv_sec  += interval.tv_sec;
	task->abstime.tv_nsec += interval.tv_nsec;
    }
    // Normalize the absolute time.
    task->abstime.tv_sec += task->abstime.tv_nsec / NANOSEC;
    task->abstime.tv_nsec = task->abstime.tv_nsec % NANOSEC;

    return task;
}

// It is important to note that this is equivalent to nft_task_discard,
// because the caller has released ownership of the task reference.
// The caller must not use the task pointer after this call returns,
// since it can easily happen that the task will have executed and
// been destroyed, before this call returns.
//
// Returns 0 on success, or EINVAL if the task parameter is invalid.
//
int
nft_task_schedule_task(nft_task * task)
{
    int error = 0;

    // Check that the task argument is a valid nft_task reference.
    if (!nft_task_cast(task)) return EINVAL;

    // Ensure task package is initialized.
    int rc = pthread_once(&QueueOnce, task_init); assert(rc == 0);

    // Lock the queue.
    rc = pthread_mutex_lock(&QueueMutex); assert(rc == 0);

    // Attempt to insert the task pointer into queue.
    if (heap_insert(&Queue, task))
    {
	// After inserting the task, obtain the current top pending task.
	nft_task * top_task;
	rc = heap_top(&Queue, &top_task); assert(rc > 0);

	/* If the new task that we just inserted is now at the top of the heap,
	 * signal the scheduler thread since the new entry must be due to execute
	 * sooner than the previous topmost task.
	 * Signal under the mutex to avoid a race condition in the WIN32 emulation of pthread_cond_wait().
	 */
	if (top_task == task) {
	    rc = pthread_cond_signal(&QueueCond); assert(rc == 0);
	}
    }
    else {
	// heap_insert failed, presumably due to memory exhaustion.
	assert(!"heap_insert failed");
	error = ENOMEM;

	// Normally, this call stores the task reference in the Queue.
	// Since that did not happen, we need to discard this reference.
	nft_task_discard(task);
    }
    rc = pthread_mutex_unlock(&QueueMutex); assert(rc == 0);

    return error;
}

// Attempt to remove the task from the Queue.
// It may not be found, if the task has already executed.
int
nft_task_cancel_task(nft_task * task)
{
    // Check that the task argument is a valid nft_task reference.
    if (!nft_task_cast(task)) return EINVAL;

    // Ensure task package is initialized.
    int rc = pthread_once(&QueueOnce, task_init); assert(rc == 0);

    // If the task is still in the queue, delete it and free the task object.
    rc = pthread_mutex_lock(&QueueMutex); assert(rc == 0);

    // Note that queue entries start at 1, not zero.
    for (unsigned i = 1; i <= Queue.count; i++)
	if (Queue.tasks[i] == task)
	{
	    // Discard the reference that was stored in the queue.
	    // This reference was created in nft_task_schedule by nft_task_create().
	    nft_task_discard(Queue.tasks[i]);

	    // Remove the task from the queue.
	    heap_delete(&Queue, i);
	    break;
	}
    rc = pthread_mutex_unlock(&QueueMutex); assert(rc == 0);
    return 0;
}


/******************************************************************************/
/*******								*******/
/*******		TASK PACKAGE PUBLIC APIS			*******/
/*******								*******/
/******************************************************************************/

/*-----------------------------------------------------------------------------
 * nft_task_schedule - Schedule a task for future execution by the scheduler.
 *
 * The abstime argument is defined as in pthread_cond_timedwait().
 * The interval argument is defined as in timer_settimer().
 * When the task comes due, the scheduler thread calls *function(argument).
 *
 * Either abstime or interval may be null.
 * If abstime is null, the task will begin running after one interval.
 * If interval is null, the task will run once at abstime.
 *
 * The function must free any resources associated with arg, if necessary.
 * If the task is cancelled, the arg is returned to the cancelling thread
 * so that it can be freed there.
 *-----------------------------------------------------------------------------
 */
nft_task_h
nft_task_schedule(struct timespec abstime,
		  struct timespec interval,
		  void         (* function)(void *),
		  void	        * argument)
{
    // Create the task.
    nft_task * task = nft_task_create(nft_task_class, sizeof(nft_task), abstime, interval, function, argument);
    if (!task) return NULL;

    // Save the handle - we cannot use task after calling nft_task_schedule_task.
    nft_task_h handle = nft_task_handle(task);
    int        error  = nft_task_schedule_task(task); assert(!error);

    return error ? NULL : handle;
}

/*-----------------------------------------------------------------------------
 * nft_task_cancel - Cancel a scheduler task.
 *
 * Returns the task->arg to the caller, in case the caller needs to free it.
 * Returns NULL if the task was not found, as with a one-shot that has already
 * executed. So yes, you need to make arg non-null to distinguish failure.
 *-----------------------------------------------------------------------------
 */
void *
nft_task_cancel(nft_task_h handle)
{
    void     * argument = NULL;
    nft_task * task = nft_task_lookup(handle);

    if (task) {
	int rc = nft_task_cancel_task(task); assert(rc == 0);

	// Save the function arg, _before_ we discard the nft_task reference
	// that we obtained from nft_task_lookup. It will not be safe to use
	// the task pointer after it has been discarded.
	argument = task->argument;
	nft_task_discard(task);
    }
    return argument;
}

/*-----------------------------------------------------------------------------
 * nft_task_this - Return the handle to the current task.
 *
 * This convenience function allows your task function to get its own
 * task handle, making it easy for repeating tasks to cancel themselves.
 * It should only be called within the task code.
 *-----------------------------------------------------------------------------
 */
nft_task_h
nft_task_this(void)
{
    int rc = pthread_mutex_lock(&QueueMutex); assert(rc == 0);
    nft_task_h task = CurrentTask;
    rc = pthread_mutex_unlock(&QueueMutex); assert(rc == 0);
    return task;
}


/******************************************************************************/
/*******								*******/
/*******		HEAP IMPLEMENTATION				*******/
/*******								*******/
/******************************************************************************/
#ifdef _WIN32
#define inline __inline
#endif

/* task_comp
 *
 * Determine earlier of two tasks, used to order the Queue.
 */
static inline int
task_comp(nft_task * t1, nft_task * t2)
{
    int sec_diff;

    /* Note that we invert the order of comparison, so that
     * smaller (earlier) times are at the top of the queue.
     */
    if ((sec_diff = (t2->abstime.tv_sec - t1->abstime.tv_sec)) != 0)
	return sec_diff;
    else
	return (t2->abstime.tv_nsec - t1->abstime.tv_nsec);
}

#define COMPARE_NODES(x, y) task_comp(heap->tasks[x], heap->tasks[y])

#define SWAP_NODES(x, y)			\
    {   void * temp    = heap->tasks[x];	\
	heap->tasks[x] = heap->tasks[y];	\
	heap->tasks[y] = temp;			\
    }

/* upheap()
 *
 * Starts from the bottom of the heap up, restoring the heap condition.
 * heap_insert() adds a new member to the end of the heap, then calls
 * this function so that the new element will percolate up until the
 * heap condition is satisfied.
 */
static void
upheap(heap_t *heap, unsigned int child)
{
    unsigned int parent;

    while (child > 1)
    {
	parent = child >> 1;

	/* If the parent is less than the child,
	 * exchange parent and child nodes in the heap.
	 */
	if (COMPARE_NODES(parent, child) < 0)
	    SWAP_NODES(parent, child)
	else
	    break;

	child = parent;
    }
}

/* downheap()
 *
 * Starts from the top of the heap down, restoring the heap condition.
 * heap_pop() removes the top element from the heap, puts the last
 * element in its place, then calls this function to restore the heap.
 */
static void
downheap(heap_t *heap, unsigned int i)
{
    unsigned int child1, child2, largest;

    while ( i <= (heap->count >> 1))	/* then i has at least one child */
    {
	child1 = i << 1;
	child2 = child1 + 1;

	/* Find the largest of the two children of i. If child2 is not
	 * within the heap, then child1 is by default.
	 */
	largest = child1;
	if ((child2 <= heap->count) &&
	    (COMPARE_NODES(child1, child2) < 0))
	    largest = child2;

	/* If i is less than its largest child, then exchange contents of i
	 * and largest, and repeat the process at largest.
	 */
	if (COMPARE_NODES(i, largest) < 0)
	    SWAP_NODES(i, largest)
	else
	    break;
	i = largest;
    }
}

/* heap_init - Initialize a heap.
 */
static void
heap_init(heap_t * heap)
{
    heap->size  = MIN_SIZE;
    heap->count = 0;
    heap->tasks = heap->min;
}

/* heap_resize
 *
 * Reallocate the heap->tasks array to the new size.
 * Returns true on success, else failure.
 */
static int
heap_resize(heap_t *heap, int nsize)
{
    nft_task ** tasks;

    assert((nsize >  heap->count));
    assert((nsize >= MIN_SIZE));
    assert((nsize != heap->size));

    if (heap->size == MIN_SIZE)
    {
	// Growing from minimal size, need to malloc tasks.
	assert(heap->tasks == heap->min);
	if (!(tasks = malloc((nsize + 1) * sizeof(nft_task *))))
	    return 0;
	memcpy(tasks + 1, heap->tasks + 1, MIN_SIZE * sizeof(nft_task *));
	heap->tasks = tasks;
    }
    else if (nsize == MIN_SIZE)
    {
	// Shrinking to minimal size, we can free tasks.
	assert(heap->tasks != heap->min);
	memcpy(heap->min + 1, heap->tasks + 1, MIN_SIZE * sizeof(nft_task *));
	free(heap->tasks);
	heap->tasks = heap->min;
    }
    else
    {
	// Reallocate the tasks array. Remember that the zeroth element is not used, so alloc nsize+1 elements.
	assert(heap->tasks != heap->min);
	if (!(tasks = realloc(heap->tasks, (nsize + 1) * sizeof(nft_task *))))
	    return 0;
	heap->tasks = tasks;
    }
    heap->size = nsize;
    return 1;
}

/* heap_insert
 *
 * Inserts a new item into the heap. The method is to  place the new node
 * at the end of the queue, and then call upheap() to restore the heap
 * condition, which will ensure that the largest item is at the head of
 * the queue.
 *
 * Returns true on success, false on malloc failure.
 */
static int
heap_insert(heap_t *heap, nft_task * item)
{
    /* Do we need to realloc heap->tasks?
     * Remember that the zeroth element is not used.
     */
    if (heap->count == heap->size)
	if (!heap_resize(heap, heap->size << 1))
	    return 0;

    assert(heap->count < heap->size);

    heap->count++;
    heap->tasks[heap->count] = item;
    upheap(heap, heap->count);
    return 1;
}

/* heap_top
 *
 * Returns the top item on the heap (or NULL), without removing it.
 * Returns the heap count.
 */
static unsigned int
heap_top(heap_t *heap, nft_task ** itemp)
{
    if (itemp) *itemp = heap->count ? heap->tasks[1] : NULL ;
    return heap->count;
}

/* heap_pop
 *
 * Removes the topmost item from the heap and returns it to the caller.
 * Returns 1 if a node was popped, or it returns zero if the heap was empty.
 */
static int
heap_pop(heap_t *heap, nft_task ** itemp)
{
    int result = 0;

    if (itemp) *itemp = heap->count ? heap->tasks[1] : NULL ;
    if (heap->count) {
	SWAP_NODES( 1, heap->count)
	heap->count--;
	downheap(heap, 1);
	result++;
    }
    // Realloc heap->tasks if less than one quarter full.
    if ((heap->count  <  (heap->size >> 2)) &&
	(MIN_SIZE     <= (heap->size >> 1)))
	heap_resize(heap, heap->size >> 1);

    return result;
}

/* heap_delete
 *
 * Deletes the node at index i in the heap. Refuses to delete 0.
 */
static void
heap_delete(heap_t *heap, unsigned int index)
{
    assert((index > 0) && (index <= heap->count));

    if (index > 0 && index < heap->count)
    {
	int comp = COMPARE_NODES(index, heap->count);
	SWAP_NODES( index, heap->count)
	heap->count--;

	if      (comp > 0) downheap(heap, index);
	else if (comp < 0)   upheap(heap, index);
    }
    else if (index == heap->count)
    {
	heap->count--;
    }

    // Realloc heap->tasks if less than one quarter full.
    if ((heap->count  <  (heap->size >> 2)) &&
	(MIN_SIZE     <= (heap->size >> 1)))
	heap_resize(heap, heap->size >> 1);
}


/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		TASK PACKAGE UNIT TEST				*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Assertions must be active in test code.
#endif
#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#else
#define random    rand
#endif

int Waiting = 0;

void
null_task(void * arg)
{
    // Does nothing
}

void
dot_task(void * arg)
{
    printf(".");
}

void
print_task(void * arg)
{
    fputs(arg, stderr);
}

void
cancel_task(void * arg)
{
    void * result;

    printf("Bang! ");
    result = nft_task_cancel(arg);
    assert(result != NULL);
    Waiting = 0;
}

void
cancel_self(void * arg)
{
    void * result = nft_task_cancel(nft_task_this());
    assert(result != NULL);
    Waiting = 0;
}

volatile int test_msec_count = 0;

void
test_msec(void * arg)
{
    static long save = 0;
    long        msec = (long) arg;
    /* printf("%d\n", msec); */
    assert(save <= msec);
    save = msec;
    test_msec_count--;
}

/* test_basic
 *
 * These tests demonstrate how to use the nft_task API.
 * Refer to test_subclass below, to see how to implement
 * a subclass that inherits from nft_task.
 */
void
test_basic() {
    // Schedule a series of tasks at one second intervals.
    {
	struct timespec absolute = nft_gettime();
	struct timespec interval = { 0, 0 };
	nft_task_h      task;

	printf("Testing a series of nine tasks at one-second intervals:\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "one\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "two\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "three\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "four\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "five\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "six\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "seven\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "eight\n");
	absolute.tv_sec += 1;
	task = nft_task_schedule(absolute, interval, print_task, "nine\n");

	// Wait until the scheduled tasks have executed.
	sleep(10);

	// The last task should not be cancelable.
	assert(!nft_task_cancel(task));

	fprintf(stderr, "Series test Passed!\n");
    }

    // Test a schedule/cancel/cancel sequence.
    {
	printf("Testing schedule/cancel/cancel - ");

	struct timespec absolute = nft_gettime();
	struct timespec interval = { 0, 0 };

	// Add one second to the current time.
	absolute.tv_sec += 1;

	void     * arg  = (void*) 123;
	nft_task_h task = nft_task_schedule(absolute, interval, dot_task, arg);
	if (!task) {
	    printf("nft_task_schedule failed!\n");
	    exit(1);
	}
	if (nft_task_cancel(task) != arg) {
	    printf("nft_task_cancel failed!\n");
	    exit(1);
	}
	if (nft_task_cancel(task) != NULL) {
	    printf("second nft_task_cancel should have failed!\n");
	    exit(1);
	}
	printf(" Passed!\n");
    }

    // Schedule a repeating task that prints a dot to the console once per second.
    {
	printf("Testing cancel of a repeating task:");

	struct timespec interval = { 1, 0 };
	void     * arg  = (void*) 123;
	nft_task_h task = nft_task_schedule((struct timespec){0,0}, interval, dot_task, arg);
	assert(task);

	// Cancel from the main thread after five seconds.
	for (int i = 0; i < 5; i++) {
	    sleep(1);
	    fflush(stdout);
	}
	printf("Bang! ");

	// nft_task_cancel should return the arg parameter.
	puts( (nft_task_cancel(task) == arg) ? " Passed!" : " Failed!");
    }

    // Schedule a repeating task that prints a dot to the console once per second.
    {
	printf("Testing scheduled cancel of a repeating task:");

	struct timespec interval = { 1, 0 };
	void          * arg  = (void *) 123;
	nft_task_h      task = nft_task_schedule((struct timespec){0,0}, interval, dot_task, arg);
	assert(task);

	// Schedule a task to cancel the dot_task in 5 seconds.
	struct timespec absolute = nft_gettime();
	absolute.tv_sec += 5;
	nft_task_schedule(absolute, (struct timespec){0,0}, cancel_task, task);

	Waiting = 1;
	while (Waiting)	{
	    sleep(1);
	    fflush(stdout);
	}
	// The dot task should be gone.
	puts( (nft_task_cancel(task) == NULL) ? " Passed!" : " Failed!");
    }

    // Schedule a repeating task that cancels itself.
    {
	printf("Testing self-cancel of a repeating task:");

	struct timespec absolute = { 0, 0 };
	struct timespec interval = { 1, 0 };
	void          * arg      = (void *) 123;
	nft_task_h      task     = nft_task_schedule(absolute, interval, cancel_self, arg);
	assert(task);

	Waiting = 1;
	while (Waiting)	{
	    sleep(1);
	    fflush(stdout);
	}

	// The task should be gone.
	puts( (nft_task_cancel(task) == NULL) ? " Passed!" : " Failed!");
    }

    // Stress test - schedule many tasks to execute randomly over 10 seconds.
    {
	int n = 10000, i;
	printf("Testing %d tasks over ten seconds:", n);
	fflush(stdout);

	/* Create a bunch of tasks over a 10-second interval.
	 * Function test_msec() verifies that tasks execute in correct order.
	 * The interval starts one second from now, so we expect all
	 * of the tasks to be created before the first one executes.
	 */
	struct timespec absolute = nft_gettime();
	for (i = 0; i < n; i++) {
	    long  msec = random() % 10000;
	    struct timespec ts = {
		absolute.tv_sec  + (msec / 1000) + 1,
		absolute.tv_nsec + (msec % 1000) * 1000000
	    };
	    nft_task_h task = nft_task_schedule(ts, (struct timespec){0,0}, test_msec, (void*) msec);
	    assert(task);
	}

	/* Wait for all of the tasks to finish.
	 * The first assert tests that no test_msec task has run as yet.
	 */
	assert(test_msec_count == 0);
	test_msec_count = i;
	while (test_msec_count > 0) {
	    sleep(1);
	    putc('.', stdout);
	    fflush(stdout);
	}
	assert(Queue.count == 0);
	assert(Queue.size  == MIN_SIZE);

	printf(" Passed!\n");
    }

    // Stress test - Overlap task scheduling and execution.
    {
	int n = 10000;
	int i;

	printf("Testing %d tasks over one second:", n);
	fflush(stdout);

	/* Here we create tasks that execute randomly over a one-second
	 * interval beginning now. The intent is to overlap task creation
	 * and execution.
	 */
	struct timespec absolute = nft_gettime();
	for (i = 0; i < n; i++)
	{
	    int msec = random() % 1000;
	    struct timespec ts = {
		absolute.tv_sec  + (msec / 1000),
		absolute.tv_nsec + (msec % 1000) * 1000000
	    };
	    nft_task_h task = nft_task_schedule(ts, (struct timespec){0,0}, null_task, NULL);
	    assert(task);
	}

	// Wait for all of the tasks to finish.
	while (Queue.count > 0)
	{
	    sleep(1);
	    putc('.', stdout);
	    fflush(stdout);
	}
	assert(Queue.size  == MIN_SIZE);

	printf(" Passed!\n");
    }
}

/*****************************************************************************************
 * nft_task_pool	- Demonstrate a subclass based on nft_task and nft_pool.
 *
 * The major caveat for the nft_task package, is that all tasks execute in the scheduler
 * thread. This means that, should any task be blocked, all subsequently scheduled tasks
 * will be delayed until the blocked task completes (if ever). The usual solution to this
 * problem, is to have your sheduled task submit a work item to a thread pool, in order
 * to guarantee that it cannot block the scheduler thread.
 *
 * Here, we illustrate a class that does this for you, using nft_task as a base class,
 * and incorporating a nft_pool to which all sheduled tasks are submitted when they
 * are ready to be executed.
 */
typedef struct nft_task_pool {
    nft_task task;	// nft_task is our base class
} nft_task_pool;

// You must define the class string, showing its derivation from nft_task_class.
#define nft_task_pool_class nft_task_class ":nft_task_pool"

// This macro expands to declare the nft_task_pool_cast, _handle, _lookup, and _discard methods.
NFT_DECLARE_WRAPPERS(nft_task_pool,static)

// This macro expands to definitions of the _cast, _handle, _lookup, and _discard methods.
NFT_DEFINE_WRAPPERS(nft_task_pool,static)

// Our class will submit scheduled tasks to this global thread pool for execution.
// We provide a pthread_once function to initialize it.
#include <nft_pool.h>
static nft_pool_h       OurPool      = NULL;
static pthread_once_t   OurPoolOnce  = PTHREAD_ONCE_INIT;
static void
nft_task_pool_init(void)
{
    // Since we don't want the task scheduler thread to block when it
    // submits a work item to the pool, we want to use a large queue limit.
    // Since we only need this for tasks that will be delayed by file and
    // network IO, we also want to allow a large number of threads.
    //
    OurPool = nft_pool_new(1024, // use a large queue_limit
			     64, // max_threads is also large
			      0);// stack_size is the system default
}

// This will override the default nft_task action.
// The default action executes the task function in the scheduler thread.
// Instead, we submit the task function with its argument to a thread pool.
void
nft_task_pool_action(nft_task * p)
{
    nft_task_pool * task = nft_task_pool_cast(p);

    // The entire point of this class is to be nonblocking,
    // so it might make sense to use nft_pool_add_wait with timeout
    // set to zero, which causes _add_wait to fail immediately with
    // ETIMEDOUT, rather than block. But the problem in that case is,
    // what to do with the task, since it will not be executed?
    //
    nft_pool_add(OurPool, task->task.function, task->task.argument);
}

// Our constructor only needs to initialize OurPool,
// and to override the default nft_task.action.
nft_task_pool *
nft_task_pool_create(struct timespec abstime,
                     struct timespec interval,
                     void         (* function)(void *),
                     void          * argument)
{
    // Ensure that that our thread pool has been created.
    int rc = pthread_once(&OurPoolOnce, nft_task_pool_init); assert(rc == 0);

    // Invoke the base class constructor, passing our class string and size.
    nft_task * task = nft_task_create(nft_task_pool_class, sizeof(nft_task_pool), abstime, interval, function, argument);
    if (!task) return NULL;

    task->action = nft_task_pool_action;
    return nft_task_pool_cast(task);
}

// nft_task_pool_schedule is just a slight varation on nft_task_schedule.
nft_task_pool_h
nft_task_pool_schedule(struct timespec abstime,
		       struct timespec interval,
		       void         (* function)(void *),
		       void          * argument)
{
    // Create the task.
    nft_task_pool * task = nft_task_pool_create(abstime, interval, function, argument);
    if (!task) return NULL;

    // Save the handle - we cannot use task after calling nft_task_schedule_task.
    nft_task_pool_h handle = nft_task_pool_handle(task);
    int error = nft_task_schedule_task((nft_task *)task);  assert(!error);
    if (error) {
	nft_task_pool_discard(task);
	handle = NULL;
    }
    return handle;
}

// sleeper
// This is a thread function for testing. It clears the indicated flag,
// after sleeping for a time corresponding to the flag's array index.
// E.g., sleeper(3) sleeps for 3 seconds and then zeroes flags[3].
// This task function would block the nft_task scheduler thread,
// so the nft_task_pool is a good solution.
//
int flags[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static void sleeper(void * arg) {
    long flag = (long) arg;
    fprintf(stderr,"\n\tsleeping for %ld seconds...", flag);
    sleep(flag);
    flags[flag] = 0;
}

void test_nft_task_pool()
{
    fputs("testing nft_task_pool...",stderr);

    // We schedule all of these tasks so that the delay plus sleep time
    // sums to ten seconds. So a task should begin sleeping once each
    // second, and they should all finish after ten seconds.
    //
    // Just change nft_task_pool_schedule to nft_task_schedule,
    // to see what would happen without the thread pool.
    //
    for (int delay = 1; delay < 10; delay++) {
	struct timespec when = nft_gettime();
	when.tv_sec += delay;
	long howlong = 10 - delay;
	nft_task_pool_h ph = nft_task_pool_schedule(when, (struct timespec){0,0}, sleeper, (void*)howlong);
	assert(ph != NULL);
    }
    // After all the tasks should have finished, confirm that the flags have been cleared.
    sleep(11);
    for (int i = 1; i < 10; i++) assert(!flags[i]);
    fputs("Passed!\n",stderr);
}

/****************************************************************************************/
void
test_heap()
{
    /* Verify that the heap sort works correctly.
     */
    heap_t heap;
    heap_init(&heap);

    printf("Testing the heap...");

    for (int i = 0; i < 10000; i++)
    {
	nft_task * task = malloc(sizeof(nft_task));

	// Treat i as a time in milliseconds.
	task->abstime.tv_sec  = (i / 1000);
	task->abstime.tv_nsec = (i % 1000) * 1000000;
	heap_insert(&heap, task);
    }
    for (int i = 0; i < 10000; i++)
    {
	nft_task * task;
	heap_pop(&heap, &task);
	assert(task->abstime.tv_sec  == (i / 1000));
	assert(task->abstime.tv_nsec == (i % 1000) * 1000000);
	free(task);
    }
    printf(" Passed!\n");
}

/*
 * main - Unit test for nft_task
 */
int
main(int argc, char *argv[])
{
    // Test the heap-sort implementation.
    test_heap();

    // Test the nft_task user APIs
    test_basic();

    // Test the subclass implementation
    test_nft_task_pool();

    printf("nft_task: All tests passed.\n");
    exit(0);
}

#endif /* MAIN */
