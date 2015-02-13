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
 * File:  nft_handle.h
 *
 * DESCRIPTION
 *
 * Provides handle-management support for nft_core.
 *
 ******************************************************************************
 */
#ifndef _NFT_HANDLE_H_
#define _NFT_HANDLE_H_

// These macros set the initial and maximum size of the handle map, log-base-2.
// The defaults below start the map at 1K handles, with a maximum of 1M handles.
// If the number of active handles reaches the maximum, nft_core_create will
// return NULL, just as if memory were exhausted.
//
#ifndef NFT_HMAPSZINI
#define NFT_HMAPSZINI 10
#endif
#ifndef NFT_HMAPSZMAX
#define NFT_HMAPSZMAX 20
#endif

int          nft_handle_init(void);
nft_handle   nft_handle_alloc(nft_core * object);
nft_core   * nft_handle_lookup(nft_handle handle);
int          nft_handle_discard(nft_core * object);
void         nft_handle_apply(void (*function)(nft_core *, const char *, void *), const char * class, void * argument);

#endif // _NFT_HANDLE_H_
