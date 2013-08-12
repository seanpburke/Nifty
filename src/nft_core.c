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
static pthread_once_t   CoreOnce  = PTHREAD_ONCE_INIT;
static pthread_mutex_t	CoreMutex = PTHREAD_MUTEX_INITIALIZER;

/*******************************************************************************
 *
 *		nft_handle
 *
 *******************************************************************************
 */
typedef struct nft_handle_map {
    // It's not strictly necessary to store handle/object pairs,
    // because the object contains its handle. But in this case,
    // some redundancy makes the system much more resilient.
    nft_handle handle;
    nft_core * object;
} nft_handle_map;

static nft_handle_map * HandleMap     = NULL;
static unsigned         HandleMapSize = 1024; // must be a power of 2
static pthread_mutex_t  HandleMutex   = PTHREAD_MUTEX_INITIALIZER;
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
    int rc;
    rc = pthread_mutex_init(&CoreMutex,   NULL); assert(rc == 0);
    rc = pthread_mutex_init(&HandleMutex, NULL); assert(rc == 0);

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

nft_handle
nft_handle_alloc(nft_core * object)
{
    nft_handle handle = NULL;

    // Ensure that the handle table and mutex are initialized.
    int rc = pthread_once(&CoreOnce, handle_init); assert(rc == 0);

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

nft_core *
nft_handle_lookup(nft_handle handle)
{
    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

    // NULL is an invalid handle by definition.
    unsigned   index  = handle_hash(handle, HandleMapSize);
    nft_core * object = handle ? HandleMap[index].object : NULL;

    // Sanity check - Does this handle really refer to this object?
    assert(!object || (handle == object->handle));

    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return object;
}

void
nft_handle_delete(nft_handle handle)
{
    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

    // NULL is an invalid handle by definition.
    if (handle) {
	unsigned long index = handle_hash(handle, HandleMapSize);

	// Sanity checks - If the handle were already deleted,
	// a different handle could be hashed to this index.
	// But a handle should not be deleted twice.
	assert(handle == HandleMap[index].handle);
	assert(handle == HandleMap[index].object->handle);

	HandleMap[index] = (nft_handle_map){ NULL, NULL };
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
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
	nft_core * this = vp;

	// If vp really refers to a nft_core instance, class should be non-null.
	assert(this->class);
	if (this->class) {
	    const char * require = class;
	    const char * inquire = this->class;

	    // Since class is a string literal, the pointers will often be identical.
	    if (require == inquire) return this;

	    // Test whether the required class is a prefix of the actual class.
	    char   c;
	    while ((c  = *require++))
		if (c != *inquire++) return NULL;
	    return this;
	}
    }
    return NULL;
}

nft_core *
nft_core_create(const char * class, size_t size)
{
    assert(class && size);
    nft_core * this = malloc(size);
    if (this) {
	nft_handle handle = nft_handle_alloc(this);
	if (handle) {
	    *this = (nft_core) { class, handle, 1, nft_core_destroy };
	}
	else { // nft_handle_alloc failed - memory exhausted or too many active handles.
	    free(this);
	    this = NULL;
	}
    }
    return this;
}

void
nft_core_destroy(nft_core * p)
{
    nft_core * this = nft_core_cast(p, nft_core_class);
    assert(this != NULL);
    if (this) {
	// Delete the object's handle, so that no new references can be obtained via _lookup.
	nft_handle_delete(this->handle);
	this->handle = NULL;

	// If the reference count is not zero, it means that nft_core_destroy was called directly,
	// rather than via a _discard method. We support this usage, because it allows the caller
	// to delete the handle and decrements one reference. Since the handle can no longer be
	// dereferenced, this ensures that the object will be freed after the last outstanding
	// reference is discarded.
	//
	// The nft_queue package takes advantage of this behavior in nft_queue_shutdown,
	// so you can depend on this behavior not to change in the future.
	//
	if    (this->reference_count  > 0) this->reference_count--;
	if    (this->reference_count == 0) free(this);
    }
}

nft_core *
nft_core_lookup(nft_handle h)
{
    int rc = pthread_mutex_lock(&CoreMutex); assert(rc == 0);

    nft_core * this = nft_core_cast(nft_handle_lookup(h), nft_core_class);
    if (this) this->reference_count++;

    rc = pthread_mutex_unlock(&CoreMutex); assert(rc == 0);

    return this;
}

void
nft_core_discard(nft_core * p)
{
    nft_core * this = nft_core_cast(p, nft_core_class);
    if (this) {
	int rc = pthread_mutex_lock(&CoreMutex); assert(rc == 0);

	assert(this->reference_count  > 0);
	if ( --this->reference_count == 0) this->destroy(this);

	rc = pthread_mutex_unlock(&CoreMutex); assert(rc == 0);
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
// We are naming our subclass "subclass" - hope that's not too confusing.
// The subclass adds a single attribute to the core class, named "substring".
//
typedef struct subclass
{
    nft_core core;
    char   * substring;
} subclass;

// You must define a macro for the subclass's class string,
// and pass it to the constructor to set the instance's .class.
#define subclass_class nft_core_class ":subclass"

// This macro expands to the following declarations:
NFT_DECLARE_HELPERS(subclass,)
// typedef struct subclass_h * subclass_h;
// subclass * subclass_cast(nft_core * p);
// subclass_h subclass_handle(const subclass * s);
// subclass * subclass_lookup(subclass_h h);
// void subclass_discard(subclass * s);

// This macro expands to definitions for the prototypes:
NFT_DEFINE_HELPERS(subclass,)

// To complete our subclass, we define the destructor and constructor:
void
subclass_destroy(nft_core * p)
{
    subclass * this = subclass_cast(p);
    free(this->substring);
    nft_core_destroy(p);
}

subclass *
subclass_create(const char * class, size_t size, const char * substring)
{
    subclass    * this = subclass_cast(nft_core_create(class, size));
    this->core.destroy = subclass_destroy;
    this->substring    = strdup(substring);
    return this;
}

// The definitions above create a complete subclass of nft_core.
// This function demonstrates use of the constructor and helper functions.
//
static void
subclass_tests()
{
    // Create the original instance, and save its handle in the variable h.
    subclass * o = subclass_create(subclass_class, sizeof(subclass), "my substring");
    subclass_h h = subclass_handle(o);

    // Look up the handle to obtain a second reference to the original instance.
    subclass * r = subclass_lookup(h);

    // The lookup operation incremented the reference count,
    // so we can safely use r to refer to the original object.
    assert(!strcmp(r->substring, "my substring"));

    // Discard the reference we obtained via lookup.
    subclass_discard(r);

    // Now discard the original reference. This will decrement its reference
    // count to zero, causing the object to be destroyed.
    subclass_discard(o);

    // The handle has been cleared, so lookup will now fail.
    r = subclass_lookup(h);
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
    subclass_tests();

#ifdef NDEBUG
    printf("This unit test is not effective when compiled with NDEBUG!\n");
#else
    printf("All tests passed.\n");
#endif
    exit(0);
}


#endif // MAIN
