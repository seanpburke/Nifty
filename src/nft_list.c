/******************************************************************************
 * (C) Copyright Xenadyne Inc, 2021 ALL RIGHTS RESERVED.
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
 * File: nft_list.c
 *
 * Description: A package for managing singly-linked lists.
 * The complete description is in the header file nft_list.h.
 *
 ******************************************************************************
 */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>

#include <nft_sack.h>
#include <nft_list.h>

// Expressions that turn into assertions in debug mode.
#ifndef NDEBUG
#include <assert.h>
#define MUST(exp)     assert(exp)
#define MUST_NOT(exp) assert(!(exp))
#else
#define MUST(exp)     exp
#define MUST_NOT(exp) exp
#endif

// List nodes are allocated from this nft_sack.
static sack_t	Sack = NULL;

// Nodes that are not in use, are stored in this free-node list.
static list_t	Free = NULL;

// The free-node list and sack are protected by the FreeListMutex,
// and initialized by freelist_init(), controlled by FreeListOnce.
static pthread_mutex_t FreeListMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  FreeListOnce  = PTHREAD_ONCE_INIT;
static void freelist_init(void);

/* Threads may opt to keep a free node list in thread-specific data,
 * by calling thread_specific_freelist_create().
 */
static pthread_key_t	FreeListKey;
static int		FreeListKeyStatus = -1;

static list_t * thread_specific_freelist_get() {
    MUST_NOT(pthread_once(&FreeListOnce, freelist_init));
    if (FreeListKeyStatus == 0) {
        return pthread_getspecific(FreeListKey);
    }
    return NULL;
}
static int thread_specific_freelist_create() {
    MUST_NOT(pthread_once(&FreeListOnce, freelist_init));
    if (FreeListKeyStatus == 0) {
        list_t * base = malloc(sizeof(list_t *));
        if (base) {
            *base = NULL;
            return pthread_setspecific(FreeListKey, base);
        }
        return ENOMEM;
    }
    return FreeListKeyStatus;
}
static void thread_specific_freelist_destroy(void * arg) {
    list_t * base = arg;
    if (base) {
        list_destroy(base);
        free(base);
    }
}

// This runs exactly once, via FreeListOnce.
static void freelist_init(void) {
    FreeListKeyStatus = pthread_key_create(&FreeListKey, thread_specific_freelist_destroy);
    Sack = sack_create(4000);
}

/* Allocate a new list node.
 */
static struct list_node *
node_alloc()
{
    struct list_node * node = NULL;

    // Attempt to alloc from the thread's freelist.
    list_t * freep = thread_specific_freelist_get();
    if (freep) {
        node = *freep;
        if (node) {
            *freep = node->rest;
            return node;
        }
    }

    // Otherwise try the global freelist.
    MUST_NOT(pthread_mutex_lock(&FreeListMutex));
    if (Free) {
        node = Free;
        Free = node->rest;
    } else {
        node = sack_alloc(Sack, sizeof(struct list_node));
    }
    MUST_NOT(pthread_mutex_unlock(&FreeListMutex));
    return node;
}

// Return a list of nodes to the free list.
static void
nodes_free(struct list_node * head, struct list_node * tail)
{
    list_t * freep = thread_specific_freelist_get();
    if (freep) {
        tail->rest = *freep;
        *freep = head;
    } else {
        MUST_NOT(pthread_mutex_lock(&FreeListMutex));
        tail->rest = Free;
        Free       = head;
        MUST_NOT(pthread_mutex_unlock(&FreeListMutex));
    }
}

static int
nodes_allocated() {
    MUST_NOT(pthread_mutex_lock(&FreeListMutex));
    long total = sack_total(Sack) / sizeof(struct list_node);
    MUST_NOT(pthread_mutex_unlock(&FreeListMutex));
    return total;
}


/*-----------------------------------------------------------------------------
 * list_apply	Apply function to every item on the list.
 *-----------------------------------------------------------------------------
 */
void list_apply(list_t l, void (*function)(void *))
{
    for ( ; l ; l = l->rest )
	function(l->first);
}

/*-----------------------------------------------------------------------------
 * list_append	Append an item to the end of the list.
 *-----------------------------------------------------------------------------
 */
