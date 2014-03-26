/******************************************************************************
 * (C) Copyright Xenadyne Inc, 2013  All rights reserved.
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
 ******************************************************************************
 *
 * File: nft_handle.c
 *
 * Description: Implement the handle-management substem used by nft_core.
 *
 ******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <nft_core.h>
#include <nft_handle.h>

// This structure provides a global table that maps handles to nft_core objects.
typedef struct nft_handle_map {
    int        refcount;
    nft_core * object;
} nft_handle_map;

// HandleMapSize sets the initial size of the HandleMap array.
// HandleMapMax limits the number of active handles, and thus the
// number of nft_core instances. If a process reaches this limit,
// nft_handle_alloc will return NULL, just as if malloc had failed.
//
static nft_handle_map * HandleMap     = NULL;
static unsigned         HandleMapSize = (1 << 10); // must be a power of 2
static unsigned         HandleMapMax  = (1 << 20); // one million handles
static pthread_once_t   HandleOnce    = PTHREAD_ONCE_INIT;

static void handle_once(void);

// Initialize the nft_handle subsystem.
// Returns zero on success, or ENOMEM on failure.
int
nft_handle_init(void)
{
    // Ensure that the handle table and mutex are initialized.
    int rc = pthread_once(&HandleOnce, handle_once); assert(rc == 0);

    // Return an error if we cannot initialize the handle subsystem.
    return HandleMap ? 0 : ENOMEM;
}

#ifdef NFT_LOCKLESS
/*******************************************************************************
 *  Lockless implementation of the nft_handle API using GCC atomic builtins.
 *
 *	void         nft_handle_init(void);
 *	nft_handle   nft_handle_alloc(nft_core * object);
 *	nft_core   * nft_handle_lookup(nft_handle handle);
 *	int          nft_handle_discard(nft_handle handle);
 *	nft_handle * nft_handle_list(const char * class);
 *
 *  The implementation below uses GCC builtin atomic operations to implement
 *  lockless management of the handle table. It is experimental, and only
 *  enabled when compiling with -D NFT_LOCKLESS.
 *
 *  Below, there is an alternative implementation of the nft_handle APIs,
 *  using conventional POSIX pthread locking.
 *
 *******************************************************************************
 */

// This is a private function to Initialize the handle map and other globals.
// It is only called via pthread_once.
static void
handle_once(void)
{
    // Allocate the handle map.
    HandleMapSize  = HandleMapSize < HandleMapMax ? HandleMapSize : HandleMapMax ;
    size_t memsize = HandleMapSize * sizeof(nft_handle_map);
    if ((HandleMap = malloc(memsize)))
	for (int i = 0; i < HandleMapSize; i++)
	    HandleMap[i] = (nft_handle_map){ -1, NULL };
}

// Return a new, unique handle.
static nft_handle
handle_next(void)
{
    // We will increment this counter to generate a sequence of unique handles.
    static unsigned long NextHandle = 1;

    // Use gcc atomic builtin to increment NextHandle, so that no mutex is needed.
    nft_handle   handle = (nft_handle) __sync_fetch_and_add(&NextHandle, 1);

    // Since NULL is an invalid handle, increment once more in that case.
    if (!handle) handle = (nft_handle) __sync_fetch_and_add(&NextHandle, 1);

    return handle;
}

// Increment or decrement a positive reference count,
// returning the prior value of the reference count.
// Counters that are zero or negative, are not modified.
static int
counter_add(int * counter, int operand)
{
    // To ensure that we do not modify a zero count, we must use compare_and_swap.
    int prior = *counter;
    int count;
    while ((count = prior) > 0) {
	prior = __sync_val_compare_and_swap(counter, count, count + operand);
	if (prior == count) break;
    }
    return prior;
}

// Attempt to increment a positive (live) reference count.
static int
handle_map_increment(nft_handle_map * slot)
{
    // counter_add returns the prior count, so a positive result means
    // that the slot was live, and we have successfully incremented it.
    return (counter_add(&slot->refcount, 1) > 0);
}

// Attempt to decrement a positive (live) reference count.
// It is possible that the decrement will zero the count,
// in which case the slot is freed and the object destroyed.
static void
handle_map_decrement(nft_handle_map * slot)
{
    if (counter_add(&slot->refcount, -1) == 1)
    {
	nft_core * object = slot->object;

	// counter_add returns the prior count. Since it has returned 1,
	// the count is now zero, and no other thread should manipulate
	// this slot in any way. We are responsible to free the slot.
	// Once we decrement the count to -1, the slot becomes available
	// to handle_map_alloc().
	//
	assert(0 == slot->refcount);
	slot->object = NULL;

	// Free the slot
	int rc = __sync_bool_compare_and_swap (&slot->refcount,  0, -1);
	assert(rc);

	// We hold the sole reference to object. Invoke the object's destroy method.
	object->destroy(object);
    }
}

