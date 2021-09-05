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
 * File:  nft_sack.c
 *
 * PURPPOSE
 *
 * Package for efficient allocation of strings and mixed object types.
 *
 *******************************************************************
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nft_sack.h>

/* This macro will round a pointer up to proper alignment.
 */
#define MALLOC_ALIGNMENT 8
#define ALIGNED(p) (char*)((unsigned long)((p) + MALLOC_ALIGNMENT - 1) & ~(MALLOC_ALIGNMENT - 1))
    
/*------------------------------------------------------------------------------
 *	sack_create
 *
 *	Creates one sack with 'size' capacity of string storage.
 *------------------------------------------------------------------------------
 */
sack_t
sack_create(int size)
{
    if (size >= SACK_MAX_SIZE)
	return NULL;

    sack_t new = (sack_t) malloc(sizeof(struct sack) + size);
    if (new != NULL) {
#ifndef NDEBUG
	/* Dirty the malloced memory, for testing purposes.
	 * In test programs, malloced memory is often zeroed,
	 * which can mask problems with uninitialized fields.
	 */
	memset(new, 1, sizeof(struct sack) + size);
#endif
	new->next = NULL;
	new->free = 0;
	new->size = size;
    }
    return new;
}

/*------------------------------------------------------------------------------
 *	sack_destroy
 *
 * 	Frees sack and all of its chained sacks.
 *------------------------------------------------------------------------------
 */
void sack_destroy(sack_t * sackp)
{
    sack_t sack = *sackp;
    while (sack) {
        sack_t next = sack->next;
        assert(sack->free <= sack->size);
        assert(sack->data[sack->size] == 1);
        free(sack);
        sack = next;
    }
    *sackp = NULL;
}

/*------------------------------------------------------------------------------
 *	sack_empty
 *
 *	Reset the sack, and all its chained sacks, to the empty state.
 *------------------------------------------------------------------------------
 */
void
sack_empty(sack_t sack)
{
    while (sack) {
        assert(sack->free <= sack->size);
        assert(sack->data[sack->size] == 1);
        sack->free = 0;
        sack = sack->next;
    }
}

/*------------------------------------------------------------------------------
 *	sack_total
 *
 *	Returns the total memory allocated in the sack, including any chained sacks.
 *------------------------------------------------------------------------------
 */
long
sack_total(sack_t sack)
{
    long total = 0;
    while (sack) {
        assert(sack->free <= sack->size);
        assert(sack->data[sack->size] == 1);
        total += sack->free;
        sack   = sack->next;
    }
    return total;
}

/*------------------------------------------------------------------------------
 *	sack_alloc
 *
 * 	Allocates size bytes of aligned storage from the sack.
 *------------------------------------------------------------------------------
 */
void *
sack_alloc(sack_t sk, int size)
{
    // Search the chain for a sack with enough free space.
    for ( ; sk ; sk = sk->next ) {
	assert(sk->free <= sk->size);
	assert(sk->data[sk->size] == 1);

        char * result = ALIGNED(sk->data + sk->free);
        int    nfree  = (result + size) - sk->data;
        if (sk->size  >= nfree) {
            sk->free   = nfree;
            return result;
        }

	// If we are at the end of the chain, try to allocate another sack.
        // Allocate the requested size, if it is larger than the default.
	if (!sk->next) {
            sk->next = sack_create((size > sk->size) ? size : sk->size);
	}
    }
    return NULL;
}

/*------------------------------------------------------------------------------
 *	sack_stralloc
 *
 * 	Allocates size+1 bytes of unaligned storage, leaving room for a terminating null.
 *------------------------------------------------------------------------------
 */
char * sack_stralloc(sack_t sk, int size)
{
    // Increment string size to allow room for terminating null.
    size++;

    // Search the chain for a sack with enough free space.
    for ( ; sk ; sk = sk->next ) {
	assert(sk->free <= sk->size);
	assert(sk->data[sk->size] == 1);

        // If this buffer has room, allocate and return.
        if (sk->size >= (sk->free + size)) {
            char * result = sk->data + sk->free;
            sk->free += size;
            assert(sk->free <= sk->size);
            return result;
        }

	// If there are no more buffers in the chain, allocate a new one.
	if (!sk->next) {
	    // Allocate the requested size, if larger than the default.
            sk->next = sack_create((size > sk->size) ? size : sk->size);
	}
    }
    return NULL;
}

/*------------------------------------------------------------------------------
 *	sack_realloc
 *
 * 	Re-allocates an existing sack pointer to size newsize.
 * 	WARNING - The space is extended (or shrunk) in place,
 *	so only the most recently allocated object can be realloced safely.
 *------------------------------------------------------------------------------
 */
