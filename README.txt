
This directory contains some nifty pthread packages that have 
been very useful in my projects. The packages are:

    nft_pool	- A thread pool to execute tasks asynchronously.
    nft_popen	- A substitute for standard popen() and pclose().
    nft_queue	- A message queue for producer-consumer systems.
    nft_rbtree	- A red/black balanced binary tree package.
    nft_rwlock	- A substitute for the POSIX readers/writer lock.
    nft_task	- Schedule tasks to execute at a specified time.
    nft_core    - The base class for many of the Nifty packages.
    nft_win32	- A simple pthread emulation layer for Windows.

Consult the header files in include/*.h for the API definitions.
The ANSI C implementations in src/*.c also contain test drivers 
(look for #ifdef MAIN) that illustrate how to use the package.

All of these packages are thread-safe, and are cancellation-safe
using the normal deferred-cancellation mode. All of the blocking
calls are deferred cancellation points. Please do not infer that
I recommend the use of thread cancellation, because I don't.

To build, tweak src/Makefile to your liking, then

   $ cd src ; make

To build and run the unit test drivers, do

   $ cd src ; make test

This code should build on most modern Unix systems, but you may need
to tweak the Makefile, and you may wish to adjust the implementation 
of the gettime() call in nft_gettime.h to suit your platform.

This file gives a detailed discussion how you can create your own
"classes" using Nifty, and why it may be advantageous to do so.
See the section "Creating packages based on nft_core" below.

The Nifty packages can be built on WIN32. For more information,
refer to the section "WIN32 Notes" below.

If you have questions or contributions, send them to sean@xenadyne.com.
-SEan


Creating packages based on nft_core
================================================================================

Many of the Nifty packages are derived in "object-oriented" style from nft_core.
This section discusses how this derivations works. Our approach is as simple as
possible. The goals are to:

* Support only simple single-inheritance,
* Produce APIs with strong static type-checking,
* Also provide effective runtime type-checking,
* Use reference-counts to manage shared pointers,
* Maximize safety in multi-threaded code.

The idea is that every object in the system will inherit from this core class:

    typedef struct nft_core {
        const char  * class;
        unsigned long handle;
        unsigned      reference_count;
        void        (*destroy)(ntf_core *);
    } nft_core;

Inheritance is as simple as it can be - the parent class must be declared
as the very first element of the subclass. This is single-inheritance,
and in consequence, a pointer to any subclass is also a pointer to its 
parent class, and grandparent class, etc.. Here is an example subclass:

    typedef struct subclass {
	nft_core core;
	char   * substring;
    } subclass;

The other major feature of the Nifty framework, is that objects derived
from nft_core have a "handle", which is an integer that uniquely identifies
an object instance. We'll discuss how handles are used later, but for now,
the important point is that, given a handle, you can use nft_core_lookup()
to obtain a pointer to the object instance:

    nft_core * object_reference = nft_core_lookup( void * handle );

A side effect of the lookup, is that the object's reference count is
incremented. This ensures that the pointer you received from the lookup
cannot be freed until you explicitly release your reference, via
nft_core_discard:

    nft_core_discard( object_reference );

Since nft_core_lookup returns a nft_core*, you will need to type cast the
nft_core* to the object's actual type. But, how can you be certain that
this pointer really points to an instance of some class, or an object 
derived from that class?

This is the purpose of the nft_core.class member. When a subclass is
based on nftcore, we arrange that the nft_core.class will be the string
"nft_core:subclass". Similarly, if we derive a sub-subclass from subclass,
its class string should be "nft_core:subclass:subsubclass". In this way,
we are able to define a safe typecast function for subclass, by comparing
the object's class string to the desired class "nft_core:nft_subclass":

    subclass *
    subclass_cast(nft_core * p) {
        if (!strncmp(p.class, "nft_core:subclass", strlen("nft_core:subclass")))
             return (subclass*) p;
        else
             return (subclass*) 0;
    }

The trick, then, is to arrange for the nft_core.class string to be constructed
properly when we instantiate subclasses. With these considerations in mind,
it should be clear why the nft_core constructor (shown below) takes both class
and size parameters - these indicate the subclass that is being instantiated,
and its size:

    nft_core *
    nft_core_create(const char * class, size_t size) {
        nft_core * self = malloc(size);
        *self = { class, 0, 1, nft_destroy };
        return self;    
    }

Note that the constructor shown above is simplified, because it omits 
the details of handle allocation. It is sufficient to illustrate some
very important issues.

The core destroyer is also quite simple - it merely frees the memory
for the instance:

    void
    nft_core_destroy(nft_core * self) {
        free(self);
    }

Below, we show a construtor for a subclass that derives from nft_core.
Note that it accepts the same size and class parameters that the core
constructor does - this enables us to derive subclasses from this subclass.
Note that it calls the parent-class ctor as the very first order of
business, passing these same size and class parameters:

    subclass *
    subclass_create(size_t size, const char * class, const char * substring) {
        subclass * self   = subclass_cast(nft_core_create(size, class));
        self.core.destroy = nft_subclass_destroy;
        self.substring    = strdup(substring);
    }

You may also have noted that the subclass ctor overrides the destructor.
This is how virtual methods work in Nifty - it is up to you to manage them.
The subclass destructor must call its parent dtor as the very last order of
business:

    void
    subclass_destroy(subclass * self) {
        free(self.substring);
        nft_core_destroy(&self.core);
    }

So, to create an instance of subclass, we simply invoke the constructor,
passing that class string, size, and the 'substring' parameter:

     subclass * this = subclass_create("nft_core:subclass", sizeof(subclass), "substring");

Let us now pause and consider, that this is the entire inheritance system
in the Nifty framework, and we have not yet used a single macro. You knew
that could not last, and we can obtain real benefits from a few simple macros.
First, we define the nft_core class string using this macro:

	#define nft_core_class    "nft_core"

This macro makes it easy to construct the class string for a subclass,
taking advantage of the fact that C will implicitly concatenate two
string literals that occur together:

	#define subclass_class nft_core_class ":subclass"

This may seem trivial, but it pays off when we derive a sub-subclass
from subclass:

	#define subsubclass_class subclass_class ":subsubclass"

The benefit should now be apparent: if subclass changes its place in the
class hierarchy, these changes will propogate automatically to the value
of subsubclass_class.

Given the subclass_class macro, the subclass_cast() function that we showed
earlier can now be created via macros. The NFT_DECLARE_CAST macro below,
creates the function prototype in a header file, and the NFT_DEFINE_CAST
macro creates the fuction definition in a .c file:

	#define NFT_DECLARE_CAST(subclass) \
	subclass * subclass##_cast(nft_core * p);

	#define NFT_DEFINE_CAST(subclass) \
	subclass * \
	subclass##_cast(nft_core * p) { \
	        return (subclass *) nft_core_cast(p, subclass##_class); \
	}

HANDLES

Every object derived from nft_core has a handle that can be used to look up
the original object. Here, we create an instance of subclass, and save its
handle in variable h:

    subclass * o = subclass_create(subclass_class, sizeof(subclass), "my substring");
    nft_handle h = o->core.handle;

The handle can be used to obtain a copy of the original pointer s. Because
it is an independent reference, the object's reference count is incremented
by nft_core_lookup, so we must discard the reference when we are done with it:

    nft_core * c = nft_core_lookup(h);
    nft_core_discard(c);

The difficulty is that the pointer returned by nft_core_lookup is really an
instance of subclass. To be type safe, We must use subclass_cast to safely
typecast the nft_core*, and we need to pass &s->core to nft_core_discard:

    subclass * s = subclass_cast(nft_core_lookup(h));
    nft_core_discard(&s->core);

We can use macros to create type-safe wrappers that clean up the code above.
First, let's give handles of subclass objects their own type, subclass_h:

    typedef struct subclass_h * subclass_h;

Now, define a simple wrapper function to cast a subclass object's handle
to type subclass_h:

    subclass_h subclass_handle(const subclass * s) { 
        return (subclass_h) s->core.handle;
    }

Next, a wrapper for nft_core_lookup in the same style:

    subclass * subclass_lookup(subclass_h h) {
        return subclass_cast(nft_core_lookup(h));
    }

And, a wrapper for nft_core_discard:

    void subclass_discard(subclass * s) {
        nft_core_discard(&s->core);
    }

These wrapper functions provide clean, strongly-typed APIs to manipulate
subclass handles and references:

    subclass * o = subclass_create(subclass_class, sizeof(subclass), "my substring");
    subclass_h h = subclass_handle(o);

    subclass * s = subclass_lookup(h);
    subclass_discard(s);

These wrapper functions can easily be defined via macros. Note that the
functions each have a macro to declare the prototype, and another to 
define the function:

#define NFT_TYPEDEF_HANDLE(subclass) \
typedef struct subclass##_h * subclass##_h;

#define NFT_DECLARE_HANDLE(subclass) \
subclass##_h subclass##_handle(const subclass * s);

#define NFT_DECLARE_LOOKUP(subclass) \
subclass * subclass##_lookup(subclass##_h h);

#define NFT_DECLARE_DISCARD(subclass) \
void subclass##_discard(subclass * s);

#define NFT_DEFINE_HANDLE(subclass) \
subclass##_h subclass##_handle(const subclass * s) { return (subclass_h) s->core.handle; }

#define NFT_DEFINE_LOOKUP(subclass) \
subclass * subclass##_lookup(subclass##_h h) { return subclass_cast(nft_core_lookup(h)); }

#define NFT_DEFINE_DISCARD(subclass) \
void subclass##_discard(subclass * s) { nft_core_discard(&s->core); }

Finally, we gather all these macros into two convenience macros:

#define NFT_DECLARE_HELPERS(subclass) \
NFT_DECLARE_CAST(subclass)   \
NFT_TYPEDEF_HANDLE(subclass) \
NFT_DECLARE_HANDLE(subclass) \
NFT_DECLARE_LOOKUP(subclass) \
NFT_DECLARE_DISCARD(subclass)

#define NFT_DEFINE_HELPERS(subclass) \
NFT_DEFINE_CAST(subclass)   \
NFT_DEFINE_HANDLE(subclass) \
NFT_DEFINE_LOOKUP(subclass) \
NFT_DEFINE_DISCARD(subclass)

These macros are all defined in nft_core.h. Using them, the entire implementation 
of our subclass consists of the following:

typedef struct subclass
{
    nft_core core;
    char   * substring;
} subclass;

#define subclass_class nft_core_class ":subclass"

NFT_DECLARE_HELPERS(subclass)
NFT_DEFINE_HELPERS(subclass)

void
subclass_destroy(nft_core * p)
{
    subclass * this = subclass_cast(p);
    free(this->substring);
    nft_core_destroy(p);
}

subclass *
subclass_create(const char * class, size_t size, const char * substring)
{
    subclass    * this = subclass_cast(nft_core_create(class, size));
    this->core.destroy = subclass_destroy;
    this->substring    = strdup(substring);
    return this;
}

We demonstrate how to implement and use this example subclass in nft_core.c,
in the unit test that begins with #ifdef MAIN. To build the demo, simply do

   make nft_core
   ./nft_core


WIN32 Notes
================================================================================

To build on WIN32 using VisualStudio, use the win32/Nifty.dsw file.
Please note that the pthreads emulation in nft_win32 has many limitations.
We strongly recommend that you use the pthread_win32 package if you can,
but if not, you should study the warnings in nft_win32.h.


