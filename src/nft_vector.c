/***********************************************************************
 * (C) Copyright Xenadyne, Inc. 2003-2020  All rights reserved.
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
 ************************************************************************
 *
 * File: nft_vector.c
 *
 * DESCRIPTION
 *
 * A package for managing ordered dynamic arrays, so that we can do
 * set operations efficiently. Some of the operations supported are:
 *
 *   - sort, unique
 *   - search
 *   - set union, intersection, difference
 *   - filter, map, reduce
 *
 * The API is documented in the header file nft_vector.h.
 *******************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef OK
#define OK 0
#endif

#include <nft_vector.h>

// Define the wrapper functions nft_vector_cast, _lookup, _discard, etc.
//
NFT_DEFINE_WRAPPERS(nft_vector,)

/******************************************************************************/
/*******								*******/
/*******		VECTOR PRIVATE APIS				*******/
/*******								*******/
/******************************************************************************/

/*______________________________________________________________________________
 *
 * vector_create 	Create a vector
 *
 * This constructor takes additional parameters class and size,
 * so that you can use it to implement a subclass of nft_rbtree.
 *______________________________________________________________________________
 */
nft_vector *
vector_create(const char * class, size_t size, int cap, nft_comparator cmp)
{
    if (cap < 0) return NULL;	// Initial capacity must be nonnegative.
    if (!cmp)    return NULL;	// The comparator is mandatory.

    nft_vector * v = nft_vector_cast(nft_core_create(class, size));
    if (v) {
        v->len = 0;
        v->cap = cap;
        v->cmp = cmp;
        v->vec = NULL;
        if (cap > 0) {
            if ((v->vec = malloc((cap + 1) * sizeof(void *))))
                v->vec[0] = NULL;
            else {
                nft_vector_discard(v);
                v = NULL;
            }
        }
    }
    return v;
}
/*______________________________________________________________________________
 *
 * vector_destroy 	Destroy a vector
 *
 * Never call this function directly or you will leak handles. Use vector_free.
 *______________________________________________________________________________
 */
void
vector_destroy(nft_core * core)
{
    // The _cast function will return NULL if core is not a nft_vector.
    nft_vector * vector = nft_vector_cast(core);
    if (vector) {
        if (vector->vec)
            free(vector->vec);
    }
    // Remember to invoke the base-class destroyer last of all.
    nft_core_destroy(core);
}
/*______________________________________________________________________________
 *
 * vector_new		Create a new vector
 *______________________________________________________________________________
 */
nft_vector *
vector_new(int capacity, nft_comparator cmp)
{
    return vector_create(nft_vector_class, sizeof(nft_vector), capacity, cmp);
}
/*______________________________________________________________________________
 *
 * vector_vnew		Create a new vector variadic
 *______________________________________________________________________________
 */
static nft_vector *
_vector_vanew (nft_comparator cmp, va_list args)
{
    // First, count parameters to determine capacity.
    va_list ap;
    va_copy(ap, args);
    int cap = 0;
    for (void * vp = va_arg(ap, void*); vp; vp = va_arg(ap, void*)) cap++;
    va_end(ap);

    // Now create, populate and sort the vector.
    nft_vector * vector = vector_new(cap, cmp);
    if (vector) {
        for (void * vp = va_arg(args, void*); vp; vp = va_arg(args, void*))
            vector_append(vector, vp);
        vector_sort(vector);
    }
    return vector;
}
nft_vector *
vector_vnew (nft_comparator cmp, ...)
{
    va_list  ap;
    va_start(ap, cmp);
    nft_vector * vector = _vector_vanew(cmp, ap);
    va_end(ap);
    return vector;
}
/*______________________________________________________________________________
 *
 * vector_free		Free a vector created by vector_new().
 *______________________________________________________________________________
 */
int
vector_free(nft_vector * vec)
{
    return vec ? nft_vector_discard(vec) : EINVAL;
}
/*______________________________________________________________________________
 *
 * vector_append	Append an item to the end of the vector.
 *______________________________________________________________________________
 */
int
vector_append(nft_vector * v, void * item)
{
    if (v->len == v->cap) {
	v->cap = 2*v->cap;
        v->vec = realloc(v->vec, (v->cap + 1) * sizeof(void*));
        // FIXME detect realloc failure
    }
    v->vec[v->len++] = item;
    v->vec[v->len]   = NULL;
    return OK;
}
/*______________________________________________________________________________
 *
 * vector_sort		Sort a vector using the compare function.
 *			Returns the input vector, sorted.
 *
 * This uses the shell sort algorithm, which is simple, compact and efficient.
 *______________________________________________________________________________
 */