void list_append(list_t *l, void *p)
{
    while (*l)
	l = &(*l)->rest;
    struct list_node * node = node_alloc();
    node->first	= p;
    node->rest	= NULL;
    *l		= node;
}

/*-----------------------------------------------------------------------------
 * list_count	Counts the number of items in the list.
 *-----------------------------------------------------------------------------
 */
unsigned list_count(list_t l)
{
    unsigned count = 0;
    for ( ; l ; l = l->rest) count++;
    return count;
}

/*-----------------------------------------------------------------------------
 * list_create	Create a list holding the given elements.
 *
 * The argument list must be terminated by a NULL, which will not appear
 * on the list. list_create(NULL) returns NULL, the empty list.
 *
 *-----------------------------------------------------------------------------
 */
list_t list_create(void * first, ...)
{
    va_list	ap;
    list_t	l = NULL;

    for (va_start( ap, first);  first != NULL;  first = va_arg(ap, void*))
	list_append(&l, first);
    va_end(ap);

    return l;
}

/*-----------------------------------------------------------------------------
 * list_destroy	Frees all of the nodes in the list, setting *list to null.
 *-----------------------------------------------------------------------------
 */
void list_destroy(list_t *l)
{
    list_t head = *l;
    if (head) {
        list_t tail = head;
        while (tail->rest) tail = tail->rest;
        nodes_free(head, tail);
        *l = NULL;
    }
}

/*-----------------------------------------------------------------------------
 * list_map	Apply function to every item on the list, returning a list of results.
 *-----------------------------------------------------------------------------
 */
list_t list_map(list_t l, void * (*function)(void *))
{
    list_t r = NULL;
    for ( ; l ; l = l->rest ) list_append(&r, function(l->first));
    return r;
}

/*-----------------------------------------------------------------------------
 * list_nth
 *
 * Returns the nth item in the list to caller, where n is zero-based.
 * If n is greater than or equal to list count, returns NULL.
 *
 *-----------------------------------------------------------------------------
 */
void *list_nth(list_t l, int n)
{
    if (n < 0) return 0;
    while (n-- && l) l = l->rest;
    return (l ? l->first : NULL);
}

/*-----------------------------------------------------------------------------
 * list_peek	Returns the first item in the list to caller.
 *-----------------------------------------------------------------------------
 */
void *list_peek(list_t l)
{
    return (l ? l->first : 0);
}

/*-----------------------------------------------------------------------------
 * list_push	Push an item onto the head of the list.
 *-----------------------------------------------------------------------------
 */
void list_push(list_t *l, void *p)
{
    list_t node = node_alloc();
    node->first = p;
    node->rest  = *l;
    *l		= node;
}

/*-----------------------------------------------------------------------------
 * list_pop	Removes and returns the first item in the list.
 *
 * Returns null if the list is empty. Note that this is not the way to
 * detect end of list, since it is OK to put a null item on a list.
 * A proper loop checks for a null list_t, as in:
 *
 *   while (list) list_pop(&list);
 *-----------------------------------------------------------------------------
 */
void * list_pop(list_t *l)
{
    void * result = NULL;
    list_t node   = *l;
    if (node) {
	result	= node->first;
	*l	= node->rest;
	nodes_free(node, node);
    }
    return result;
}

/*-----------------------------------------------------------------------------
 * list_reduce
 *
 * Similar to _apply, but function takes two arguments. On the first call,
 * the first two items on the list are passed, and on subsequent calls,
 * we pass the result of the prior call as the first parameter, and the
 * next list item as the second parameter. Operations like min, max and sum
 * are easily implemented using this mechanism.
 *
 *-----------------------------------------------------------------------------
 */
void * list_reduce(list_t l, void * (*function)(void *, void *))
{
    void * result = NULL;
    if (l) {
	result = l->first;
	while ((l = l->rest))
	    result = function(result, l->first);
    }
    return result;
}


/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		LIST  PACKAGE TEST DRIVER			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Enable asserts for test code.
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <nft_gettime.h>

// Timing stuff.
struct timespec mark, done;
#define MARK	mark = nft_gettime()
#define TIME	done = nft_gettime()
#define ELAPSED 0.000000001 * nft_timespec_comp(done, mark)

#define MAX_WORDS 100000
char * words[MAX_WORDS];

