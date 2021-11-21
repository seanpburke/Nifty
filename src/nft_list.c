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
static sack_t Sack = NULL;

// Nodes that are not in use, are stored in this free-node list.
static list_t Free = NULL;

// The free-node list and sack are protected by the FreeListMutex,
// and initialized by freelist_init(), controlled by FreeListOnce.
static pthread_mutex_t FreeListMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  FreeListOnce  = PTHREAD_ONCE_INIT;
static void freelist_init(void);

// Threads may opt to keep a free node list in thread-specific data,
// by calling thread_specific_freelist_create().
static pthread_key_t FreeListKey;
static int           FreeListKeyStatus = -1;

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
        if (!pthread_getspecific(FreeListKey)) {
            list_t * base = malloc(sizeof(list_t *));
            if (base) {
                *base = NULL;
                return pthread_setspecific(FreeListKey, base);
            }
            return ENOMEM;
        }
        return 0; // It is OK to call this function twice.
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
    Sack = sack_create(4064); // 254 nodes
}

// Allocate a new list node.
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

/*-----------------------------------------------------------------------------
 * list_enable_thread_freelist
 *
 * Installs a thread-local free-node list in the calling thread.
 * This relieves contention for the FreeListMutex in threaded applications
 * that use this package heavily, at the cost of fragmenting the free node pool.
 *-----------------------------------------------------------------------------
 */
int list_enable_thread_freelist() {
    return thread_specific_freelist_create();
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
    while (*l) l = &(*l)->rest;
    struct list_node * node = node_alloc();
    node->first = p;
    node->rest  = NULL;
    *l          = node;
}

/*-----------------------------------------------------------------------------
 * list_cat	Concatenate list 1 and list 2. List 2 is nulled.
 *-----------------------------------------------------------------------------
 */
void list_cat(list_t *l, list_t * l2)
{
    while (*l) l = &(*l)->rest;
    *l  = *l2;
    *l2 = NULL;
}

/*-----------------------------------------------------------------------------
 * list_copy	 Returns a copy of the given list.
 *-----------------------------------------------------------------------------
 */
list_t list_copy(list_t l)
{
    list_t  copy = NULL;
    list_t *ptr  = &copy;

    for ( ; l ; l = l->rest ) {
        list_t cons = node_alloc();
        cons->first = l->first;
        *ptr        = cons;
        ptr         = &cons->rest;
    }
    *ptr = NULL;

    return copy;
}

/*-----------------------------------------------------------------------------
 * list_count	Counts the number of items in the list.
 *-----------------------------------------------------------------------------
 */
int list_count(list_t l)
{
    int count = 0;
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
    list_t  l = NULL;
    va_list ap;
    for (va_start( ap, first);  first != NULL;  first = va_arg(ap, void*))
        list_append(&l, first);
    va_end(ap);

    return l;
}

/*-----------------------------------------------------------------------------
 * list_delete	Removes every occurence of the given item from the list.
 *-----------------------------------------------------------------------------
 */
