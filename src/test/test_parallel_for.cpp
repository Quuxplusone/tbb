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

// Test for function template parallel_for.h

#include "tbb/parallel_for.h"
#include "tbb/atomic.h"
#include "harness_assert.h"
#include "harness.h"

static tbb::atomic<int> FooBodyCount;

//! An range object whose only public members are those required by the Range concept.
template<size_t Pad>
class FooRange {
    //! Start of range
    int start;

    //! Size of range
    int size;
    FooRange( int start_, int size_ ) : start(start_), size(size_) {
        zero_fill<char>(pad, Pad);
        pad[Pad-1] = 'x';
    }
    template<size_t Pad_> friend void Flog( int nthread );
    template<size_t Pad_> friend class FooBody;
    void operator&();

    char pad[Pad];
public:
    bool empty() const {return size==0;}
    bool is_divisible() const {return size>1;}
    FooRange( FooRange& original, tbb::split ) : size(original.size/2) {
        original.size -= size;
        start = original.start+original.size;
        ASSERT( original.pad[Pad-1]=='x', NULL );
        pad[Pad-1] = 'x';
    }
};

//! An range object whose only public members are those required by the parallel_for.h body concept.
template<size_t Pad>
class FooBody {
    static const int LIVE = 0x1234;
    tbb::atomic<int>* array;
    int state;
    friend class FooRange<Pad>;
    template<size_t Pad_> friend void Flog( int nthread );
    FooBody( tbb::atomic<int>* array_ ) : array(array_), state(LIVE) {}
public:
    ~FooBody() {
        --FooBodyCount;
        for( size_t i=0; i<sizeof(*this); ++i )
            reinterpret_cast<char*>(this)[i] = -1;
    }
    //! Copy constructor 
    FooBody( const FooBody& other ) : array(other.array), state(other.state) {
        ++FooBodyCount;
        ASSERT( state==LIVE, NULL );
    }
    void operator()( FooRange<Pad>& r ) const {
        for( int k=0; k<r.size; ++k )
            array[r.start+k]++;
    }
};

#include "tbb/tick_count.h"

static const int N = 1000;
static tbb::atomic<int> Array[N];

template<size_t Pad>
void Flog( int nthread ) {
    tbb::tick_count T0 = tbb::tick_count::now();
    for( int i=0; i<N; ++i ) {
        for ( int mode = 0; mode < 4; ++mode) 
        {
            FooRange<Pad> r( 0, i );
            const FooRange<Pad> rc = r;
            FooBody<Pad> f( Array );
            const FooBody<Pad> fc = f;
            memset( Array, 0, sizeof(Array) );
            FooBodyCount = 1;
            switch (mode) {
                case 0:
                    tbb::parallel_for( rc, fc );
                break;
                case 1:
                    tbb::parallel_for( rc, fc, tbb::simple_partitioner() );
                break;
                case 2:
                    tbb::parallel_for( rc, fc, tbb::auto_partitioner() );
                break;
                case 3: {
                    static tbb::affinity_partitioner affinity;
                    tbb::parallel_for( rc, fc, affinity );
                }
                break;
            }
            for( int j=0; j<i; ++j ) 
                ASSERT( Array[j]==1, NULL );
            for( int j=i; j<N; ++j ) 
                ASSERT( Array[j]==0, NULL );
            // Destruction of bodies might take a while, but there should be at most one body per thread
            // at this point.
            while( FooBodyCount>1 && FooBodyCount<=nthread )
                __TBB_Yield();
            ASSERT( FooBodyCount==1, NULL );
        }
    }
    tbb::tick_count T1 = tbb::tick_count::now();
    if( Verbose )
        REPORT("time=%g\tnthread=%d\tpad=%d\n",(T1-T0).seconds(),nthread,int(Pad));
}

// Testing parallel_for with step support
const size_t PFOR_BUFFER_TEST_SIZE = 1024;
// test_buffer has some extra items beyound right bound
const size_t PFOR_BUFFER_ACTUAL_SIZE = PFOR_BUFFER_TEST_SIZE + 1024; 
size_t pfor_buffer[PFOR_BUFFER_ACTUAL_SIZE];

template<typename T>
void TestFunction(T index){
    pfor_buffer[index]++;
}

