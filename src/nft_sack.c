/***************************************************************************
 * (C) Copyright 2021-2023 Xenadyne, Inc. ALL RIGHTS RESERVED
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
 ***************************************************************************
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nft_sack.h>

/* This macro will round a pointer up to proper alignment.
 */
#define MALLOC_ALIGNMENT 8
#define ALIGNED(p) (char*)((unsigned long)((p) + MALLOC_ALIGNMENT - 1) & ~(MALLOC_ALIGNMENT - 1))

// A function to mark the unused area so that missing null-terminators
// will be evident, and so that buffer overruns can be detected.
void
sack_mark(sack_t sack) {
    // We allocate sizeof(struct sack) + size bytes, but write from sack.data,
    // so the difference between offsetof(struct sack,data) and sizeof(struct sack)
    // comprises a "padding" area.
#ifndef NDEBUG
    int padding = sizeof(struct sack) - offsetof(struct sack,data);
    memset(sack->data + sack->free, 1, sack->size + padding - sack->free);
#endif
}

// A macro to check that the padding area has not been written into.
// We allow data[size] to be written - it's handy for adding a null terminator.
#define SACK_TEST(sack) \
    assert(sack->free <= sack->size); \
    assert(sack->last <= sack->free); \
    assert(sack->data[sack->size + 1] == 1)


/*------------------------------------------------------------------------------
 * sack_create
 *
 * Creates one sack with 'size' capacity of string storage.
 *------------------------------------------------------------------------------
 */
sack_t
sack_create(int size)
{
    if (size >= SACK_MAX_SIZE)
        return NULL;

    sack_t new = (sack_t) malloc(sizeof(struct sack) + size);
    if (new != NULL) {
        *new = (struct sack){ .next = NULL, .free = 0, .last = 0, .size = size };
        sack_mark(new); // Dirty the malloced memory, for testing purposes.
        SACK_TEST(new);
    }
    return new;
}

/*------------------------------------------------------------------------------
 * sack_destroy
 *
 * Frees sack and all of its chained sacks.
 *------------------------------------------------------------------------------
 */
void sack_destroy(sack_t sack)
{
    while (sack) {
        sack_t next = sack->next;
        SACK_TEST(sack);
        free(sack);
        sack = next;
    }
}

/*------------------------------------------------------------------------------
 * sack_empty
 *
 * Reset the sack, and all its chained sacks, to the empty state.
 *------------------------------------------------------------------------------
 */
void
sack_empty(sack_t sack)
{
    while (sack) {
        SACK_TEST(sack);
        sack->free = 0;
        sack->last = 0;
        sack = sack->next;
    }
}

/*------------------------------------------------------------------------------
 * sack_total
 *
 * Returns the total memory allocated in the sack, including any chained sacks.
 *------------------------------------------------------------------------------
 */
long
sack_total(sack_t sack)
{
    long total = 0;
    while (sack) {
        SACK_TEST(sack);
        total += sack->free;
        sack   = sack->next;
    }
    return total;
}

/*------------------------------------------------------------------------------
 * sack_alloc
 *
 * Allocates size bytes of aligned storage from the sack.
 *------------------------------------------------------------------------------
 */
void *
sack_alloc(sack_t sk, int size)
{
    assert(size > 0);
    if (size <= 0) return NULL;

    // Search the chain for a sack with enough free space.
    for ( ; sk ; sk = sk->next ) {
        SACK_TEST(sk);

        char * result = ALIGNED(sk->data + sk->free);
        unsigned last = result - sk->data;
        unsigned free = last + size;
        if (free <= sk->size) {
            sk->last = last;
            sk->free = free;
            return result;
        }

        // If we are at the end of the chain, try to allocate another sack.
        if (!sk->next) {
            // If this item exceeds the default size, create a bigger sack.
            // The capacity is slightly smaller for aligned allocations.
            int adjust = offsetof(struct sack, data) % MALLOC_ALIGNMENT;
            int sksize = (size > sk->size - adjust) ? size + adjust : sk->size;
            sk->next = sack_create(sksize);
        }
    }
    return NULL;
}

/*------------------------------------------------------------------------------
 * sack_stralloc
 *
 * Allocates size+1 bytes of unaligned storage, leaving room for a terminating null.
 *------------------------------------------------------------------------------
 */
char * sack_stralloc(sack_t sk, int size)
{
    assert(size >= 0);
    if (size < 0) return NULL;

    // Increment string size to allow room for terminating null.
    size++;

    // Search the chain for a sack with enough free space.
    for ( ; sk ; sk = sk->next ) {
        SACK_TEST(sk);

        // If this buffer has room, allocate and return.
        if (sk->size >= (sk->free + size)) {
            char * result = sk->data + sk->free;
            sk->last = sk->free;
            sk->free += size;
            assert(sk->free <= sk->size);
            return result;
        }

        // If there are no more buffers in the chain, allocate a new one.
        // Allocate the requested size, if larger than the default.
        if (!sk->next) {
            sk->next = sack_create((size > sk->size) ? size : sk->size);
        }
    }
    return NULL;
}