nft_vector *
vector_sort(nft_vector *v)
{
    void ** vec = v->vec;
    int     h, i;

    // Find our initial stride.
    for (h = 1; h <= v->len; h = 3*h + 1);

    // Perform the shell sort.
    do {
	h = h / 3;

	for (i = h ; i < v->len ; i++)
	{
	    void* t = vec[i];
	    int   j = i;

	    while ((j >= h) && (v->cmp(vec[j - h], t) > 0))
	    {
		vec[j] = vec[j - h];
		j = j - h;
	    }
	    if (j != i)
		vec[j] = t;
	}
    } while (h > 1);

    return v;
}
/*______________________________________________________________________________
 *
 * vector_unique	Remove duplicate items in a _sorted_ vector.
 *			Returns the given vector.
 *______________________________________________________________________________
 */
nft_vector *
vector_unique(nft_vector * v)
{
    void ** vec = v->vec;
    int     i, j;

    if (v->len <= 1)
	return v;

    for (i = 0, j = 1; j < v->len; j++) {
	long c = v->cmp(vec[j], vec[i]);
	if (c > 0)
	    if (++i < j)
		vec[i] = vec[j];

	assert(c >= 0);	// The input vector must be sorted.
    }
    v->len = ++i;
    vec[i] = NULL;

    return v;
}
/*______________________________________________________________________________
 *
 * vector_find() - Find an item in a sorted slice of a vector.
 *
 * Returns a slice that starts at the position where the item _would_ be,
 * if the item were present. The slice is empty if the item is not present.
 *______________________________________________________________________________
 */
nft_slice
vector_find( nft_vector * v, const void * item, nft_slice s)
{
    void ** vec = v->vec;
    int  first = s.x;
    int  last  = s.y - 1;
    int  pos   = s.x;
    long cmp   = -1;
    while (last >= first) {
        pos = (first + last) >> 1;
        cmp = v->cmp(item, vec[pos]);
        if      (cmp < 0) last  = pos - 1;
        else if (cmp > 0) first = pos + 1;
        else break;
    }
    if (cmp == 0) return (nft_slice){ pos,     pos + 1 };
    if (cmp  > 0) return (nft_slice){ pos + 1, pos + 1 };
    return (nft_slice){ pos, pos };
}
/*______________________________________________________________________________
 *
 * vector_index() - Look for an instance of item in a sorted vector.
 *
 * Returns the index of item, or -1 if item is not found.
 *______________________________________________________________________________
 */
int
vector_index( nft_vector * v, const void * item)
{
    nft_slice s = vector_find(v, item, (nft_slice){ 0, v->len });
    return nft_slice_len(s) ? s.x : -1 ;
}
/*______________________________________________________________________________
 *
 * vector_search_slice	Search for an item in a _sorted_ slice of a vector.
 *
 * Returns the possibly-empty subslice that contains the item.
 *______________________________________________________________________________
 */
nft_slice
vector_search_slice(nft_vector *v, const void *item, nft_slice s)
{
    nft_slice r = vector_find(v, item, s);

    // The slice length will be zero if the item was not found.
    if (nft_slice_len(r)) {
        void ** vec = v->vec;
        int i;

        // Search for the start of the range.
        for (i = r.x - 1; (i >= s.x) && (v->cmp(item, vec[i]) == 0); i--);
        r.x = i + 1;

        // Search for the end of the range.
        for (i = r.y; (i <  s.y) && (v->cmp(item, vec[i]) == 0); i++);
        r.y = i;
    }
    return r;
}
/*______________________________________________________________________________
 *
 * vector_search	Search for an item in a _sorted_ vector.
 *
 * Returns the possibly-empty subslice that contains the item.
 *______________________________________________________________________________
 */
nft_slice
vector_search(nft_vector *v, const void *item)
{
    return vector_search_slice(v, item, (nft_slice){0, v->len});
}
/*______________________________________________________________________________
 *
 * vector_slice		Copy a slice from a vector.
 *______________________________________________________________________________
 */
nft_vector *
vector_slice(nft_vector *v, nft_slice s)
{
    int          len  = nft_slice_len(s);
    nft_vector * copy = vector_new(len, v->cmp);

    if (len && copy->vec) {
        memcpy(copy->vec, &v->vec[s.x], len * sizeof(void *));
        copy->vec[len] = NULL;
        copy->len      = len;
    }
    return copy;
}
/*______________________________________________________________________________
 *
 * vector_insert	Insert an item into its sorted position in the vector.
 *______________________________________________________________________________
 */
int
vector_insert(nft_vector *v, void *item)
{
    if (v->len == v->cap) {
	v->cap = 2*v->cap;
        v->vec = realloc(v->vec, (v->cap + 1) * sizeof(void*));
        // FIXME detect realloc failure
    }
    // Find the position where this item should be inserted.
    nft_slice s = vector_find(v, item, (nft_slice){ 0, v->len });

    // Open a slot at s.x,
    v->len += 1;
    for (int i = v->len; i > s.x; i--)
	v->vec[i] = v->vec[i - 1];

    // Insert the item.
    v->vec[s.x] = item;

    return OK;
}
/*______________________________________________________________________________
 *
 * vector_delete	Delete all instances of item from the sorted vector.
 *______________________________________________________________________________
 */
