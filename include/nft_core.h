/******************************************************************************
 * (C) Copyright Xenadyne, Inc. 2013  All rights reserved.
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
 ******************************************************************************
 *
 * File:  nft_core.h
 *
 * DESCRIPTION
 *
 * This package is the core class for nft_queue, nft_pool and nft_task.
 * It provides referenced counted objects, that can be identified by a
 * unique handle. For more information about handles, and object-oriented
 * development using nft_core, see the README.txt file.
 *
 ******************************************************************************
 */
#ifndef _nft_core_header
#define _nft_core_header

// Since all Nifty-based code must ultimately include this header,
// this is a convenient place to choose pthread.h or nft_win32.h.
//
#ifdef _WIN32
#include "nft_win32.h"
#else
#include <pthread.h>
#endif
#include <nft_gettime.h>


#define nft_core_class "nft_core"

typedef void * nft_handle;

typedef struct nft_core {
    const char  * class;
    nft_handle    handle;
    unsigned      reference_count;
    void        (*destroy)(struct nft_core *);
} nft_core;

nft_core * nft_core_create(const char * class, size_t size);
void       nft_core_destroy(nft_core * this);
nft_core * nft_core_lookup(nft_handle h);
int        nft_core_discard(nft_core * this);
void     * nft_core_cast(void * vp, const char * class);

#define NFT_TYPEDEF_HANDLE(subclass) \
typedef struct subclass##_h * subclass##_h;

#define NFT_DECLARE_CAST(subclass)		\
subclass * subclass##_cast(void *);
#define NFT_DECLARE_HANDLE(subclass) \
subclass##_h subclass##_handle(const subclass *);
#define NFT_DECLARE_LOOKUP(subclass) \
subclass * subclass##_lookup(subclass##_h);
#define NFT_DECLARE_DISCARD(subclass) \
int subclass##_discard(subclass *);

// Note that static is a parameter, which can be empty.
#define NFT_DECLARE_WRAPPERS(subclass, static) \
NFT_TYPEDEF_HANDLE(subclass) \
static NFT_DECLARE_CAST(subclass)   \
static NFT_DECLARE_HANDLE(subclass) \
static NFT_DECLARE_LOOKUP(subclass) \
static NFT_DECLARE_DISCARD(subclass)


#define NFT_DEFINE_CAST(subclass)		\
subclass * subclass##_cast(void * vp) { return nft_core_cast(vp, subclass##_class); }
#define NFT_DEFINE_HANDLE(subclass) \
subclass##_h subclass##_handle(const subclass * sc) { return ((nft_core*)sc)->handle; }
#define NFT_DEFINE_LOOKUP(subclass) \
subclass * subclass##_lookup(subclass##_h hl) { return subclass##_cast(nft_core_lookup(hl)); }
#define NFT_DEFINE_DISCARD(subclass) \
int subclass##_discard(subclass * sc) { return nft_core_discard((nft_core*) sc); }

// Note that static is a parameter, which can be empty.
#define NFT_DEFINE_WRAPPERS(subclass, static) \
static NFT_DEFINE_CAST(subclass)   \
static NFT_DEFINE_HANDLE(subclass) \
static NFT_DEFINE_LOOKUP(subclass) \
static NFT_DEFINE_DISCARD(subclass)

#endif // _nft_core_header
