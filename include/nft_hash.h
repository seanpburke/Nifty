/********************************************************************
 * (C) Copyright Xenadyne, Inc. 2002-2013  All rights reserved.
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
 * File:	tmi_hasht.h
 *
 * PURPOSE	Generic hash table package.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_count(hasht_t *table)
 *
 * Return number of entries in the table.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_insert(hasht_t *table,	 -- hash tbl.
 *		void	*key,	 -- key value
 *		void	*val)	 -- token value
 *
 * Creates a node for the (key, val) pair and hashes it into the table.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_search(hasht_t *table,
 *		void   **key,
 *		void   **val)
 *
 * Hashes the key to an entry in the list, then checks the nodes linked
 * there for a match.  Returns TRUE if the key is found, else FALSE.
 * If the val argument is non-null, the corresponding value is returned also.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_search_filter(hasht_t *table,
 *		       void    *key,
 *		       void   **val,
 *		       int    (*pred)(void *key, void *val, void *pred_arg),
 *		       void    *pred_arg)
 *
 * Search all occurrences of key, applying the predicate function
 * to each associated value. Returns the first value for  which the
 * predicate evaluates to true via **val. Returns TRUE if any matching
 * value is found, else FALSE. The predicate function is called with
 * the arguments (key, val, pred_arg), where pred_arg is supplied via
 * the arguments to hasht_search_filter.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_delete(hasht_t	 *table,
 *		void	 *key,
 *		void	**val)
 *
 * Searches for the given key and deletes at most one node that matches key.
 * Returns TRUE if the key was found, else FALSE. This means that you would
 * need to call this function repeatedly to delete all occurrences of the key.
 * If the val argument is non-null, the corresponding value is returned also.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_first(hasht_t  *table,  -- hash tbl.
 *		    void    **key,    -- double pointer to the key
 *		    void    **val)    -- double pointer to the value
 *
 * This initializes a walk of the hash table, return the first <key, val>
 * pair via the pointers if they are non-null. Returns TRUE, unless the
 * table is empty.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_next(hasht_t  *table,
 *		   void	   **key,
 *		   void	   **val)
 *
 * Each iteration of hasht_walk_next() returns new <key, val> pair.
 * Returns TRUE while new pairs are returned, then FALSE.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_first_node(hasht_t  *table,	-- hash tbl.
 *			 int	  *walk_index,	-- walk state
 *			 hnode_t **walk_node)	-- walk state
 *
 * This initializes a walk of the hash table, Returns TRUE, unless the
 * table is empty.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_next_node(hasht_t	 *table,
 *			int	 *walk_index,
 *			hnode_t **walk_node)
 *
 * Returns TRUE while new pairs are found, then FALSE.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_get_node(hnode_t	 *walk_node,	-- walk state
 *		       void	**key,		-- double pointer to the key
 *		       void	**val)		-- double pointer to the value
 *
 * Return TRUE and the <key, val> pair of current node if it is non-null,
 * otherwise FALSE.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_key_first(hasht_t *table,
 *			void	*key,	-- focus walk to this key
 *			void   **val)	-- double pointer to the value
 *
 * This initializes a walk of the hash table, returning the first <key, val>`
 * where key is equal to the given key. Returns TRUE, unless no such pair
 * is found.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_walk_key_next (hasht_t *table,
 *			void	*key,	-- focus walk to this key
 *			void   **val)	-- double pointer to the value
 *
 * Each iteration of hasht_walk_key_next() returns next <key, val> pair,
 * with the given key. Returns TRUE while new pairs are returned, then FALSE.
 *
 *
 *-----------------------------------------------------------------------------
 * int
 * hasht_apply( hasht_t * table,
 * 		void   (* apply)( void * key, void * val, void * arg),
 *		void    * arg)
 *
 * Apply a function to each key/val pair in the hash table.
 *
 *-----------------------------------------------------------------------------
 * unsigned int
 * hasht_strhash(const void * key)
 *
 * String to number hash function from Aho & Ullman, for hashing
 * null-terminated C strings. Use this and strcmp() in hasht_create().
 *
 *
 *-----------------------------------------------------------------------------
 * void
 * hasht_analyze(hasht_t *table)
 *
 * Analyzes the data distribution of the hash table, i.e.
 * the number of slots with a given number of collisions.
 *
 *
 *-----------------------------------------------------------------------------
 */

#ifndef nft_hash_header
#define nft_hash_header

typedef struct nft_hash nft_hash;

/*-----------------------------------------------------------------------------
 * nft_hash_create
 *
 * Allocate and intialize a hash table of the given size and return it
 * to the caller. The caller provides two functions. The first is a hash
 * function that takes the key pointer and hashes it to an unsigned integer
 * value. The second is a comparison function that returns zero if two keys
 * are identical. For string keys, nft_hash_strhash and strcmp will work.
 *-----------------------------------------------------------------------------
 */
nft_hash *
nft_hash_create(int size,
		unsigned long (*hash_fun)(const void*),
		int           (*compare) (const void*, const void*));

void  nft_hash_destroy(nft_hash *table);
void  nft_hash_analyze(nft_hash *table);
int   nft_hash_count  (nft_hash *table);
int   nft_hash_insert (nft_hash *table, void  *key, void  *val);
int   nft_hash_delete (nft_hash *table, void **key, void **val);
int   nft_hash_search (nft_hash *table, void **key, void **val);

int   nft_hash_apply  (nft_hash * table,
		       void    (* apply)( void * key, void * val, void * arg),
                       void     * arg );

unsigned long nft_hash_strhash(const void * key);

#endif // nft_hash_header
