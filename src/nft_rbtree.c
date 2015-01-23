/***********************************************************************
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
 ************************************************************************
 *
 * File: nft_rbtree.c
 *
 * DESCRIPTION
 *
 * This package implements a balanced binary tree for use as a
 * sorting/searching mechanism, and as an efficient associative map.
 * For a full description of this package, including the detailed
 * API description, please refer to the header file nft_rbtree.h.
 * Usage is illustrated by the unit test below (see #ifdef MAIN).
 *
 * This package illustrates the design approach where you take a 
 * traditional C package and "wrap" it with Nifty handle-based APIs,
 * while retaining the option to use the tradional direct APIs.
 * Plus, it is a really handy package.
 *
 * DUPLEX KEYS
 *
 * This package provides some support for the use of "duplex" keys,
 * where both the key and data are used to sort nodes. To create a
 * duplex tree, you simply provide a comparison function that takes
 * four parameters: int (*compare)(key1, key2, data1, data2). When
 * you search a duplex tree, you must initialize the data parameter.
 * 
 *******************************************************************
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <nft_rbtree.h>

// Define the wrapper functions nft_rbtree_cast, _lookup, _discard, etc.
//
NFT_DEFINE_WRAPPERS(nft_rbtree,)


// The NIL node pointer points to tree->nil.
#define	NIL(t)	(&(t)->nil)

// The root node is stored as the left child of the sentinel.
#define ROOT(t) (t)->nil.child[0]

// macros to reference left and right child nodes.
#define LEFT	child[0]
#define RIGHT	child[1]

// The least significant bit of the parent pointer stores the color
// (red or black) of the edge from the node's parent.
//
#define	PARENT(n)	((nft_rbnode *)   ((long) (n)->parent & ~3))
#define SET_PARENT(n,p) (n->parent = (((long) (n)->parent &  3) | (long) p))

#define RED(n)		(n->parent &   1)
#define SET_RED(n)	(n->parent |=  1, assert(n != NIL(tree)) )
#define RESET_RED(n)	(n->parent &= ~1)



/* Returns the first node in tree, or NULL if empty.
 */
static nft_rbnode *
node_first(nft_rbtree *tree)
{
    nft_rbnode * node = ROOT(tree);

    if (node == NIL(tree))
	return NULL;

    // If tree is not empty, go to the leftmost node in the tree.
    while (node->LEFT != NIL(tree))
	node = node->LEFT;

    return node;
}


/* Find the successor in key order to the given node.
 *
 * To find the next node, we look for:
 *
 * 1>   The leftmost node of the right-hand subtree of
 *	the current node, or if there is no right subtree:
 *
 * 2>   The first ancestor node for which the current 
 *	node is a member of the left-hand subtree. In 
 *	other words, we ascend until we get to a left-hand
 *	child node, and return the parent.
 *
 * Returns successor node, or NULL if none.
 */
static nft_rbnode *
node_successor(nft_rbtree *tree, nft_rbnode * node)
{
    assert(node != NIL(tree));
    assert((node - tree->nodes) >= 0);
    assert((node - tree->nodes) <  tree->num_nodes);
    
    if (node->RIGHT != NIL(tree))
    {
	node = node->RIGHT;
	while (node->LEFT != NIL(tree))	node = node->LEFT;
	return node;
    }
    else
    {
	nft_rbnode * parent;

	while (((parent = PARENT(node)) != NIL(tree)) && (parent->RIGHT == node))
	    node = parent;

	return ((parent != NIL(tree)) ? parent : NULL);
    }
}


/*************** insertion and balancing functions **********************/


/* Attach a new leaf node. Note that the caller must ensure that
 * tree->next_free < tree->num_nodes, by calling rbtree_resize().
 */
static nft_rbnode *
attach_leaf(nft_rbtree *tree, nft_rbnode *parent, void *key, void *data, int which)
{
    assert(tree->next_free < tree->num_nodes);

    nft_rbnode * node = &tree->nodes[tree->next_free++];

    *node = (nft_rbnode) { { NIL(tree), NIL(tree) }, (long)parent, key, data };

    parent->child[which] = node;

    return node;
}


/* Rotate the tree so that grandson gs is promoted and the son s is demoted.
 * If gs is a left child, this will do a right rotate, otherwise a left-rotate.
 */
static void
rotate(nft_rbnode *node, nft_rbnode *s, nft_rbnode *gs)
{
    int left  = (gs == s->LEFT) ? 0 : 1;
    int right = left ^ 1;

    // Promote gs and demote s while maintaining the tree ordering.
    s->child[left] = gs->child[right];
    SET_PARENT(gs->child[right], s);
    gs->child[right]= s;
    SET_PARENT(s, gs);

    // Put gs where s used to be.
    if (s == node->LEFT) node->LEFT  = gs;
    else                 node->RIGHT = gs;
    SET_PARENT(gs, node);
    return;
}