int
vector_delete(nft_vector * v, void * item)
{
    nft_slice s = vector_search(v, item);
    int       l = nft_slice_len(s);
    if (l > 0) {
        memmove(&v->vec[s.x], &v->vec[s.y], (v->len - s.y) * sizeof(void*));
	v->len -= l;
	v->vec[v->len] = NULL;
	return OK;
    }
    return ESRCH;
}
/*______________________________________________________________________________
 *
 * vector_union		Return the union of two vectors.
 *			The arguments are destroyed.
 *______________________________________________________________________________
 */
nft_vector *
vector_union(nft_vector * v1, nft_vector * v2)
{
    int i1 = 0;
    int i2 = 0;
    int i  = 0;

    // Handle the trivial case, where one vector is empty.
    if      (v1->len == 0) {
	vector_free(v1);
	return v2;
    }
    else if (v2->len == 0) {
	vector_free(v2);
	return v1;
    }

    // Allocate a vector large enough to hold v1 and v2.
    nft_vector * u = vector_new(v1->len + v2->len, v1->cmp);
    void      ** vec = u->vec;

    // Do a merge sort.
    while (i1 < v1->len && i2 < v2->len)
    {
	long c = u->cmp(v1->vec[i1], v2->vec[i2]);

	if      (c < 0)	  vec[i++] = v1->vec[i1++];
	else if (c > 0)	  vec[i++] = v2->vec[i2++];
	else		{ vec[i++] = v1->vec[i1++]; i2++; }

	// The input should have no duplicates.
	assert(i < 2 || u->cmp(vec[i-1], vec[i-2]) > 0);
    }
    // Cleanup leftovers.
    while (i1 < v1->len)
	vec[i++] = v1->vec[i1++];
    while (i2 < v2->len)
	vec[i++] = v2->vec[i2++];

    u->len = i;
    vec[i] = NULL;
    vector_free(v1);
    vector_free(v2);

    return u;
}
/*______________________________________________________________________________
 *
 * vector_intersection	Return the intersection of two vectors.
 *			The arguments are destroyed.
 *______________________________________________________________________________
 */
nft_vector *
vector_intersection(nft_vector * v1, nft_vector * v2)
{
    // If either set is empty, the intersection is empty.
    if      (v1->len == 0) {
	vector_free(v2);
	return v1;
    }
    else if (v2->len == 0) {
	vector_free(v1);
	return v2;
    }
    // Something like the merge sort:
    // we look for identical pairs, assuming there are no duplicates.
    void     **vec= v1->vec;
    int	       i1 = 0;
    int	       i2 = 0;
    int	       i  = 0;
    while (i1 < v1->len && i2 < v2->len)
    {
	long c = v1->cmp(v1->vec[i1], v2->vec[i2]);

	if      (c < 0)	  i1++;
	else if (c > 0)	  i2++;
	else		{ vec[i++] = vec[i1++];  i2++; }

	// The input should have no duplicates.
	assert(i < 2 || v1->cmp(vec[i-1], vec[i-2]) > 0);
    }
    v1->len = i;
    vec[i] = NULL;
    vector_free(v2);

    return v1;
}
/*______________________________________________________________________________
 *
 * vector_difference	Returns items in v1 that are not in v2.
 *			The arguments are destroyed.
 *______________________________________________________________________________
 */
nft_vector *
vector_difference(nft_vector * v1, nft_vector * v2)
{
    // If either v1 or v2 is empty, the result is v1.
    if (v1->len == 0 || v2->len == 0) {
	vector_free(v2);
	return v1;
    }
    // Very similar to intersection,
    // but we exclude items from v2 and any that are matches.
    void     **vec= v1->vec;
    int	       i1 = 0;
    int	       i2 = 0;
    int	       i  = 0;
    while (i1 < v1->len && i2 < v2->len)
    {
	long c = v1->cmp(v1->vec[i1], v2->vec[i2]);

	if      (c < 0)	  vec[i++] = vec[i1++];
	else if (c > 0)	  i2++;
	else		{ i1++; i2++; }

	// The input should have no duplicates.
	assert(i < 2 || v1->cmp(vec[i-1], vec[i-2]) > 0);
    }
    // Add any remaining items from v1.
    while (i1 < v1->len)
	vec[i++] = vec[i1++];
    v1->len = i;
    vec[i] = NULL;
    vector_free(v2);

    return v1;
}
/*______________________________________________________________________________
 *
 * vector_equal		Returns true if v1 is setwise equal to v2.
 *			The arguments are NOT destroyed.
 *______________________________________________________________________________
 */