/*------------------------------------------------------------------------------
 * sack_realloc
 *
 * Re-allocates an existing sack memory region to size newsize.
 * WARNING Only the most-recently-allocated region can be extended
 * (or shrunk) in place, so any other use will allocate new space.
 *------------------------------------------------------------------------------
 */
void *
sack_realloc(sack_t head, void * pointer, int newsize)
{
    char * ptr = pointer;
    if (!ptr) return NULL;

    assert(newsize >= 0);
    if (newsize < 0) return NULL;

    // Search the chain for the sack in which ptr was allocated.
    // Return NULL if not found.
    sack_t sk = head;
    while ((ptr < sk->data) || (ptr > sk->data + sk->free))
        if (!(sk = sk->next))
            return NULL;
    SACK_TEST(sk);

    // Compute the offset of this region in the sack data array.
    // Any pointer following the last region, must be bogus.
    unsigned offset = ptr - sk->data;
    assert(offset <= sk->last);
    if (offset > sk->last) {
        return NULL;
    }

    // It is a mistake to realloc any but the last-allocated region,
    // since we can only shrink or grow the last region in the sack.
    // But as a best-effort solution, we can allocate a new region.
    if (offset < sk->last) {
        // If ptr was aligned, ensure that the new region is too.
        char * new = (ptr == ALIGNED(ptr))
            ? sack_alloc(head, newsize)
            : sack_stralloc(head, newsize-1);

        if (new) {
            // We do not know the size of the region at offset,
            // only that it cannot be larger than (last - offset),
            // so we copy the smaller of newsize or (last - offset).
            int oldsize = (sk->last - offset) < newsize ? (sk->last - offset) : newsize;
            memcpy(new, ptr, oldsize);
        }
        return new;
    }

    // The happy path: Since this is the last region in the sack,
    // if there is room to extend the region, then we can simply
    // adjust sk->free and return.
    if (offset + newsize <= sk->size) {
        sk->free = offset + newsize;
        return ptr;
    }

    // We will need to relocate the memory pointed to by ptr.
    char * new = (ptr == ALIGNED(ptr))
        ? sack_alloc(head, newsize)
        : sack_stralloc(head, newsize-1);

    if (new) {
        // Copy data to the new region, and release the old region.
        memcpy(new, ptr, sk->free - offset);
        sk->free = offset;
    }
    return new;
}

/*------------------------------------------------------------------------------
 * sack_insert
 *
 * Allocates storage and copies the argument string into the new area.
 * This is equivalent to, but more efficient than:
 *
 *   strcpy(sack_stralloc(sack, strlen(string)), string);
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
            sk->last = sk->free;
            sk->free = dst - sk->data;
            SACK_TEST(sk);
            return sk->data + sk->last;
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
 * sack_insert_substring
 *
 * Allocates storage and copies a substring.
 * Returns NULL if any arguments are invalid, or if memory is exhausted.
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
 * sack_strcat
 *
 * Concatenate string2 onto string1, assuming that string1 was allocated
 * from the sack, and that nothing else has been allocated after string1.
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

    // It is an error to concatenate a string that is not the last.
    assert(string1 == sk->data + sk->last);
    if (string1 != sk->data + sk->last)
        return NULL;

    // Attempt to copy string2, beginning with string1's terminating null.
    char * end  = sk->data + sk->size;
    char * dst  = sk->data + sk->free - 1; assert(*dst == '\0');
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
/*******                                                                *******/
/*******                SACK PACKAGE TEST DRIVER                        *******/
/*******                                                                *******/
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
#define MARK mark = nft_gettime()
#define TIME done = nft_gettime()
#define ELAPSED 0.000000001 * nft_timespec_comp(done, mark)

#define MAXSTRINGS  100000
char * strings[MAXSTRINGS];