/* This is an insert-fixup procedure, derived from the algorithm 
 * presented in Cormen, Leiserson and Rivest, p268.
 *
 * However, we employ a trick here to exploit the symmetry of
 * the situation to shrink our code. The code below is modeled
 * on the case where the parent of x is a left node. If this
 * is not the case, we simply swap the sense of left and right.
 * The right child is normally child 1. We also use the fact
 * that left == right ^ 1.
 */
static void
insert_fixup(nft_rbtree *tree, nft_rbnode * x)
{
    while ((x != ROOT(tree)) && RED(PARENT(x)))
    {
	nft_rbnode * p  = PARENT(x);
	nft_rbnode * gp = PARENT(p);
	int	 right = (p == gp->LEFT) ? 1 : 0 ;
	nft_rbnode * y  = gp->child[right];
	  
	if (RED(y)) {
	    RESET_RED(y);
	    RESET_RED(p);
	    SET_RED(gp);
	    x = gp;
	}
	else {
	    if (x == p->child[right])
	    {
		rotate(gp, p, x);
		x = x->child[right ^ 1]; /* left */
		p = PARENT(x);
		gp= PARENT(p);
	    }
	    RESET_RED(p);
	    SET_RED(gp);
	    rotate(PARENT(gp), gp, p);
	}
    }
    RESET_RED(ROOT(tree));
    return;
}


/*  Insert a node into a non-empty tree.
 *  attach_leaf() is used on empty trees.
 */
static void
insert_node(nft_rbtree *tree, void *key, void *data)
{
    nft_rbnode * x = ROOT(tree);
    nft_rbnode * node;
    int	   (*compare)() = tree->compare;	
    int      c;

    // This never gets called on an empty tree.
    assert  (ROOT(tree) != NIL(tree));

    // Find the node to which to attach the new leaf.
    do {
	node = x;

	/* Provide four parameters to comparison function to support duplex keys.
	 * A simple comparison function can ignore the last two parameters.
	 */
	c = compare(key, node->key, data, node->data);

	x = (c < 0) ? node->LEFT : node->RIGHT ;

    } while (x != NIL(tree));

    // Attach a new leaf under node, and rebalance the tree.
    nft_rbnode * leaf = attach_leaf(tree, node, key, data, c >= 0 );
    SET_RED(leaf);
    insert_fixup(tree, leaf);
    return;
}

/* This is a delete-fixup procedure, derived from the
 * algorithm presented in Cormen, Leiserson and Rivest, p272.
 *
 * However, we employ a trick here to exploit the symmetry of
 * the situation to shrink our code. The code below is modeled
 * on the case where x is a left child node. If this
 * is not the case, we simply swap the sense of left and right.
 * The right child is normally child 1. We also use the fact
 * that left == right ^ 1.
 */
static void
delete_fixup(nft_rbtree * tree, nft_rbnode * x)
{
    nft_rbnode  * w;

    while ((x != ROOT(tree)) && !RED(x))
    {
	nft_rbnode  * p = PARENT(x);

	// If x is the left child, use normal left/right, otherwise flip...
	int right = (x == p->LEFT) ? 1 : 0;
	w = p->child[right];
	assert(w != NIL(tree));

	if (RED(w)) {
	    RESET_RED(w);
	    SET_RED(p);
	    rotate(PARENT(p), p, w);
	    w = p->child[right];
	    assert(w != NIL(tree));
	}

	// w is now black. If both children are also black...
	if (!RED(w->LEFT) && !RED(w->RIGHT))
	{
	    SET_RED(w);
	    x = p;
	}
	else
	{
	    if (!RED(w->child[right])) // right child black, left child is red
	    {
		int left  = right ^ 1;

		RESET_RED(w->child[left]);
		SET_RED(w);
		rotate(p, w, w->child[left]);
		w = p->child[right];
		assert(w != NIL(tree));
	    }
	    // Now the right child is red, and the left black 
	    if (RED(p))
		SET_RED(w);
	    else
		RESET_RED(w);
	    RESET_RED(p);
	    RESET_RED(w->child[right]);
	    rotate(PARENT(p), p, w);
	    x = ROOT(tree);
	    break;
	}
    }
    RESET_RED(x);
    return;
}

/* Here is the RB-Delete procedure from C,L&R.
 * To delete the node z, we handle two cases:
 *
 * 1. If either of the node's subtrees is empty, we promote the 
 *    other child into node's place, thereby raising the whole 
 *    child's subtree. If the other child is Nil, no problem.
 *
 * 2. If z has two children, we find z's successor node y, copy
 *    y's key and data into z, and then delete y. Node y will
 *    have no left child, so it can be deleted as in case 1.
 *    (You can show that if a node has two children, then its
 *    successor can have no left child, and its predecessor can
 *    have no right child.)
 * 
 * If the node that is deleted in case 1 is black, then we call
 * the procedure delete_fixup() to restore tree balance.
 * 
 * We also do some tricks with the tree->current pointer
 * so that tree walks won't be disturbed by the deletion.
 */
