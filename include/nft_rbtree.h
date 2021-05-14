/****************************************************************************
 * (C) Copyright Xenadyne, Inc. 2003-2015  All rights reserved.
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
 * File:  nft_rbtree.h
 *
 * DESCRIPTION
 *
 * The rbtree package is an associative (or map) data structure.
 * It allows you to record an association between a key and a data value.
 * Both key and data must be pointer-sized. This package allows you to
 * look up data very efficiently given the key, using a balanced binary
 * tree algorithm. Therefore, you must also provide a comparison function
 * so that keys can be sorted.
 *
 * In order to serve as an associative map, the key and data values are
 * stored separately in the tree nodes. In many cases, the key is part of
 * the object that you wish to store. For example, assume that you have
 * records that represent a person, and you wish to be able to access these
 * records up by the name. Then your inserts would look like this:
 *
 *   nft_rtbree_insert(t, person->name, person);
 *
 * One benefit, is that you can use strcmp() as the comparison function
 * for any tree whose keys are strings, instead of writing a separate
 * comparator for every object type that you store.
 *
 * In addition to insert and search functions, this package provides
 * a tree walker which traverses the tree in key order, so that you can
 * do a linear search for non-key attributes.  There are also functions
 * for replacing and deleting keys. The insert function can insert multiple
 * nodes with duplicate keys - it will not fail if the key alrady exists.
 * The _replace() operation will replace an existing key-data pair instead.
 * The only way to find all duplicates of a key walk the tree.
 *
 * Duplex Keys: You may wish to sort nodes on both key and data value.
 * In cases where you will be inserting duplicate keys, this allows you
 * to search for a specific key-data pair. We call this a "duplex key".
 * To use duplex keys, simply provide a comparison function that takes
 * four parameters: key1, key2, data1 and data2. We always invoke the
 * comparison function with these four arguments. In the case of simplex
 * keys, the extra parameters are simply ignored. Many of the API calls
 * have special requirements when using duplex keys, so read the notes
 * carefully.
 *
 * Multithreading: This package provides optional thread-safety.
 * Each tree contains a shared-reader/exclusive-writer lock, that allows
 * concurrent searches and traversals, and enforces exclusive insert,
 * replace, and delete operations. But you MUST ENABLE locking via
 * nft_rbtree_locking() - it is not enabled by default.
 *
 * Algorithm: This package implements the red-black balanced binary tree
 * algorithm as described in Sedgewick, chapter 15, to maintain semi-
 * balanced trees: the balance criterion is that the deepest leaf node
 * is not more than twice as deep as the shallowest leaf node.
 *
 */
#ifndef _NFT_RBTREE_H_
#define _NFT_RBTREE_H_

#include "nft_core.h"

typedef struct nft_rbtree nft_rbtree;
typedef struct nft_rbnode nft_rbnode;

// Define the Nifty class string, showing nft_rbtree derives from nft_core.
#define nft_rbtree_class nft_core_class ":nft_rbtree"

// Declare helper functions nft_rbtree_cast, _handle, _lookup, _discard
NFT_DECLARE_WRAPPERS(nft_rbtree,)

// Don't use this to cast - your comparison function must return long.
typedef long    (* RBTREE_COMPARE)( /* void * obj, void * obj2 */ );

/* Ease of use typedefs so the user can easily cast function pointers.
 */
typedef void    (* RBTREE_APPLY)  ( void * key, void * obj, void * arg);
typedef void    (* RBTREE_APPLYX) (             void * obj, void * arg);


//______________________________________________________________________________
//
nft_rbtree_h nft_rbtree_new( int init_nodes, RBTREE_COMPARE comparator);
/*
 Creates a new tree. Memory is allocated for min_nodes. The tree will
 be realloced if you exceed the initial allocation. The comparator is
 a function that compares two pointer-sized arguments and returns an
 integer that is less than, greater than, or equal to zero indicating
 the relative order of the arguments. For example, the C-library function
 strcmp() is a suitable comparator for arguments that are char *'s.

 For duplex keys, the comparator takes four parameters: key1, key2,
 data1, and data2.
*/

