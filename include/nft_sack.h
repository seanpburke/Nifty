/***************************************************************************
 * (C) Copyright 2021 Xenadyne, Inc. ALL RIGHTS RESERVED
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
 ***************************************************************************
 *
 * File:  nft_sack.h
 *
 * DESCRIPTION
 *
 * This package offers a flexible, efficient means for allocating memory
 * in mixed sizes, that you will free all at once (in bulk), when some
 * transaction*has been completed. A sack initially consists of a single
 * malloced region from which smaller increments of memory are allocated,
 * but when this memory has been consumed, additional sacks will be added
 * to the chain, transparently to the calling code.
 *
 * This package is a good solution when you need to store a largenumber
 * of small strings, and wish to minimize the overhead of malloc.
 * The sack_insert() call will copy a string into the buffer without
 * computing itslength first, and so the string will only be traversed once,
 * except in the case where it turns out to be too large to fit the space
 * remaining. Use this function, rather than sack_alloc() with strlen()
 * and strcpy(), if you can.
 *
 * The public calls in the nft_sack package are
 * 
 * sack_t sack_create(size)
 *
 * 	Creates an initial sack with room for size bytes of storage.
 *	Additional chained sacks will default to this size as well.
 *	Returns NULL if memory is not available.
 * 
 * void sack_destroy(sack_t *)
 * 
 * 	Frees all of the memory allocated to the sack, including any chained sacks.
 * 
 * void sack_empty(sack_t)
 * 
 * 	Resets the sack to its empty state, inluding any chained sacks.
 *
 * long	sack_total(sack_t)
 *
 *	Returns the total memory allocated in the sack, including any chained sacks.
 * 
 * void *sack_alloc(sack_t, int size);
 *
 * 	Allocates size bytes of 8-byte aligned storage, returning NULL
 *	if memory is not available.
 *
 * char *sack_stralloc(sack_t, int size)
 *
 *	Allocates size+1 bytes of unaligned storage.
 *	This is suitable for storing null-terminated strings.
 * 
 * char *sack_realloc(sack_t head, char *oldstr, int stringsize);
 *
 * 	Re-allocates an existing sack string to size stringsize+1.
 * 	WARNING - The string may be extended (or shrunk) in place,
 *	so only the most recently allocated entry can be realloced in this way.
 *
 * char *sack_insert(sack_t, const char * string);
 *
 * 	Creates a copy of a null-terminated string, and returns a pointer
 *	to the copy. This call is faster than using strlen(), sbuff_alloc()
 *	and strcpy(), because it does not precompute the string length.
 *	In most cases, the input string will only be traversed once.
 *	Returns NULL if memory is not available.
 *
 * char *sack_insert_substring(sack_t head, const char *string, int start, int length);
 *
 * 	Similar to sack_insert, but you can select a substring.
 *	Returns NULL if either of the start or length arguments are negative,
 *	or if memory is exhausted.
 * 
 * char * sack_strcat(sack_t b, char * string1, const char * string2);
 *
 *	Concatenate string1 and string2. This is subject to the same
 *	constraints as sack_realloc(), which is that string1 must be
 *	the most-recently-allocated string.
 *
 ***************************************************************************
 */
#ifndef TMI_SACK_H
#define TMI_SACK_H

#include <limits.h>

#define SACK_MAX_SIZE  INT_MAX

typedef struct sack {
    struct sack * next;	  /* for chaining extra buffers		*/
    unsigned	  free;	  /* index to first free char in data[]	*/
    unsigned	  size;	  /* true size of the data[] array 	*/
    char   	  data[1];/* allocated for (size+1) bytes	*/
} * sack_t;

sack_t	  sack_create(int sz);
void	  sack_destroy(sack_t*);
void	  sack_empty(sack_t);
long      sack_total(sack_t);
void	* sack_alloc(sack_t, int sz);
void	* sack_realloc(sack_t, void *ptr, int sz);
char	* sack_stralloc(sack_t, int sz);
char	* sack_insert(sack_t, const char *str);
char	* sack_insert_substring(sack_t head, const char *str, int offset, int length);
char	* sack_strcat(sack_t b, char * str1, const char * str2);

#endif
