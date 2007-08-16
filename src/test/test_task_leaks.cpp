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


#define  COUNT_TASK_NODES 1
#include "../tbb/task.cpp"

#include "tbb/atomic.h"
#include "harness_assert.h"
#include <cstdlib>

//------------------------------------------------------------------------
// Test for task::spawn_children and TaskList
//------------------------------------------------------------------------

tbb::atomic<int> Count;

#include "tbb/task_scheduler_init.h"
#include "harness.h"
using namespace tbb;
using namespace tbb::internal;

class TaskGenerator: public tbb::task {
    const int my_child_count;
    int my_depth;
public:
    TaskGenerator(int child_count, int depth) : my_child_count(child_count), my_depth(depth) {}
    /*override*/ tbb::task* execute() {
        if( my_depth>0 ) {
            tbb::task& c  = *new( tbb::task::allocate_continuation() ) tbb::empty_task;
            c.set_ref_count( my_child_count );
            recycle_as_child_of(c);
            for( int j=1; j<my_child_count; ++j ) {
                tbb::task& t = *new( c.allocate_child() ) TaskGenerator(my_child_count, my_depth-1);
                c.spawn(t);
            }
            --my_depth;
            return this;
        }
        return NULL;
    }
};

#include "harness_memory.h"
#include <math.h>

//! Tests whether task scheduler allows thieves to hoard task objects.
/** The test takes a while to run, so we run it only with the default
    number of threads. */
void TestTaskReclamation() {
    if( Verbose )
        printf("testing task reclamation\n");
    size_t initial_amount_of_memory = 0;
    double task_count_sum = 0;
    double task_count_sum_square = 0;
    double average, sigma;

    tbb::task_scheduler_init init;
    int N = 20;
    for( int i=0; i<N; ++i ) {
        // First N iterations fill internal buffers and collect initial statistics
        for( int j=0; j<10; ++j ) {
            tbb::task& t = *new( tbb::task::allocate_root() ) TaskGenerator(/*child_count=*/4, /*depth=*/6);
            tbb::task::spawn_root_and_wait(t);
        }

        size_t m = GetMemoryUsage();
        if( m-initial_amount_of_memory > 0)
            initial_amount_of_memory = m;

        tbb::internal::intptr n = (*reinterpret_cast<GenericScheduler**>(&init))->get_task_node_count( /*count_arena_workers=*/true );
        task_count_sum += n;
        task_count_sum_square += n*n;
    }
    // Calculate statistical values
    average = task_count_sum / N;
    sigma   = sqrt( (task_count_sum_square - task_count_sum*task_count_sum/N)/N );
    if( Verbose )
        printf("Average task count: %g, sigma: %g, sum: %g, square sum:%g \n", average, sigma, task_count_sum, task_count_sum_square);

    int error_count = 0;
    for( int i=0; i<500; ++i ) {
        // These iterations check for excessive memory use and unreasonable task count
        for( int j=0; j<10; ++j ) {
            tbb::task& t = *new( tbb::task::allocate_root() ) TaskGenerator(/*child_count=*/4, /*depth=*/6);
            tbb::task::spawn_root_and_wait(t);
        }
        tbb::internal::intptr n = (*reinterpret_cast<GenericScheduler**>(&init))->get_task_node_count( /*count_arena_workers=*/true );
        size_t m = GetMemoryUsage();

        if( (m-initial_amount_of_memory > 0) && (n > average+4*sigma) ) {
            ++error_count;
            // Use 4*sigma interval (for normal distribution, 3*sigma contains ~99% of values).
            // Issue a warning for the first couple of times, then errors
            printf( "%s: possible leak of up to %ld bytes; currently %ld cached task objects (iteration=%d)\n",
                    error_count>3?"Error":"Warning", long(m-initial_amount_of_memory), long(n), i );
            initial_amount_of_memory = m;
            if( error_count>5 ) break;
        } else {
            if( Verbose )
                printf( "Consumed %ld bytes and %ld objects (iteration=%d)\n", long(m), long(n), i );
        }
    }
}

//------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#if _WIN32||_WIN64
    // This initialization is normally done by DllMain in task.cpp,
    // but because this test directly includes task.cpp,
    // the initialization must be done explicitly.
    InitializeCriticalSection(&OneTimeInitializationCriticalSection);
#endif /* _WIN32||_WIN64 */
    srand(2);
    MinThread = 1;
    ParseCommandLine( argc, argv );
    TestTaskReclamation();
    printf("done\n");
    return 0;
}

