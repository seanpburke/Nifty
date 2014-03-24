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
#include <nft_handle.h>

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
    if ( object ) {
	// Initialize the object with a null handle.
	*object = (nft_core) { class, NULL, nft_core_destroy };

	// Attempt to allocate a handle for this object.
	if (!nft_handle_alloc(object)) {
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
    assert(object);
    return object ? nft_handle_discard(object) : EINVAL ;
}

void
nft_core_destroy(nft_core * p)
{
    nft_core * object = nft_core_cast(p, nft_core_class);
    assert(object);
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

NFT_DECLARE_WRAPPERS(nft_string,static)
//
// The macro above expands to the following declarations:
//
//   typedef struct nft_string_h * nft_string_h;
//   static nft_string *   nft_string_cast(nft_core * p);
//   static nft_string_h   nft_string_handle(const nft_string * s);
//   static nft_string *   nft_string_lookup(nft_string_h h);
//   static void           nft_string_discard(nft_string * s);
//   static nft_string_h * nft_string_list(void);
//
// The macro below defines the functions declared above:
//
NFT_DEFINE_WRAPPERS(nft_string,static)

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
    assert(nft_core_destroy == p->destroy);

    // Test handle lookup/discard.
    nft_handle  h = p->handle;
    nft_core  * q = nft_core_lookup(h);
    assert(q == p);
    int         s = nft_core_discard(q);
    assert(0 == s);

    // Test the destructor.
    s = nft_core_discard(p);
    assert(0 == s);
    nft_core  * r = nft_core_lookup(h);
    assert(NULL == r);

    // Test handle_map_enlarge
    // int     n = 10000;
    int        n = 1000;
    nft_core * parray[n];
    for (int i = 0; i < n; i++) {
	nft_core * core = nft_core_create(nft_core_class, sizeof(nft_core));
	assert(NULL != core);
	parray[i] = core;
    }

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
