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

#include <nft_core.h>

// Declare the handle management API.
static int          nft_handle_init(void);
static nft_handle   nft_handle_alloc(nft_core * object);
static nft_handle * nft_handle_list(const char * class);
static nft_core *   nft_handle_lookup(nft_handle handle);
static int          nft_handle_discard(nft_handle handle);


/*******************************************************************************
 *
 *		nft_core Public APIs
 *
 *******************************************************************************
 */
void *
nft_core_cast(void * vp, const char * class)
{
    nft_core * object = vp;

    // It is OK to pass a null object pointer to this call, but if the
    // object->class pointer is null, that means  that the object has
    // already been freed, which is a much more serious problem.
    assert(!object || object->class);

    if (object && object->class && class)
    {
	const char * require = class;
	const char * inquire = object->class;

	// Since class is a string literal, the pointers will often be identical.
	if (require == inquire) return object;

	// Test whether the required class is a prefix of the actual class.
	char    c;
	while ((c  = *require++))
	    if (c != *inquire++) return NULL;
	return object;
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

	// _discard returns the reference_count after decrement.
	int count = nft_handle_discard(object->handle);
	if (count == 0) object->destroy(object);
	if (count >= 0) return 0;
    }
    return EINVAL;
}

void
nft_core_destroy(nft_core * p)
{
    nft_core * object = nft_core_cast(p, nft_core_class);
    assert(!object || !object->reference_count);
    if (object) {
	// Null the class pointer, to ensure that nft_core_cast
	// will fail if given a pointer to freed memory.
	object->class = NULL;
	free(object);
    }
}

nft_handle *
nft_core_list(const char * class)
{
    return nft_handle_list(class);
}