int
vector_equal(nft_vector *v1, nft_vector *v2)
{
    int len = v1->len;

    // If lengths aren't equal, they can't be equal.
    if (v2->len != len)
	return 0;

    // If corresponding items aren't equal, return false.
    for (int i = 0; i < len; i++)
	if (v1->cmp(v1->vec[i], v2->vec[i]) != 0)
	    return 0;

    return 1;
}
/*______________________________________________________________________________
 *
 * vector_apply		Apply a function to each item in the vector.
 *______________________________________________________________________________
 */
void
vector_apply(nft_vector *v, nft_vector_apply func, void * arg)
{
    for (int j = 0; j < v->len; j++) func(v->vec[j], arg);
}
/*______________________________________________________________________________
 *
 * vector_plex		Apply a function to each item in the vector,
 *			and return the union of the results.
 *			Destroys the input vector.
 *______________________________________________________________________________
 */
nft_vector *
vector_plex(nft_vector *v, nft_vector_plex func, void * arg)
{
    nft_vector * u = vector_new(v->len, v->cmp);

    for (int j = 0; j < v->len; j++)
        u = vector_union(u, vector_unique(func(v->vec[j], arg)));
    vector_free(v);

    return u;
}
/*______________________________________________________________________________
 *
 * vector_filter	Apply a functional filter to each item in the vector.
 *			If the function returns false, the item is removed.
 *______________________________________________________________________________
 */
nft_vector *
vector_filter(nft_vector *v, nft_vector_filter filter, const void * arg)
{
    void ** vec = v->vec;
    if (vec) {
        int i = 0;
        for (int j = 0; j < v->len; j++)
            if (filter(vec[j], arg))
                vec[i++] = vec[j];
        v->len = i;
        vec[i] = NULL;
    }
    return v;
}
/*______________________________________________________________________________
 *
 * vector_filter_2	Like vector_filter(), with extra argument to filter.
 *______________________________________________________________________________
 */
nft_vector *
vector_filter_2(nft_vector *v, nft_vector_filter_2 filter, const void * arg1, const void * arg2)
{
    void ** vec = v->vec;
    if (vec) {
        int i = 0;
        for (int j = 0; j < v->len; j++)
            if (filter(vec[j], arg1, arg2))
                vec[i++] = vec[j];
        v->len = i;
        vec[i] = NULL;
    }
    return v;
}
/*______________________________________________________________________________
 *
 * vector_reduce	Apply a reducing function to each item in the vector.
 *______________________________________________________________________________
 */
void *
vector_reduce(nft_vector *v, nft_vector_reduce func)
{
    void * result = NULL;

    if (v->len > 0) {
	result = v->vec[0];
	for (int j = 1; j < v->len; j++)
	    result = func(result, v->vec[j]);
    }
    return result;
}
/*______________________________________________________________________________
 *
 * vector_to_array
 *
 * Converts a vector to an array of void *, returning the count via *num.
 * If the vector is empty, returns NULL. The input vector is destroyed.
 *______________________________________________________________________________
 */
void **
vector_to_array(nft_vector * v, int * nump)
{
    void ** result  = NULL;
    int	    nresult = 0;

    if (v->len > 0) {
	result  = v->vec;
	nresult = v->len;
        v->len = 0;
        v->cap = 0;
        v->vec = NULL;
    }
    vector_free(v);

    if ( nump )
	*nump = nresult;

    return result;
}
/*______________________________________________________________________________
 *
 * vector_string|strcase|integer_comparator
 *______________________________________________________________________________
 */
long vector_string_comparator(char * a, char * b)
{
    return (long) strcmp(a, b);
}
long vector_strcase_comparator(char * a, char * b)
{
    return (long) strcasecmp(a, b);
}
long vector_integer_comparator(void * a, void * b)
{
    return (long) ((uintptr_t)a - (uintptr_t) b);
}
/******************************************************************************/
/*******								*******/
/*******		VECTOR PUBLIC APIS				*******/
/*******								*******/
/*******  These APIs are Nifty wrappers around the private APIs.	*******/
/*******								*******/
/******************************************************************************/

