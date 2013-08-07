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
 ******************************************************************************
 */
#ifndef nft_core_header
#define nft_core_header

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
void       nft_core_discard(nft_core * this);
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
void subclass##_discard(subclass *);

// Note that static is a parameter, which can be empty.
#define NFT_DECLARE_HELPERS(subclass, static) \
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
void subclass##_discard(subclass * sc) { nft_core_discard((nft_core*) sc); }

// Note that static is a parameter, which can be empty.
#define NFT_DEFINE_HELPERS(subclass, static) \
static NFT_DEFINE_CAST(subclass)   \
static NFT_DEFINE_HANDLE(subclass) \
static NFT_DEFINE_LOOKUP(subclass) \
static NFT_DEFINE_DISCARD(subclass)


// Do NOT call these nft_handle_* functions unless you know what you are doing.
// In particular, note that nft_handle_lookup does not increment the reference
// count, so the pointer you get could become stale at any time.
nft_handle nft_handle_alloc(void * vp);
void     * nft_handle_lookup(nft_handle h);
void       nft_handle_delete(nft_handle h, void *vp);

#endif // nft_core_header
