/****************************************************************************
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
 ********************************************************************
 *
 * File:  nft_vector.h
 *
 * DESCRIPTION
 *
 * This package offers efficient operations on sorted arrays of void pointers.
 * Some of the operations that this package offers are:
 *
 *   - search
 *   - filter
 *   - map-reduce
 *   - set union, intersection, difference, equality
 *
 * This package works best with collections of items that do not change.
 * In a typical vector lifecycle, the items are appended and then sorted,
 * after which the vector can be used efficiently:
 *
 *   nft_vector_h v = nft_vector_new(DATALEN, vector_string_comparator);
 *   for (int i = 0; i < DATALEN ; i++)
 *       nft_vector_append(v, data[i]);
 *   nft_vector_sort(v);
 *   ...
 *   ... (operations on the vector)
 *   ...
 *   nft_vector_free(v);
 *
 * It is your responsibility, to ensure that vectors are kept sorted.
 * This package includes operations nft_vector_insert() and nft_vector_delete(),
 * that  allow you to add or remove items while keeping the vector sorted,
 * but these operations are very costly. Look to the rbtree package if you
 * wish to work with collections that are dynamic.
 *
 * For examples that illustrate the use of this package, please refer
 * to the unit test in Nifty/src/nft_vector.c, beginning at #ifdef MAIN.
 */
#ifndef _NFT_VECTOR_H_
#define _NFT_VECTOR_H_

#include "nft_core.h"

typedef struct nft_vector nft_vector;

// Define the Nifty class string, showing nft_vector derives from nft_core.
#define nft_vector_class nft_core_class ":nft_vector"

// Declare helper functions nft_vector_cast, _handle, _lookup, _discard
NFT_DECLARE_WRAPPERS(nft_vector,)

// Declare function parameter types.
typedef long        (* nft_comparator)     ( /* void * a, void * b */ );
typedef void	    (* nft_vector_apply)   (const void *, const void *);
typedef nft_vector *(* nft_vector_plex)    (const void *, const void *);
typedef int 	    (* nft_vector_filter)  (const void *, const void *);
typedef int 	    (* nft_vector_filter_2)(const void *, const void *, const void *);
typedef void *      (* nft_vector_reduce)  (const void *, const void *);

// Struct nft_slice works similarly to perl or python - the slice describes
// a range within the array, starting at index x, up to but not including y.
// For example, the slice { 0, v.len } includes all the items in the vector,
// and the slice { 0, 0 } is empty.
//
typedef struct    nft_slice { int x, y; } nft_slice;
static inline int nft_slice_len(nft_slice s) {
    return s.y - s.x;
}
static inline int nft_slice_cmp(nft_slice a, nft_slice b) {
    if (b.x - a.x)
        return b.x - a.x;
    return b.y - a.y;
}


nft_vector_h nft_vector_new(int capacity, nft_comparator comparator);
/*
  Create a vector, given the initial capacity and comparison function.
  The vector will be resized by doubling as necessary.
  strcmp() would be suitable for null-terminated strings, except that
  it returns an int, when comparators should return a long integer.
  See "handy comparators" below for string and integer comparators.
*/

int nft_vector_free ( nft_vector_h handle );
/*
  Releases the vector's handle, and frees the associated memory.
  Returns zero on success, or EINVAL for handles that are invalid,
  and for handles that are not nft_vector objects.
*/

int nft_vector_append(nft_vector_h handle, void * item);
/*
  Appends the item to the end of the vector.
  Returns zero on success, ENOMEM on memory exhaustion,
  and EINVAL for handles that are invalid, or handles that
  do not refer to nft_vector objects.
*/

int nft_vector_delete(nft_vector_h handle, void * item);
/*
  Deletes all instances of item from the vector.
  Returns zero on success, ESRCH if no items were deleted,
  and EINVAL on invalid or non-vector handles.
*/

int nft_vector_index(nft_vector_h handle, const void * item);
/*
  Searches for an instance of the item in a _sorted_ vector.
  Returns the index of the item, or -1 if none were found.
*/

int nft_vector_insert(nft_vector_h handle, void *item);
/*
  Inserts the given item at the correct index in a _sorted_ vector.
  Returns zero on success, ENOMEM on memory exhaustion,
  and EINVAL on invalid or non-vector handles.
*/

int nft_vector_len(nft_vector_h h);
/*
  Returns the length of the given vector, or -1 on invalid handles.
*/

void * nft_vector_nth(nft_vector_h handle, int index);
/*
  Returns the item at the given index, or NULL if index is out of bounds.
*/