nft_vector_h
nft_vector_new(int capacity, nft_comparator cmp)
{
    nft_vector * vector = vector_new(capacity, cmp);
    return nft_vector_handle(vector);
}
nft_vector_h
nft_vector_vnew (nft_comparator cmp, ...)
{
    va_list  ap;
    va_start(ap, cmp);
    nft_vector * vector = _vector_vanew(cmp, ap);
    va_end(ap);
    return nft_vector_handle(vector);
}
int
nft_vector_free(nft_vector_h h)
{
    int result = EINVAL;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        // Double-discard, to release the reference returned from vector_create().
        if ((result = nft_vector_discard(vector)) == 0)
             result = nft_vector_discard(vector);
        assert(result == 0);
    }
    return result;
}
int
nft_vector_append(nft_vector_h h, void * item)
{
    int result = EINVAL;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector_append(vector, item);
        nft_vector_discard(vector);
    }
    return result;
}
nft_vector_h
nft_vector_sort(nft_vector_h h)
{
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        vector_sort(vector);
        nft_vector_discard(vector);
    }
    return h;
}
nft_vector_h
nft_vector_unique(nft_vector_h h)
{
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        vector_unique(vector);
        nft_vector_discard(vector);
    }
    return h;
}
int
nft_vector_index(nft_vector_h h, const void * item)
{
    int result = -1;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector_index(vector, item);
        nft_vector_discard(vector);
    }
    return result;
}
int
nft_vector_len(nft_vector_h h)
{
    int result = -1;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector->len;
        nft_vector_discard(vector);
    }
    return result;
}
void *
nft_vector_nth(nft_vector_h h, int index)
{
    void * result = NULL;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector_nth(vector, index);
        nft_vector_discard(vector);
    }
    return result;
}
nft_slice
nft_vector_search(nft_vector_h h, const void * item)
{
    nft_slice    result = (nft_slice){-1,-1};
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector_search_slice(vector, item, (nft_slice){0, vector->len});
        nft_vector_discard(vector);
    }
    return result;
}
nft_vector_h
nft_vector_slice(nft_vector_h h, nft_slice s)
{
    nft_vector_h result = NULL;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        nft_vector * copy = vector_slice(vector, s);
        result = nft_vector_handle(copy);
        nft_vector_discard(vector);
    }
    return result;
}
int
nft_vector_insert(nft_vector_h h, void *item)
{
    int result = EINVAL;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector_insert(vector, item);
        nft_vector_discard(vector);
    }
    return result;
}
int
nft_vector_delete(nft_vector_h h, void * item)
{
    int result = EINVAL;
    nft_vector * vector = nft_vector_lookup(h);
    if (vector) {
        result = vector_delete(vector, item);
        nft_vector_discard(vector);
    }
    return result;
}
nft_vector_h
nft_vector_union(nft_vector_h a, nft_vector_h b)
{
    nft_vector_h result = NULL;
    nft_vector * va = nft_vector_lookup(a);
    if (va) {
        nft_vector * vb = nft_vector_lookup(b);
        if (vb) {
            result = nft_vector_handle(vector_union(va, vb));
            nft_vector_discard(vb);
        }
        nft_vector_discard(va);
    }
    return result;
}
nft_vector_h
nft_vector_intersection(nft_vector_h a, nft_vector_h b)
{
    nft_vector_h result = NULL;
    nft_vector * va = nft_vector_lookup(a);
    if (va) {
        nft_vector * vb = nft_vector_lookup(b);
        if (vb) {
            result = nft_vector_handle(vector_intersection(va, vb));
            nft_vector_discard(vb);
        }
        nft_vector_discard(va);
    }
    return result;
}
nft_vector_h
nft_vector_difference(nft_vector_h a, nft_vector_h b)
{
    nft_vector_h result = NULL;
    nft_vector * va = nft_vector_lookup(a);
    if (va) {
        nft_vector * vb = nft_vector_lookup(b);
        if (vb) {
            result = nft_vector_handle(vector_difference(va, vb));
            nft_vector_discard(vb);
        }
        nft_vector_discard(va);
    }
    return result;
}
int
nft_vector_equal(nft_vector_h a, nft_vector_h b)
{
    int result = 0;
    nft_vector * va = nft_vector_lookup(a);
    if (va) {
        nft_vector * vb = nft_vector_lookup(b);
        if (vb) {
            result = vector_equal(va, vb);
            nft_vector_discard(vb);
        }
        nft_vector_discard(va);
    }
    return result;
}
/*
 * TODO: I have yet to write handle-based wrappers for these functional apis:
 * vector_apply
 * vector_plex
 * vector_filter
 * vector_filter_2
 * vector_reduce
*/

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		VECTOR PACKAGE UNIT TEST			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Assertions must be active in test code.
#endif
#include <assert.h>
#include <stdio.h>
#include <sys/times.h>
#include <sys/resource.h>

void print_vector(nft_vector * v) {
    for (int i = 0; i < v->len; i++)
        fprintf(stderr, "%p, ", v->vec[i]);
    fprintf(stderr, "\n");
}

intptr_t itest[] = { 7, 2, 3, 9, 1, 0, 5, 8, 6, 4, 2, 3, 3 };
const unsigned ITEST_LEN = (sizeof(itest) / sizeof(itest[0]));

// This function is used with vector_reduce to sum the items.
static long sum( long a, long b) { return (a + b); }