void *
sack_realloc(sack_t head, void * pointer, int newsize)
{
    char * ptr = pointer;
    if (!ptr) return NULL;

    // Search the chain for the sack in which ptr is allocated.
    // Return NULL if not found.
    sack_t sk = head;
    while ((ptr < sk->data) || (ptr > sk->data + sk->free))
	if (!(sk = sk->next))
	    return NULL;

    // Does this sack have room to extend this region?
    // If so, simply adjust sk->free and return.
    if (ptr + newsize <= sk->data + sk->size) {
        sk->free = ptr + newsize - sk->data;
        return ptr;
    }

    // We will need to relocate the memory pointed to by ptr.
    // If ptr was aligned, ensure that new region is too.
    char * new = (ptr == ALIGNED(ptr))
        ? sack_alloc(head, newsize)
        : sack_stralloc(head, newsize-1);

    if (new) {
        // Compute the size of this region, assuming that nothing has been allocated
        // subsequent to this, so that sk->free indicates the end of ptr's allocation.
        int oldsize = (sk->data + sk->free) - ptr;
        memcpy(new, ptr, oldsize);

        // Remove old string from 
        sk->free = ptr - sk->data;
    }
    return new;
}

/*------------------------------------------------------------------------------
 *	sack_insert
 *
 * 	Allocates storage and copies the argument string into the new area.
 *	This is equivalent to, but more efficient than:
 *
 *		strcpy(sack_stralloc(sack, strlen(string)), string);
 *------------------------------------------------------------------------------
 */
char *
sack_insert(sack_t sack, const char *string)
{
    if (!string) return NULL;

    // Go to the end of the chain.
    sack_t sk = sack;
    while (sk->next) sk = sk->next;

    // Attempt to copy the string into the free space.
    const char * src = string;
    char * dst = sk->data + sk->free;
    char * end = sk->data + sk->size;
    while (dst < end) {
        if (!(*dst++ = *src++)) {
            // We were able to copy the entire string.
            char * result = sk->data + sk->free;
            sk->free = dst - sk->data;
            assert(sk->free <= sk->size);
            return result;
        }
    }
    // There is not enough room. Allocate with sack_stralloc().
    while (*src) src++;
    char * result = sack_stralloc(sack, src - string);
    if (result)
        strcpy(result, string);
    return result;
}

/*------------------------------------------------------------------------------
 *	sack_insert_substring
 *
 * 	Allocates storage and copies a substring.
 *      Returns NULL if any arguments are invalid, or if memory is exhausted.
 *------------------------------------------------------------------------------
 */
char *
sack_insert_substring(sack_t head, const char *string, int start, int length)
{
    char * result = NULL;

    if (head && string && (start >= 0) && (length >= 0)) {
	if ((result = sack_stralloc(head, length))) {
            memcpy(result, string + start, length);
            result[length] = '\0';
        }
    }
    return result;
}

/*------------------------------------------------------------------------------
 * 	sack_strcat
 *
 *	Concatenate string2 onto string1, assuming that string1 was allocated
 *	from the sack, and that nothing else has been allocated after string1.
 *------------------------------------------------------------------------------
 */
char *
sack_strcat(sack_t head, char * string1, const char * string2)
{
    if (!head)    return NULL;
    if (!string1) return NULL;
    if (!string2) return string1;

    // Search the chain for the sack in which string1 is allocated.
    // Return NULL if not found.
    sack_t sk = head;
    while ((string1 < sk->data) || (string1 > sk->data + sk->free))
	if (!(sk = sk->next))
	    return NULL;

    // Attempt to copy string2, beginning with string1's terminating null.
    char * end  = sk->data + sk->size;
    char * dst  = sk->data + sk->free - 1; // assert(*dst == '\0');
    int    len1 = dst - string1;
    const char * src = string2;
    while (dst < end) {
        if (!(*dst++ = *src++)) {
            // We were able to append string2 in place.
            sk->free = dst - sk->data;
            assert(sk->free <= sk->size);
            return string1;
        }
    }
    // There was not enough room. Reallocate string1 with sufficient space.
    while (*src) src++;
    char  * result = sack_realloc(head, string1, len1 + (src - string2) + 1);
    if (result)
        strcpy(result + len1, string2);
    return result;
}


/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		SACK PACKAGE TEST DRIVER			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Enable asserts for test code.
#endif
#include <assert.h>
#include <stdio.h>
#include <nft_gettime.h>

// Timing stuff.
struct timespec mark, done;
#define MARK	mark = nft_gettime()
#define TIME	done = nft_gettime()
#define ELAPSED 0.000000001 * nft_timespec_comp(done, mark)

#define MAXSTRINGS  100000
char * strings[MAXSTRINGS];