static void
delete_node(nft_rbtree * tree, nft_rbnode * z)
{
    nft_rbnode * p = NULL;
    nft_rbnode * x = NULL;
    nft_rbnode * y = NULL;

    assert(z != NIL(tree));
  
    /* y is the node that we will remove from the tree.
     * y will have at least one Nil child, so that we
     * can splice y out by promoting the other child.
     */
    if (z->LEFT == NIL(tree) || z->RIGHT == NIL(tree))
    {
	y = z;
	
	// If z was to be the next walk node, increment the walk to z's successor. 
	if (tree->current == y)    
	    tree->current = node_successor(tree, y);
    }
    else
    {
	/* Both of z's subtree's are full, so copy the key/data of z's successor to z,
	 * and remove the successor.
	 */
	y = node_successor(tree, z);
	z->key  = y->key;
	z->data = y->data;

	/* If y was to be the next walk node, set the tree walk pointer to z,
	 * as that is where y's key/data are now.
	 */
	if (tree->current == y)    
	    tree->current  = z;
    }
    assert(y != NIL(tree));

    // x is the child of y that we will promote into y's place.  x could be the Nil node.
    x = (y->LEFT != NIL(tree)) ? y->LEFT : y->RIGHT;

    // To promote x, set its parent pointer to y's parent.
    p = PARENT(y);
    SET_PARENT(x, p);

    // Set the appropriate child pointer in p to x.
    if (p->LEFT == y)
	p->LEFT  = x;
    else
	p->RIGHT = x;

    // Balance the tree if we deleted a black node.
    if (!RED(y)) delete_fixup(tree, x);

    /* Node y is now a free node. In order to preserve our scheme where
     * tree->next_free indicates the next free node, we have to copy the
     * node at (tree->next_free - 1) into y and then decrement next_free.
     */
    z = &tree->nodes[--tree->next_free];
    if (y != z)
    {
	*y = *z;

	// Switch parent pointers in z's children to point to y.
	SET_PARENT(z->LEFT,  y);
	SET_PARENT(z->RIGHT, y);

	// Switch the child pointer in z's parent to point to y.
	p = PARENT(z);
	if (p->LEFT == z)
	    p->LEFT  = y;
	else
	    p->RIGHT = y;

	// If z was current walk node, switch it to y.
	if (tree->current == z)
	    tree->current  = y;
    }
    return;
}

/* This function expands or shrinks the tree by reallocing the nodes array.
 * Returns 1 on success, zero on failure.
 */
static int
resize_nodes(nft_rbtree *tree, int new_size)
{
    assert(new_size >= tree->next_free);

    // Attempt to realloc the nodes array to the new size.
    nft_rbnode * new_nodes = realloc(tree->nodes, new_size * sizeof(nft_rbnode));

    if (!new_nodes) return 0;

    if (new_size == 0)
    {
	// If new_size is zero, realloc behaves like free.
	new_nodes = NULL;
    }
    else if (new_nodes != tree->nodes)
    {
	// If realloc moved the nodes array, adjust all the tree pointers.
	long offset = new_nodes - tree->nodes;
	for (int  i = 0; i < tree->next_free; i++)
	{
	    nft_rbnode * n = &new_nodes[i];

	    if (n->LEFT  != NIL(tree)) n->LEFT   += offset;
	    if (n->RIGHT != NIL(tree)) n->RIGHT  += offset;
	    if (PARENT(n)!= NIL(tree)) n->parent += offset * sizeof(nft_rbnode);
	}
	// Offset the tree root pointer.
	if (ROOT(tree) != NIL(tree)) ROOT(tree) += offset;

	// Also offset the walk pointer, if set.
	if (tree->current != NULL    &&
	    tree->current != NIL(tree))
	    tree->current += offset;
    }
    tree->nodes     = new_nodes;
    tree->num_nodes = new_size;

    assert(rbtree_validate(tree));

    return 1;
}

/******************************************************************************/
/*******								*******/
/*******		RBTREE PRIVATE APIS				*******/
/*******								*******/
/******************************************************************************/

/*-----------------------------------------------------------------------------
 *
 * rbtree_create
 *
 * This constructor takes additional parameters class and size,
 * so that you can use it to implement a subclass of nft_rbtree.
 *
 *-----------------------------------------------------------------------------
 */
