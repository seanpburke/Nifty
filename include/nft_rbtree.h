/****************************************************************************
 * (C) Copyright Xenadyne, Inc. 2003-2014  All rights reserved.
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
 * For example, assume that you have records that represent a person,
 * and that you wish to be able to look these records up by the name,
 * which is a string. In this case, you can use strcmp() as your 
 * comparison function, with person.name as the key and &person as data.
 *
 * In addition to insert and search functions, this package provides
 * a tree walker which traverses the tree in key order, so that you can
 * do a linear search for non-key attributes.  There are also functions
 * for changing and deleting keys. The insert function will insert multiple 
 * nodes with duplicate keys - use the _replace() opertion if that is not
 * desired. The only way to find all duplicates of a key walk the tree.
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
 * Multithreading: This package provides synchronization to allow multiple
 * threads to share access to a single rbtree.
 *
 * Algorithm: This package implements the red-black balanced binary tree
 * algorithm as described in Sedgewick, chapter 15, to maintain semi-
 * balanced trees: the balance criterion is that the deepest leaf node
 * is not more than twice as deep as the shallowest leaf node.
 *
 * SYNOPSIS
 *
 *------------------------------------------------------------------------------
 * nft_rbtree * nft_rbtree_create (
 *	int   n,		-- initial number of nodes to allocate
 * 	int (*comparator)() )	-- key ordering function
 * 
 * Creates a new tree. Memory is allocated using malloc. The tree will
 * be realloced if you exceed the initial allocation. The comparator is
 * a function that compares two pointer-sized arguments and returns an
 * integer that is less than, greater than, or equal to zero indicating
 * the relative order of the arguments. For example, the C-library function
 * strcmp() is a suitable comparator for arguments that are char *'s.
 *
 * For duplex keys, the comparator takes four parameters: key1, key2,
 * data1, and data2.
 *
 *
 *------------------------------------------------------------------------------
 * void nft_rbtree_destroy( nft_rbtree *tree )
 * 
 * Deallocates the tree, and all associated node storage.
 * 
 *
 *------------------------------------------------------------------------------
 * int  nft_rbtree_insert ( nft_rbtree *tree, 
 *		 	    void	 *key, 
 *		 	    void	 *data )
 * 
 * Inserts the key-data pair into the tree, using the comparator function
 * that was supplied to rbtree-create. The key argument must be suitable for
 * passing to the comparator. This function will insert duplicate keys - it
 * does not overwrite the existing key-data pair if you insert a new pair
 * with the identical key. Use nft_rbtree_replace() to change the data for an
 * existing key. Returns 1 (true) on success, or zero when memory is exhausted.
 * 
 *------------------------------------------------------------------------------
 * int nft_rbtree_replace(nft_rbtree	*tree, 
 * 		          void		*key, 
 * 		          void		*data )
 * 
 * Change the data of an existing key-data pair. Returns true if key is found,
 * else false. If the key is not found, it is not inserted. This function is not
 * to be used with duplex keys - instead you must do delete followed by insert.
 *
 *
 *------------------------------------------------------------------------------
 * int nft_rbtree_search(nft_rbtree	*tree, 
 * 		         void		*key, 
 * 		         void          **data)
 * 
 * Searches the tree for key, returning 1 (true) if key is found, or zero
 * if it is not found. If key is found, and *data is non-null, the associated
 * data value is returned via *data. If duplex keys are in use, *data must
 * specify a data value. If this key-data pair is found, nft_rbtree_search()
 * replaces *data with the orignally inserted data and returns true.
 * 
 * 
 *------------------------------------------------------------------------------
 * int nft_rbtree_delete(nft_rbtree	*tree,
 * 		         void		*key, 
 *		         void          **data)
 * 
 * Deletes one node with the given key. Returns true if a node was found and
 * deleted, and also returns the corresponding value if data is non-null.
 * If duplex keys are in use, *data must specify a data value, and will delete
 * only the specified key-data pair. If this key-data pair is found, *data is
 * replaced with the deleted data, and nft_rbtree_delete() returns true.
 *
 *
 *------------------------------------------------------------------------------
 * int	nft_rbtree_apply( nft_rbtree * rb, RBTREE_APPLY apply, void * arg);
 *
 * Call the function 'apply' on every item in the tree.
 * For each item the function is passed the key, data, and the void * 'arg'. 
 * Returns the number of items in the tree. The applied function must not
 * insert or delete items from the tree.
 *
 *
 *------------------------------------------------------------------------------
 * int nft_rbtree_walk_first (nft_rbtree *tree, 
 * 			      void	  **key,
 *  			      void	  **data )
 * 
 * int nft_rbtree_walk_first_r(nft_rbtree *tree, 
 * 			       void	   **key,
 *  			       void	   **data,
 *  			       void	   **walk )
 * 
 * Initiate a walk of tree in order of ascending keys. Returns true if there
 * is at least one node in the tree, else false. Key-data pairs are returned
 * via *key and *data if they are non-null.
 *
 * The first form is non-reentrant - you may not use this walk concurrently
 * in separate threads, nor recursively within a single thread. You are able
 * to insert and delete items during the walk, however.
 * 
 * The second version is reentrant, so you may use it to conduct a walk within
 * a function which itself walks the tree, or in a concurrent thread. You may
 * not insert or delete items during a reentrant walk.
 *
 * In general, it is preferable to use the nft_rbtree_apply() call where possible.
 *
 *-----------------------------------------------------------------------------
 * int nft_rbtree_walk_next(nft_rbtree  *tree,
 * 		            void	 **key, 
 *      		    void	 **data )
 *
 * int nft_rbtree_walk_next_r(nft_rbtree *tree,
 * 		              void	  **key, 
 *      		      void	  **data,
 *			      void	  **walk)
 * 
 * Each call returns the next key and data. You must call nft_rbtree_walk_first()
 * to start a walk using _walk_next(), and use walk_first_r() to initiate a walk
 * using walk_next_r(). Returns TRUE while key-data pairs are being returned,
 * and returns FALSE after the walk has been completed.
 *
 * The first form is non-reentrant - you may not use this walk concurrently
 * in separate threads, nor recursively within a single thread. You are able
 * to delete items during the walk, however. 
 * 
 * The second version is reentrant, so you may use it to conduct a walk within
 * a function which itself walks the tree, or in a concurrent thread. You may
 * not insert or delete items during a reentrant walk.
 *
 *------------------------------------------------------------------------------
 * int nft_rbtree_validate(nft_rbtree *tree)
 *
 * Test tree's pointers and key ordering integrity.
 * Returns TRUE (1) if the tree is valid, else 0.
 *
 *
 *------------------------------------------------------------------------------
 * int nft_rbtree_count( nft_rbtree * rb)
 *
 * Return the number of items in a tree.
 *
 *
 *------------------------------------------------------------------------------ 
 */

