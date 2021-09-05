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
 * File: nft_list.h
 *
 * Description: Lisp-style lists.
 *
 * The purpose of this package is to create singly-linked lists, where
 * the list pointers are stored in a `list_node' that is external to the
 * data structures stored on the list.
 *
 * One benefit is that you can make lists of data structures that don't
 * have internal fields for list-linking. For example, if you have an
 * array of integers, you can still form a list of elements of the array.
 * You also get the flexibility to place the same object on more than one
 * list simultaneously.
 *
 * USAGE
 *
 * Note there is a test driver at the end of list.c that gives some
 * examples of usage. Also, bear in mind that functions in the list
 * package that modify the list take a list_t *, while those that
 * will not change the list take a list_t.
 *
 * A list_t is defined to be a pointer to a struct list_node. The empty
 * list is simply a null pointer. So, to create an empty list, you can
 * simply do this:
 *
 *	list_t my_list = NULL;
 *
 * You can also use list_create() to create a list with an initial set
 * of items. list_create() takes a variable number of arguments, which
 * _must_ be terminated by NULL. The NULL is not entered on the list.
 * For example:
 *
 *	list_t my_list = list_create("one", "two", "three", NULL);
 *
 * There are various styles for iterating over a list, but the general
 * idea is that a list consists of two parts: the `first' item on the
 * list, and the `rest' of the list, which is itself a list. So you can
 * iterate by looking at successive `tails' of the list:
 *
 *	list_t temp;
 *
 * 	for (temp = my_list; temp != NULL; temp = temp->rest)
 * 	{
 * 	    my_data_t x = temp->first;
 * 	    ...
 * 	}
 *
 * Beware of a common mistake that occurs when you delete items from
 * a list while you are traversing it - if we delete the item 'x'
 * in the loop above, the list node temp is freed, so the increment
 * clause of the loop "temp = temp->rest" is referring to a freed node.
 * To avoid this, get your next list node prior to deleting the item,
 * and call list_delete on the list root, my_list (not on temp!)
 * as shown in the example below:
 *
 * 	for (temp = my_list; temp != NULL; )
 * 	{
 * 	    my_data_t x = temp->first;
 *
 *	    temp = temp->rest;	// Increment temp before deleting x
 *
 *	    if (...) list_delete(&my_list, x);  // RIGHT!
 *	    if (...) list_delete(&temp,     x);  // WRONG!
 * 	}
 *
 * If you wish to consume the list as you iterate, meaning to process
 * every item in the list leaving the list empty, then the following
 * loop does the job:
 *
 * 	while (my_list != NULL)
 * 	    process_my_data((my_data_t) list_pop( &my_list ));
 *
 *
 * TYPE-SAFE WRAPPERS
 *
 * Since these calls take void pointers, there is little type checking
 * going on. If you wish to improve this, this package provides some
 * macros that you can use to define a custom set of strongly-typed
 * wrapper functions around the basic list package. For example,
 * say you wish to make lists to hold pointers to instances of foo_t:
 *
 * typedef struct { ... } foo_t;
 *
 * The macro below defines the prototypes for your wrapper functions.
 * The first argument takes the place of 'list' in the function names,
 * the second defines the list type, and the last is the contained
 * object type. The fourth argument can be 'static' or 'extern', to
 * specify local or global linkage.
 *
 *    LIST_PROTOTYPES(foo, foo_list_t, foo_t, extern);
 *
 * The macro above creates the following definition:
 *
 *    typedef struct foo_list_t_node
 *    {
 *	   foo_t           *first;
 *	   foo_list_t_node *rest;
 *    } * foo_list_t;
 *
 * The LIST_PROTOTYPES macro also creates a function prototype
 * for each of the list functions that is specialized for the
 * foo_list_t, so the LIST_PROTOTYPES macro invocation will
 * generally be placed in a header file. For example, in place
 * of list_push() we define the function foo_push() with the
 * prototype:
 *
 *    extern void foo_push( foo_list_t *list, foo_t *data);
 *
 * The macro LIST_WRAPPERS defines the actual wrapper functions,
 * so the following invocation would go in one of the .c files:
 *
 *    LIST_WRAPPERS(foo, foo_list_t, foo_t, extern);
 *
 * This macro defines the wrapper for list_push() as:
 *
 *    extern void foo_push( foo_list_t *list, foo_t *data)
 *    {
 * 	   list_push((list_t *) list, (void *) node);
 *    }
 *
 * All of the other list_*() functions are wrapped in similar fashion,
 * Static linkage is preferred, as this will encourage your compiler
 * to inline these functions, eliminating the wrapper function call.
 *
 *
 * ==== TIPS ====
 *
 * - There is a test driver at the end of nft_list.c that shows examples
 * of list usage.
 *
 * - You can easily tell which functions in the list package
 * will modify the input list. Functions that modify their
 * argument take a list_t *, while those that will not
 * change the list take a list_t.
 *
 * - list_append()'s time cost grows linearly with the list
 * length. This means that building a list of N items with
 * list_append() is an order N-squared operation. If you
 * are using list_append because you care about the order
 * of your list items, consider whether you can instead build
 * your list with list_push() and then do a list_reverse()
 * when the list is completed. This will create the list in
 * linear time, which is a huge advantage for long lists.
 *
 ******************************************************************************
 */