nft_rbtree *
rbtree_create (const char * class, size_t size, int min_nodes, int (*compare)() )
{
    /* The initial size must be nonnegative. The caller may specify zero.
     * In cases where it is not certain that any items will be inserted,
     * this causes us not to allocate any nodes until an insert is done.
     */
    if (min_nodes < 0) return NULL;
    
    // The compare arg is mandatory.
    if (!compare) return NULL;

    nft_rbtree  * tree = nft_rbtree_cast(nft_core_create(class, size));
    if (!tree) return NULL;

    // Allocate initial storage for the nodes, if requested.
    if (min_nodes > 0) {
	if (!(tree->nodes = malloc(min_nodes * sizeof(nft_rbnode)))) {
	    nft_rbtree_discard(tree);
	    return NULL;
	}
    }
    else
	tree->nodes = NULL;

    // Initialize the other tree data.
    tree->core.destroy = rbtree_destroy;
    tree->min_nodes = min_nodes;
    tree->num_nodes = min_nodes;
    tree->next_free = 0;
    tree->compare   = compare;
    tree->current   = NULL;

    // Initialize the tree root. NIL is the empty node.
    ROOT(tree) = NIL(tree);

    // Set the NIL node's parent pointer to itself.
    // There is no need to use SET_PARENT in this case.
    NIL(tree)->parent = (long) NIL(tree);

    pthread_rwlock_init(&tree->rwlock, NULL);

    return tree;
}

/*-----------------------------------------------------------------------------
 *
 * rbtree_destroy	Free the tree's memory.
 *
 *-----------------------------------------------------------------------------
 */
void rbtree_destroy(nft_core * core)
{
    // The _cast function will return NULL if core is not a nft_rbtree.
    nft_rbtree * rbtree = nft_rbtree_cast(core);
    if (rbtree) {
	if (rbtree->nodes) free(rbtree->nodes);
	pthread_rwlock_destroy(&rbtree->rwlock);
    }
    // Remember to invoke the base-class destroyer last of all.
    nft_core_destroy(core);
}

/*-----------------------------------------------------------------------------
 *
 * rbtree_insert	Insert a new key/data pair into the tree.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_insert(nft_rbtree *tree,  void * key,  void * data)
{
    if (!tree) return 0;

    assert(tree->next_free >= 0);
    assert(tree->next_free <= tree->num_nodes);

    // Ensure that there is room to insert a key.
    if (tree->next_free == tree->num_nodes)
    {
	// The tree is full, so double its size.
	int new_size = 2 * tree->num_nodes;

	// If the tree was initially empty, allocate four nodes now.
	if (new_size == 0)
	    new_size =  4;

	if (!resize_nodes(tree, new_size))
	    return 0;
    }

    // The first node in an empty tree is made the left child of NIL.
    if (tree->next_free == 0)
	attach_leaf(tree, NIL(tree), key, data, 0);
    else
	insert_node(tree, key, data);

    return 1;
}

/*-----------------------------------------------------------------------------
 *
 * rbtree_delete	Remove a key,data pair from the tree.
 * 			If duplex keys are in use, *data must be specified.
 *
 *-----------------------------------------------------------------------------
 */
int 
rbtree_delete(nft_rbtree * tree,  void *key,  void **data)
{
    int		(*compare)();
    void	 *tok = data ? *data : NULL;
    nft_rbnode       *node;
    int   	  c = -1;

    if ( tree == NULL) return 0;

    compare = tree->compare;

    // Find the given key.
    for (node = ROOT(tree);
	 (node != NIL(tree) && (c = compare(key, node->key, tok, node->data)) != 0);
	 node = (c < 0) ? node->LEFT : node->RIGHT);

    // If the key is found, store the data and delete node.
    if (c == 0) 
    {
	if (data) *data = node->data;

	delete_node(tree, node);

	/* If only a quarter of the nodes are in use, halve the tree size.
	 * Don't shrink below the initial allocation, though.
	 */
	if ((tree->next_free <  tree->num_nodes/4) &&
	    (tree->min_nodes <= tree->num_nodes/2)  )
	    resize_nodes(tree,  tree->num_nodes/2);
    }
    return (c == 0);
}

