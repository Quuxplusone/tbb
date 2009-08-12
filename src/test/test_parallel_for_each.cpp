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

#include "tbb/parallel_for_each.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/atomic.h"
#include "harness.h"
#include "harness_iterator.h"

tbb::atomic<size_t> sum;
// This function is called via parallel_for_each
void TestFunction (size_t value) {
    sum += (unsigned int)value;
}

const size_t NUMBER_OF_ELEMENTS = 1000;

// Tests tbb::parallel_for_each functionality
template <typename Iterator>
void RunPForEachTests()
{
    size_t test_vector[NUMBER_OF_ELEMENTS + 1];

    sum = 0;
    size_t test_sum = 0;

    for (size_t i =0; i < NUMBER_OF_ELEMENTS; i++) { 
        test_vector[i] = i;
        test_sum += i;
    }
    test_vector[NUMBER_OF_ELEMENTS] = 1000000; // parallel_for_each shouldn't touch this element

    Iterator begin(&test_vector[0]);
    Iterator end(&test_vector[NUMBER_OF_ELEMENTS]);

    tbb::parallel_for_each(begin, end, TestFunction);
    ASSERT(sum == test_sum, "Not all items of test vector were processed by parallel_for_each");
    ASSERT(test_vector[NUMBER_OF_ELEMENTS] == 1000000, "parallel_for_each processed an extra element");
}

// Exception support test
#define HARNESS_EH_SIMPLE_MODE 1
#include "tbb/tbb_exception.h"
#include "harness_eh.h"

void test_function_with_exception(size_t)
{
    ThrowTestException();
}

template <typename Iterator>
void TestExceptionsSupport()
{
    REMARK (__FUNCTION__);
    size_t test_vector[NUMBER_OF_ELEMENTS + 1];

    for (size_t i = 0; i < NUMBER_OF_ELEMENTS; i++) { 
        test_vector[i] = i;
    }

    Iterator begin(&test_vector[0]);
    Iterator end(&test_vector[NUMBER_OF_ELEMENTS]);

    TRY();
        tbb::parallel_for_each(begin, end, test_function_with_exception);
    CATCH_AND_ASSERT();
}

// Cancellaton support test
void function_to_cancel(size_t ) {
    ++g_CurExecuted;
    CancellatorTask::WaitUntilReady();
}

template <typename Iterator>
class my_worker_pforeach_task : public tbb::task
{
    tbb::task_group_context &my_ctx;

    tbb::task* execute () {
        size_t test_vector[NUMBER_OF_ELEMENTS + 1];
        for (size_t i = 0; i < NUMBER_OF_ELEMENTS; i++) { 
            test_vector[i] = i;
        }
        Iterator begin(&test_vector[0]);
        Iterator end(&test_vector[NUMBER_OF_ELEMENTS]);

        tbb::parallel_for_each(begin, end, function_to_cancel);
        
        return NULL;
    }
public:
    my_worker_pforeach_task ( tbb::task_group_context &context) : my_ctx(context) { }
};

template <typename Iterator>
void TestCancellation()
{
    REMARK (__FUNCTION__);
    ResetEhGlobals();
    RunCancellationTest<my_worker_pforeach_task<Iterator>, CancellatorTask>();
}

#include "harness_cpu.h"

__TBB_TEST_EXPORT
int main( int argc, char* argv[] ) {
    MinThread=1;
    MaxThread=2;
    ParseCommandLine( argc, argv );
    if( MinThread<1 ) {
        REPORT("number of threads must be positive\n");
        exit(1);
    }

    for( int p=MinThread; p<=MaxThread; ++p ) {
        tbb::task_scheduler_init init( p );
        RunPForEachTests<Harness::RandomIterator<size_t> >();
        RunPForEachTests<Harness::InputIterator<size_t> >();
        RunPForEachTests<Harness::ForwardIterator<size_t> >();

#if !__TBB_EXCEPTION_HANDLING_BROKEN
        TestExceptionsSupport<Harness::RandomIterator<size_t> >();
        TestExceptionsSupport<Harness::InputIterator<size_t> >();
        TestExceptionsSupport<Harness::ForwardIterator<size_t> >();
#endif
        if (p > 1) {
            TestCancellation<Harness::RandomIterator<size_t> >();
            TestCancellation<Harness::InputIterator<size_t> >();
            TestCancellation<Harness::ForwardIterator<size_t> >();
        }
        // Test that all workers sleep when no work
        TestCPUUserTime(p);
    }
#if __TBB_EXCEPTION_HANDLING_BROKEN
    REPORT("Warning: Exception handling tests are skipped due to a known issue.\n");
#endif
    REPORT("done\n");
    return 0;
}