int nft_vector_push(nft_vector_h handle, void * item);
/*
  An alias for vector_append, for symmetry with nft_vector_pop().
  The _push and _pop functions let you use the vector like stack.
*/

int nft_vector_pop(nft_vector_h handle, void ** item);
/*
  Removes the last item in the vector and saves it in *item.
  The _push and _pop functions let you use the vector like stack.
  Returns zero on success, and EINVAL on invalid or non-vector handles.
  If the vector is empty, *itemp is set to NULL and ENOENT is returned.
*/

nft_slice nft_vector_search(nft_vector_h handle, const void * item);
/*
  Searches a _sorted_ vector for all instances of the item.
  Returns a possibly-empty slice describing the range of matching items.
*/

nft_vector_h nft_vector_slice(nft_vector_h h, nft_slice s);
/*
  Returns a new vector containing the items of the input vector
  within the range specified by the slice, which can be empty.
  Use (nft_slice){ 0, nft_vector_len(h) } to duplicate the vector.
*/

nft_vector_h nft_vector_sort(nft_vector_h handle);
/*
  Sorts the vector, using the comparator function to order the items.
  Returns the input handle.
*/

nft_vector_h nft_vector_unique(nft_vector_h handle);
/*
  Removes any duplicate items in a _sorted_ vector.
  Returns the input handle.
*/

nft_vector_h nft_vector_union       (nft_vector_h a, nft_vector_h b);
nft_vector_h nft_vector_intersection(nft_vector_h a, nft_vector_h b);
nft_vector_h nft_vector_difference  (nft_vector_h a, nft_vector_h b);
int          nft_vector_equal       (nft_vector_h a, nft_vector_h b);
/*
  Perform set operations on _sorted_, _uniqued_ vectors.
  Except for equality, these operations free their inputs.
  Or you should think of it that way. When it is efficient,
  one of the inputs may be returned as the output, possibly
  with modifications. If you wish to preserve the inputs,
  pass duplicates created with nft_vector_slice().
*/

/*================================================================================
 *
 * These are the private, direct-access APIs. You can use them when subclassing,
 * and in situations where you wish to avoid the overhead of the handle-based API.
 *
 *================================================================================
 */
struct nft_vector
{
    nft_core		core;
    nft_comparator	cmp;	/* Comparison function	*/
    void	      **vec;	/* Vector of void*'s  	*/
    int 		cap;	/* Maximum length allocated */
    int			len;	/* Current length of vector */
};

nft_vector * vector_new(int cap, nft_comparator cmp);
nft_vector * vector_vnew (nft_comparator cmp, ...);
int          vector_free(nft_vector * v);

void         vector_apply (nft_vector * v, nft_vector_apply func, void * arg);
int          vector_append(nft_vector * v, void *p);
int          vector_delete(nft_vector * v, void *item);
nft_vector * vector_difference(nft_vector * v1, nft_vector * v2);
int          vector_equal (nft_vector * v1, nft_vector *v2);
nft_vector * vector_filter(nft_vector * v, nft_vector_filter filter, const void * arg);
nft_vector * vector_filter_2(nft_vector * v, nft_vector_filter_2 filter, const void * arg1, const void * arg2);
nft_slice    vector_find  (nft_vector * v, const void * item, nft_slice s);
int          vector_index (nft_vector * v, const void * item);
int          vector_insert(nft_vector * v, void *p);
nft_vector * vector_intersection(nft_vector * v1, nft_vector * v2);
nft_vector * vector_plex  (nft_vector * v, nft_vector_plex func, void * arg);
void *       vector_reduce(nft_vector * v, nft_vector_reduce func);
nft_slice    vector_search(nft_vector * v, const void *item);
nft_slice    vector_search_slice(nft_vector * v, const void *item, nft_slice s);
nft_vector * vector_slice (nft_vector * v, nft_slice s);
nft_vector * vector_sort  (nft_vector * v);
void      ** vector_to_array(nft_vector * v, int * nump);
nft_vector * vector_union (nft_vector * v1, nft_vector * v2);
nft_vector * vector_unique(nft_vector * v);

// Some handy comparators
long vector_string_comparator (char * a, char * b);
long vector_strcase_comparator(char * a, char * b);
long vector_integer_comparator(void * a, void * b);

/* These calls should only be used by subclasses.
 */
nft_vector * vector_create(const char * class, size_t size, int cap, nft_comparator cmp);
void         vector_destroy(nft_core   *);

/* Access the nth item in a vector.
 * Returns null if vector is null, or n is out of range.
 */
static inline void * vector_nth(nft_vector * v, int i) {
    return (v && (i >= 0) && (i < v->len)) ? v->vec[i] : NULL ;
}

#endif // _NFT_VECTOR_H_