/*-----------------------------------------------------------------------------
 *
 * rbtree_search	Look up a key in a tree
 *			If duplex keys are in use, *data must be specified.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_search(nft_rbtree *tree, void *key, void  **data)
{
    int	       (*compare)();
    void	*tok = data ? *data : NULL;
    nft_rbnode	*node;
    int		 c = -1;

    if ( tree == NULL) return 0;

    compare = tree->compare;

    // Find the given key.
    for (node = ROOT(tree);
	 (node != NIL(tree) && (c = compare(key, node->key, tok, node->data)) != 0);
	 node = (c < 0) ? node->LEFT : node->RIGHT);

    // Return data if key found. 
    if ((c == 0) && data)
	*data = node->data;

    return (c == 0);
}

/*-----------------------------------------------------------------------------
 *
 * rbtree_replace	Change the data of an existing key.
 *			Returns false if key is not found.
 *			Not usable with duplex keys.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_replace(nft_rbtree *tree, void *key, void *data)
{
    int		(*compare)();
    nft_rbnode	 *node;
    int		  c = -1;

    if ( tree == NULL) return 0;

    compare = tree->compare;

    // Find the given key.
    for (node = ROOT(tree);
	 (node != NIL(tree) && (c = compare(key, node->key, data, node->data)) != 0);
	 node = (c < 0) ? node->LEFT : node->RIGHT);

    // If key found, change data.
    if (c == 0)
	node->data = data;

    return (c == 0);
}

/*-----------------------------------------------------------------------------
 *
 *	rbtree_count() - Return number of entries in RB tree.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_count( nft_rbtree * tree)
{
    if (!tree) return 0;

    return(tree->next_free);
}

/*-----------------------------------------------------------------------------
 *
 *	rbtree_walk_first_r    Initiate a walk of tree in order of ascending keys.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_walk_first_r(nft_rbtree *tree, void  **key, void  **data, void **walk)
{
    nft_rbnode * node;
    int	     result = 0;

    if (tree == NULL) return 0;

    // If tree is not empty, set *walk to node's successor.
    if ((node = node_first(tree)))
    {
	if (key)  *key  = node->key;
	if (data) *data = node->data;
	*walk = node_successor(tree, node);
	result = 1;
    }
    return result;
}

/*-----------------------------------------------------------------------------
 *
 *	rbtree_walk_next_r	Returns the next key and data in ascending order.
 *
 *	You must call rbtree_walk_first_r() to start the walk.
 *      You may not insert or delete keys during the walk.
 *	Returns TRUE until the last node has been returned,
 *	then returns FALSE for all subsequent calls.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_walk_next_r(nft_rbtree *tree, void **key, void **data, void **walk)
{
    nft_rbnode * node;
    int	     result = 0;

    /* Make sure that this node wasn't deleted during the previous 
     * iteration. We clear the parent pointer when a node is deleted.
     */
    if ((node = (nft_rbnode *) *walk) && (node->parent != 0))
    {
	if (key)  *key  = node->key;
	if (data) *data = node->data;
	*walk = node_successor(tree, node);
	result = 1;
    }
    return result;
}

/*-----------------------------------------------------------------------------
 *
 *	rbtree_walk_first	Single-threaded walk-first call.
 *
 *	Only one thread may walk a shared tree at a time.
 *	The caller must enforce this constraint.
 *
 *	rbtree_walk_first_r() + rbtree_walk_next_r() allow concurrent
 *      walks, but keys may not be inserted or deleted.
 *-----------------------------------------------------------------------------
 */
int
rbtree_walk_first(nft_rbtree *tree, void  **key, void  **data)
{
    return rbtree_walk_first_r(tree, key, data, (void**) &tree->current);
}

/*-----------------------------------------------------------------------------
 *
 *	rbtree_walk_next	Single threaded walk_next call.
 *
 *	Only one thread may walk a shared tree at a time. 
 *	The caller must enforce this constraint.
 *	In multithreaded code, use rbtree_walk_first_r(), rbtree_walk_next_r().
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_walk_next(nft_rbtree *tree, void **key, void **data)
{
    return rbtree_walk_next_r(tree, key, data, (void**) &tree->current);
}

/*-----------------------------------------------------------------------------
 *	rbtree_apply() - Call a function on every entry in the rb tree.
 *	Return the number of entries in the tree.
 *
 *	Function takes args of (key, data, extra_arg). 
 *
 *	WARNING - Your function must _NOT_ modify or delete the key,
 *	otherwise the tree will not function correctly.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_apply( nft_rbtree * rb,
		  void (* apply)( void * key, void * obj, void * arg),
		  void * arg)
{
    nft_rbnode * walk;
    int      num = 0;

    if (rb == NULL) return 0;
    
    for (walk = node_first(rb);
	 walk != NULL;
	 walk = node_successor(rb, walk))
    {
	num++;
	(*apply)( walk->key, walk->data, arg);
    }

    return num;
}

/********************* tree validation functions ************************/

/* 
 *  Verify that node pointers point within the allocated node area,
 *  and that parent-child pointer relationships are consistent.
 *  Verify that the red-black node color constraint is satisfied.
 */
static int
check_pointers(nft_rbtree *tree, nft_rbnode   *node)
{
    if (node != NIL(tree))
    {
	// Check that node points into the malloc'ed area. 
	if ((node < tree->nodes) ||
	    (node >= (tree->nodes + tree->next_free)))
	{
	    assert(!"rbtree_validate:Node pointer out of bounds!\n");
	    return 0;
	}
	
	/* Check that parent pointers are consistent
	 */
	if ((node->RIGHT != NIL(tree) && PARENT(node->RIGHT) != node) ||
	    (node->LEFT  != NIL(tree) && PARENT(node->LEFT)  != node))
	{
	    assert(!"rbtree_validate:Bad parent pointer!\n");
	    return 0;
	}

	/* If this node is red, both children must be black.
	 */
	if (RED(node) && (RED(node->LEFT) || RED(node->RIGHT)))
	{
	    assert(!"rbtree_validate:Red-black violation!\n");
	    return 0;
	}
	return(check_pointers(tree, node->LEFT) &&
	       check_pointers(tree, node->RIGHT));
    }
    return 1;
}