#ifndef _NFT_LIST_H_
#define _NFT_LIST_H_

typedef struct list_node {
  void		   * first;
  struct list_node * rest;
} * list_t;

// Push item onto the front of the list.
void list_push  (list_t *l, void * item);

// Remove and return the first item in the list.
void *list_pop  (list_t *l);

// Return the first item in the list --- but leave the list undisturbed.
// This is a convenience function, as it does the exact same thing as xx = list->first.
void *list_peek(list_t l);

// Return the nth item in the list.
void *list_nth(list_t l, int n);

// Append item to the end of the list.
void list_append(list_t *l, void *item);

// Reverses the order of the list.
void list_reverse(list_t *l);

// Attaches list 2 at the end of list 1.
// List 2 will be empty afterward.
void list_cat(list_t *l1, list_t *l2);

// Returns a copy of the list.
list_t list_copy(list_t l);

// Returns 1 if the item is in the list, else 0.
int  list_search(list_t  l, void *item);

// Removes every occurence of the item from the list.
void * list_delete(list_t  *l, void *item);

// Replaces every occurence of p with v in the list.
void list_replace(list_t *l, void *p, void *v);

// Creates a list with the specified set of items.
// The argument list MUST be terminated by NULL.
list_t list_create(void * first_item, ...);

// Frees all of the nodes in the list, setting the list to null.
void list_destroy(list_t *l);

// Counts the number of items in the list.
unsigned list_count(list_t l);

// Applies function to each element of the list.
void list_apply(list_t l, void (*function)(void *));

// Apply function to every item on the list, returning a list of results.
list_t list_map(list_t l, void * (*function)(void *));

/* Reduce a list by calling the function, multiple times,
   setting void *a and void *b each time. The first call will be with
   a and b set to the first two elements of the list, subsequent calls
   will be done by setting a to the result of the previous call and b
   to the next element in the list.

   Returns the result of the last call to function. If the list is empty
   then NULL is returned. If the list only contains one element then that
   element is returned and the function is not executed. Using this function,
   you can implement operations like min, max, and sum.

   For example:
     void * least (void * a, void * b) { return strcmp((char*) a, (char*) b) < 0 ? a : b; }
     char * min = list_reduce(list, least);
 */
void * list_reduce(list_t l, void * (*function)(void *, void *));

/* Convert the list into a null-terminated array of list items.
 * The nump parameter, if non-null, returns the number of list elements,
 * not counting the null terminator. On success, the input list is destroyed.
 * If malloc fails, the input string is preserved and NULL is returned.
 */
void ** list_to_array(list_t *l, int * nump);

/* Convert a list to a null-terminated array of int-sized items.
 * Returns NULL on malloc failure, and the input list is unmodified.
 */
int * list_to_int_array(list_t *lp, unsigned long * nump);

// Convert a list to a null-terminated array of long-sized items.
long * list_to_long_array(list_t *lp, unsigned long * nump);

#endif // _NFT_LIST_H_