void * list_delete(list_t *l, void *p)
{
    void * r = NULL;
    while (*l)
        if ((*l)->first == p)
            r = list_pop(l);
        else
            l = &(*l)->rest;
    return r;
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
    *l          = node;
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
        result  = node->first;
        *l      = node->rest;
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

/*-----------------------------------------------------------------------------
 * list_reverse	Reverses the order of the nodes in a clist.
 *-----------------------------------------------------------------------------
 */
void list_reverse(list_t *l)
{
    list_t src = *l;
    list_t rev = 0;

    while (src) {
        list_t rest = src->rest;
        src->rest   = rev;
        rev         = src;
        src         = rest;
    }
    *l = rev;
}

/*-----------------------------------------------------------------------------
 * list_search	Returns 1 if the given item is in the list, else 0.
 *-----------------------------------------------------------------------------
 */
int list_search(list_t l, void *p)
{
    for ( ; l ; l = l->rest)
        if (l->first == p) return 1;
    return 0;
}

/*-----------------------------------------------------------------------------
 * list_to_array
 *
 * Convert a list to a null-terminated array of list items, freeing the list
 * in the process. The nump argument, if non-null, is used to return the number
 * of elements in the array, not counting the NULL terminator.
 *
 * Returns NULL on malloc failure, and the input list is unmodified.
 *
 *-----------------------------------------------------------------------------
 */
void **
list_to_array(list_t *lp, int * nump)
{
    int     count = list_count(*lp);
    void ** array = malloc((count + 1) * sizeof(void *));

    if (array != NULL) {
        void ** a = array;
        while (*lp) *a++ = list_pop(lp);
        *a = NULL;
        if (nump != NULL)
            *nump = count;
    }
    return array;
}


/******************************************************************************/
/******************************************************************************/
/*******                                                                *******/
/*******                LIST  PACKAGE TEST DRIVER                       *******/
/*******                                                                *******/
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
#define MARK    mark = nft_gettime()
#define TIME    done = nft_gettime()
#define ELAPSED 0.000000001 * nft_timespec_comp(done, mark)

// Report the total number of list nodes allocated.
static int
nodes_allocated() {
    return sack_total(Sack) / sizeof(struct list_node);
}

// Functions to be used with list_{apply,map,reduce}
static void   checker(void * arg) { assert(*(char*)arg == 'a'); }
static void * mapper (void * arg) { checker(arg); return arg; }
static void * least  ( void * a, void * b) { return (strcmp(a, b) < 0 ? a : b ); }

#define WORDS_MAX 100000
static char * Words[WORDS_MAX];
static int    Count = 0;

// Read strings from stdin and store them in Words[].
int read_words(sack_t strs) {
    int i;
    for (i = 0; i < WORDS_MAX; i++) {
        char * line = sack_stralloc(strs, 127);

        if (fgets(line, 128, stdin)) {
            int len = strlen(line);
            // Trim any trailing linefeed.
            if (line[len-1] == '\n') {
                line[len-1]  = '\0';
                len -= 1;
            }
            Words[i] = sack_realloc(strs, line, len + 1);
        }
        else {
            sack_realloc(strs, line, 0);
            break;
        }
    }
    return i;
}

#define NUM_THREADS 4
static pthread_t Threads[NUM_THREADS];

// Thread worker to test the list-node allocater under contention.
void * thread_worker(void * arg) {
    intptr_t nt = (intptr_t) arg;
    intptr_t rc = 0;

    // Install a thread-specific free-node list in this thread.
    // If you comment this out, you will see a BIG difference.
    rc = list_enable_thread_freelist();

    // Threads running this loop will contend heavily for the free-node list.
    // It is not realistic, but it does hilite the benefit of per-thread free lists.
    for (int round = 0; round < 1000; round++) {
        // Each thread lists its own portion of Words.
        list_t l = NULL;
        for (int i = nt; i < Count; i += NUM_THREADS)
            list_push(&l, Words[i]);
        while (l)
            list_pop(&l);
    }
    return (void*) rc;
}

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
        assert(1 == list_search(l, list[0]));
        assert(1 == list_search(l, list[1]));
        assert(1 == list_search(l, list[2]));

        list_t m = list_copy(l);
        list_reverse(&m);
        assert(m->first == list[2]);
        assert(m->rest->first == list[1]);
        assert(m->rest->rest->first == list[0]);
        assert(m->rest->rest->rest  == NULL);
        assert(3 == list_count(m));
        assert(1 == list_search(l, list[0]));
        assert(list_delete(&m, list[1]) == list[1]);
        assert(0 == list_search(m, list[1]));
        assert(m->first == list[2]);
        assert(m->rest->first == list[0]);
        assert(m->rest->rest  == NULL);
        list_delete(&m, list[0]);
        assert(0 == list_search(m, list[0]));
        assert(m->first == list[2]);
        assert(m->rest  == NULL);
        list_delete(&m, list[2]);
        assert(0 == list_search(m, list[2]));
        assert(m == NULL);
        list_delete(&m, list[0]);
        assert(m == NULL);

        list_destroy(&l);
        assert(l == NULL);
        list_destroy(&l);
        assert(l == NULL);
        assert(6 == list_count(Free));
        assert(6 == nodes_allocated());
        assert(6 * sizeof(struct list_node) == Sack->free);
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

    // Read in the strings from stdin and store them in Words[].
    sack_t strs = sack_create(4064);
    MARK;
    Count = read_words(strs);
    TIME;
    printf("nft_list read    %d words: %5.2f sec\n", Count, ELAPSED);

    {   // Single-thread performance tests.

        // Push alls words onto a list.
        list_t wlist = NULL;
        int    i = 0;
        MARK;
        for (i = 0; i < Count; i++)
            list_push(&wlist, Words[i]);
        TIME;
        printf("nft_list insert  %d words: %5.2f sec\n", Count, ELAPSED);
        assert(Count == list_count(wlist));

        // Pop all words from the list.
        MARK;
        while (wlist)
            assert(list_pop(&wlist) == Words[--i]);
        TIME;
        printf("nft_list pop     %d words: %5.2f sec\n", Count, ELAPSED);
        list_destroy(&wlist);

        // Confirm that every node allocated has been returned to the free list.
        assert(Count == nodes_allocated());
        assert(Count == list_count(Free));
    }

    {   // Multi-thread performance tests.
        MARK;

        // Launch the worker threads.
        for (intptr_t i = 0; i < NUM_THREADS; i++)
            assert(0 == pthread_create(&Threads[i], NULL, thread_worker, (void*) i));

        // Join the worker threads.
        void * thread_return;
        for (int i = 0; i < NUM_THREADS; i++) {
            assert(0 == pthread_join(Threads[i], &thread_return));
            assert(NULL == thread_return);
        }
        TIME;
        printf("nft_list threads %d words: %5.2f sec\n", Count, ELAPSED);

        // Confirm that thread_specific_freelist_destroy() has returned
        // all the per-thread freelist nodes to the global free list.
        assert(Count == nodes_allocated());
        assert(Count == list_count(Free));
    }

    // Free the sack from which list-node are allocated.
    assert(Count == nodes_allocated());
    sack_destroy(Sack);

    // Free the sack holding the Words[] strings.
    sack_destroy(strs);

    printf("nft_list: All tests passed.\n");
    exit(0);
}

#endif // MAIN