/*-----------------------------------------------------------------------------
 *
 * rbtree_validate	Test tree's pointers and key ordering integrity.
 * 			Returns TRUE (1) if the tree is valid, else 0.
 *
 *-----------------------------------------------------------------------------
 */
int
rbtree_validate( nft_rbtree * tree)
{
    int result = 1;

    // The Nil node should never be colored red, and it should never be on the freelist.
    assert(!RED((NIL(tree))));

    // The walk pointer should never point to a free node.
    assert(!tree->current ||
	   ((tree->current >= tree->nodes) &&
	    (tree->current < (tree->nodes + tree->next_free))));

    if (PARENT(ROOT(tree)) != NIL(tree))
    {
	assert(!"Root node parent != NIL\n");
	result = 0;
    }

    if (check_pointers(tree, ROOT(tree)))
    {
	int  (*compare)() = tree->compare;
	void 	*prevkey  = NULL;
	void	*prevdata = NULL;
	int	 first    = 1;
	nft_rbnode	*node;

	for (node = node_first(tree); node;  node = node_successor(tree, node))
	{
	    if (!first && compare(node->key, prevkey, node->data, prevdata) < 0)
	    {
		assert(!"rbtree_validate: Key order violation!\n");
		result = 0;
	    }
	    prevkey  = node->key;
	    prevdata = node->data;
	    first    = 0;
	}
    }
    else
    	result = 0;

    return result;
}


/******************************************************************************/
/*******								*******/
/*******		RBTREE PUBLIC APIS				*******/
/*******								*******/
/******************************************************************************/

/* These APIs are simply Nifty wrappers around the private APIs.
 * Note that the public APIs enforce concurrent-reades and exclusive writers.
 * I like this approach, because it gives the option to use the private API
 * with or without locking, depending on your needs.
 */

/*-----------------------------------------------------------------------------
 *
 * nft_rbtree_new	Create a new nft_rbtree
 *
 * PARAMETERS	min_size - initial number of nodes to allocate
 *		compare  - key comparison predicate function 
 *
 * RETURNS      Handle of type nft_rbtree_h
 *
 *-----------------------------------------------------------------------------
 */
nft_rbtree_h
nft_rbtree_new(int min_nodes, int (*compare)() )
{
    return nft_rbtree_handle(rbtree_create(nft_rbtree_class, sizeof(nft_rbtree), min_nodes, compare));
}

/*-----------------------------------------------------------------------------
 *
 * nft_rbtree_free	Release the nft_rbtree's handle and memory.
 *
 * RETURNS      EINVAL if the handle is invalid, otherwise zero.
 *		If you see EINVAL, you are probably freeing twice.
 *
 *-----------------------------------------------------------------------------
 */
int
nft_rbtree_free(nft_rbtree_h h)
{
    int result = EINVAL;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree)
    {
	// Double-discard, to release the reference returned from rbtree_create().
	if ((result = nft_rbtree_discard(rbtree)) == 0)
	     result = nft_rbtree_discard(rbtree);
    }
    return result;
}



int
nft_rbtree_count(nft_rbtree_h h)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_count(rbtree);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}

int
nft_rbtree_validate(nft_rbtree_h h)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_validate(rbtree);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_insert(nft_rbtree_h h, void  *key, void  *data)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_wrlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_insert(rbtree, key, data);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_replace(nft_rbtree_h h, void  *key, void  *data)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_wrlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_replace(rbtree, key, data);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_delete(nft_rbtree_h h, void  *key, void **data)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_wrlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_delete(rbtree, key, data);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_search(nft_rbtree_h h, void  *key, void **data)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_search(rbtree, key, data);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_walk_first(nft_rbtree_h h, void **key, void **data)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_walk_first(rbtree, key, data);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_walk_next(nft_rbtree_h h, void **key, void **data)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_walk_next(rbtree, key, data);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_walk_first_r(nft_rbtree_h h, void **key, void **data, void **walk)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_walk_first_r(rbtree, key, data, walk);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_walk_next_r(nft_rbtree_h h, void **key, void **data, void **walk)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_walk_next_r(rbtree, key, data, walk);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}
int
nft_rbtree_apply(nft_rbtree_h h, RBTREE_APPLY apply, void * arg)
{
    int          result = 0;
    nft_rbtree * rbtree = nft_rbtree_lookup(h);
    if (rbtree) {
	int r = pthread_rwlock_rdlock(&rbtree->rwlock); assert(r == 0);

	result = rbtree_apply(rbtree, apply, arg);

	r = pthread_rwlock_unlock(&rbtree->rwlock); assert(r == 0);
	nft_rbtree_discard(rbtree);
    }
    return result;
}


