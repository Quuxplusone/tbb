/*
    Copyright 2005-2007 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#include "tbb/cache_aligned_allocator.h"
#include "tbb_misc.h"
#include <cstdlib>

#if _WIN32||_WIN64
#include <windows.h>
#else
#include <dlfcn.h>
#endif /* _WIN32||_WIN64 */

namespace tbb {

namespace internal {

//! Dummy routine used for first indirect call via MallocHandler.
static void* DummyMalloc( size_t size );

//! Dummy routine used for first indirect call via FreeHandler.
static void DummyFree( void * ptr );

//! Handler for memory allocation
static void* (*MallocHandler)( size_t size ) = &DummyMalloc;

//! Handler for memory deallocation
static void (*FreeHandler)( void* pointer ) = &DummyFree;

//! Table describing the how to link the handlers.
static const DynamicLinkDescriptor MallocLinkTable[] = {
    {"scalable_malloc",ADDRESS_OF_HANDLER(&MallocHandler)},
    {"scalable_free",ADDRESS_OF_HANDLER(&FreeHandler)}
};

#if TBB_DO_ASSERT
#define DEBUG_SUFFIX "_debug"
#else
#define DEBUG_SUFFIX
#endif /* TBB_DO_ASSERT */

// MALLOCLIB_NAME is the name of the TBB memory allocator library.
#if _WIN32||_WIN64
#define MALLOCLIB_NAME "tbbmalloc" DEBUG_SUFFIX ".dll"
#elif __APPLE__
#define MALLOCLIB_NAME "libtbbmalloc" DEBUG_SUFFIX ".dylib"
#elif __linux__
#define MALLOCLIB_NAME "libtbbmalloc" DEBUG_SUFFIX ".so"
#else
#error Unknown OS
#endif

//! Initialize the allocation/free handler pointers.
/** Caller is responsible for ensuring this routine is called exactly once.
    The routine attempts to dynamically link with the TBB memory allocator.
    If that allocator is not found, it links to malloc and free. */
void initialize_cache_aligned_allocator() {
    __TBB_ASSERT( MallocHandler==&DummyMalloc, NULL );
    bool success = FillDynamicLinks( MALLOCLIB_NAME, MallocLinkTable, 2 );
    if( !success ) {
        // If unsuccessful, set the handlers to the default routines.
        // This must be done now, and not before FillDynanmicLinks runs, because if other
        // threads call the handlers, we want them to go through the DoOneTimeInitializations logic,
        // which forces them to wait.
        FreeHandler = &free;
        MallocHandler = &malloc;
    }
    PrintExtraVersionInfo( "ALLOCATOR", success?"scalable_malloc":"malloc" );
}

//! Defined in task.cpp
extern void DoOneTimeInitializations();

//! Executed on very first call throught MallocHandler
static void* DummyMalloc( size_t size ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( MallocHandler!=&DummyMalloc, NULL );
    return (*MallocHandler)( size );
}

//! Executed on very first call throught FreeHandler
static void DummyFree( void * ptr ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( FreeHandler!=&DummyFree, NULL );
    (*FreeHandler)( ptr );
}

static size_t NFS_LineSize = 128;

size_t NFS_GetLineSize() {
    return NFS_LineSize;
}

//! Requests for blocks this size and higher are handled via malloc/free,
const size_t BigSize = 4096;

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning( push )
// unary minus operator applied to unsigned type, result still unsigned
#pragma warning( disable: 4146 )
#endif /* _MSC_VER && !defined(__INTEL_COMPILER) */

void* NFS_Allocate( size_t n, size_t element_size, void* hint ) {
    using namespace internal;
    size_t m = NFS_LineSize;
    __TBB_ASSERT( m<=NFS_MaxLineSize, "illegal value for NFS_LineSize" );
    __TBB_ASSERT( (m & m-1)==0, "must be power of two" );
    size_t bytes = n*element_size;
    unsigned char* base;
    if( bytes<n || bytes+m<bytes || !(base=(unsigned char*)(bytes>=BigSize?malloc(m+bytes):(*MallocHandler)(m+bytes))) ) {
        // Overflow
        throw std::bad_alloc();
    }
    // Round up to next line
    unsigned char* result = (unsigned char*)((uintptr)(base+m)&-m);
    // Record where block actually starts.  Use low order bit to record whether we used malloc or MallocHandler.
    ((uintptr*)result)[-1] = uintptr(base)|(bytes>=BigSize);
    return result;
}

void NFS_Free( void* p ) {
    if( p ) {
        __TBB_ASSERT( (uintptr)p>=0x4096, "attempt to free block not obtained from cache_aligned_allocator" );
        using namespace internal;
        // Recover where block actually starts
        unsigned char* base = ((unsigned char**)p)[-1];
        __TBB_ASSERT( (void*)((uintptr)(base+NFS_LineSize)&-NFS_LineSize)==p, "not allocated by NFS_Allocate?" );
        if( uintptr(base)&1 ) {
            // Is a big block - use free
            free(base-1);
        } else {
            // Is a small block - use scalable allocator
            (*FreeHandler)( base );
        }
    }
}

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning( pop )
#endif /* _MSC_VER && !defined(__INTEL_COMPILER) */

} // namespace internal

} // namespace tbb
