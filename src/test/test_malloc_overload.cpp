/*
    Copyright 2005-2009 Intel Corporation.  All Rights Reserved.

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


#if __linux__
#define MALLOC_REPLACEMENT_AVAILABLE 1
#elif _WIN32
#define MALLOC_REPLACEMENT_AVAILABLE 2
#include "tbb/tbbmalloc_proxy.h"
#endif

#if MALLOC_REPLACEMENT_AVAILABLE

#include "harness_report.h"
#include "harness_assert.h"
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <new>

#if __linux__
#include <dlfcn.h>
#include <unistd.h> // for sysconf
#include <stdint.h> // for uintptr_t

#elif _WIN32
#include <stddef.h>
#if __MINGW32__
#include <unistd.h>
#else
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#endif

#endif /* OS selection */

#if _WIN32
// On Windows, the tricky way to print "done" is necessary to create 
// dependence on msvcpXX.dll, for sake of a regression test.
// On Linux, C++ RTL headers are undesirable because of breaking strict ANSI mode.
#include <string>
#endif


template<typename T>
static inline T alignDown(T arg, uintptr_t alignment) {
    return T( (uintptr_t)arg  & ~(alignment-1));
}
template<typename T>
static inline bool isAligned(T arg, uintptr_t alignment) {
    return 0==((uintptr_t)arg &  (alignment-1));
}

/* Below is part of MemoryAllocator.cpp. */

/*
 * The identifier to make sure that memory is allocated by scalable_malloc.
 */
const uint64_t theMallocUniqueID=0xE3C7AF89A1E2D8C1ULL; 

struct LargeObjectHeader {
    void        *unalignedResult;   /* The base of the memory returned from getMemory, this is what is used to return this to the OS */
    size_t       unalignedSize;     /* The size that was requested from getMemory */
    uint64_t     mallocUniqueID;    /* The field to check whether the memory was allocated by scalable_malloc */
    size_t       objectSize;        /* The size originally requested by a client */
};

/*
 * Objects of this size and larger are considered large objects.
 */
const uint32_t minLargeObjectSize = 8065;

/* end of inclusion from MemoryAllocator.cpp */

/* Correct only for large blocks, i.e. not smaller then minLargeObjectSize */
static bool scalableMallocLargeBlock(void *object, size_t size)
{
    ASSERT(size >= minLargeObjectSize, NULL);
#if MALLOC_REPLACEMENT_AVAILABLE == 2
    // Check that _msize works correctly
    ASSERT(_msize(object) >= size, NULL);
#endif

    LargeObjectHeader *h = (LargeObjectHeader*)((uintptr_t)object-sizeof(LargeObjectHeader));
    return h->mallocUniqueID==theMallocUniqueID && h->objectSize==size;
}

struct BigStruct {
    char f[minLargeObjectSize];
};

int main(int , char *[]) {
    void *ptr, *ptr1;

#if MALLOC_REPLACEMENT_AVAILABLE == 1
    if (NULL == dlsym(RTLD_DEFAULT, "scalable_malloc")) {
        REPORT("libtbbmalloc not found\nfail\n");
        return 1;
    }
#endif

    ptr = malloc(minLargeObjectSize);
    ASSERT(ptr!=NULL && scalableMallocLargeBlock(ptr, minLargeObjectSize), NULL);
    free(ptr);

    ptr = calloc(minLargeObjectSize, 2);
    ASSERT(ptr!=NULL && scalableMallocLargeBlock(ptr, minLargeObjectSize*2), NULL);
    ptr1 = realloc(ptr, minLargeObjectSize*10);
    ASSERT(ptr1!=NULL && scalableMallocLargeBlock(ptr1, minLargeObjectSize*10), NULL);
    free(ptr1);

#if MALLOC_REPLACEMENT_AVAILABLE == 1

    int ret = posix_memalign(&ptr, 1024, 3*minLargeObjectSize);
    ASSERT(0==ret && ptr!=NULL && scalableMallocLargeBlock(ptr, 3*minLargeObjectSize), NULL);
    free(ptr);

    ptr = memalign(128, 4*minLargeObjectSize);
    ASSERT(ptr!=NULL && scalableMallocLargeBlock(ptr, 4*minLargeObjectSize), NULL);
    free(ptr);

    ptr = valloc(minLargeObjectSize);
    ASSERT(ptr!=NULL && scalableMallocLargeBlock(ptr, minLargeObjectSize), NULL);
    free(ptr);

    long memoryPageSize = sysconf(_SC_PAGESIZE);
    int sz = 1024*minLargeObjectSize;
    ptr = pvalloc(sz);
    ASSERT(ptr!=NULL &&                // align size up to the page size
           scalableMallocLargeBlock(ptr, ((sz-1) | (memoryPageSize-1)) + 1), NULL);
    free(ptr);

    struct mallinfo info = mallinfo();
    // right now mallinfo initialized by zero
    ASSERT(!info.arena && !info.ordblks && !info.smblks && !info.hblks 
           && !info.hblkhd && !info.usmblks && !info.fsmblks 
           && !info.uordblks && !info.fordblks && !info.keepcost, NULL);

#elif MALLOC_REPLACEMENT_AVAILABLE == 2

    ptr = _aligned_malloc(minLargeObjectSize,16);
    ASSERT(ptr!=NULL && scalableMallocLargeBlock(ptr, minLargeObjectSize), NULL);

    ptr1 = _aligned_realloc(ptr, minLargeObjectSize*10,16);
    ASSERT(ptr1!=NULL && scalableMallocLargeBlock(ptr1, minLargeObjectSize*10), NULL);
    _aligned_free(ptr1);

#endif

    BigStruct *f = new BigStruct;
    ASSERT(f!=NULL && scalableMallocLargeBlock(f, sizeof(BigStruct)), NULL);
    delete f;

    f = new BigStruct[10];
    ASSERT(f!=NULL && scalableMallocLargeBlock(f, 10*sizeof(BigStruct)), NULL);
    delete []f;

    f = new(std::nothrow) BigStruct;
    ASSERT(f!=NULL && scalableMallocLargeBlock(f, sizeof(BigStruct)), NULL);
    delete f;

    f = new(std::nothrow) BigStruct[2];
    ASSERT(f!=NULL && scalableMallocLargeBlock(f, 2*sizeof(BigStruct)), NULL);
    delete []f;

#if _WIN32
    std::string stdstring = "done";
    const char* s = stdstring.c_str();
#else
    const char* s = "done";
#endif
    REPORT("%s\n", s);
    return 0;
}

#define HARNESS_NO_PARSE_COMMAND_LINE 1
#include "harness.h"

#else  /* !MALLOC_REPLACEMENT_AVAILABLE */
#include <stdio.h>

int main(int , char *[]) {
    printf("skip\n");
}
#endif /* !MALLOC_REPLACEMENT_AVAILABLE */
