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
 * File: tmi_hasht.h
 *
 * This is a generic hash table packate. It is useful for a variety
 * of purposes. This header file documents this package's public API,
 * and the unit test code in nft_hash.c (look for #ifdef MAIN)
 * provides examples of usage.
 *
 *********************************************************************
 */
#ifndef _nft_hash_header
#define _nft_hash_header

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

/*-----------------------------------------------------------------------------
 * hasht_insert
 *
 * Insert a (key, val) pair into the table.
 *-----------------------------------------------------------------------------
 */
int   nft_hash_insert (nft_hash *table, void  *key, void  *val);

/*-----------------------------------------------------------------------------
 * hasht_search
 *
 * Search for the key in the hash table, returning true if the key is found.
 * The *keyp parameter is overwritten by the key that was originally inserted.
 * If the valp argument is non-null, the value is stored in *valp.
 *-----------------------------------------------------------------------------
 */
int   nft_hash_search (nft_hash *table, void **keyp, void **valp);

/*-----------------------------------------------------------------------------
 * hasht_delete
 *
 * Searches for the given key and deletes at most one node that matches key.
 * Returns TRUE if the key was found, else FALSE. This means that you would
 * need to call this function repeatedly to delete all occurrences of the key.
 * If the valp argument is non-null, the corresponding value is written to *valp.
 *-----------------------------------------------------------------------------
 */
int   nft_hash_delete (nft_hash *table, void **keyp, void **valp);

/*-----------------------------------------------------------------------------
 * hasht_destroy
 *
 * Free the hash table. Any resources associated with key/value data in
 * the table will be lost. hasht_apply provides a means to iterate over
 * the contents of the table, prior to destroying it.
 *-----------------------------------------------------------------------------
 */
void  nft_hash_destroy(nft_hash *table);

/*-----------------------------------------------------------------------------
 * int
 * hasht_count(hasht_t *table)
 *
 * Return number of entries in the table.
 *-----------------------------------------------------------------------------
 */
int   nft_hash_count  (nft_hash *table);

/*-----------------------------------------------------------------------------
 * nft_hash_apply
 *
 * Apply a function to each key/val pair in the hash table.
 * Do not attempt to modify the table within the apply.
 *-----------------------------------------------------------------------------
 */
int   nft_hash_apply  (nft_hash * table,
		       void    (* apply)( void * key, void * val, void * arg),
                       void     * arg );

/*-----------------------------------------------------------------------------
 * hasht_analyze
 *
 * Analyzes the data distribution of the hash table, i.e.
 * the number of slots with a given number of collisions.
 * Prints summary information to stdout.
 *-----------------------------------------------------------------------------
 */
void  nft_hash_analyze(nft_hash *table);

/*-----------------------------------------------------------------------------
 * hasht_strhash
 *
 * String to number hash function from Aho & Ullman, for hashing
 * null-terminated C strings. Use this and strcmp() in hasht_create().
 *-----------------------------------------------------------------------------
 */
unsigned long nft_hash_strhash(const void * key);

#endif // _nft_hash_header