/*******************************************************************************
 *  Below we implement these functions for handle management:
 *
 *	static void         handle_init(void);
 *	static nft_handle   nft_handle_alloc(nft_core * object);
 *	static nft_handle * nft_handle_list(const char * class);
 *	static nft_core *   nft_handle_lookup(nft_handle handle);
 *	static int          nft_handle_discard(nft_handle handle);
 *
 *  The implementation that is shown here, is a good compromise for
 *  simplicity, efficiency and portability, but there is definitely
 *  room for innovation here. Handle management is isolated within
 *  these five API calls, so that it will be easy to substitute
 *  improved implementations.
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
static unsigned long    NextHandle    = 1;

// HandleMapSize sets the initial size of the HandleMap array.
// HandleMapMax limits the number of active handles, and thus the
// number of nft_core instances. If a process reaches this limit,
// nft_handle_alloc will return NULL, just as if malloc had failed.
//
static unsigned         HandleMapSize = (1 << 10); // must be a power of 2
static unsigned         HandleMapMax  = (1 << 20); // one million handles

// Define a mutex to protect the handle table and object reference counts.
static pthread_once_t   HandleOnce  = PTHREAD_ONCE_INIT;
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
    size_t memsize = HandleMapSize * sizeof(nft_handle_map);
    if ((HandleMap = malloc(memsize))) memset(HandleMap,0,memsize);
    assert(HandleMap);
}

static int
nft_handle_init(void)
{
    // Ensure that the handle table and mutex are initialized.
    int rc = pthread_once(&HandleOnce, handle_once); assert(rc == 0);

    // Return an error if we cannot initialize the handle subsystem.
    return HandleMap ? 0 : ENOMEM;
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
    if (nft_handle_init()) return NULL;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

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
    nft_core * object = NULL;

    // NULL is an invalid handle by definition.
    if (!handle) return NULL;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);
    unsigned   index  = handle_hash(handle, HandleMapSize);

    // The handle is only valid if it matches the handle in the table.
    if (handle == HandleMap[index].handle) {
	object =  HandleMap[index].object;

	// Sanity check - Does this handle really refer to this object?
	assert(handle == object->handle);

	// Lookup always increments the object reference count.
	object->reference_count++;
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return object;
}

// Look up the handle and decrement the object reference count.
// Deletes the handle if the new reference count is zero.
// Returns the new reference count, or -1 on stale handles.
static int
nft_handle_discard(nft_handle handle)
{
    int count = -1;

    // NULL is an invalid handle by definition.
    if (!handle) return count;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);
    unsigned long index = handle_hash(handle, HandleMapSize);

    // Correct code should never attempt to discard a stale handle.
    assert(handle == HandleMap[index].handle);
    if    (handle == HandleMap[index].handle)
    {
	nft_core * object = HandleMap[index].object;

	// Sanity check - Does this handle really refer to this object?
	assert(handle == object->handle);

	// Decrement reference count, deleting the handle if zero.
	if (!(count = --object->reference_count))
	    HandleMap[index] = (nft_handle_map){ NULL, NULL };
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return count;
}

// List all the handles of the given class, for diagnostic purposes.
// Returns a NULL-terminated list of handles to the caller.
static nft_handle *
nft_handle_list(const char * class)
{
    nft_handle * handles = NULL;

    // Ensure that the handle table and mutex are initialized.
    if (nft_handle_init()) return NULL;

    int rc = pthread_mutex_lock(&HandleMutex); assert(rc == 0);

    // First, count the number of handles in class.
    int count = 0;
    for (unsigned i = 0; i < HandleMapSize; i++)
	if (nft_core_cast(HandleMap[i].object, class)) count++;

    // Allocate an array to hold the handles, plus a null terminator.
    if ((handles = malloc((count+1)*sizeof(nft_handle))))
    {
	unsigned j = 0;
	for (unsigned i = 0; i < HandleMapSize; i++)
	    if (nft_core_cast(HandleMap[i].object, class))
		handles[j++] = HandleMap[i].handle;

	// Add the terminating NULL handle.
	assert(j == count);
	handles[j] = NULL;
    }
    rc = pthread_mutex_unlock(&HandleMutex); assert(rc == 0);
    return handles;
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		CORE PACKAGE TEST DRIVER			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Enable asserts for test code.
#endif
#include <assert.h>
#include <stdio.h>

// Here we demonstrate how to create a subclass derived from nft_core.
// This subclass provides a simple reference-counted string class.
// The README.txt file provides a detailed explanation of this.
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
// nft_string_h * nft_string_list(void);

// This macro expands to definitions for the prototypes:
NFT_DEFINE_WRAPPERS(nft_string,)

// The destructor should take a nft_core * parameter.
void
nft_string_destroy(nft_core * p)
{
    nft_string * object = nft_string_cast(p);

    // The _cast function will return NULL if p is not a nft_string.
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

// This function demonstrates the use of nft_string_lookup/_discard,
// to implement your class's accessor methods.
void
nft_string_print(nft_string_h handle)
{
    nft_string * object = nft_string_lookup(handle);
    if (object) {
	fprintf(stderr, "string[%p] => '%s'\n", object->core.handle, object->string);
	nft_string_discard(object);
    }
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

    // The handle has been deleted, so lookup will now fail.
    r = nft_string_lookup(h);
    assert(r == NULL);

    // We can safely call nft_string_print, even though the object
    // has been freed, because stale handles are ignored.
    nft_string_print(h);

    // nft_string_list returns an array with the handle of every nft_string instance.
    // It is created automatically by the DEFINE_HELPERS macro, for every Nifty class.
    // To demonstrate how this works, we first create ten nft_string instances.
    const char * words[] = { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", NULL };
    for (int i = 0; words[i] ; i++ )
	nft_string_create(nft_string_class, sizeof(nft_string), words[i]);

    // Get an array of handles for all current nft_string instances.
    // Even if there are no live strings, you will still get an empty array,
    // but nft_string_list will return NULL if memory is exhausted.
    nft_string_h * handles = nft_string_list();
    if (handles) {
	// Iterate over the array, which is terminated by a NULL handle.
	for (int i = 0; handles[i]; i++) nft_string_print(handles[i]);

	// In order to free these nft_strings, we must lookup the handle
	// and discard the resulting reference twice.
	for (int i = 0; handles[i]; i++) {
	    nft_string * string = nft_string_lookup(handles[i]);
	    if (string) {
		int error;
		error = nft_string_discard(string); assert(0 == error);
		error = nft_string_discard(string); assert(0 == error);
	    }
	}
	// Don't forget to free the array of handles.
	free(handles);
    }
}


static void
basic_tests()
{
    // Test the constructor.
    nft_core  * p = nft_core_create(nft_core_class, sizeof(nft_core));
    assert(0 == strcmp(p->class, nft_core_class));
    assert(0 != p->handle);
    assert(1 == p->reference_count);
    assert(nft_core_destroy == p->destroy);

    // Test handle lookup/discard.
    nft_handle  h = p->handle;
    nft_core  * q = nft_core_lookup(h);
    assert(q == p);
    assert(2 == p->reference_count);
    int         s = nft_core_discard(q);
    assert(0 == s);
    assert(1 == p->reference_count);

    // Test the destructor.
    s = nft_core_discard(p);
    assert(0 == s);
    nft_core  * r = nft_core_lookup(h);
    assert(NULL == r);

    // Test handle_map_enlarge
    int        n = 10000;
    nft_core * parray[n];
    for (int i = 0; i < n; i++)
	parray[i] = nft_core_create(nft_core_class, sizeof(nft_core));

    // Confirm that the objects we created are all present.
    nft_handle * handles = nft_core_list(nft_core_class);
    int i = 0;
    while (handles[i]) i++;
    assert(i == n);
    free(handles);

    // Now discard all of the object references, freeing the objects.
    for (int i = 0; i < n; i++)
	nft_core_discard(parray[i]);

    // Confirm that all objects have been destroyed.
    handles = nft_core_list(nft_core_class);
    i = 0;
    while (handles[i]) i++;
    assert(i == 0);
    free(handles);
}

int
main(int argc, char *argv[])
{
    // First, perform the basic tests.
    basic_tests();

    // Now, test nft_string, which uses nft_core as a base class.
    nft_string_tests();

    printf("nft_core: All tests passed.\n");
    exit(0);
}


#endif // MAIN
