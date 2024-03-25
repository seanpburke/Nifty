/*******************************************************************************
 * (C) Xenadyne Inc, 2014.	All Rights Reserved
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
 * File: nft_string.c
 *
 * DESCRIPTION
 *
 * This package illustrates a very simple shared string class,
 * purely to demonstrate how subclasses are created from nft_core.
 * This file includes a test driver that illustrates how the core
 * APIs are used.
 *
 *******************************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#include <unistd.h>
#endif

#include <nft_string.h>

/* Each subclass must have a unique class name. The class name must be
   prefixed by "nft_core", so that the libnifty code will recognize the
   object as a subclass of nft_core. Similarly, each parent class name
   must appear in sequence, so that upcasts work correctly.

   The subclass name must be defined as a macro or string constant
   named "<subclass>_class". The macro that defines the typesafe-cast
   function subclass_cast(), expects this symbol.
*/
#define nft_string_class nft_core_class ":" "nft_string"


// The macro below defines the following functions:
//
//   nft_string *   nft_string_cast(nft_core * p);
//   nft_string_h   nft_string_handle(const nft_string * s);
//   nft_string *   nft_string_lookup(nft_string_h h);
//   void           nft_string_discard(nft_string * s);
//
NFT_DEFINE_WRAPPERS(nft_string,)


// The destructor should take a nft_core * parameter.
void
nft_string_destroy(nft_core * core)
{
    nft_string * object = nft_string_cast(core);

    // The _cast function will return NULL if core is not a nft_string.
    if (object) free(object->string);

    // Remember to invoke the base-class destroyer last of all.
    nft_core_destroy(core);
}

// The _create constructor accepts class and size parameters,
// so that further subclasses can be derived from this subclass.
nft_string *
nft_string_create(const char * class, size_t size, const char * string)
{
    nft_string  * object = nft_string_cast(nft_core_create(class, size));
    object->core.destroy = nft_string_destroy;
    object->string = strdup(string);
    return object;
}

//  The _new constructor takes only the string and returns a handle.
nft_string_h
nft_string_new(const char * string) {
    return nft_string_handle(nft_string_create(nft_string_class, sizeof(nft_string), string));
}

// This function demonstrates the use of nft_string_lookup/_discard,
// to implement your class's accessor methods.
void
nft_string_print(nft_string_h handle)
{
    nft_string * object = nft_string_lookup(handle);
    if (object) {
	printf("%s[%p] -> '%s'\n", object->core.class, object->core.handle, object->string);
	nft_string_discard(object);
    }
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		STRING PACKAGE TEST DRIVER			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Enable asserts for test code.
#endif
#include <assert.h>
#include <nft_handle.h>

int
main(int argc, char *argv[])
{
    // Create the original instance, and save its handle in the variable h.
    nft_string_h h = nft_string_new("my string");

    // Look up the handle to obtain a second reference to the original instance.
    nft_string * r = nft_string_lookup(h);

    // The lookup operation incremented the reference count,
    // so we can safely use r to refer to the original object.
    assert(!strcmp(r->string, "my string"));

    // Discard the reference we obtained via lookup.
    nft_string_discard(r);

    // Now discard the original reference. This will decrement its reference
    // count to zero, causing the object to be destroyed.
    nft_string_discard(r);

    // The handle has been deleted, so lookup will now fail.
    r = nft_string_lookup(h);
    assert(r == NULL);

    // We can safely call nft_string_print, even though the object
    // has been freed, because stale handles are ignored.
    nft_string_print(h);

    // nft_core_gather returns an array of handles to every instance of a class.
    //
    // WARNING - nft_core_gather will return handles to objects that have
    // been created, but are not fully initialized, so you must be careful!
    // This call is helpful for development, testing and debugging,
    // but you should probably avoid using it in production code.
    //
    // To demonstrate how this works, we first create ten nft_string instances.
    const char * words[] = { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", NULL };
    for (int i = 0; words[i] ; i++ ) nft_string_new(words[i]);

    // Get an array of handles for all current nft_string instances.
    // Even if there are no live strings, you will still get an empty array,
    // but nft_core_gather will return NULL if memory is exhausted.
    //
    nft_handle * handles = nft_core_gather(nft_string_class);
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

    // Verify that no Nifty handles remain.
    assert(0 == nft_handle_apply(NULL, NULL, NULL));

    fprintf(stderr, "nft_string: All tests passed.\n");
    exit(0);
}

#endif // MAIN