/******************************************************************************/
/******************************************************************************/
/*******								*******/
/*******		RBTREE PACKAGE UNIT TEST			*******/
/*******								*******/
/******************************************************************************/
/******************************************************************************/
#ifdef MAIN
#ifdef NDEBUG
#undef NDEBUG  // Assertions must be active in test code.
#endif
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/resource.h>

/*
 * Store a set of strings that we will use to test this package.
 */
#define MAXKEYS 200000
char   *keys[MAXKEYS];
int	nkeys = 0;


/* Print indented tree */
void print_tree(nft_rbtree * tree, nft_rbnode * node, int depth)
{
    if (node != NIL(tree))
    {
	print_tree(tree, node->LEFT, depth + 1);
	for (int i = 0; i < depth; i++) putchar('	');
	printf("%c:%s\n", (RED(node) ? 'R' : 'B'), (char *) node->key);
	print_tree(tree, node->RIGHT, depth + 1);
    }
}


static int
strcmp_duplex(char *key1, char *key2, char *tok1, char *tok2)
{
    int res = strcmp(key1, key2);
    if (res == 0)
	res = strcmp(tok1, tok2);
    return res;
}

static void
test_duplex_keys(void)
{
    char * test[10] = { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten" };

    nft_rbtree * u = rbtree_create(nft_rbtree_class, sizeof(nft_rbtree), 10, strcmp_duplex);

    rbtree_insert(u, "bob", test[0]);
    rbtree_insert(u, "bob", test[1]);
    rbtree_insert(u, "bob", test[2]);
    rbtree_insert(u, "bob", test[3]);
    rbtree_insert(u, "bob", test[4]);
    rbtree_insert(u, "bob", test[5]);

    if (!rbtree_search(u, "bob", (void**) &test[1]))
	printf("search: duplex key not found!\n");

    if (!rbtree_delete(u, "bob", (void**) &test[3]))
	printf("delete: duplex key not found!\n");

    if (!rbtree_validate(u))
	printf("validate failure with duplex test!\n");

    int result = nft_rbtree_discard(u);
    assert(0 == result);
}

static void
test_private_api(void)
{
    void       *key, *lastkey = NULL;
    void       *data;
    int		result = 0;
    int 	i, j;

    /* Timing stuff.
     */
    struct tms 	start, done;
    float	usert, syst;
    float	tick = 1.0 / sysconf(_SC_CLK_TCK);
#define MARK	times(&start)
#define TIME	times(&done); \
    usert = (done.tms_utime - start.tms_utime) * tick; \
    syst  = (done.tms_stime - start.tms_stime) * tick

    printf("\nTesting the pointer-based API\n");

    /* Create a new tree with 512 nodes allocated.
     * Be sure to allocate prior to allocating strings,
     * in order to force a pass thru the offset code in
     * realloc-nodes.
     */
    nft_rbtree * t = rbtree_create(nft_rbtree_class, sizeof(nft_rbtree), 
				   512, (int (*)(void*, void*)) strcmp);

    /* Insert strings into tree  */
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
	rbtree_insert(t, keys[i], 0);
    TIME;			/* compute time usage */
    printf("Time to insert %d keys: %.2fu %.2fs\n", i, usert, syst);

    /* Walk the tree */
    MARK;			/* record start time */
    for (i = 0, result = rbtree_walk_first(t, &key, &data);
	 result;
	 i++,   result = rbtree_walk_next (t, &key, &data))
    {
	if (lastkey && strcmp((char *) lastkey, (char *) key) > 0)
	    printf("\nWalk generated keys in wrong order!\n");
	lastkey = key;
    }
    TIME;			/* compute time usage */
    if (i != nkeys)
	printf("Walk generated only %d keys!\n", i);
    printf("Time to walk   %d keys: %.2fu %.2fs\n", i, usert, syst);

    /* Search all keys */
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
	if (!rbtree_search(t, keys[i], &data))
	    printf("search: key not found %s\n", keys[i]);
    TIME;			/* compute time usage */
    printf("Time to search %d keys: %.2fu %.2fs\n", i, usert, syst);

    /* Delete all keys */
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
    {
	if (!rbtree_delete(t, keys[i], &data))
	    printf("delete: key not found %s\n", keys[i]);
    }
    TIME;			/* compute time usage */
    printf("Time to delete %d keys: %.2fu %.2fs\n", i, usert, syst);


    /* A loop which randomly deletes or inserts the keys.
     *
     * nkeys = 1-100: We print the tree at each iteration so you
     *	can monitor the trees balance retention. We also validate
     *	the tree after each operation to catch bugs.
     *
     * nkeys = 101-1000: In this range we skip print and validition,
     *	in order to test performance. The tests above just aren't
     *	sensitive enought to catch subtle performance changes.
     */
    if (nkeys <= 1000) 
    {
	int limit = (nkeys <= 100) ? 10000*nkeys : 500*nkeys;
      
	srand48(time(0));	/* seed the random number generator */

	MARK;

	j = 0;

	rbtree_walk_first(t, &lastkey, NULL);

	for (i = 0; i < limit; i++)
	{
	    key = keys[lrand48() % nkeys];

	    if (rbtree_search(t, key, 0))
	    {
		rbtree_delete(t, key, 0);
		j--;
	    }
	    else
	    {
		rbtree_insert(t, key, 0);
		j++;
	    }

	    /* This does a walk within a walk.
	     */
	    assert(rbtree_count(t) == j);

	    if (nkeys <= 100)
	    {
		/* validate the tree */
		rbtree_validate(t);

		puts("--------------------------------------------------------");
		print_tree(t, ROOT(t), 0);
	    }

	    /* To increase stress, do a walk while 
	     * everything else is going on.
	     */
	    if (!rbtree_walk_next(t, &key, NULL))
		rbtree_walk_first(t, &lastkey, NULL);
	    else
	    {
		if (strcmp((char *) lastkey, (char *) key) >= 0)
		    printf("\nWalk generated keys in wrong order: %s -> %s\n",
			   (char *) lastkey, (char *) key);

		lastkey = key;
	    }
	}

	TIME;			/* compute time usage */
	printf("Time for stress test     : %.2fu %.2fs\n", usert, syst);
    }

    // Do not call nft_rbtree_destroy!
    result = nft_rbtree_discard(t);
    assert(0 == result);
}


// One big cut-n-paste job on test_private_api(). ;-)
static void
test_handle_api(void)
{
    void       *key, *lastkey = NULL;
    void       *data;
    int		result = 0;
    int 	i;

    /* Timing stuff.
     */
    struct tms 	start, done;
    float	usert, syst;
    float	tick = 1.0 / sysconf(_SC_CLK_TCK);
#define MARK	times(&start)
#define TIME	times(&done); \
    usert = (done.tms_utime - start.tms_utime) * tick; \
    syst  = (done.tms_stime - start.tms_stime) * tick

    printf("\nTesting the handle-based API\n");

    /* Create a new tree with 512 nodes allocated.
     * Be sure to allocate prior to allocating strings,
     * in order to force a pass thru the offset code in
     * realloc-nodes.
     */
    nft_rbtree_h h = nft_rbtree_new(512, (int (*)(void*, void*)) strcmp);

    /* Insert strings into tree  */
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
	nft_rbtree_insert(h, keys[i], 0);
    TIME;			/* compute time usage */
    printf("Time to insert %d keys: %.2fu %.2fs\n", i, usert, syst);

    /* Walk the tree */
    MARK;			/* record start time */
    for (i = 0, result = nft_rbtree_walk_first(h, &key, &data);
	 result;
	 i++,   result = nft_rbtree_walk_next (h, &key, &data))
    {
	if (lastkey && strcmp((char *) lastkey, (char *) key) > 0)
	    printf("\nWalk generated keys in wrong order!\n");
	lastkey = key;
    }
    TIME;			/* compute time usage */
    if (i != nkeys)
	printf("Walk generated only %d keys!\n", i);
    printf("Time to walk   %d keys: %.2fu %.2fs\n", i, usert, syst);

    /* Search all keys */
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
	if (!nft_rbtree_search(h, keys[i], &data))
	    printf("search: key not found %s\n", keys[i]);
    TIME;			/* compute time usage */
    printf("Time to search %d keys: %.2fu %.2fs\n", i, usert, syst);

    /* Delete all keys */
    MARK;			/* record start time */
    for (i = 0; i < nkeys; i++)
    {
	if (!nft_rbtree_delete(h, keys[i], &data))
	    printf("delete: key not found %s\n", keys[i]);
    }
    TIME;			/* compute time usage */
    printf("Time to delete %d keys: %.2fu %.2fs\n", i, usert, syst);

    result = nft_rbtree_free(h);
    assert(0 == result);
}


int
main(int argc, char * argv[])
{
    /* Set limit on keys.
     */
    int limit;
    limit  = ((argc > 1) ? atoi(argv[1]) : MAXKEYS);
    limit  = ((limit > MAXKEYS) ? MAXKEYS : limit);

    /* Insert strings into key table
     */
    char string[256];
    int  i = 0;
    while (fgets(string, sizeof(string), stdin) && (i < limit))
	keys[i++] = strdup(string);
    nkeys = i;

    /* This test excercises the private pointer-based APIs.
     */
    test_private_api();

    /* This test excercises the private pointer-based APIs.
     */
    test_handle_api();

    /* Test with duplex keys.
     */
    test_duplex_keys();

    /* Free malloc'd storage.
     */
    for (i = 0; i < nkeys; i++)
	free(keys[i]);

    fprintf(stderr, "nft_rbtree: All tests passed.\n");
    exit(0);
}

#endif

