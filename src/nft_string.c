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
 * File: nft_string.c
 *
 * Description:
 *
 * This package illustrates a very simple shared string class,
 * purely to demonstrate how subclasseses are created from nft_core.
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


//   static nft_string *   nft_string_cast(nft_core * p);
//   static nft_string_h   nft_string_handle(const nft_string * s);
//   static nft_string *   nft_string_lookup(nft_string_h h);
//   static void           nft_string_discard(nft_string * s);
//   static nft_string_h * nft_string_list(void);
//
// The macro below defines the functions shown above:
//
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

// You can also provide a simple constructor, for convenience.
nft_string *
nft_string_new(const char * string) {
    return nft_string_create(nft_string_class, sizeof(nft_string), string);
}

// This function demonstrates the use of nft_string_lookup/_discard,
// to implement your class's accessor methods.
void
nft_string_print(nft_string_h handle)
{
    nft_string * object = nft_string_lookup(handle);
    if (object) {
	printf("string[%p] => '%s'\n", object->core.handle, object->string);
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

int
main(int argc, char *argv[])
{
    // Create the original instance, and save its handle in the variable h.
    nft_string * o = nft_string_new("my string");
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
    for (int i = 0; words[i] ; i++ ) nft_string_new(words[i]);

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

    fprintf(stderr, "nft_string: All tests passed.\n");
    exit(0);
}

#endif // MAIN