#include <stdexcept> // std::invalid_argument
template <typename T>
void TestParallelForWithStepSupport()
{
    const T pfor_buffer_test_size = static_cast<T>(PFOR_BUFFER_TEST_SIZE);
    const T pfor_buffer_actual_size = static_cast<T>(PFOR_BUFFER_ACTUAL_SIZE);
    // Testing parallel_for with different step values
    for (T begin = 0; begin < pfor_buffer_test_size - 1; begin += pfor_buffer_test_size / 10 + 1) {
        T step;
        for (step = 1; step < pfor_buffer_test_size; step++) {
            memset(pfor_buffer, 0, pfor_buffer_actual_size * sizeof(size_t));
            tbb::parallel_for(begin, pfor_buffer_test_size, step, TestFunction<T>);
            // Verifying that parallel_for processed all items it should
            for (T i = begin; i < pfor_buffer_test_size; i = i + step) {
                ASSERT(pfor_buffer[i] == 1, "parallel_for didn't process all required elements");
                pfor_buffer[i] = 0;
            }
            // Verifying that no extra items were processed and right bound of array wasn't crossed
            for (T i = 0; i < pfor_buffer_actual_size; i++) {
                ASSERT(pfor_buffer[i] == 0, "parallel_for processed an extra element");
            }
        }
    }

    // Testing some corner cases
    tbb::parallel_for(static_cast<T>(2), static_cast<T>(1), static_cast<T>(1), TestFunction<T>);
#if !__TBB_EXCEPTION_HANDLING_TOTALLY_BROKEN
    try{
        tbb::parallel_for(static_cast<T>(1), static_cast<T>(100), static_cast<T>(0), TestFunction<T>);  // should cause std::invalid_argument
    }catch(std::invalid_argument){
        return;
    }
    ASSERT(0, "std::invalid_argument should be thrown");
#endif
}

// Exception support test
#define HARNESS_EH_SIMPLE_MODE 1
#include "tbb/tbb_exception.h"
#include "harness_eh.h"

void test_function_with_exception(size_t)
{
    ThrowTestException();
}

void TestExceptionsSupport()
{
    REMARK (__FUNCTION__);
    ResetEhGlobals();
    TRY();
        tbb::parallel_for((size_t)0, (size_t)PFOR_BUFFER_TEST_SIZE, (size_t)1, test_function_with_exception);
    CATCH_AND_ASSERT();
}

// Cancellation support test
void function_to_cancel(size_t ) {
    ++g_CurExecuted;
    CancellatorTask::WaitUntilReady();
}

class my_worker_pfor_step_task : public tbb::task
{
    tbb::task_group_context &my_ctx;

    tbb::task* execute () {
        tbb::parallel_for((size_t)0, (size_t)PFOR_BUFFER_TEST_SIZE, (size_t)1, function_to_cancel, my_ctx);
        
        return NULL;
    }
public:
    my_worker_pfor_step_task ( tbb::task_group_context &context) : my_ctx(context) { }
};

void TestCancellation()
{
    ResetEhGlobals();
    RunCancellationTest<my_worker_pfor_step_task, CancellatorTask>();
}

#include <cstdio>
#include "tbb/task_scheduler_init.h"
#include "harness_cpu.h"

__TBB_TEST_EXPORT
int main( int argc, char* argv[] ) {
    MinThread = 1;
    ParseCommandLine(argc,argv);
    if( MinThread<1 ) {
        REPORT("number of threads must be positive\n");
        exit(1);
    }
    for( int p=MinThread; p<=MaxThread; ++p ) {
        if( p>0 ) {
            tbb::task_scheduler_init init( p );
            Flog<1>(p);
            Flog<10>(p);
            Flog<100>(p);
            Flog<1000>(p);
            Flog<10000>(p);

            // Testing with different integer types
            TestParallelForWithStepSupport<short>();
            TestParallelForWithStepSupport<unsigned short>();
            TestParallelForWithStepSupport<int>();
            TestParallelForWithStepSupport<unsigned int>();
            TestParallelForWithStepSupport<long>();
            TestParallelForWithStepSupport<unsigned long>();
            TestParallelForWithStepSupport<long long>();
            TestParallelForWithStepSupport<unsigned long long>();
            TestParallelForWithStepSupport<size_t>();
#if !__TBB_EXCEPTION_HANDLING_BROKEN && !(__GNUC__==4 && __GNUC_MINOR__==1 && __TBB_ipf)
            TestExceptionsSupport();
#endif
            if (p>1) TestCancellation();
            // Test that all workers sleep when no work
            TestCPUUserTime(p);
        }
    }
#if __TBB_EXCEPTION_HANDLING_BROKEN || (__GNUC__==4 && __GNUC_MINOR__==1 && __TBB_ipf)
    REPORT("Warning: Exception handling tests are skipped due to a known issue.\n");
#endif
    REPORT("done\n");
    return 0;
}