#ifndef _NFT_RBTREE_H_
#define _NFT_RBTREE_H_

#include "nft_core.h"

typedef struct nft_rbtree nft_rbtree;
typedef struct nft_rbnode nft_rbnode;

/*
 * Ease of use typedefs so the user can easily cast function pointers.
 */
typedef int     (* RBTREE_COMPARE)( );
typedef void    (* RBTREE_APPLY)  ( void * key, void * obj, void * arg);

// Define the Nifty class string, showing nft_rbtree derives from nft_core.
#define nft_rbtree_class nft_core_class ":nft_rbtree"

// Declare helper functions nft_rbtree_cast, _handle, _lookup, _discard
NFT_DECLARE_WRAPPERS(nft_rbtree,)


nft_rbtree_h	nft_rbtree_new         (int init_nodes, RBTREE_COMPARE comparator);
int             nft_rbtree_free        (nft_rbtree_h);
int		nft_rbtree_count       (nft_rbtree_h);
int		nft_rbtree_validate    (nft_rbtree_h);
int		nft_rbtree_insert      (nft_rbtree_h, void  *key, void  *data);
int		nft_rbtree_replace     (nft_rbtree_h, void  *key, void  *data);
int		nft_rbtree_delete      (nft_rbtree_h, void  *key, void **data);
int		nft_rbtree_search      (nft_rbtree_h, void  *key, void **data);
int		nft_rbtree_walk_first  (nft_rbtree_h, void **key, void **data);
int		nft_rbtree_walk_next   (nft_rbtree_h, void **key, void **data);
int		nft_rbtree_walk_first_r(nft_rbtree_h, void **key, void **data, void **walk);
int		nft_rbtree_walk_next_r (nft_rbtree_h, void **key, void **data, void **walk);
int		nft_rbtree_apply       (nft_rbtree_h, RBTREE_APPLY apply, void * arg);




/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * These are the private, direct-access APIs. You can use them when subclassing,
 * and in situations where you wish to avoid the overhead of the handle-based API.
 * Note that these APIs do not themselves use the rwlock. It is up to you 
 * calls, it is up to you to  the lock if necessary.
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
struct nft_rbnode
{
    nft_rbnode    * child[2];  // pointers to left and right child nodes
    long            parent;    // pointer to parent node, plus color bit
    void          * key;       // search key for node
    void          * data;      // data associated with key
};
struct nft_rbtree
{
    nft_core         core;

    int           (* compare)(); // key comparison predicate function
    nft_rbnode     * nodes;      // Pointer to array of tree nodes
    nft_rbnode     * current;    // Maintain walk state for non-reentrant walk
    nft_rbnode       nil;        // Sentinel node. The root node is nil.child[0]
    int              min_nodes;  // Initial number of nodes to allocate
    int              num_nodes;  // Current size of the nodes[] array
    int              next_free;  // Index of the next free node in nodes[]
    pthread_rwlock_t rwlock;     // Multi-reader/single-writer lock
};

nft_rbtree * rbtree_create      (const char * class, size_t size,
				 int init_nodes, RBTREE_COMPARE comparator);
void         rbtree_destroy     (nft_core   *);
int          rbtree_count       (nft_rbtree *);
int          rbtree_validate    (nft_rbtree *);
int          rbtree_insert      (nft_rbtree *, void  *key, void  *data);
int          rbtree_replace     (nft_rbtree *, void  *key, void  *data);
int          rbtree_delete      (nft_rbtree *, void  *key, void **data);
int          rbtree_search      (nft_rbtree *, void  *key, void **data);
int          rbtree_walk_first  (nft_rbtree *, void **key, void **data);
int          rbtree_walk_next   (nft_rbtree *, void **key, void **data);
int          rbtree_walk_first_r(nft_rbtree *, void **key, void **data, void **walk);
int          rbtree_walk_next_r (nft_rbtree *, void **key, void **data, void **walk);
int          rbtree_apply       (nft_rbtree *, RBTREE_APPLY apply, void * arg);


#endif // _NFT_RBTREE_H_
