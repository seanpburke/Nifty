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
 * DESCRIPTION
 *
 * This is the core "class" from which libnifty packages are derived,
 * including nft_pool, nft_queue, and nft_task. The nft_string package
 * provides a very simple demonstration, how to create a new package
 * based on nft_core.
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

// gather_apply is passed to nft_handle_apply, which calls it on every nft_core object.
// If the object is in the given class, the object's handle is added to the array.
//
struct handle_array {
    unsigned     next;
    unsigned     size;
    nft_handle * array;
};
static void
gather_apply(nft_core * object, const char * class, void * argument)
{
    struct handle_array * hap = argument;

    if (nft_core_cast(object, class))
    {
	// Have we reached the limit of our current array?
	if (hap->next == hap->size) {
	    // Allocate an array of twice the size, plus one more for the terminating null.
	    nft_handle * new_array = realloc(hap->array, (2*hap->size + 1) * sizeof(nft_handle));
	    if (new_array) {
		hap->array = new_array;
		hap->size *= 2;
	    }
	}
	if (hap->next < hap->size) {
	    hap->array[hap->next++] = object->handle;
	}
    }
}

// Returns a null-terminated array of handles, for every object of the given class.
nft_handle *
nft_core_gather(const char * class)
{
    int          size  = 126;
    nft_handle * array = malloc((size + 1) * sizeof(nft_handle));
    struct handle_array ha = (struct handle_array){ 0, size, array };
    if (ha.array) {
	nft_handle_apply(gather_apply, class, &ha);
	ha.array[ha.next] = NULL;
    }
    return ha.array;
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

#include <nft_string.h>

// Test (and demonstrate) use of the constructor and helper functions.
//
int
main(int argc, char *argv[])
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
    nft_handle * handles = nft_core_gather(nft_core_class);
    int i = 0;
    while (handles[i]) i++;
    assert(i == n);
    free(handles);

    // Now discard all of the object references, freeing the objects.
    for (int i = 0; i < n; i++)
	nft_core_discard(parray[i]);

    // Confirm that all objects have been destroyed.
    handles = nft_core_gather(nft_core_class);
    i = 0;
    while (handles[i]) i++;
    assert(i == 0);
    free(handles);

    printf("nft_core: All tests passed.\n");
    exit(0);
}

#endif // MAIN
