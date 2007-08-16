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

#include "tbb/task.h"
#include "tbb/atomic.h"
#include "harness_assert.h"
#include <cstdlib>

//------------------------------------------------------------------------
// Test for task::spawn_children and task_list
//------------------------------------------------------------------------

tbb::atomic<int> Count;

class RecursiveTask: public tbb::task {
    const int my_child_count;
    const int my_depth; 
    //! Spawn tasks in list.  Exact method depends upon my_depth&bit_mask.
    void spawn_list( tbb::task_list& list, int bit_mask ) {
        if( my_depth&bit_mask ) {
            spawn(list);
            ASSERT( list.empty(), NULL );
            wait_for_all();
        } else {
            spawn_and_wait_for_all(list);
            ASSERT( list.empty(), NULL );
        }
    }
public:
    RecursiveTask( int child_count, int depth ) : my_child_count(child_count), my_depth(depth) {}
    /*override*/ tbb::task* execute() {
        ++Count;
        if( my_depth>0 ) {
            tbb::task_list list;
            ASSERT( list.empty(), NULL );
            for( int k=0; k<my_child_count; ++k ) {
                list.push_back( *new( tbb::task::allocate_child() ) RecursiveTask(my_child_count/2,my_depth-1 ) );
                ASSERT( !list.empty(), NULL );
            }
            set_ref_count( my_child_count+1 );
            spawn_list( list, 1 );
            // Now try reusing this as the parent.
            set_ref_count(2);
            list.push_back( *new (tbb::task::allocate_child() ) tbb::empty_task() );
            spawn_list( list, 2 );
        }
        return NULL;
    }
};

//! Compute what Count should be after RecursiveTask(child_count,depth) runs.
static int Expected( int child_count, int depth ) {
    return depth<=0 ? 1 : 1+child_count*Expected(child_count/2,depth-1);
}

#include "tbb/task_scheduler_init.h"
#include "harness.h"

//! Test task::spawn( task_list& )
void TestSpawnChildren( int nthread ) {
    if( Verbose ) 
        printf("testing task::spawn_children for %d threads\n",nthread);
    tbb::task_scheduler_init init(nthread);
    for( int j=0; j<50; ++j ) {
        Count = 0;
        RecursiveTask& p = *new( tbb::task::allocate_root() ) RecursiveTask(j,4);
        tbb::task::spawn_root_and_wait(p);
        int expected = Expected(j,4);
        ASSERT( Count==expected, NULL );
    }
}

//! Test task::spawn_root_and_wait( task_list& )
void TestSpawnRootList( int nthread ) {
    if( Verbose ) 
        printf("testing task::spawn_root_and_wait(task_list&) for %d threads\n",nthread);
    tbb::task_scheduler_init init(nthread);
    for( int j=0; j<5; ++j )
        for( int k=0; k<10; ++k ) {
            Count = 0;
            tbb::task_list list; 
            for( int i=0; i<k; ++i )
                list.push_back( *new( tbb::task::allocate_root() ) RecursiveTask(j,4) );
            tbb::task::spawn_root_and_wait(list);
            int expected = k*Expected(j,4);
            ASSERT( Count==expected, NULL );
        }    
}

//------------------------------------------------------------------------
// Test for task::recycle_as_safe_continuation
//------------------------------------------------------------------------

class TaskGenerator: public tbb::task {
    int my_child_count;
    int my_depth;
    
public:
    TaskGenerator( int child_count, int depth ) : my_child_count(child_count), my_depth(depth) {}
    ~TaskGenerator( ) { my_child_count = my_depth = -125; }

    /*override*/ tbb::task* execute() {
        ASSERT( my_child_count>=0 && my_depth>=0, NULL );
        if( my_depth>0 ) {
            recycle_as_safe_continuation();
            set_ref_count( my_child_count+1 );
            for( int j=0; j<my_child_count; ++j ) {
                tbb::task& t = *new( allocate_child() ) TaskGenerator(my_child_count/2,my_depth-1);
                spawn(t);
            }
            --my_depth;
#if __linux__||__APPLE__
            sched_yield();
#else
            Sleep(0);
#endif /* __linux__ */
            ASSERT( state()==recycle && ref_count()>0, NULL);
        }
        return NULL;
    }
};

void TestSafeContinuation( int nthread ) {
    if( Verbose ) 
        printf("testing task::recycle_as_safe_continuation for %d threads\n",nthread);
    tbb::task_scheduler_init init(nthread);
    for( int j=8; j<33; ++j ) {
        TaskGenerator& p = *new( tbb::task::allocate_root() ) TaskGenerator(j,5);
        tbb::task::spawn_root_and_wait(p);
    }
}

//------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    srand(2);
    MinThread = 1;
    ParseCommandLine( argc, argv );
    for( int p=MinThread; p<=MaxThread; ++p ) {
        TestSpawnChildren( p );
        TestSpawnRootList( p );
        TestSafeContinuation( p );
    }
    printf("done\n");
    return 0;
}

