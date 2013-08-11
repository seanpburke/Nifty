/********************************************************************
 * (c) Copyright 2001 - 2013 Xenadyne, Inc.	ALL RIGHTS RESERVED
 *
 * The software and information contained herein are proprietary to, 
 * and comprise valuable trade secrets of, ObjectStream, Inc., which
 * intends to preserve as trade secrets such software and information.
 * This software is furnished pursuant to a written license agreement and
 * may be used, copied, transmitted, and stored only in accordance with
 * the terms of such license and with the inclusion of the above copyright
 * notice.  This software and information or any other copies thereof may
 * not be provided or otherwise made available to any other person.
 *
 ********************************************************************
 *
 * NAME: nft_hash.c
 *
 * This is a generic hash table packate. It is useful for a variety
 * of purposes. The header file nft_hash.h documents this package's
 * public API, and the unit test code below (look for #ifdef MAIN)
 * provides examples of usage.
 *
 * Unlike most other Nifty packages, this does not have any pthread
 * features - you will notice that the hash table has no mutex.
 * I've found that it's rarely useful to have internal locks in a
 * collection class, because I nearly always need to synchronize
 * adding an object to a collection with changes to the object,
 * or to other data. So the internal mutex virtually always turns
 * out to be superfluous.
 * 
 * IMPLEMENTATION NOTES
 *
 * The hash table data structure consists of a two parts: the base table
 * object, and an array of nodes. This is a little confusing, because
 * although there is only a single array of hash list nodes, it would
 * be better to think of this as two arrays: one array of list roots -
 * the .list fields, and a second array of list nodes consisting of
 * <key, val, next> triplets.
 *
 * The list roots contain a list of all the triplets which hashed to that index.
 * Thus, the nodes[i].list field has nothing to do with nodes[i].<key,val,next>.
 * Lists are terminated by a null next pointer.
 *
 * The free nodes are arranged in a doubly-linked list that is rooted in the
 * 'free_nodes' field of the hash table header. In order to optimize for
 * caches, we try to allocate list nodes near to the root of their hash list.
 * To enable this, we structure the free list as a doubly-linked list, using
 * the 'key' and 'val' slots to hold the link pointers. Nodes that are on the
 * free list are marked by setting their 'next' slot to -1.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nft_hash.h>

/* This structure describes a node in the hash table.
 */
typedef struct nft_hnode
{
    void	 *key;	// key
    void	 *val;	// token
    struct nft_hnode *next;	// next link
    struct nft_hnode *list;	// hash table slots
} nft_hnode;

/* This structure describes the hash table. 
 */
struct nft_hash
{
    unsigned int    size;			        // size of hash table
    unsigned int    count;			        // number of entries 
    nft_hnode	  * free;			        // free node pointer
    nft_hnode	  * nodes;			        // malloc'ed node storage
    unsigned long (*hash_fun)(const void*);		// hashing function
    int		  (*compare)(const void*, const void*);	// comparison function
};

/* These macros are used to manipulate free nodes.
 */
#define IS_FREE(node)	((node)->next == (nft_hnode *) 1)
#define MARK_FREE(node)	((node)->next =  (nft_hnode *) 1)
#define NEXT(node)	(node)->key
#define PREV(node)	(node)->val


/* init_free_list - Put all of table's nodes on the free list,
 * which is actually a ring.
 */
static void
init_free_list(nft_hash *table)
{
    nft_hnode * prev = &table->nodes[table->size - 1];
    nft_hnode * node = table->nodes;
    nft_hnode * next = &table->nodes[1];

    // Arrange the free node list, and null the list fields.
    while (node < (table->nodes + table->size))
    {
	NEXT(node) = next;
	PREV(node) = prev;
	MARK_FREE(node);
	node->list = NULL;

	prev = node;
	node = next;
	next++;
    }
    // Close the circular free node list.
    NEXT(prev)  = table->nodes;
    table->free	= table->nodes;
    return;
}


/* alloc_hnode
 *
 * Allocate a node from the free list, preferring the node at the hash index,
 * and insert node into the hash list. When the list pointer table->nodes[hash].list
 * points to table->nodes[hash], this is an "optimal node". The first item on the
 * hash list is in the same slot as the hash list pointer, so a search that terminates
 * there avoids any cache misses to follow the hash list pointers.
 *
 * Benchmarks show a significant win when the compiler inlines this function.
 */