int main(int argc, char *argv[])
{
    {	// sack_insert and _insert_substring tests
        sack_t sk = sack_create(8);
        assert( 0 == sack_total(sk));
        char * wd = sack_insert(sk, "tarantula");
        assert( 0 == sk->free);
        assert(10 == sk->next->free);
        assert(10 == sk->next->size);
        assert(10 == sack_total(sk));
        char * sb = sack_insert_substring(sk, wd, 2, 4);
        assert(sb == sk->data);
        assert( 5 == sk->free);
        assert( 0 == strcmp(sb, "rant"));
        sack_destroy(&sk);

        printf("sack_insert, _insert_substring tests passed\n");
    }

    {	// sack_stralloc,realloc tests
        sack_t sk = sack_create(16);
        char * bx = sack_stralloc(sk, 12);
        assert(13   == sk->free);
        assert(16   == sk->size);
        assert(NULL == sk->next);
        strcpy(bx, "giraffe");
        char * nx = sack_realloc(sk, bx, strlen(bx) + 1);
        assert(nx == bx);
        assert(8  == sk->free);

        // An aligned alloc should fit in the first sack.
        char * af = sack_alloc(sk, 8);
        assert(16   == sk->free);
        assert(NULL == sk->next);

        // A realloc to zero should not move it.
        char * bf = sack_realloc(sk, af, 0);
        assert(bf == af);
        assert(8  == sk->free);
        assert(NULL == sk->next);

        // A realloc to 12 should force a chained sack.
        char * cf = sack_realloc(sk, bf, 12);
        assert(cf != bf);
        assert(8  == sk->free);
        assert(NULL != sk->next);
        assert(12 == sk->next->free);
        assert(16 == sk->next->size);
        
        sack_empty(sk);
        assert( 0   == sk->free);
        assert( 0   == sk->next->free);
        sack_destroy(&sk);

        printf("sack_stralloc, realloc tests passed\n");
    }

    {	// sack_strcat tests
        sack_t sk = sack_create(8);
        char * wd = sack_insert(sk, "the");
        assert(4 == sk->free);
        assert(0 == strcmp(wd, "the"));

        // Concatenation should happen in place.
        char * th = sack_strcat(sk, wd, "ta");
        assert(th == wd);
        assert( 6 == sk->free);
        assert( 0 == strcmp(wd, "theta"));

        // Concatenating an empty string should change nothing.
        th = sack_strcat(sk, th, "");
        assert(th == wd);
        assert( 6 == sk->free);
        assert( 0 == strcmp(wd, "theta"));

        // Concatenation that requires a chained sack.
        char * st = sack_strcat(sk, th, "stic");
        assert(st   != th);
        assert( 0   == sk->free);
        assert(NULL != sk->next);
        assert(NULL == sk->next->next);
        assert(10   == sk->next->free);
        assert(10   == sk->next->size);
        assert(0 == strcmp(st, "thetastic"));

    	// Stress test sack_strcat()
        char * item = sack_insert(sk, "a");
        for (int i = 2; i < 1024; i++) {
            item = sack_strcat(sk, item, "a");
            assert(item != NULL);
            assert(strspn(item, "a") == i);
        }
        sack_empty(sk);
        assert( 0   == sk->free);
        assert( 0   == sk->next->free);
        sack_destroy(&sk);

        printf("sack_strcat tests done\n");
    }

    {	// Performance tests on strings read from stdin.
        int   printon = 0;
        int   limit = ((argc == 2) ? ((atoi(argv[1]) < MAXSTRINGS) ? atoi(argv[1]) : MAXSTRINGS) : MAXSTRINGS);
  
        // Read in the strings from stdin.
        sack_t strs = sack_create(4064);
        for (int i = 0; i < limit; i++) {
            char * line = sack_stralloc(strs, 128);
            
            if (fgets(line, 128, stdin)) {
                int len = strlen(line);
                // Trim any trailing linefeed.
                if (line[len-1] == '\n') {
                    line[len-1]  = '\0';
                    len -= 1;
                }
                strings[i] = sack_realloc(strs, line, len + 1);
            }
            else {
                sack_realloc(strs, line, 0);
                limit = i;
                break;
            }
        }
        
        sack_t sk = sack_create(4064);
        if (!sk) {
            printf("Error allocating sack.\n");
            exit(1);
        }
        MARK;
        for (int i = 0; i < limit; i++)
        {
            char *item = sack_insert(sk, strings[i]);
            if (item) {
                if (printon) printf("Inserted item %s\n", item);
            }
            else {
                printf("Error inserting item %s\n", item);
                exit(1);
            }
            assert(strlen(item) == strlen(strings[i]));

            char * suff = "_foobar";
            char * word = sack_strcat(sk, item, suff);
            assert(strlen(word) == strlen(strings[i]) + strlen(suff));
            if (printon && (word != item))
                printf("sack_strcat() relocated %s\n", word);
        }
        TIME;
        printf("Time to insert %d strings: %5.2f\n", limit, ELAPSED);
        sack_empty(sk);
        assert(0 == sack_total(sk));
        sack_destroy(&sk);
        sack_destroy(&strs);
    }
    exit(0);
}
#endif