// Given a handle, compute the index of this handle's map table slot.
static unsigned
handle_hash(nft_handle handle, unsigned modulo)
{
    // For speed's sake, we do binary modulo arithmetic,
    // but this assumes that modulo is a power of two.
    return (unsigned long) handle & (modulo - 1); // handle % modulo;
}

// Given a nft_core object, allocate a handle and add it to the HandleMap.
nft_handle
nft_handle_alloc(nft_core * object)
{
    // Ensure that the handle table and mutex are initialized.
    if (nft_handle_init()) return NULL;

    // Scan for the next open slot in the HandleMap.
    for (unsigned n = 0; n < HandleMapSize; n++)
    {
	// Allocate a fresh unique handle and try again.
	nft_handle       handle = handle_next();
	unsigned         index  = handle_hash(handle, HandleMapSize);
	nft_handle_map * slot   = &HandleMap[index];

	// If this map slot is not in use, allocate it to this handle.
	// Free slots have a reference count -1, and slots with zero are "busy".
	if (__sync_bool_compare_and_swap (&slot->refcount, -1, 0)) {
	    object->handle = handle;
	    slot->object   = object;
	    __sync_bool_compare_and_swap (&slot->refcount,  0, 1);
	    return handle;
	}
    }
    // If the HandleMap table is full, we fail.
    return NULL;
}

// Look up a handle, atomically incrementing the object reference count.
nft_core *
nft_handle_lookup(nft_handle handle)
{
    if (!handle) return NULL; // NULL is an invalid handle by definition.

    // Compute our index into the HandleMap table.
    unsigned         index = handle_hash(handle, HandleMapSize);
    nft_handle_map * slot  = &HandleMap[index];

    if (handle_map_increment(slot) > 0)
    {
	// handle_map_acquire will only increment refcounts that were already positive,
	// so this result means that we have locked a live object reference.
	nft_core * object = slot->object;

	// Is this the object that we are looking for?
	if (handle == object->handle) return object;

	// This object has a different handle, so we must decrement our increment.
	handle_map_decrement(slot);
    }
    return NULL;
}

// Decrement the object's reference count. If the new reference
// count is zero, free the map slot and destroy the object.
// Returns zero on success, or EINVAL on invalid handles.
int
nft_handle_discard(nft_core * object)
{
    unsigned         index = handle_hash(object->handle, HandleMapSize);
    nft_handle_map * slot  = &HandleMap[index];

    // Unlike nft_handle_lookup, this should only be called on live objects.
    assert(slot->object == object);
    assert(slot->refcount > 0);
    if (slot->object != object || slot->refcount <= 0) return EINVAL;

    // Decrement the counter, and destroy the object if necessary.
    handle_map_decrement(slot);
    return 0;
}

void
nft_handle_apply(void (*function)(nft_core *, const char *, void *), const char * class, void * argument)
{
    // Ensure that the handle table and mutex are initialized.
    if (nft_handle_init()) return;

    for(unsigned i = 0; i < HandleMapSize; i++)
    {
	nft_handle_map * slot = &HandleMap[i];
	if (handle_map_increment(slot) > 0)
	{
	    nft_core * object = slot->object;

	    // Call function, passing object, class and argument.
	    function(object, class, argument);

	    handle_map_decrement(slot);
	}
    }
}

#else  // if not defined NFT_LOCKLESS

/*******************************************************************************
 *  POSIX portable implementation of the nft_handle APIs using mutexes.
 *
 *	void         handle_init(void);
 *	nft_handle   nft_handle_alloc(nft_core * object);
 *	nft_core   * nft_handle_lookup(nft_handle handle);
 *	int          nft_handle_discard(nft_handle handle);
 *	nft_handle * nft_handle_list(const char * class);
 *
 *  The implementation that is shown here, is a good compromise for
 *  simplicity, efficiency and portability.
 *
 *******************************************************************************
 */

// Define a mutex to protect the handle table and object reference counts.
static pthread_mutex_t  HandleMutex = PTHREAD_MUTEX_INITIALIZER;

// This is a private function to Initialize the handle map and other globals.
// It is only called via pthread_once.
static void
handle_once(void)
{
    // We initialize the mutex dynamically, in order to be compatible with
    // nft_win32 pthread emulation, which cannot statically initialize mutexes.
    int rc = pthread_mutex_init(&HandleMutex, NULL); assert(rc == 0);

    // Allocate the handle map.
    HandleMapSize  = HandleMapSize < HandleMapMax ? HandleMapSize : HandleMapMax ;
    size_t memsize = HandleMapSize * sizeof(nft_handle_map);
    if ((HandleMap = malloc(memsize))) memset(HandleMap,0,memsize);
}