static void   checker(void * arg) { assert(*(char*)arg == 'a'); }
static void * mapper (void * arg) { checker(arg); return arg; }
static void * least  ( void * a, void * b) { return (strcmp(a, b) < 0 ? a : b ); }

int main(int argc, char *argv[])
{
    {   // Basic tests of push, pop, append
        list_t l = NULL;
        list_push(&l, "a");
        assert(Sack->data == (void*)l);
        assert(Sack->free == sizeof(*l));
        assert(Sack->next == NULL);
        assert(1 == nodes_allocated());

        list_append(&l, "b");
        assert(Sack->free == 2 * sizeof(*l));
        assert(Sack->next == NULL);
        assert(0 == strcmp(list_peek(l), "a"));
        assert(0 == strcmp(list_nth(l, 0), "a"));
        assert(0 == strcmp(list_nth(l, 1), "b"));
        assert(0 == strcmp(list_peek(l->rest), "b"));
        assert(2 == nodes_allocated());

        assert(0 == strcmp(list_pop(&l), "a"));
        assert(0 == strcmp(list_pop(&l), "b"));
        assert(l == NULL);

        list_append(&l, "b");
        assert(0 == strcmp(list_nth(l, 0), "b"));
        assert(0 == strcmp(list_pop(&l), "b"));
        assert(l == NULL);
        assert(2 == nodes_allocated());
    }

    {   // Test list_create, _destroy
        char * list[] = { "one", "two", "three", NULL };
        list_t l = list_create(list[0], list[1], list[2], NULL);
        assert(l->first == list[0]);
        assert(l->rest->first == list[1]);
        assert(l->rest->rest->first == list[2]);
        assert(l->rest->rest->rest  == NULL);
        assert(3 == list_count(l));
        list_destroy(&l);
        assert(l == NULL);
        list_destroy(&l);
        assert(3 == list_count(Free));
        assert(3 == nodes_allocated());
        assert(3 * sizeof(struct list_node) == Sack->free);
    }

    {   // Test apply, map, reduce
        list_t l = list_create("a", "a", "a", "a", "a", NULL);
        list_apply(l, checker);
        list_t m = list_map(l, mapper);
        list_apply(m, checker);
        list_destroy(&m);
        list_destroy(&l);
        assert(NULL == l);
        assert(NULL == list_reduce(l, least));
        list_push(&l, "foo");
        assert(!strcmp("foo", list_reduce(l, least)));
        list_push(&l, "bar");
        assert(!strcmp("bar", list_reduce(l, least)));
        list_push(&l, "zap");
        assert(!strcmp("bar", list_reduce(l, least)));
        list_destroy(&l);
    }

    {   // Performance tests on strings read from stdin.
        int   printon = 0;
        int   limit = ((argc == 2) ? atoi(argv[1]) : MAX_WORDS);

        // Set a thread-specific free-node list for this test.
        assert(0 == thread_specific_freelist_create());

        // Read in the strings from stdin.
        MARK;
        sack_t strs = sack_create(4064);
        for (int i = 0; i < limit; i++) {
            char * line = sack_stralloc(strs, 127);

            if (fgets(line, 128, stdin)) {
                int len = strlen(line);
                // Trim any trailing linefeed.
                if (line[len-1] == '\n') {
                    line[len-1]  = '\0';
                    len -= 1;
                }
                words[i] = sack_realloc(strs, line, len + 1);
            }
            else {
                sack_realloc(strs, line, 0);
                limit = i;
                break;
            }
        }
        TIME;
        printf("nft_list read   %d words: %5.2f sec\n", limit, ELAPSED);

        if (limit < 5) {
            printf("Provide at least 5 keys!\n");
            exit(0);
        }
        printon = (limit <= 20);

        list_t b = NULL;
        MARK;
        for (int i = 0; i < limit; i++)
            list_push(&b, words[i]);
        TIME;
        printf("nft_list insert %d strgs: %5.2f sec\n", limit, ELAPSED);

        MARK;
        for (list_t t = b; t; t = t->rest) {
            char * x = t->first;
            if (printon) printf("walk:  %s\n", x);
        }
        TIME;
        printf("nft_list walk   %d words: %5.2f sec\n", limit, ELAPSED);

        assert(limit == list_count(b));
        list_destroy(&b);

        assert(limit == nodes_allocated());
        sack_destroy(&Sack);
    }
}

#endif // MAIN
