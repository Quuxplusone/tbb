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

#include "tbb/task.h"
#include "tbb/atomic.h"
#include "tbb/tbb_thread.h"
#include "harness_assert.h"
#include <cstdlib>

//------------------------------------------------------------------------
// Test for task::spawn_children and task_list
//------------------------------------------------------------------------

#if __TBB_TASK_DEQUE

class UnboundedlyRecursiveOnUnboundedStealingTask : public tbb::task {
    typedef UnboundedlyRecursiveOnUnboundedStealingTask this_type;

    this_type *m_Parent;
    const int m_Depth; 
    volatile bool m_GoAhead;

    volatile uintptr_t m_Anchor;

    // Well, virtually unboundedly, for any practical purpose
    static const int max_depth = 1000000; 

public:
    UnboundedlyRecursiveOnUnboundedStealingTask( this_type *parent = NULL, int depth = max_depth )
        : m_Parent(parent)
        , m_Depth(depth)
        , m_GoAhead(true)
        , m_Anchor(0)
    {}

    /*override*/
    tbb::task* execute() {
        if( !m_Parent || (m_Depth > 0 &&  m_Parent->m_GoAhead) ) {
            if ( m_Parent ) {
                // We are stolen, let our parent to start waiting for us
                m_Parent->m_GoAhead = false;
            }
            tbb::task &t = *new( tbb::task::allocate_child() ) this_type(this, m_Depth - 1);
            set_ref_count( 2 );
            spawn( t );
            // Give a willing thief a chance to steal
            for( int i = 0; i < 1000000 && m_GoAhead; ++i ) {
                m_Anchor += 1;
                __TBB_Yield();
            }
            // If our child has not been stolen yet, then prohibit it siring ones 
            // of its own (when this thread executes it inside the next wait_for_all)
            m_GoAhead = false;
            wait_for_all();
        }
        return NULL;
    }
}; // UnboundedlyRecursiveOnUnboundedStealingTask

#endif /* __TBB_TASK_DEQUE */


tbb::atomic<int> Count;

