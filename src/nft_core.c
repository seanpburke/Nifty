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
 * File: nft_core.c
 *
 * Description: Core class for Nifty framework.
 *
 * This is the core "class" from which many Nifty packages are derived,
 * including nft_pool, nft_queue, and nft_task. The test driver at the
 * bottom of this file illustrates how to create a subclass of nft_core.
 *
 ******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <nft_core.h>

// Define a mutex to protect the handle table and object reference counts.
// Use pthread_once to initialize the mutex dynamically, in order to be
// compatible with nft_win32 pthread emulation, which cannot statically
// initialize mutexes.
//
static pthread_once_t   HandleOnce  = PTHREAD_ONCE_INIT;
static pthread_mutex_t  HandleMutex = PTHREAD_MUTEX_INITIALIZER;

/*******************************************************************************
 *
 *		nft_handle
 *
 *******************************************************************************
 */
typedef struct nft_handle_map {
    // It's not strictly necessary to store handle/object pairs,
    // because the object contains its handle. But the goal of
    // of handles is to improve safety, so redundancy is useful.
    nft_handle handle;
    nft_core * object;
} nft_handle_map;

static nft_handle_map * HandleMap     = NULL;
static unsigned         HandleMapSize = 1024; // must be a power of 2
static unsigned long    NextHandle    = 1;

// This limits the number of active handles, and thus the number
// of nft_core instances. I would hope that 16 million will suffice.
// If a process reaches this limit, nft_handle_alloc will return NULL,
// just as if malloc had failed.
//
#define MAX_HANDLES (1 << 24)

// Initialize the handle map and other globals.
static void
handle_init(void) {
    int rc = pthread_mutex_init(&HandleMutex, NULL); assert(rc == 0);

    size_t memsize = HandleMapSize * sizeof(nft_handle_map);
    if ((HandleMap = malloc(memsize))) memset(HandleMap,0,memsize);
    assert(HandleMap);
}

static unsigned
handle_hash(nft_handle handle, unsigned modulo)
{
    // For speed's sake, we do binary modulo arithmetic,
    // but this requires that modulo be a power of two.
    return (unsigned long) handle & (modulo - 1); // handle % modulo;
}

// Grow the handle map by doubling when it becomes full.
// Doubling ensures that no hash collisions will occur in the new map.
static int
handle_map_enlarge(void)
{
    unsigned newsize = HandleMapSize << 1;
    size_t   memsize = newsize * sizeof(nft_handle_map);

    // Refuse to allocate a ridiculous number of handles.
    assert(newsize <= MAX_HANDLES);
    if (newsize > MAX_HANDLES) return 0;

    nft_handle_map * newmap = malloc(memsize);
    if (newmap) {
	memset(newmap,0,memsize);
	for (unsigned i = 0; i < HandleMapSize; i++) {
	    nft_handle handle = HandleMap[i].handle;

	    // Confirm that the handle and object are consistent.
	    assert(handle == HandleMap[i].object->handle);

	    // Confirm that this is not a hash collision.
	    assert(newmap[handle_hash(handle, newsize)].handle == NULL);
	    newmap[handle_hash(handle, newsize)] = HandleMap[handle_hash(handle, HandleMapSize)];
	}
	free(HandleMap);
	HandleMap     = newmap;
	HandleMapSize = newsize;
	return 1;
    }
    return 0;
}

