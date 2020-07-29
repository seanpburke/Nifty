/******************************************************************************
 * (C) Copyright Xenadyne, Inc. 2002-2013  All rights reserved.
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
 * File:  nft_gettime.h
 *
 * PURPOSE
 *
 * This file defines the gettime() call, which provides the current time
 * using either ftime(), gettimeofday() or clock_gettime(), depending on
 * your preference and what your operating system supports.
 *
 * To choose the time source, define one of these compilation symbols:
 *
 *	USE_FTIME		.. for ftime()
 *	USE_GETTIMEOFDAY	.. for gettimeofday()
 *	USE_CLOCKGETTIME	.. for clock_gettime()
 *
 * By default, ftime() is used. You can change the default by editing
 * the commented #define's below.
 *
 *****************************************************************************
 */
#ifndef _NFT_GETTIME_H_
#define _NFT_GETTIME_H_

// Select the default time source by uncommenting one of these defines:
//
#if !defined(USE_FTIME) && !defined(USE_GETTIMEOFDAY) && !defined(USE_CLOCKGETTIME)
#define USE_FTIME        1
// #define USE_GETTIMEOFDAY 1
// #define USE_CLOCKGETTIME 1
#endif

#if defined(USE_FTIME)
#include <sys/types.h>
#include <sys/timeb.h>
#elif defined(USE_GETTIMEOFDAY)
#include <sys/time.h>
#elif defined(USE_CLOCKGETTIME)
#include <time.h>
#endif

#ifndef NANOSEC
#define NANOSEC         1000000000
#endif

// Ensure that struct timespec is defined.
//
#ifndef _WIN32
#include <sys/time.h>
#else
struct	timespec {
	time_t tv_sec;
	long   tv_nsec;
};
#endif

// nft_gettime returns the current time as a struct timespec, converting if necessary.
//
static inline struct timespec
nft_gettime(void)
{
#if defined(USE_FTIME)
    struct timeb tb;
    ftime( &tb );
    return (struct timespec){ tb.time, tb.millitm * 1000000 };	/* milli to nano secs  */

#elif defined(USE_GETTIMEOFDAY)
    struct timeval tv;
    int rc = gettimeofday(&tv, NULL); assert(rc == 0);
    return (struct timespec){ tv.tv_sec, tv.tv_usec * 1000 };	/* micro to nano secs  */

#elif defined(USE_CLOCKGETTIME)
    struct timespec ts;
    int rc = clock_gettime(CLOCK_REALTIME, &ts); assert(rc == 0);
    return ts;
#endif
}

// Compare two timespecs, returning the difference in nanosec.
static inline int64_t
nft_timespec_comp(struct timespec now, struct timespec then)
{
    return (now.tv_sec  - then.tv_sec ) * NANOSEC + (now.tv_nsec - then.tv_nsec);
}

// Returns the normalized timespec.
static inline struct timespec
nft_timespec_norm(struct timespec ts)
{
    ts.tv_sec += ts.tv_nsec / NANOSEC;
    ts.tv_nsec = ts.tv_nsec % NANOSEC;
    return ts;
}

// Return the timespec offset by an interval.
static inline struct timespec
nft_timespec_add(struct timespec ts, struct timespec interval)
{
    ts.tv_nsec += interval.tv_nsec;
    ts.tv_sec  += interval.tv_sec;
    return nft_timespec_norm(ts);
}

#endif // _NFT_GETTIME_H_