class RecursiveTask: public tbb::task {
    const int m_ChildCount;
    const int m_Depth; 
    //! Spawn tasks in list.  Exact method depends upon m_Depth&bit_mask.
    void SpawnList( tbb::task_list& list, int bit_mask ) {
        if( m_Depth&bit_mask ) {
            spawn(list);
            ASSERT( list.empty(), NULL );
            wait_for_all();
        } else {
            spawn_and_wait_for_all(list);
            ASSERT( list.empty(), NULL );
        }
    }
public:
    RecursiveTask( int child_count, int depth ) : m_ChildCount(child_count), m_Depth(depth) {}
    /*override*/ tbb::task* execute() {
        ++Count;
        if( m_Depth>0 ) {
            tbb::task_list list;
            ASSERT( list.empty(), NULL );
            for( int k=0; k<m_ChildCount; ++k ) {
                list.push_back( *new( tbb::task::allocate_child() ) RecursiveTask(m_ChildCount/2,m_Depth-1 ) );
                ASSERT( !list.empty(), NULL );
            }
            set_ref_count( m_ChildCount+1 );
            SpawnList( list, 1 );
            // Now try reusing this as the parent.
            set_ref_count(2);
            list.push_back( *new (tbb::task::allocate_child() ) tbb::empty_task() );
            SpawnList( list, 2 );
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

#if __TBB_TASK_DEQUE
void TestStealLimit( int nthread ) {
    REMARK( "testing steal limiting heuristics for %d threads\n", nthread );
    tbb::task_scheduler_init init(nthread);
    tbb::task &t = *new( tbb::task::allocate_root() ) UnboundedlyRecursiveOnUnboundedStealingTask();
    tbb::task::spawn_root_and_wait(t);
}
#endif /* __TBB_TASK_DEQUE */

//! Test task::spawn( task_list& )
void TestSpawnChildren( int nthread ) {
    REMARK("testing task::spawn_children for %d threads\n",nthread);
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
    REMARK("testing task::spawn_root_and_wait(task_list&) for %d threads\n",nthread);
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
    int m_ChildCount;
    int m_Depth;
    
public:
    TaskGenerator( int child_count, int depth ) : m_ChildCount(child_count), m_Depth(depth) {}
    ~TaskGenerator( ) { m_ChildCount = m_Depth = -125; }

    /*override*/ tbb::task* execute() {
        ASSERT( m_ChildCount>=0 && m_Depth>=0, NULL );
        if( m_Depth>0 ) {
            recycle_as_safe_continuation();
            set_ref_count( m_ChildCount+1 );
            for( int j=0; j<m_ChildCount; ++j ) {
                tbb::task& t = *new( allocate_child() ) TaskGenerator(m_ChildCount/2,m_Depth-1);
                spawn(t);
            }
            --m_Depth;
            __TBB_Yield();
            ASSERT( state()==recycle && ref_count()>0, NULL);
        }
        return NULL;
    }
};

void TestSafeContinuation( int nthread ) {
    REMARK("testing task::recycle_as_safe_continuation for %d threads\n",nthread);
    tbb::task_scheduler_init init(nthread);
    for( int j=8; j<33; ++j ) {
        TaskGenerator& p = *new( tbb::task::allocate_root() ) TaskGenerator(j,5);
        tbb::task::spawn_root_and_wait(p);
    }
}

//------------------------------------------------------------------------
// Test affinity interface
//------------------------------------------------------------------------
tbb::atomic<int> TotalCount;

struct AffinityTask: public tbb::task {
    const tbb::task::affinity_id expected_affinity_id; 
    bool noted;
    /** Computing affinities is NOT supported by TBB, and may disappear in the future.
        It is done here for sake of unit testing. */
    AffinityTask( int expected_affinity_id_ ) : 
        expected_affinity_id(tbb::task::affinity_id(expected_affinity_id_)), 
        noted(false) 
    {
        set_affinity(expected_affinity_id);
        ASSERT( 0u-expected_affinity_id>0u, "affinity_id not an unsigned integral type?" );  
        ASSERT( affinity()==expected_affinity_id, NULL );
    } 
    /*override*/ tbb::task* execute() {
        ++TotalCount;
        return NULL;
    }
    /*override*/ void note_affinity( affinity_id id ) {
        // There is no guarantee in TBB that a task runs on its affinity thread.
        // However, the current implementation does accidentally guarantee it
        // under certain conditions, such as the conditions here.
        // We exploit those conditions for sake of unit testing.
        ASSERT( id!=expected_affinity_id, NULL );
        ASSERT( !noted, "note_affinity_id called twice!" );
        ASSERT ( &tbb::task::self() == (tbb::task*)this, "Wrong innermost running task" );
        noted = true;
    }
};

/** Note: This test assumes a lot about the internal implementation of affinity.
    Do NOT use this as an example of good programming practice with TBB */
void TestAffinity( int nthread ) {
    TotalCount = 0;
    int n = tbb::task_scheduler_init::default_num_threads();
    if( n>nthread ) 
        n = nthread;
    tbb::task_scheduler_init init(n);
    tbb::empty_task* t = new( tbb::task::allocate_root() ) tbb::empty_task;
    tbb::task::affinity_id affinity_id = t->affinity();
    ASSERT( affinity_id==0, NULL );
    // Set ref_count for n-1 children, plus 1 for the wait.
    t->set_ref_count(n);
    // Spawn n-1 affinitized children.
    for( int i=1; i<n; ++i ) 
        t->spawn( *new(t->allocate_child()) AffinityTask(i) );
    if( n>1 ) {
        // Keep master from stealing
        while( TotalCount!=n-1 ) 
            __TBB_Yield();
    }
    // Wait for the children
    t->wait_for_all();
    t->destroy(*t);
}

struct NoteAffinityTask: public tbb::task {
    bool noted;
    NoteAffinityTask( int id ) : noted(false)
    {
        set_affinity(tbb::task::affinity_id(id));
    }
    ~NoteAffinityTask () {
        ASSERT (noted, "note_affinity has not been called");
    }
    /*override*/ tbb::task* execute() {
        return NULL;
    }
    /*override*/ void note_affinity( affinity_id /*id*/ ) {
        noted = true;
        ASSERT ( &tbb::task::self() == (tbb::task*)this, "Wrong innermost running task" );
    }
};

// This test checks one of the paths inside the scheduler by affinitizing the child task 
// to non-existent thread so that it is proxied in the local task pool but not retrieved 
// by another thread. 
void TestNoteAffinityContext() {
    tbb::task_scheduler_init init(1);
    tbb::empty_task* t = new( tbb::task::allocate_root() ) tbb::empty_task;
    t->set_ref_count(2);
    // This master in the absence of workers will have an affinity id of 1. 
    // So use another number to make the task get proxied.
    t->spawn( *new(t->allocate_child()) NoteAffinityTask(2) );
    t->wait_for_all();
    t->destroy(*t);
}

//------------------------------------------------------------------------
// Test that recovery actions work correctly for task::allocate_* methods
// when a task's constructor throws an exception.
//------------------------------------------------------------------------

static int TestUnconstructibleTaskCount;

struct ConstructionFailure {
};

#if _MSC_VER && !defined(__INTEL_COMPILER)
    // Suppress pointless "unreachable code" warning.
    #pragma warning (push)
    #pragma warning (disable: 4702)
#endif

//! Task that cannot be constructed.  
template<size_t N>
struct UnconstructibleTask: public tbb::empty_task {
    char space[N];
    UnconstructibleTask() {
        throw ConstructionFailure();
    }
};

#if _MSC_VER && !defined(__INTEL_COMPILER)
    #pragma warning (pop)
#endif

#define TRY_BAD_CONSTRUCTION(x)                  \
    {                                            \
        try {                                    \
            new(x) UnconstructibleTask<N>;       \
        } catch( const ConstructionFailure& ) {                                                    \
            ASSERT( parent()==original_parent, NULL ); \
            ASSERT( ref_count()==original_ref_count, "incorrectly changed ref_count" );\
            ++TestUnconstructibleTaskCount;      \
        }                                        \
    }

template<size_t N>
struct RootTaskForTestUnconstructibleTask: public tbb::task {
    tbb::task* execute() {
        tbb::task* original_parent = parent();
        ASSERT( original_parent!=NULL, NULL );
        int original_ref_count = ref_count();
        TRY_BAD_CONSTRUCTION( allocate_root() );
        TRY_BAD_CONSTRUCTION( allocate_child() );
        TRY_BAD_CONSTRUCTION( allocate_continuation() );
        TRY_BAD_CONSTRUCTION( allocate_additional_child_of(*this) );
        return NULL;
    }
};

template<size_t N>
void TestUnconstructibleTask() {
    TestUnconstructibleTaskCount = 0;
    tbb::task_scheduler_init init;
    tbb::task* t = new( tbb::task::allocate_root() ) RootTaskForTestUnconstructibleTask<N>;
    tbb::task::spawn_root_and_wait(*t);
    ASSERT( TestUnconstructibleTaskCount==4, NULL );
}

//------------------------------------------------------------------------
// Test for alignment problems with task objects.
//------------------------------------------------------------------------

#if _MSC_VER && !defined(__INTEL_COMPILER)
    // Workaround for pointless warning "structure was padded due to __declspec(align())
    #pragma warning (push)
    #pragma warning (disable: 4324)
#endif

//! Task with members of type T.
/** The task recursively creates tasks. */
template<typename T> 
class TaskWithMember: public tbb::task {
    T x;
    T y;
    unsigned char count;
    /*override*/ tbb::task* execute() {
        x = y;
        if( count>0 ) { 
            set_ref_count(2);
            tbb::task* t = new( tbb::task::allocate_child() ) TaskWithMember<T>(count-1);
            spawn_and_wait_for_all(*t);
        }
        return NULL;
    }
public:
    TaskWithMember( unsigned char n ) : count(n) {}
};

#if _MSC_VER && !defined(__INTEL_COMPILER)
    #pragma warning (pop)
#endif

template<typename T> 
void TestAlignmentOfOneClass() {
    typedef TaskWithMember<T> task_type;
    tbb::task* t = new( tbb::task::allocate_root() ) task_type(10);
    tbb::task::spawn_root_and_wait(*t);
}

#include "harness_m128.h"

void TestAlignment() {
    REMARK("testing alignment\n");
    tbb::task_scheduler_init init;
    // Try types that have variety of alignments
    TestAlignmentOfOneClass<char>();
    TestAlignmentOfOneClass<short>();
    TestAlignmentOfOneClass<int>();
    TestAlignmentOfOneClass<long>();
    TestAlignmentOfOneClass<void*>();
    TestAlignmentOfOneClass<float>();
    TestAlignmentOfOneClass<double>();
#if HAVE_m128
    TestAlignmentOfOneClass<__m128>();
#endif /* HAVE_m128 */
}

//------------------------------------------------------------------------
// Test for recursing on left while spawning on right
//------------------------------------------------------------------------

int Fib( int n );

struct RightFibTask: public tbb::task {
    int* y;
    const int n;
    RightFibTask( int* y_, int n_ ) : y(y_), n(n_) {}
    task* execute() {
        *y = Fib(n-1);
        return 0;
    } 
};

int Fib( int n ) {
    if( n<2 ) {
        return n;
    } else {
        // y actually does not need to be initialized.  It is initialized solely to suppress
        // a gratuitous warning "potentially uninitialized local variable". 
        int y=-1;
        tbb::task* root_task = new( tbb::task::allocate_root() ) tbb::empty_task;
        root_task->set_ref_count(2);
        root_task->spawn( *new( root_task->allocate_child() ) RightFibTask(&y,n) );
        int x = Fib(n-2);
        root_task->wait_for_all();
        tbb::task::self().destroy(*root_task);
        return y+x;
    }
}

void TestLeftRecursion( int p ) {
    REMARK("testing non-spawned roots for %d threads\n",p);
    tbb::task_scheduler_init init(p);
    int sum = 0; 
    for( int i=0; i<100; ++i )
        sum +=Fib(10);
    ASSERT( sum==5500, NULL );
}

//------------------------------------------------------------------------
// Test for computing with DAG of tasks.
//------------------------------------------------------------------------

class DagTask: public tbb::task {
    typedef unsigned long long number_t;
    const int i, j;
    number_t sum_from_left, sum_from_above;
    void check_sum( number_t sum ) {
        number_t expected_sum = 1;
        for( int k=i+1; k<=i+j; ++k ) 
            expected_sum *= k;
        for( int k=1; k<=j; ++k ) 
            expected_sum /= k;
        ASSERT(sum==expected_sum, NULL);
    }
public:
    DagTask *successor_to_below, *successor_to_right;
    DagTask( int i_, int j_ ) : i(i_), j(j_), sum_from_left(0), sum_from_above(0) {}
    task* execute() {
        __TBB_ASSERT( ref_count()==0, NULL );
        number_t sum = i==0 && j==0 ? 1 : sum_from_left+sum_from_above;
        check_sum(sum);
        ++execution_count;
        if( DagTask* t = successor_to_right ) {
            t->sum_from_left = sum;
            if( t->decrement_ref_count()==0 )
                // Test using spawn to evaluate DAG
                spawn( *t );
        }
        if( DagTask* t = successor_to_below ) {
            t->sum_from_above = sum;
            if( t->decrement_ref_count()==0 ) 
                // Test using bypass to evaluate DAG
                return t;
        } 
        return NULL;  
    }
    ~DagTask() {++destruction_count;}
    static tbb::atomic<int> execution_count;
    static tbb::atomic<int> destruction_count;
};

tbb::atomic<int> DagTask::execution_count;
tbb::atomic<int> DagTask::destruction_count;

void TestDag( int p ) {
    REMARK("testing evaluation of DAG for %d threads\n",p);
    tbb::task_scheduler_init init(p);
    DagTask::execution_count=0;
    DagTask::destruction_count=0;
    const int n = 10;
    DagTask* a[n][n];
    for( int i=0; i<n; ++i ) 
        for( int j=0; j<n; ++j )
            a[i][j] = new( tbb::task::allocate_root() ) DagTask(i,j);
    for( int i=0; i<n; ++i ) 
        for( int j=0; j<n; ++j ) {
            a[i][j]->successor_to_below = i+1<n ? a[i+1][j] : NULL;
            a[i][j]->successor_to_right = j+1<n ? a[i][j+1] : NULL;
            a[i][j]->set_ref_count((i>0)+(j>0));
        }
    a[n-1][n-1]->increment_ref_count();
    a[n-1][n-1]->spawn_and_wait_for_all(*a[0][0]);
    ASSERT( DagTask::execution_count == n*n - 1, NULL );
    a[n-1][n-1]->destroy(*a[n-1][n-1]);
    ASSERT( DagTask::destruction_count > n*n - p, NULL );
    while ( DagTask::destruction_count != n*n )
        __TBB_Yield();
}

#include "harness_barrier.h"

class RelaxedOwnershipTask: public tbb::task {
    tbb::task &m_taskToSpawn,
              &m_taskToDestroy,
              &m_taskToExecute;
    static Harness::SpinBarrier m_barrier;

    tbb::task* execute () {
        tbb::task &p = *parent();
        tbb::task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
        r.set_ref_count( 1 );
        m_barrier.wait();
        p.spawn( *new(p.allocate_child()) tbb::empty_task );
        p.spawn( *new(p.allocate_additional_child_of(p)) tbb::empty_task );
        p.spawn( m_taskToSpawn );
        p.destroy( m_taskToDestroy );
        r.spawn_and_wait_for_all( m_taskToExecute );
        p.destroy( r );
        return NULL;
    }
public:
    RelaxedOwnershipTask ( tbb::task& toSpawn, tbb::task& toDestroy, tbb::task& toExecute )
        : m_taskToSpawn(toSpawn)
        , m_taskToDestroy(toDestroy)
        , m_taskToExecute(toExecute)
    {}
    static void SetBarrier ( int numThreads ) { m_barrier.initialize( numThreads ); }
};

Harness::SpinBarrier RelaxedOwnershipTask::m_barrier;

void TestRelaxedOwnership( int p ) {
    if ( p < 2 )
        return;
#if __TEST_TBB_RML
    if( unsigned(p)>tbb::tbb_thread::hardware_concurrency() )
        return;
#endif
    REMARK("testing tasks exercising relaxed ownership freedom for %d threads\n", p);
    tbb::task_scheduler_init init(p);
    RelaxedOwnershipTask::SetBarrier(p);
    tbb::task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
    tbb::task_list tl;
    for ( int i = 0; i < p; ++i ) {
        tbb::task &tS = *new( r.allocate_child() ) tbb::empty_task,
                  &tD = *new( r.allocate_child() ) tbb::empty_task,
                  &tE = *new( r.allocate_child() ) tbb::empty_task;
        tl.push_back( *new( r.allocate_child() ) RelaxedOwnershipTask(tS, tD, tE) );
    }
    r.set_ref_count( 5 * p + 1 );
    r.spawn_and_wait_for_all( tl );
    r.destroy( r );
}

//------------------------------------------------------------------------
// Test for running TBB scheduler on user-created thread.
//------------------------------------------------------------------------

void RunSchedulerInstanceOnUserThread( int n_child ) {
    tbb::task* e = new( tbb::task::allocate_root() ) tbb::empty_task;
    e->set_ref_count(1+n_child);
    for( int i=0; i<n_child; ++i )
        e->spawn( *new(e->allocate_child()) tbb::empty_task );
    e->wait_for_all();
    e->destroy(*e);
}

void TestUserThread( int p ) {
    tbb::task_scheduler_init init(p);
    // Try with both 0 and 1 children.  Only the latter scenario permits stealing.
    for( int n_child=0; n_child<2; ++n_child ) {
        tbb::tbb_thread t( RunSchedulerInstanceOnUserThread, n_child );
        t.join();
    }
}


class TaskWithChildToSteal : public tbb::task {
    const int m_Depth; 
    volatile bool m_GoAhead;

public:
    TaskWithChildToSteal( int depth )
        : m_Depth(depth)
        , m_GoAhead(false)
    {}

    /*override*/
    tbb::task* execute() {
        m_GoAhead = true;
        if ( m_Depth > 0 ) {
            TaskWithChildToSteal &t = *new( tbb::task::allocate_child() ) TaskWithChildToSteal(m_Depth - 1);
            t.SpawnMeAndWaitOn( *this );
        }
        else
            Harness::Sleep(50); // The last task in chain sleeps for 50 ms
        return NULL;
    }

    void SpawnMeAndWaitOn( tbb::task& parent ) {
        parent.set_ref_count( 2 );
        parent.spawn( *this );
        while (!this->m_GoAhead )
            __TBB_Yield();
        parent.wait_for_all();
    }
}; // TaskWithChildToSteal

void TestDispatchLoopResponsiveness() {
    REMARK("testing that dispatch loops do not go into eternal sleep when all remaining children are stolen\n");
    // Recursion depth values test the following sorts of dispatch loops
    // 0 - master's outermost
    // 1 - worker's nested
    // 2 - master's nested
    tbb::task_scheduler_init init(2);
    tbb::task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
    for ( int depth = 0; depth < 3; ++depth ) {
        TaskWithChildToSteal &t = *new( r.allocate_child() ) TaskWithChildToSteal(depth);
        t.SpawnMeAndWaitOn(r);
    }
    r.destroy(r);
    // The success criteria of this test is not hanging
}

__TBB_TEST_EXPORT
int main(int argc, char* argv[]) {
    MinThread = 1;
    ParseCommandLine( argc, argv );
#if !__TBB_EXCEPTION_HANDLING_TOTALLY_BROKEN
    TestUnconstructibleTask<1>();
    TestUnconstructibleTask<10000>();
#endif
    TestAlignment();
    TestNoteAffinityContext();
    TestDispatchLoopResponsiveness();
    for( int p=MinThread; p<=MaxThread; ++p ) {
        TestSpawnChildren( p );
        TestSpawnRootList( p );
        TestSafeContinuation( p );
        TestLeftRecursion( p );
        TestDag( p );
        TestAffinity( p );
        TestUserThread( p );
#if __TBB_TASK_DEQUE
        TestStealLimit( p );
#endif /* __TBB_TASK_DEQUE */
#if __TBB_RELAXED_OWNERSHIP
        TestRelaxedOwnership( p );
#endif /* __TBB_RELAXED_OWNERSHIP */
    }
    REPORT("done\n");
    return 0;
}