//______________________________________________________________________________
//
int nft_rbtree_free ( nft_rbtree_h );
/*
 Releases the tree's handle, and frees associated node storage.
 Returns zero on success, or EINVAL for handles that have already
 been release, and for handles that are not nft_rbtree objects.
*/

//______________________________________________________________________________
//
int nft_rbtree_insert ( nft_rbtree_h tree,
                        void       * key,
                        void       * data );
/*
 Inserts the key-data pair into the tree, using the comparator function
 that was supplied to rbtree-create. The key argument must be suitable for
 passing to the comparator. This function will insert duplicate keys -
 it does not overwrite the existing key-data pair if you insert a new pair
 with an equivalent key. Use nft_rbtree_replace() if you wish for existing
 key-data pairs to be replaced.
 Returns 1 (true) on success, or zero when memory is exhausted.
*/

//______________________________________________________________________________
//
int nft_rbtree_replace ( nft_rbtree_h   tree,
                         void         * key,
                         void         **data );
/*
 Inserts the key-data pair, or replaces the existing pair if an equal key is found.
 If an existing pair is replaced, the previous data value is stored in *data,
 and the value 2 is returned. Returns 1 if the key-data was  inserted successfully,
 without replacing a previous pair. Returns zero when  the key-data pair could not
 be inserted due memory exhaustion.
 This function is not to be used with duplex keys - instead you must first delete
 any existing pair, and then insert the new pair.
*/

//______________________________________________________________________________
//
int nft_rbtree_search ( nft_rbtree_h tree,
                        void       * key,
                        void       **data );
/*
 Searches the tree for key, returning 1 (true) if key is found, or zero
 if it is not found. If key is found, and *data is non-null, the associated
 data value is returned via *data. If duplex keys are in use, *data must
 specify a data value. If this key-data pair is found, nft_rbtree_search()
 replaces *data with the orignally inserted data and returns true.
*/

//______________________________________________________________________________
//
int nft_rbtree_delete ( nft_rbtree_h tree,
                        void       * key,
                        void      ** data);
/*
 Deletes one node with the given key. Returns true if a node was found and
 deleted, and also returns the corresponding value if data is non-null.
 If duplex keys are in use, *data must specify a data value, and will delete
 only the specified key-data pair. If this key-data pair is found, *data is
 replaced with the deleted data, and nft_rbtree_delete() returns true.
*/

//______________________________________________________________________________
//
int nft_rbtree_apply ( nft_rbtree_h handle,
                       RBTREE_APPLY apply,
                       void       * arg);
/*
 Call the function 'apply' on every item in the tree.
 For each item the function is passed the key, data, and the void * arg.
 Returns the number of items in the tree. The applied function must not
 insert or delete items from the tree.
*/


//______________________________________________________________________________
//
int nft_rbtree_walk_first ( nft_rbtree_h tree,
                            void      ** key,
                            void      ** data );

int nft_rbtree_walk_next  ( nft_rbtree_h tree,
                            void      ** key,
                            void      ** data );
/*
 Initiate a walk of tree in order of ascending keys. _walk_first returns true
 if there is at least one node in the tree, else false. Key-data pairs are
 returned via the optional *key and *data pointers.

 This iterator is non-reentrant - you may not use this walk concurrently
 in separate threads, nor recursively within a single thread. You are able
 to insert and delete items during the walk, however.

 The alternative below is reentrant, so you may use it to conduct a walk
 within a function which itself walks the tree, or in a concurrent thread.
 not insert or delete items during a reentrant walk.

 The unit test code in src/nft_rbtree.c illustrates the use of this iterator.
 In general, it is preferable to use the nft_rbtree_apply() call where possible.
*/


//______________________________________________________________________________
//
int nft_rbtree_walk_first_r ( nft_rbtree_h tree,
                              void      ** key,
                              void      ** data,
                              void      ** walk );
int nft_rbtree_walk_next_r  ( nft_rbtree_h tree,
                              void      ** key,
                              void      ** data,
                              void      ** walk);