static nft_hnode *
alloc_hnode(nft_hash * table, unsigned hash)
{
    // All keys that hash to this hash, will be arranged
    // in a list that is rooted at table->nodes[hash].list.
    nft_hnode * root = &table->nodes[hash];
    nft_hnode * next = &table->nodes[(hash + 1) % table->size];
    nft_hnode * new;

    /* If it is free, allocate the list root itself.
     * If not, our second-best choice is the node
     * that is next to the root in the table.
     * Otherwise use the head of the freelist.
     */
    new = IS_FREE(root) ? root :
          IS_FREE(next) ? next : table->free ;
	    
    /* If the root->list already points to root,
     * insert the new node in the second place.
     */
    if (root->list == root)
    {	new->next = root->next; root->next = new; }
    else
    {	new->next = root->list; root->list = new; }

    // Remove new from the free list.
    NEXT((nft_hnode *)PREV(new)) = NEXT(new);
    PREV((nft_hnode *)NEXT(new)) = PREV(new);

    // Update the free list pointer if new was the head of the list.
    if (table->free == new) {
	table->free = NEXT(new);

	// If new was the last node on the list, null the free list.
	if (table->free == new)
	    table->free = NULL;
    }
    return new;
}


/* free_hnode - return a node to the free list.
 */
static void
free_hnode(nft_hash *table, nft_hnode *node)
{
    if (table->free) {
	NEXT(node) = table->free;
	PREV(node) = PREV(table->free);

	PREV((nft_hnode *) NEXT(node)) = node;
	NEXT((nft_hnode *) PREV(node)) = node;
    }
    else {
	NEXT(node) = node;
	PREV(node) = node;
    }
    MARK_FREE(node);
    table->free = node;
}


/* realloc_nodes
 *
 * Increase the capacity of the table.
 * This requires reinserting every node in the table.
 */
static int
realloc_nodes(nft_hash *table)
{
    nft_hnode  * newnodes;
    nft_hnode  * oldnodes;
    unsigned int size    = table->size;
    unsigned int newsize = size + ((size+1) >> 1); // increase by 50%
    unsigned int i;

    // First, attempt to allocate new node storage.
    if (!(newnodes = malloc(newsize * sizeof(nft_hnode))))
	return 0;

    // OK. Save the old nodes, replace them with the new node storage.
    oldnodes = table->nodes;
    table->nodes = newnodes;
    table->size	 = newsize;

    // Rebuild the free list.
    init_free_list(table);

    // Now, for each item in the old nodes, re-insert it into the table.
    for (i = 0; i < size; i++)
    {
	for (nft_hnode * node = oldnodes[i].list; node; node = node->next)
	{
	    /* This is equivalent to nft_hash_insert(table, node->key, node->val),
	     * but we don't want to make a recursive call because the caller
	     * should already hold the mutex.
	     */
	    unsigned long hash = table->hash_fun(node->key);
	    nft_hnode   * new  = alloc_hnode(table, hash % table->size );
	    new->key = node->key;
	    new->val = node->val;
	}
    }
    // Lastly, free the old node list.
    free(oldnodes);
    return 1;
}


/*****************************************************************************
 ******									******
 ******			Hash Table Public Functions			******
 ******									******
 *****************************************************************************
 */

/*------------------------------------------------------------------------------
 *
 *  nft_hash_create
 *
 *------------------------------------------------------------------------------
 */