int main(int argc, char *argv[])
{
    {   // sack_insert and _insert_substring tests
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
        sack_destroy(sk);

        printf("sack_insert, _insert_substring tests passed\n");
    }

    {   // sack_stralloc,realloc tests
        sack_t sk = sack_create(12);
        char * bx = sack_stralloc(sk, 8);
        assert( 0 == sk->last);
        assert( 9 == sk->free);
        assert(12 == sk->size);
        assert(NULL == sk->next);

        // realloc should shrink bx in place
        strcpy(bx, "foo");
        char * nx = sack_realloc(sk, bx, strlen(bx) + 1);
        assert(nx == bx);
        assert(4  == sk->free);

        // An aligned alloc should fit in the first sack.
        char * af = sack_alloc(sk, 8);
        assert( 4 == sk->last);
        assert(12 == sk->free);
        assert(NULL == sk->next);

        // A realloc to zero should not move it.
        char * bf = sack_realloc(sk, af, 0);
        assert(bf == af);
        assert( 4 == sk->last);
        assert( 4 == sk->free);
        assert(NULL == sk->next);

        // A realloc to 12 should force a chained sack.
        char * cf = sack_realloc(sk, bf, 12);
        assert(cf != bf);
        assert( 0 == ((intptr_t)cf % MALLOC_ALIGNMENT));
        assert( 4 == sk->last);
        assert( 4 == sk->free);
        assert(NULL != sk->next);
        assert(cf == sk->next->data + sk->next->last);
        assert(12 == sk->next->free - sk->next->last);

        sack_empty(sk);
        assert( 0 == sk->free);
        assert( 0 == sk->next->free);
        sack_destroy(sk);

        printf("sack_stralloc, realloc tests passed\n");
    }

    {   // sack_strcat tests
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
        assert(st != th);
        assert( 0 == sk->free);
        assert(NULL != sk->next);
        assert(NULL == sk->next->next);
        assert(10 == sk->next->free);
        assert(10 == sk->next->size);
        assert( 0 == strcmp(st, "thetastic"));

        // Stress test sack_strcat()
        char * item = sack_insert(sk, "a");
        for (int i = 2; i < 1024; i++) {
            item = sack_strcat(sk, item, "a");
            assert(item != NULL);
            assert(strspn(item, "a") == i);
        }
        sack_empty(sk);
        assert(0 == sk->free);
        assert(0 == sk->next->free);
        sack_destroy(sk);

        printf("sack_strcat tests passed\n");
    }

    {   // sack_stralloc/realloc with snprintf tests
        sack_t sk = sack_create(64);

        // allocate  string buffer of eight bytes
        int    sz = 8;
        char * bf = sack_stralloc(sk, sz - 1);
        assert(0 == sk->last);
        assert(8 == sk->free);

        // write four bytes to the buffer
        int wr = snprintf(bf, sz, "%s", "abc");
        assert(wr == strlen("abc"));
        assert(0  == strcmp("abc", bf));

        // realloc the buffer to release unused space.
        sz = wr + 1;
        bf = sack_realloc(sk, bf, sz);
        assert(0 == sk->last);
        assert(4 == sk->free);
        assert(0 == strcmp("abc", bf));

        // attempt to write eight bytes to the four-byte buffer
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        wr = snprintf(bf, sz, "%s", "abcdefg");
#pragma GCC diagnostic pop
        assert(wr == strlen("abcdefg"));
        assert(sz == strlen(bf) + 1);
        assert(0  == strcmp("abc", bf));

        // expand the buffer to provide the needed space
        sz = wr + 1;
        bf = sack_realloc(sk, bf, sz);
        wr = snprintf(bf, sz, "%s", "abcdefg");
        assert(wr == strlen("abcdefg"));
        assert(sz == strlen(bf) + 1);
        assert(0  == strcmp("abcdefg", bf));

        // for example
        int    size = 12;
        char * arg  = "ambidextrous";
        assert(strlen(arg) == size);
        char * buff = sack_stralloc(sk, size);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        int    len  = snprintf(buff, size, "%s", arg);
#pragma GCC diagnostic pop
        assert(len == size);
        assert(strlen(buff) == len - 1);
        buff = sack_realloc(sk, buff, len + 1);
        assert(NULL != buff);
        if (len >= size) {
            snprintf(buff, len + 1, "%s", arg);
        }
        assert(0 == strcmp(buff, arg));

        sack_empty(sk);
        sack_destroy(sk);

        printf("sack_stralloc/realloc with snprintf tests passed\n");
    }

    { // Performance tests on strings read from stdin.
        int printon = 0;
        int limit = ((argc == 2) ? ((atoi(argv[1]) < MAXSTRINGS) ? atoi(argv[1]) : MAXSTRINGS) : MAXSTRINGS);

        // Read in the strings from stdin.
        sack_t strs = sack_create(4064);
        for (int i = 0; i < limit; i++) {
            char * line = sack_stralloc(strs, 128);

            if (fgets(line, 128, stdin)) {
                int len = strlen(line);
                // Trim any trailing linefeed.
                if (line[len-1] == '\n') {
                    line[len-1] =  '\0';
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
            assert(item != NULL);
            assert(!strcmp(item, strings[i]));
            if (printon)
                printf("Inserted item %s\n", item);

            char * suff = "_foobar";
            char * word = sack_strcat(sk, item, suff);
            assert(strlen(word) == strlen(strings[i]) + strlen(suff));
            if (printon && (word != item))
                printf("sack_strcat() relocated %s\n", word);
        }
        TIME;
        printf("sack_insert %d strings: %5.2f\n", limit, ELAPSED);
        sack_empty(sk);
        assert(0 == sack_total(sk));
        sack_destroy(sk);
        sack_destroy(strs);
    }

    printf("nft_sack: All tests passed.\n");
    exit(0);
}
#endif