/*
 The nft_rbtree_walk_first_r() starts a traversal of the tree, which you can
 continues using _walk_next_r(). _walk_first_r returns true if there is at least
 one key-data pair in the tree. _walk_next_r returns false after the last pair
 has been traversed.

 This iterator is reentrant, so you can use it to conduct a walk within a function
 which itself walks the tree, or in a concurrent thread. You may not insert or delete
 items during a reentrant walk.

 It is generally preferable to iterate using nft_rbtree_apply(), but walking is
 more convenient.
*/

//______________________________________________________________________________
// Test tree's pointers and key ordering integrity.
// Returns TRUE (1) if the tree is valid, else 0.
int nft_rbtree_validate (nft_rbtree_h tree);

// Return the number of items in a tree.
int nft_rbtree_count( nft_rbtree_h tree);

// Enable shared-readers/single-writer locking for this rbtree.
void nft_rbtree_locking(nft_rbtree_h h, unsigned enabled);


/*______________________________________________________________________________
 *
 * These are the private, direct-access APIs. You can use them when subclassing,
 * and in situations where you wish to avoid the overhead of the handle-based API.
 * Note that unlike the handle-based API's nft_rbtree_new(), the rbtree_new()
 * function returns an rbtree whose shared-readers/single-writer lock is disabled.
 * Use rbtree_locking() to enable the readers/writer lock.
 *______________________________________________________________________________
 */
struct nft_rbnode
{
    void          * key;         // search key for node
    void          * data;        // data associated with key
    unsigned int    child[2];    // Index of left and right child nodes
    unsigned int    parent;      // Index of parent node
    unsigned int    red;         // boolean: red if true, else black
};
struct nft_rbtree
{
    nft_core         core;

    nft_rbnode     * nodes;      // Pointer to array of tree nodes
    RBTREE_COMPARE   compare;    // key comparison predicate function
    unsigned int     current;    // Maintain walk state for non-reentrant walk
    unsigned int     min_nodes;  // Initial number of nodes to allocate
    unsigned int     num_nodes;  // Current size of the nodes[] array
    unsigned int     next_free;  // Index of the next free node in nodes[]
    unsigned int     locking;    // rwlock is enabled if true.
    pthread_rwlock_t rwlock;     // Multi-reader/single-writer lock
};

nft_rbtree * rbtree_new         (int min_nodes, RBTREE_COMPARE compare);
nft_rbtree * rbtree_vnew        (int min_nodes, RBTREE_COMPARE compare, void * key, ...);
int          rbtree_free        (nft_rbtree * tree);
unsigned     rbtree_count       (nft_rbtree *);
void         rbtree_locking     (nft_rbtree *, unsigned enabled);
int          rbtree_validate    (nft_rbtree *);
int          rbtree_insert      (nft_rbtree *, void  *key, void  *data);
int          rbtree_replace     (nft_rbtree *, void  *key, void **data);
int          rbtree_delete      (nft_rbtree *, void  *key, void **data);
int          rbtree_search      (nft_rbtree *, void  *key, void **data);
int          rbtree_walk_first  (nft_rbtree *, void **key, void **data);
int          rbtree_walk_next   (nft_rbtree *, void **key, void **data);
int          rbtree_walk_first_r(nft_rbtree *, void **key, void **data, void **walk);
int          rbtree_walk_next_r (nft_rbtree *, void **key, void **data, void **walk);
int          rbtree_apply       (nft_rbtree *, RBTREE_APPLY  apply, void * arg);
int          rbtree_applyx      (nft_rbtree *, RBTREE_APPLYX apply, void * arg);
long         rbtree_compare_pointers(void * h1, void * h2);
long         rbtree_compare_strings (char * s1, char * s2);

/* These calls should only be used by subclasses.
 */
nft_rbtree * rbtree_create      (const char * class, size_t size, int min_nodes, RBTREE_COMPARE compare);
void         rbtree_destroy     (nft_core   *);


#endif // _NFT_RBTREE_H_