static nft_handle
nft_handle_alloc(nft_core * object)
{
    nft_handle handle = NULL;

    // Ensure that the handle table and mutex are initialized.
    int rc = pthread_once(&HandleOnce, handle_init); assert(rc == 0);

    rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

rescan: // Scan for the next open slot in the HandleMap
    for (unsigned i = 0; i < HandleMapSize; i++, NextHandle++) {
	if (NextHandle) {
	    unsigned index = handle_hash((nft_handle)NextHandle, HandleMapSize);
	    if (HandleMap[index].handle == NULL) {
		handle = (nft_handle) NextHandle++;
		HandleMap[index] = (nft_handle_map){ handle, object };
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
static nft_core *
nft_handle_lookup(nft_handle handle)
{
    // NULL is an invalid handle by definition.
    if (!handle) return NULL;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);
    unsigned   index  = handle_hash(handle, HandleMapSize);
    nft_core * object = NULL;

    // The handle is only valid if it matches the handle in the table.
    if (handle == HandleMap[index].handle)
    {
	object  = HandleMap[index].object;

	// Sanity check - Does this handle really refer to this object?
	assert(handle == object->handle);

	// Lookup always increments the object reference count.
	object->reference_count++;
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return object;
}

// Look up the handle and decrement the object reference count.
// Deletes the handle if the new reference count is zero,
// returning the new reference count.
static int
nft_handle_discard(nft_handle handle)
{
    int count = -1;

    // NULL is an invalid handle by definition.
    if (!handle) return count;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

    unsigned long index = handle_hash(handle, HandleMapSize);

    // The handle is only valid if it matches the handle in the table.
    if (handle == HandleMap[index].handle)
    {
	nft_core * object = HandleMap[index].object;

	// Sanity checks - If the handle were already deleted,
	// a different handle could be hashed to this index.
	// But a handle should not be deleted twice.
	assert(handle == object->handle);

	if (!(count = --object->reference_count)) {
	    HandleMap[index] = (nft_handle_map){ NULL, NULL };
	    object->handle   = NULL;
	}
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return count;
}

/*******************************************************************************
 *
 *		nft_core Public APIs
 *
 *******************************************************************************
 */
void *
nft_core_cast(void * vp, const char * class)
{
    assert(class); // The class parameter is mandatory.

    if (vp && class) {
	nft_core * object = vp;

	// If vp really refers to a nft_core instance, class should be non-null.
	assert(object->class);
	if (object->class) {
	    const char * require = class;
	    const char * inquire = object->class;

	    // Since class is a string literal, the pointers will often be identical.
	    if (require == inquire) return object;

	    // Test whether the required class is a prefix of the actual class.
	    char   c;
	    while ((c  = *require++))
		if (c != *inquire++) return NULL;
	    return object;
	}
    }
    return NULL;
}

nft_core *
nft_core_create(const char * class, size_t size)
{
    assert(class && size);
    nft_core * object = malloc(size);
    if (object) {
	nft_handle handle = nft_handle_alloc(object);
	if (handle) {
	    *object = (nft_core) { class, handle, 1, nft_core_destroy };
	}
	else { // nft_handle_alloc failed - memory exhausted or too many active handles.
	    free(object);
	    object = NULL;
	}
    }
    return object;
}

nft_core *
nft_core_lookup(nft_handle h)
{
    nft_core * object = nft_handle_lookup(h);
    assert(!object || nft_core_cast(object, nft_core_class));
    return object;
}

int
nft_core_discard(nft_core * p)
{
    nft_core * object = nft_core_cast(p, nft_core_class);
    if (object) {
	assert(object->reference_count > 0);
	if (0 == nft_handle_discard(object->handle))
	    object->destroy(object);
	return 0;
    }
    return EINVAL;
}

void
nft_core_destroy(nft_core * p)
{
    nft_core * object = nft_core_cast(p, nft_core_class);
    assert(object != NULL);
    if (object) {
	assert(object->reference_count == 0);
	free(object);
    }
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		CORE PACKAGE TEST DRIVER			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/

#ifdef MAIN
#include <stdio.h>

// Here we demonstrate how to create a subclass derived from nft_core.
// This subclass provides a simple reference-counted string class.
//
typedef struct nft_string
{
    nft_core core;
    char   * string;
} nft_string;

// You must define a macro for the subclass's class name,
// and pass it to the constructor to set the instance's .class.
#define nft_string_class nft_core_class ":nft_string"

// This macro expands to the following declarations:
NFT_DECLARE_WRAPPERS(nft_string,)
// typedef struct nft_string_h * nft_string_h;
// nft_string * nft_string_cast(nft_core * p);
// nft_string_h nft_string_handle(const nft_string * s);
// nft_string * nft_string_lookup(nft_string_h h);
// void nft_string_discard(nft_string * s);

// This macro expands to definitions for the prototypes:
NFT_DEFINE_WRAPPERS(nft_string,)

// The destructor should take a nft_core * parameter.
void
nft_string_destroy(nft_core * p)
{
    nft_string * object = nft_string_cast(p);

    // The _cast function will return NULL if p is not a nft_string *.
    if (object) free(object->string);

    // Remember to invoke the base-class destroyer last of all.
    nft_core_destroy(p);
}

// The constructor should accept class and size parameters,
// so that further subclasses can be derived from this subclass.
nft_string *
nft_string_create(const char * class, size_t size, const char * string)
{
    nft_string  * object = nft_string_cast(nft_core_create(class, size));
    object->core.destroy = nft_string_destroy;
    object->string = strdup(string);
    return object;
}

// The definitions above create a complete subclass of nft_core.
// This function demonstrates use of the constructor and helper functions.
//
static void
nft_string_tests()
{
    // Create the original instance, and save its handle in the variable h.
    nft_string * o = nft_string_create(nft_string_class, sizeof(nft_string), "my string");
    nft_string_h h = nft_string_handle(o);

    // Look up the handle to obtain a second reference to the original instance.
    nft_string * r = nft_string_lookup(h);

    // The lookup operation incremented the reference count,
    // so we can safely use r to refer to the original object.
    assert(!strcmp(r->string, "my string"));

    // Discard the reference we obtained via lookup.
    nft_string_discard(r);

    // Now discard the original reference. This will decrement its reference
    // count to zero, causing the object to be destroyed.
    nft_string_discard(o);

    // The handle has been cleared, so lookup will now fail.
    r = nft_string_lookup(h);
    assert(r == NULL);
}


static void
basic_tests()
{
    // Test the constructor.
    nft_core * p = nft_core_create(nft_core_class, sizeof(nft_core));
    assert(!strcmp(p->class, nft_core_class));
    assert(p->handle);
    assert(p->reference_count == 1);
    assert(p->destroy = nft_core_destroy);

    // Test handle lookup/discard.
    nft_handle h = p->handle;
    nft_core * q = nft_core_lookup(h);
    assert(q == p);
    assert(q->reference_count == 2);
    nft_core_discard(q);
    assert(p->reference_count == 1);

    // Test the destructor.
    nft_core_discard(p);
    nft_core * r = nft_core_lookup(h);
    assert(r == NULL);

    // Test handle_map_enlarge
    int        n = 10000;
    nft_core * parray[n];
    for (int i = 0; i < n; i++) {
	parray[i] = nft_core_create(nft_core_class, sizeof(nft_core));
    }
    for (int i = 0; i < n; i++) {
	nft_core_discard(parray[i]);
    }
}

int
main(int argc, char *argv[])
{
    // First, perform the basic tests.
    basic_tests();

    // Now, test use nft_core as a base class.
    nft_string_tests();

#ifdef NDEBUG
    printf("This unit test is not effective when compiled with NDEBUG!\n");
#else
    printf("All tests passed.\n");
#endif
    exit(0);
}


#endif // MAIN
