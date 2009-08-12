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

#include "tbb/scalable_allocator.h"
#include "harness.h"
#include "harness_barrier.h"

// current minimal size of object that treated as large object
const size_t minLargeObjectSize = 8065;
// current difference between size of consequent cache bins
const int largeObjectCacheStep = 8*1024;

const int LARGE_MEM_SIZES_NUM = 10;
const size_t MByte = 1024*1024;

class AllocInfo {
    int *p;
    int val;
    int size;
public:
    AllocInfo() : p(NULL), val(0), size(0) {}
    explicit AllocInfo(int size) : p((int*)scalable_malloc(size*sizeof(int))),
                                   val(rand()), size(size) {
        ASSERT(p, NULL);
        for (int k=0; k<size; k++)
            p[k] = val;
    }
    void check() const {
        for (int k=0; k<size; k++)
            ASSERT(p[k] == val, NULL);
    }
    void clear() {
        scalable_free(p);
    }
};

class Run: NoAssign {
    const int allThreads;
    Harness::SpinBarrier *barrier;
public:
    static int largeMemSizes[LARGE_MEM_SIZES_NUM];

    Run( int allThreads, Harness::SpinBarrier *barrier ) : 
        allThreads(allThreads), barrier(barrier) {}
    void operator()( int /*mynum*/ ) const {
        testObjectCaching();
    }
private:
    void testObjectCaching() const {
        AllocInfo allocs[LARGE_MEM_SIZES_NUM];

        // push to maximal cache limit
        for (int i=0; i<2; i++) {
            const int sizes[] = { MByte/sizeof(int),
                                  (MByte-2*largeObjectCacheStep)/sizeof(int) };
            for (int q=0; q<2; q++) {
                size_t curr = 0;
                for (int j=0; j<LARGE_MEM_SIZES_NUM; j++, curr++)
                    new (allocs+curr) AllocInfo(sizes[q]);

                for (size_t j=0; j<curr; j++) {
                    allocs[j].check();
                    allocs[j].clear();
                }
            }
        }
        
        barrier->wait();

        // check caching correctness
        for (int i=0; i<1000; i++) {
            size_t curr = 0;
            for (int j=0; j<LARGE_MEM_SIZES_NUM-1; j++, curr++)
                new (allocs+curr) AllocInfo(largeMemSizes[j]);

            new (allocs+curr) 
                AllocInfo((int)(4*minLargeObjectSize +
                                2*minLargeObjectSize*(1.*rand()/RAND_MAX)));
            curr++;

            for (size_t j=0; j<curr; j++) {
                allocs[j].check();
                allocs[j].clear();
            }
        }
    }
};

int Run::largeMemSizes[LARGE_MEM_SIZES_NUM];

__TBB_TEST_EXPORT
int main(int argc, char* argv[]) {
    ParseCommandLine( argc, argv );

    for (int i=0; i<LARGE_MEM_SIZES_NUM; i++)
        Run::largeMemSizes[i] = (int)(minLargeObjectSize + 
                                      2*minLargeObjectSize*(1.*rand()/RAND_MAX));

    for( int p=MaxThread; p>=MinThread; --p ) {
        Harness::SpinBarrier *barrier = new Harness::SpinBarrier(p);
        NativeParallelFor( p, Run(p, barrier) );
        delete barrier;
    }

    REPORT("done\n");
    return 0;
}

/* On this platforms __TBB_machine_pause is defined in TBB library,
 * so have to provide it manually. 
 */
#if (_WIN32||_WIN64) && defined(_M_AMD64)

extern "C" void __TBB_machine_pause(__int32) { __TBB_Yield(); }

#elif __linux__ && __ia64__
extern "C" void __TBB_machine_pause(int32_t) { __TBB_Yield(); }

pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* As atomics are used only as atomic addition in Harness::SpinBarrier 
 * implementation, it's OK to have this mutex.
 */
int32_t __TBB_machine_fetchadd4__TBB_full_fence (volatile void *ptr, 
                                                 int32_t value)
{
    pthread_mutex_lock(&counter_mutex);
    int32_t result = *(int32_t*)ptr;
    *(int32_t*)ptr = result + value;
    pthread_mutex_unlock(&counter_mutex);
    return result;
}

#endif