nft_hash *
nft_hash_create(int             size,
	     unsigned long (*hash_fun)(const void*),
	     int	   (*compare) (const void*, const void*))
{
    nft_hash  * table = malloc(sizeof(nft_hash));
    nft_hnode * nodes = malloc(size * sizeof(nft_hnode));
    if (!table || !nodes) return NULL;

    // Record data in table header
    table->size	 = (size > 0) ? size : 1;
    table->count = 0;
    table->nodes = nodes;
    table->hash_fun = hash_fun;
    table->compare  = compare;

    // Arrange free node list.
    init_free_list(table);
    return table;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_destroy
 *
 *------------------------------------------------------------------------------
 */
void
nft_hash_destroy(nft_hash *table)
{
    if (!table) return;
    free(table->nodes);
    free(table);
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_count() - return number of entries in hash table.
 *
 *------------------------------------------------------------------------------
 */
int
nft_hash_count(nft_hash *table)
{
    return table ? table->count : 0 ;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_insert - Insert key/value pair. Returns TRUE if successful.
 *
 *------------------------------------------------------------------------------
 */
int
nft_hash_insert(nft_hash * table,  void * key,  void * val)
{
    // Ensure that free nodes are available.
    // Return failure if the reallocation fails.
    if (!table->free)
	if (!realloc_nodes(table))
	    return 0;

    // Modulo the hash value with the table size to get list root.
    // This must occur after realloc_nodes(), since that resizes table.
    unsigned  long hash = table->hash_fun(key) % table->size;

    // Allocate a new hash node and insert into hash list.
    nft_hnode    * new = alloc_hnode(table, hash);
	
    // Store key and value.
    new->key = key;
    new->val = val;
    table->count++;
    return 1;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_strhash
 *
 * String to number hash function from Aho & Ullman, for hashing null-
 * terminated C strings. Use this and strcmp() in nft_hash_create().
 *
 *------------------------------------------------------------------------------
 *
 */
unsigned long
nft_hash_strhash(const void * arg)
{
    const char * s = arg; 
    unsigned int h = 0, j, k;
    unsigned int c;

    while ((c = *s++)) {
	j = (h << 3) + c;
	k = (h >> 12);
	h = (j ^ k);
    }
    return h ;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_search - Search for key/value. Returns true if found, else false.
 *
 *------------------------------------------------------------------------------
 */
int
nft_hash_search(nft_hash * table,  void **pkey,  void **pval)
{
    void	* key  = *pkey;
    unsigned long hash = table->hash_fun(key) % table->size;

    // Search for the first occurence of key in the hash list.
    for (nft_hnode * node = table->nodes[hash].list;
	 node != NULL;
	 node = node->next)
    {
	if (table->compare(node->key, key) == 0) {
	    *pkey = node->key;
	    if (pval) *pval = node->val;
	    return 1;
	}
    }
    return 0;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_delete - delete key. Returns true if key was present.
 *
 *------------------------------------------------------------------------------
 */
int
nft_hash_delete(nft_hash * table,  void **pkey,  void **pval)
{
    void	* key  = *pkey;
    unsigned long hash = table->hash_fun(key) % table->size;
    nft_hnode  ** pnode;
    nft_hnode	* node;
    
    /* For each node in the list, look for the key,
     * while keeping track of the upstream pointer
     * to the node in pnode;
     */
    for (pnode = &table->nodes[hash].list, node = *pnode;
	 node != NULL;
	 pnode = &node->next,  		   node = *pnode)
    {
	if (table->compare(node->key, key) == 0)
	{
	    *pkey = node->key;
	    if (pval) *pval = node->val;

	    // Splice node out of the list and free it.
	    *pnode = node->next;
	    free_hnode(table, node);
	    table->count--;
	    return 1;
	}
    }
    return 0;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_apply
 *
 * Apply a function to each of the hash table entries.
 * Return the number of entries in the table.
 *
 * Function takes args of (key, token, extra_arg). 
 *
 * WARNING - Your function must _NOT_ modify or delete the key,
 * otherwise the table will not function correctly.
 *
 *------------------------------------------------------------------------------
 */
int
nft_hash_apply(nft_hash * table,
	       void    (* apply)( void * key, void * obj, void * arg),
	       void     * arg)
{
    int count = 0;
    for (unsigned i = 0; i < table->size; i++)
	for (nft_hnode * node = table->nodes[i].list; node; node = node->next) {
	    (apply)(node->key, node->val, arg);
	    count++;
	}
    return count;
}


/*------------------------------------------------------------------------------
 *
 *  nft_hash_analyze
 *
 * Analyzes the data distribution of the hash table, i.e.
 * the number of slots with a given number of collisions.
 *
 *------------------------------------------------------------------------------
 */
void
nft_hash_analyze(nft_hash	*table)
{
#define NBINS 20

    int		 list_bins[NBINS];
    int		 count_bins[NBINS];
    int		 bin_index;
    nft_hnode  * node = 0;
    unsigned int total = 0;
    unsigned int cache_optimal = 0;
    unsigned int num_lists = 0;
    unsigned int count;
    unsigned int index;

    /* zero "bins" */
    for (index = 0; index < NBINS; index++)
    {
	list_bins[index] = 0;
	count_bins[index] = 0;
    }

    for (index = 0; index < table->size; index++)
    {
	count = 0;
	node  = &table->nodes[index];

	if (node->list != NULL)
	    num_lists++;
	
	// Count lists whose first item is the same node.
	if (node == node->list)
	    cache_optimal++;

	// Count the number of items in this list.
	for (node = node->list; node ; node = node->next) {
	    count++;
	    total++;
	}
	if (count >= NBINS)
	    bin_index = NBINS - 1;
	else
	    bin_index = count;

	list_bins[  bin_index ]++;
	count_bins[ bin_index ] += count;
    }

    assert(total == table->count);

    fprintf(stderr,"\nnft_hash usage: %d out of %d\n", total, table->size);
    fprintf(stderr,"list length		#lists	#nodes\n");
    fprintf(stderr,"-----------		------	------\n");

    for (index = 0; index < NBINS; index++)
	fprintf(stderr, "%d			%d	%d\n",
	       index, list_bins[index], count_bins[index]);

    fprintf(stderr,"--------------------------------------\n");
    fprintf(stderr,"Cache-optimized lists: %d of %d\n\n", cache_optimal, num_lists);

    // Verify that free list entries are correctly marked.
    node = table->free;
    do {
	assert(IS_FREE(node));
	if (!IS_FREE(node))
	    fprintf(stderr, "Hash Table - corrupted free-list detected!\n");
    }
    while ((node = NEXT(node)) != table->free);
}

/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		HASH PACKAGE UNIT TEST				*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN

#include <sys/types.h>
#ifndef WIN32
#include <sys/times.h>
#include <sys/resource.h>
#else
#include <time.h>
#endif


#define MAXKEYS		1000000
char   *keys[MAXKEYS];
int     nkeys = 0;

int
main(int argc, char *argv[])
{
    char	string[256];
    int		limit;
    void	*key;
    void	*value;
    long	i = 0;

    /* Timing stuff.
     */
#ifndef CLK_TCK
#define CLK_TCK 100
#endif
#if !defined(WIN32)
    struct tms	start, done;
    float		usert, syst, tick = 1.0 / CLK_TCK;
#define MARK	times(&start)
#define TIME	times(&done); \
    usert = (done.tms_utime - start.tms_utime) * tick; \
    syst  = (done.tms_stime - start.tms_stime) * tick

#else /* WIN32 */
    clock_t start, done;

#define MARK start=clock();

    double usert = 0, syst;
#define TIME syst=(double)(clock() - start) /  CLOCKS_PER_SEC;

#endif /* WIN32 */

    // Set limit on keys.
    limit  = ((argc > 1) ? atoi(argv[1]) : MAXKEYS);
    limit  = ((limit > MAXKEYS) ? MAXKEYS : limit);

    // Insert strings into key table
    while (fgets(string, sizeof(string), stdin) && (i < limit))
	keys[i++] = strdup(string);
    nkeys = i;

    // Create a new table with 1023 nodes allocated.
    nft_hash * t = nft_hash_create(1023, nft_hash_strhash, (int (*)(const void *, const void *)) strcmp);
    assert(t != NULL);

    // Insert strings into table
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
	assert(nft_hash_insert(t, keys[i], keys[i]));
    TIME;			/* compute time usage */
    fprintf(stderr,"Time to insert %ld keys: %.2fu %.2fs\n", i, usert, syst);

    /* Search all keys */
    MARK;			/* record start time */
    for (key = keys[i = 0]; i < nkeys; key = keys[++i])
	assert(nft_hash_search(t, &key, &value));
    TIME;			/* compute time usage */
    fprintf(stderr,"Time to search %ld keys: %.2fu %.2fs\n", i, usert, syst);

    /* Print out the hash distribution */
    nft_hash_analyze(t);

    /* Delete all keys */
    MARK;			/* record start time */
    for (key = keys[i = 0]; i < nkeys; key = keys[++i])
	assert(nft_hash_delete(t, &key, &value));
    TIME;			/* compute time usage */
    fprintf(stderr,"Time to delete %ld keys: %.2fu %.2fs\n", i, usert, syst);

    for (i = 0; i < 10*nkeys; i++)
    {
	key = (void *) keys[rand() % nkeys];

	if (nft_hash_search(t, &key, &value)) {
	    nft_hash_delete(t, &key, &value);
	    assert(!nft_hash_search(t, &key, &value));
	}
	else {
	    nft_hash_insert(t, key, key);
	    assert(nft_hash_search(t, &key, &value));
	}
    }
    TIME;			/* compute time usage */
    fprintf(stderr,"Time to do stress test: %.2fu %.2fs\n", usert, syst);

    // Free malloc'd storage to verify no leakage.
    nft_hash_destroy(t);

    for (i = 0; i < nkeys; i++)	free(keys[i]);

    exit(0);
}
#endif // MAIN
