/******************************************************************************
 * (C) Copyright Xenadyne, Inc. 2014  All rights reserved.
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
 * File:  nft_string.h
 *
 * DESCRIPTION
 *
 * This package illustrates a very simple shared string class,
 * purely to demonstrate how subclasses are created from nft_core.
 * Refer to src/nft_string.c to see how this subclass is implemented.
 *
 ******************************************************************************
 */
#ifndef _NFT_STRING_H_
#define _NFT_STRING_H_

#include <nft_core.h>

// Here we demonstrate how to create a subclass derived from nft_core.
// This subclass provides a simple reference-counted string class.
// The README.txt file provides a detailed explanation of this.
//
typedef struct nft_string
{
    nft_core core;
    char   * string;
} nft_string;

// The macro below expands to the following declarations:
//
//   typedef struct nft_string_h * nft_string_h;
//   nft_string *   nft_string_cast(nft_core * p);
//   nft_string_h   nft_string_handle(const nft_string * s);
//   nft_string *   nft_string_lookup(nft_string_h h);
//   void           nft_string_discard(nft_string * s);
//
NFT_DECLARE_WRAPPERS(nft_string,)

// Declare public APIs for our string package
nft_string_h nft_string_new(const char * data);
void         nft_string_print(nft_string_h handle);

// These calls should only be used when subclassing nft_string.
nft_string * nft_string_create(const char * class, size_t size, const char * data);
void         nft_string_destroy(nft_core * p);

#endif // _NFT_STRING_H_