// Test the public API
static void test_public(void)
{
    nft_vector_h v = nft_vector_new(4, vector_integer_comparator);

    // Test vector_append().
    for (int i = 0; i < ITEST_LEN ; i++)
	nft_vector_append(v, (void*) itest[i]);
    for (int i = 0; i < ITEST_LEN ; i++)
	assert(nft_vector_nth(v, i) == (void*) itest[i]);

    // Test vector_sort().
    nft_vector_sort(v);
    for (int i = 0; i < ITEST_LEN - 1; i++)
	assert(nft_vector_nth(v, i) <= nft_vector_nth(v, i+1));

    // Test vector_search().
    for (int i = 0; i < ITEST_LEN ; i++)
	assert(nft_slice_len(nft_vector_search(v, (void*) itest[i])) > 0);
    
    nft_vector_h w = nft_vector_new(4, vector_integer_comparator);

    // Test vector_insert().
    for (int i = 0; i < ITEST_LEN ; i++)
	nft_vector_insert(w, (void*) itest[i]);
    for (int i = 0; i < ITEST_LEN - 1; i++)
	assert(nft_vector_nth(w, i) == nft_vector_nth(v, i));

    // Test vector_unique().
    nft_vector_unique(w);

    // Test vector_index().
    assert( nft_vector_index(w, (void*)  0) ==  0);
    assert( nft_vector_index(w, (void*)  1) ==  1);
    assert( nft_vector_index(w, (void*)  2) ==  2);
    assert( nft_vector_index(w, (void*)  3) ==  3); // after vector_unique()
    assert( nft_vector_index(w, (void*) -1) == -1);

    // Test vector_delete().
    assert(nft_vector_delete(w, (void*) itest[2]) == OK);
    assert(nft_vector_index( w, (void*) itest[2]) == -1);
    assert(nft_vector_insert(w, (void*) itest[2]) == OK);
    assert(nft_vector_index( w, (void*) itest[2]) >=  0);

    // Test vector free().
    assert(nft_vector_free(v) == OK);
    assert(nft_vector_free(w) == OK);

    // Test the set operations.
    v = nft_vector_vnew(vector_string_comparator, "a", "b", "c", NULL);
    w = nft_vector_vnew(vector_string_comparator, "c", "d", "e", NULL);
    {   // test union
        // x is our expected result. y and z are duplicates of v and w.
        nft_vector_h x = nft_vector_vnew(vector_string_comparator, "a", "b", "c", "d", "e", NULL);
        nft_vector_h y = nft_vector_slice(v, (nft_slice){ 0, nft_vector_len(v)});
        nft_vector_h z = nft_vector_slice(w, (nft_slice){ 0, nft_vector_len(w)});
        nft_vector_h r = nft_vector_union(y, z);
        assert(nft_vector_equal(r, x));
        // Remember that y and z will be freed by nft_vector_union,
        // so we only need to free x and r (tho r may actually be y or z).
        assert(nft_vector_free(x) == OK);
        assert(nft_vector_free(r) == OK);
    }
    {   // test intersection
        nft_vector_h x = nft_vector_vnew(vector_string_comparator, "c", NULL);
        nft_vector_h y = nft_vector_slice(v, (nft_slice){ 0, nft_vector_len(v)});
        nft_vector_h z = nft_vector_slice(w, (nft_slice){ 0, nft_vector_len(w)});
        nft_vector_h r = nft_vector_intersection(y, z);
        assert(nft_vector_equal(r, x));
        assert(nft_vector_free(x) == OK);
        assert(nft_vector_free(r) == OK);
    }
    {   // test difference
        nft_vector_h x = nft_vector_vnew(vector_string_comparator, "a", "b", NULL);
        nft_vector_h y = nft_vector_slice(v, (nft_slice){ 0, nft_vector_len(v)});
        nft_vector_h z = nft_vector_slice(w, (nft_slice){ 0, nft_vector_len(w)});
        nft_vector_h r = nft_vector_difference(y, z);
        assert(nft_vector_equal(r, x));
        assert(nft_vector_free(x) == OK);
        assert(nft_vector_free(r) == OK);
    }
    assert(nft_vector_free(v) == OK);
    assert(nft_vector_free(w) == OK);

    fprintf(stderr, "nft_vector: test_public passes.\n");
}

