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

////////////////////////////////////////////////////////////////////////////////
// Private functions to manage handles

// FIXME For now, a _very_ simple handle table.
#define MAX_HANDLE 100000
static nft_core    * Instance[MAX_HANDLE];
static unsigned long NextHandle = 1;

static void
handle_init(void)
{
    int rc = pthread_mutex_init(&CoreMutex, NULL); assert(rc == 0);
}

static nft_handle
handle_alloc(nft_core * p)
{
    nft_handle result = NULL;
    int rc;

    // Ensure that the handle table and mutex are initialized.
    rc = pthread_once(&CoreOnce, handle_init); assert(rc == 0);

    rc = pthread_mutex_lock(&CoreMutex); assert(rc == 0);
    if (NextHandle < MAX_HANDLE) {
	Instance[NextHandle] = p;
	result = (void*) NextHandle++;
    }
    else {
	assert(!"need a larger handle table");
    }
    rc = pthread_mutex_unlock(&CoreMutex); assert(rc == 0);

    return result;
}

static nft_core *
handle_lookup(nft_handle h) {
    // NULL is an invalid handle by definition.
    return h ? Instance[(unsigned long) h] : NULL ;
}

static void
handle_clear(nft_handle h)
{
    Instance[(unsigned long) h] = NULL;
}

////////////////////////////////////////////////////////////////////////////////
// Public APIs

nft_core *
nft_core_create(const char * class, size_t size)
{
    nft_core * this = malloc(size);
    if (this) {
	*this = (nft_core) { class, handle_alloc(this), 1, nft_core_destroy };
	assert(this->class);
	assert(this->handle);
    }
    return this;
}

void
nft_core_destroy(nft_core * p)
{
    nft_core * this = nft_core_cast(p, nft_core_class);
    assert(this != NULL);
    if (this) {
	assert(this->reference_count == 0);
	free(this);
    }
}

void *
nft_core_cast(nft_core * this, const char * class)
{
    if (this && this->class && !strncmp(this->class, class, strlen(class)))
	return this;
    return NULL;
}

nft_core *
nft_core_lookup(nft_handle h)
{
    int rc = pthread_mutex_lock(&CoreMutex); assert(rc == 0);

    nft_core * this = handle_lookup(h);
    if (this) this->reference_count++;

    rc = pthread_mutex_unlock(&CoreMutex); assert(rc == 0);

    return this;
}

void
nft_core_discard(nft_core * p)
{
    nft_core * this = nft_core_cast(p, nft_core_class);
    if (this) {
	assert(this->reference_count > 0);

	int rc = pthread_mutex_lock(&CoreMutex); assert(rc == 0);
	if (--this->reference_count == 0) {
	    handle_clear(this->handle);
	    this->destroy(this);
	}
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

// This macro expands to the following definitions:
NFT_DEFINE_HELPERS(subclass,)
// subclass * subclass_cast(nft_core * p) { return nft_core_cast(p, "nft_core" ":subclass");
// subclass_h subclass_handle(const subclass * s) { return s->core.handle;
// subclass * subclass_lookup(subclass_h h) { return subclass_cast(nft_core_lookup(h));
// void subclass_discard(subclass * s) { nft_core_discard(&s->core); }

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