static unsigned
handle_hash(nft_handle handle, unsigned modulo)
{
    // For speed's sake, we do binary modulo arithmetic,
    // but this assumes that modulo is a power of two.
    return (unsigned long) handle & (modulo - 1); // handle % modulo;
}

// Grow the handle map by doubling when it becomes full.
// Doubling ensures that no hash collisions will occur in the new map.
// The caller must hold HandleMapMutex.
static int
handle_map_enlarge(void)
{
    unsigned newsize = HandleMapSize << 1;
    size_t   memsize = newsize * sizeof(nft_handle_map);

    // Refuse to allocate more than HandleMapMax handles.
    if (newsize > HandleMapMax) return 0;

    nft_handle_map * newmap = malloc(memsize);
    if (newmap) {
	memset(newmap,0,memsize);

	// Copy all current objects to the new handle map.
	for (unsigned i = 0; i < HandleMapSize; i++)
	    if (HandleMap[i].refcount) {
		nft_handle handle = HandleMap[i].object->handle;

		// Confirm that this is not a hash collision.
		assert(newmap[handle_hash(handle, newsize)].refcount == 0);

		newmap[handle_hash(handle, newsize)] = HandleMap[handle_hash(handle, HandleMapSize)];
	    }

	// Replace the old HandleMap with the new map.
	free(HandleMap);
	HandleMap     = newmap;
	HandleMapSize = newsize;
	return 1;
    }
    return 0;
}

// Allocate a new handle for object, storing it in the HandleMap table.
// Returns the new handle, or NULL on failure.
nft_handle
nft_handle_alloc(nft_core * object)
{
    nft_handle handle = NULL;

    // We will increment this counter to generate a sequence of unique handles.
    static unsigned long NextHandle = 1;

    // Ensure that the handle table and mutex are initialized.
    if (nft_handle_init()) return NULL;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

rescan: // Scan for the next open slot in the HandleMap
    for (unsigned i = 0; i < HandleMapSize; i++, NextHandle++) {
	if (NextHandle) {
	    unsigned index = handle_hash((nft_handle)NextHandle, HandleMapSize);
	    if (HandleMap[index].refcount == 0) {
		object->handle = handle = (nft_handle) NextHandle++;
		HandleMap[index] = (nft_handle_map){ 1, object };
		break;
	    }
	}
    }
    // If the table is full, and we are able to enlarge it, try again.
    if (!handle && handle_map_enlarge())
	goto rescan;
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return handle;
}

// Look up a handle, atomically incrementing the object reference count.
nft_core *
nft_handle_lookup(nft_handle handle)
{
    nft_core * object = NULL;

    // NULL is an invalid handle by definition.
    if (!handle) return NULL;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);
    unsigned         index = handle_hash(handle, HandleMapSize);
    nft_handle_map * slot  = &HandleMap[index];

    // The map slot only contains a valid object if the refcount is positive.
    // The handle is only valid if it matches the object's handle.
    if (0      <  slot->refcount &&
	handle == slot->object->handle) {
	object =  slot->object;

	// Lookup always increments the object reference count.
	slot->refcount++;
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return object;
}

// Look up the handle and decrement the object reference count.
// If the new reference count is zero, delete the handle and
// destroy the object.
// Returns zero on success, or EINVAL on invalid handles.
int
nft_handle_discard(nft_core * object)
{
    int result  = EINVAL;
    int destroy = 0;
    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

    unsigned         index = handle_hash(object->handle, HandleMapSize);
    nft_handle_map * slot  = &HandleMap[index];

    // Unlike nft_handle_lookup, this should only be called on live objects.
    // Correct code should never attempt to discard a stale handle.
    assert(slot->object == object);
    assert(slot->refcount > 0);

    if (slot->refcount  > 0 && slot->object == object)
    {
	// Decrement reference count, deleting the handle if zero.
	if (--slot->refcount == 0) {
	    *slot = (nft_handle_map){ 0, NULL };

	    // We hold the sole reference to object, so we must destroy
	    // the object, but only after we have released the mutex.
	    destroy = 1;
	}
	result = 0; // Success.
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    if (destroy) object->destroy(object);
    return result;
}

void
nft_handle_apply(void (*function)(nft_core *, const char *, void *), const char * class, void * argument)
{
    // Ensure that the handle table and mutex are initialized.
    if (nft_handle_init()) return;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

    for (unsigned i = 0; i < HandleMapSize; i++)
    {
	nft_handle_map * slot = &HandleMap[i];
	if (slot->refcount > 0)	{
	    // Call function, passing object and argument.
	    function(slot->object, class, argument);
	}
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
}

#endif // NFT_LOCKLESS

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		HANDLE PACKAGE TEST DRIVER			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Enable asserts for test code.
#endif
#include <assert.h>
#include <stdio.h>


int
main(int argc, char *argv[])
{
    // FIXME - implement some tests!

    printf("nft_handle: All tests passed.\n");
    exit(0);
}

#endif // MAIN