// Test the private API
static void test_private(void)
{
    {   // x: Test variadic vector_vnew().
        // The variadic argument list must be terminated by a null.
        // This null will not be incorporated into the vector,
        // but all vectors have a terminating null at v.vec[v.len].
        // The returned vector will have been sorted.
        nft_vector * x = vector_vnew(vector_integer_comparator, 4L, 2L, 3L, 5L, 1L, 0L);
        for (intptr_t i = 0; i < 5; i++)
            assert(x->vec[i] == (void*)(i+1));
        assert(x->vec[5] == NULL);
        assert(x->cap == 5);
        assert(x->len == 5);

        // x: Test vector_find().
        // nft_slice s = vector_find(x, (nft_slice){0,0}, (void*) 1); fprintf(stderr,"s[%d:%d]\n", s.x, s.y);
        assert(!nft_slice_cmp(vector_find(x, (void*) 0, (nft_slice){0,5}), (nft_slice){0,0}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 1, (nft_slice){0,0}), (nft_slice){0,0}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 1, (nft_slice){0,1}), (nft_slice){0,1}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 2, (nft_slice){0,1}), (nft_slice){1,1}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 2, (nft_slice){0,2}), (nft_slice){1,2}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 5, (nft_slice){0,5}), (nft_slice){4,5}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 5, (nft_slice){4,5}), (nft_slice){4,5}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 5, (nft_slice){5,5}), (nft_slice){5,5}));
        assert(!nft_slice_cmp(vector_find(x, (void*) 6, (nft_slice){0,5}), (nft_slice){5,5}));

        // y: Test vector_slice(x).
        nft_vector * y = vector_slice(x, (nft_slice){1, 4});
        for (intptr_t i = 0; i < 3; i++)
            assert(y->vec[i] == (void*)(i+2));
        assert(y->vec[3] == NULL);
        assert(y->cap == 3);
        assert(y->len == 3);

        vector_free(y);
        vector_free(x);
    }

    nft_vector * v = vector_new(4, vector_integer_comparator);

    // Test vector_append().
    for (int i = 0; i < ITEST_LEN ; i++)
	vector_append(v, (void*) itest[i]);
    for (int i = 0; i < ITEST_LEN ; i++)
	assert(v->vec[i] == (void*) itest[i]);

    // Test vector_sort().
    vector_sort(v);
    for (int i = 0; i < ITEST_LEN - 1; i++)
	assert(v->vec[i] <= v->vec[i + 1]);

    // Test vector_search().
    for (int i = 0; i < ITEST_LEN ; i++)
	assert(nft_slice_len(vector_search(v, (void*) itest[i])) > 0);

    // Test vector_search_slice().
    nft_slice all = (nft_slice){ 0, v->len };
    assert(!nft_slice_cmp(vector_search_slice(v, (void*) 2, all), (nft_slice){ 2, 4 }));
    assert(!nft_slice_cmp(vector_search_slice(v, (void*) 3, all), (nft_slice){ 4, 7 }));

    // Test vector_reduce()
    assert(53 == (long) vector_reduce(v, (nft_vector_reduce) sum));

    // Test vector_insert().
    nft_vector * w = vector_new(4, vector_integer_comparator);
    for (int i = 0; i < ITEST_LEN ; i++)
	vector_insert(w, (void*) itest[i]);
    for (int i = 0; i < ITEST_LEN - 1; i++)
	assert(w->vec[i] == v->vec[i]);

    // Test vector_unique().
    vector_unique(w);

    // Test vector_index().
    assert( vector_index(w, (void*)  0) ==  0);
    assert( vector_index(w, (void*)  1) ==  1);
    assert( vector_index(w, (void*)  2) ==  2);
    assert( vector_index(w, (void*)  3) ==  3); // after vector_unique()
    assert( vector_index(w, (void*) -1) == -1);

    // Test vector_delete().
    assert(vector_delete(w, (void*) itest[2]) == OK);
    assert(vector_index( w, (void*) itest[2]) == -1);
    assert(vector_insert(w, (void*) itest[2]) == OK);
    assert(vector_index( w, (void*) itest[2]) >=  0);

    // Test vector_union. Note that w is freed by this.
    v = vector_union(vector_unique(v), w);
    for (int i = 0; i < ITEST_LEN ; i++)
	assert(vector_index(v, (void*) itest[i]) >= 0);

    // Test vector_intersection. Again, w is freed.
    w = vector_vnew(vector_integer_comparator, (void*) 3, (void*) 0);
    assert(w->len == 1);
    assert(w->vec[0] == (void*) 3);
    v = vector_intersection(v, w);
    assert(v->len == 1);
    assert(v->vec[0] == (void*) 3);

    // Test vector_difference. Again, w is freed.
    w = vector_vnew(vector_integer_comparator, (void*) 2, (void*) 3, (void*) 4, (void*) 0);
    v = vector_difference(w, v);
    assert(v->len == 2);
    assert(v->vec[0] == (void*) 2);
    assert(v->vec[1] == (void*) 4);

    // Test vector_equal
    w = vector_vnew(vector_integer_comparator, (void*) 2, (void*) 4, (void*) 0);
    assert(vector_equal(v, w));

    vector_free(v);
    vector_free(w);
    fprintf(stderr, "nft_vector: test_private passes.\n");
}

// Test performance using the private API
static void test_performance(char ** keys, int nkeys)
{
    int i;
#ifndef CLK_TCK
#define CLK_TCK 100
#endif
    float	usert = 0.0, syst = 0.0;
    float       tick  = 1.0 / CLK_TCK;
    struct tms 	start, done;
#define MARK	times(&start)
#define TIME	times(&done); \
    usert = (done.tms_utime - start.tms_utime) * tick; \
    syst  = (done.tms_stime - start.tms_stime) * tick

    /* Load vector a from the first half of keys, using vector_insert.
     * vector_insert maintains sorted order, so it is very slow on long vectors.
     */
    nft_vector * a = vector_new(nkeys/2, vector_string_comparator);
    MARK; /* record start time */
    for (i = 0; i < nkeys/2; i++) {
        if (nkeys <= 20) {
            nft_slice s = vector_find(a, keys[i], (nft_slice){0, a->len});
            fprintf(stderr, "index [%d:%d] %s\n", s.x, s.y, keys[i]);
        }
	vector_insert(a, keys[i]);
    }
    TIME; /* compute time usage */
    fprintf(stderr, "Time to insert %d strings: %5.2fu %5.2fs\n", i, usert, syst);

    /* Load up vector b with the second half of keys, using vector_append.
     * You must call vector_sort() to restore sorted order.
     */
    nft_vector * b = vector_new(nkeys/2, vector_string_comparator);
    MARK;
    for (i = 0; i < nkeys/2; i++)
	vector_append(b, keys[nkeys/2 + i]);
    TIME;
    fprintf(stderr, "Time to append %d strings: %5.2fu %5.2fs\n", i, usert, syst);
    MARK;
    vector_sort(b);
    TIME;
    fprintf(stderr, "Time to sort   %d strings: %5.2fu %5.2fs\n", i, usert, syst);

    /* Create a union of both vectors, preserving the original input vectors.
     * The union should contain every item in keys[].
     */
    nft_vector * c = vector_union(vector_slice(a, (nft_slice){0, a->len}),
                                  vector_slice(b, (nft_slice){0, b->len}));
    MARK;
    for (i = 0; i < nkeys; i++)
	assert(vector_index(c, keys[i]) >= 0);
    TIME;
    fprintf(stderr, "Time to search %d strings: %5.2fu %5.2fs\n", i, usert, syst);

    /* The difference c - a should equal b.
     */
    c = vector_difference(c, vector_slice(a, (nft_slice){0, a->len}));
    assert(vector_equal(c, b));
    vector_free(c);

    /* Create the intersection of both vectors. It should be empty.
     */
    c = vector_intersection(vector_slice(a, (nft_slice){0, a->len}),
                            vector_slice(b, (nft_slice){0, b->len}));
    assert(c->len == 0);
    vector_free(c);

    /* Test the various search functions.
     */
    for (int i = 0; i < nkeys/2; i++)
    {
	assert(vector_index(a, keys[i]) >= 0);
	assert(nft_slice_len(vector_search(a, keys[i])) == 1);
	assert(nft_slice_len(vector_search_slice(a, keys[i], (nft_slice){0, a->len})) == 1);

        int j = nkeys/2 + i;
	assert(vector_index(b, keys[j]) >= 0);
	assert(nft_slice_len(vector_search(b, keys[j])) == 1);
	assert(nft_slice_len(vector_search_slice(b, keys[j], (nft_slice){0, b->len})) == 1);
    }

    // Free everything to make valgrind happy.
    vector_free(a);
    vector_free(b);
    for (int i = 0; i < nkeys; i++) free(keys[i]);

    fprintf(stderr, "nft_vector: test_performance passes.\n");
}

// Returns the number of unfreed handles.
int count_handles() {
    int result = 0;
    nft_handle * handles = nft_core_gather(nft_core_class);
    for (result = 0; handles[result] ; result++);
    free(handles);
    return result;
}

/* This test inserts one string per line of the standard input into keys.
 * You can give a limit on the command line, eg:
 *
 *   ./nft_vector 10 < /usr/share/dict/words
 */
int main(int argc, char * argv[])
{
    const int MAX_KEYS = 100000;
    char * keys[MAX_KEYS];

    // Test the public and private APIs.
    test_public();
    test_private();

    // Load keys from stdin.
    int    limit = ((argc == 2) ? ((atoi(argv[1]) <= MAX_KEYS) ? atoi(argv[1]) : MAX_KEYS) : MAX_KEYS);
    char   linebuff[80];
    int    i = 0;
    while (fgets(linebuff, sizeof(linebuff), stdin) && (i < limit)) {
        char * nl = strchr(linebuff, '\n');
        if (nl) *nl = '\0';
	keys[i++] = strdup(linebuff);
    }
    // Round keys[] to an even number of entries.
    if (i & 1)
	free(keys[i--]);

    // Run the performance tests.
    test_performance(keys, i);

    // Test for leaked handles.
    assert(0 == count_handles());

    fprintf(stderr, "nft_vector: All tests passed.\n");
    exit(0);
}

#endif
