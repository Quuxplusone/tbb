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

#include <limits.h> // for INT_MAX
#include "tbb/task_scheduler_init.h"
#include "tbb/tbb_exception.h"
#include "tbb/task.h"
#include "tbb/atomic.h"
#include "tbb/parallel_for.h"
#include "tbb/parallel_reduce.h"
#include "tbb/parallel_do.h"
#include "tbb/pipeline.h"
#include "tbb/parallel_scan.h"
#include "tbb/blocked_range.h"
#include "harness_assert.h"

#if __TBB_EXCEPTIONS && !__TBB_EXCEPTION_HANDLING_TOTALLY_BROKEN

#define FLAT_RANGE  100000
#define FLAT_GRAIN  1000
#define NESTING_RANGE  100
#define NESTING_GRAIN  10
#define NESTED_RANGE  (FLAT_RANGE / NESTING_RANGE)
#define NESTED_GRAIN  (FLAT_GRAIN / NESTING_GRAIN)

tbb::atomic<intptr_t> g_FedTasksCount; // number of tasks added by parallel_do feeder

inline intptr_t Existed () { return INT_MAX; }

#include "harness_eh.h"

inline void ResetGlobals (  bool throwException = true, bool flog = false ) {
    ResetEhGlobals( throwException, flog );
    g_FedTasksCount = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Tests for tbb::parallel_for and tbb::parallel_reduce

typedef size_t count_type;
typedef tbb::blocked_range<count_type> range_type;

inline intptr_t NumSubranges ( intptr_t length, intptr_t grain ) {
    intptr_t n = 1;
    for( ; length > grain; length -= length >> 1 )
        n *= 2;
    return n;
}

template<class Body>
intptr_t TestNumSubrangesCalculation ( intptr_t length, intptr_t grain, intptr_t nested_length, intptr_t nested_grain ) {
    ResetGlobals();
    g_ThrowException = false;
    intptr_t nestingCalls = NumSubranges(length, grain),
             nestedCalls = NumSubranges(nested_length, nested_grain),
             maxExecuted = nestingCalls * (nestedCalls + 1);
    tbb::parallel_for( range_type(0, length, grain), Body() );
    ASSERT (g_CurExecuted == maxExecuted, "Wrong estimation of bodies invocation count");
    return maxExecuted;
}

class NoThrowParForBody {
public:
    void operator()( const range_type& r ) const {
        volatile long x;
        count_type end = r.end();
        for( count_type i=r.begin(); i<end; ++i )
            x = 0;
    }
};

void Test0 () {
    ResetGlobals();
    tbb::simple_partitioner p;
    for( size_t i=0; i<10; ++i ) {
        tbb::parallel_for( range_type(0, 0, 1), NoThrowParForBody() );
        tbb::parallel_for( range_type(0, 0, 1), NoThrowParForBody(), p );
        tbb::parallel_for( range_type(0, 128, 8), NoThrowParForBody() );
        tbb::parallel_for( range_type(0, 128, 8), NoThrowParForBody(), p );
    }
} // void Test0 ()

//! Template that creates a functor suitable for parallel_reduce from a functor for parallel_for.
template<typename ParForBody>
class SimpleParReduceBody: NoAssign {
    ParForBody m_Body;
public:
    void operator()( const range_type& r ) const { m_Body(r); }
    SimpleParReduceBody() {}
    SimpleParReduceBody( SimpleParReduceBody& left, tbb::split ) : m_Body(left.m_Body) {}
    void join( SimpleParReduceBody& /*right*/ ) {}
}; // SimpleParReduceBody

//! Test parallel_for and parallel_reduce for a given partitioner.
/** The Body need only be suitable for a parallel_for. */
template<typename ParForBody, typename Partitioner>
void TestParallelLoopAux( Partitioner& partitioner ) {
    for( int i=0; i<2; ++i ) {
        ResetGlobals();
        TRY();
            if( i==0 )
                tbb::parallel_for( range_type(0, FLAT_RANGE, FLAT_GRAIN), ParForBody(), partitioner );
            else {
                SimpleParReduceBody<ParForBody> rb;
                tbb::parallel_reduce( range_type(0, FLAT_RANGE, FLAT_GRAIN), rb, partitioner );
            }
        CATCH_AND_ASSERT();
        ASSERT (exceptionCaught, "No exception thrown from the nesting parallel_for");
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
        ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
        if ( !g_SolitaryException )
            ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    }
}

//! Test with parallel_for and parallel_reduce, over all three kinds of partitioners.
/** The Body only needs to be suitable for tbb::parallel_for. */
template<typename Body>
void TestParallelLoop() {
    // The simple and auto partitioners should be const, but not the affinity partitioner.
    const tbb::simple_partitioner p0;
    TestParallelLoopAux<Body>( p0 );
    const tbb::auto_partitioner p1;
    TestParallelLoopAux<Body>( p1 );
    tbb::affinity_partitioner p2;
    TestParallelLoopAux<Body>( p2 );
}

class SimpleParForBody: NoAssign {
public:
    void operator()( const range_type& r ) const {
        Harness::ConcurrencyTracker ct;
        volatile long x;
        for( count_type i = r.begin(); i != r.end(); ++i )
            x = 0;
        ++g_CurExecuted;
        WaitUntilConcurrencyPeaks();
        ThrowTestException(1);
    }
};

void Test1() {
    TestParallelLoop<SimpleParForBody>();
} // void Test1 ()

class NestingParForBody: NoAssign {
public:
    void operator()( const range_type& ) const {
        Harness::ConcurrencyTracker ct;
        ++g_CurExecuted;
        tbb::parallel_for( tbb::blocked_range<size_t>(0, NESTED_RANGE, NESTED_GRAIN), SimpleParForBody() );
    }
};

//! Uses parallel_for body containing a nested parallel_for with the default context not wrapped by a try-block.
/** Nested algorithms are spawned inside the new bound context by default. Since
    exceptions thrown from the nested parallel_for are not handled by the caller
    (nesting parallel_for body) in this test, they will cancel all the sibling nested
    algorithms. **/
void Test2 () {
    TestParallelLoop<NestingParForBody>();
} // void Test2 ()

class NestingParForBodyWithIsolatedCtx {
public:
    void operator()( const range_type& ) const {
        tbb::task_group_context ctx(tbb::task_group_context::isolated);
        ++g_CurExecuted;
        tbb::parallel_for( tbb::blocked_range<size_t>(0, NESTED_RANGE, NESTED_GRAIN), SimpleParForBody(), tbb::simple_partitioner(), ctx );
    }
};

//! Uses parallel_for body invoking a nested parallel_for with an isolated context without a try-block.
/** Even though exceptions thrown from the nested parallel_for are not handled
    by the caller in this test, they will not affect sibling nested algorithms
    already running because of the isolated contexts. However because the first
    exception cancels the root parallel_for only the first g_NumThreads subranges
    will be processed (which launch nested parallel_fors) **/
void Test3 () {
    ResetGlobals();
    typedef NestingParForBodyWithIsolatedCtx body_type;
    intptr_t  nestedCalls = NumSubranges(NESTED_RANGE, NESTED_GRAIN),
            minExecuted = (g_NumThreads - 1) * nestedCalls;
    TRY();
        tbb::parallel_for( range_type(0, NESTING_RANGE, NESTING_GRAIN), body_type() );
    CATCH_AND_ASSERT();
    ASSERT (exceptionCaught, "No exception thrown from the nesting parallel_for");
    if ( g_SolitaryException ) {
        ASSERT (g_CurExecuted > minExecuted, "Too few tasks survived exception");
        ASSERT (g_CurExecuted <= minExecuted + (g_ExecutedAtCatch + g_NumThreads), "Too many tasks survived exception");
    }
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test3 ()

class NestingParForExceptionSafeBody {
public:
    void operator()( const range_type& ) const {
        tbb::task_group_context ctx(tbb::task_group_context::isolated);
        TRY();
            tbb::parallel_for( tbb::blocked_range<size_t>(0, NESTED_RANGE, NESTED_GRAIN), SimpleParForBody(), tbb::simple_partitioner(), ctx );
        CATCH();
    }
};

//! Uses parallel_for body invoking a nested parallel_for (with default bound context) inside a try-block.
/** Since exception(s) thrown from the nested parallel_for are handled by the caller
    in this test, they do not affect neither other tasks of the the root parallel_for
    nor sibling nested algorithms. **/
void Test4 () {
    ResetGlobals( true, true );
    intptr_t  nestedCalls = NumSubranges(NESTED_RANGE, NESTED_GRAIN),
            nestingCalls = NumSubranges(NESTING_RANGE, NESTING_GRAIN),
            maxExecuted = nestingCalls * nestedCalls;
    TRY();
        tbb::parallel_for( range_type(0, NESTING_RANGE, NESTING_GRAIN), NestingParForExceptionSafeBody() );
    CATCH();
    ASSERT (!exceptionCaught, "All exceptions must have been handled in the parallel_for body");
    intptr_t  minExecuted = 0;
    if ( g_SolitaryException ) {
        minExecuted = maxExecuted - nestedCalls;
        ASSERT (g_Exceptions == 1, "No exception registered");
        ASSERT (g_CurExecuted >= minExecuted, "Too few tasks executed");
        ASSERT (g_CurExecuted <= minExecuted + g_NumThreads, "Too many tasks survived exception");
    }
    else {
        minExecuted = g_Exceptions;
        ASSERT (g_Exceptions > 1 && g_Exceptions <= nestingCalls, "Unexpected actual number of exceptions");
        ASSERT (g_CurExecuted >= minExecuted, "Too many executed tasks reported");
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived multiple exceptions");
        ASSERT (g_CurExecuted <= nestingCalls * (1 + g_NumThreads), "Too many tasks survived exception");
    }
} // void Test4 ()

class ParForBodyToCancel {
public:
    void operator()( const range_type& ) const {
        ++g_CurExecuted;
        CancellatorTask::WaitUntilReady();
    }
};

template<class B>
class ParForLauncherTask : public tbb::task {
    tbb::task_group_context &my_ctx;

    tbb::task* execute () {
        tbb::parallel_for( range_type(0, FLAT_RANGE, FLAT_GRAIN), B(), tbb::simple_partitioner(), my_ctx );
        return NULL;
    }
public:
    ParForLauncherTask ( tbb::task_group_context& ctx ) : my_ctx(ctx) {}
};

//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
void Test5 () {
    ResetGlobals( false );
    RunCancellationTest<ParForLauncherTask<ParForBodyToCancel>, CancellatorTask>( NumSubranges(FLAT_RANGE, FLAT_GRAIN) / 4 );
    ASSERT (g_CurExecuted < g_ExecutedAtCatch + g_NumThreads, "Too many tasks were executed after cancellation");
} // void Test5 ()

class CancellatorTask2 : public tbb::task {
    tbb::task_group_context &m_GroupToCancel;

    tbb::task* execute () {
        Harness::ConcurrencyTracker ct;
        WaitUntilConcurrencyPeaks();
        m_GroupToCancel.cancel_group_execution();
        g_ExecutedAtCatch = g_CurExecuted;
        return NULL;
    }
public:
    CancellatorTask2 ( tbb::task_group_context& ctx, intptr_t ) : m_GroupToCancel(ctx) {}
};

class ParForBodyToCancel2 {
public:
    void operator()( const range_type& ) const {
        ++g_CurExecuted;
        Harness::ConcurrencyTracker ct;
        // The test will hang (and be timed out by the test system) if is_cancelled() is broken
        while( !tbb::task::self().is_cancelled() )
            __TBB_Yield();
    }
};

//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
/** This version also tests task::is_cancelled() method. **/
void Test6 () {
    ResetGlobals();
    RunCancellationTest<ParForLauncherTask<ParForBodyToCancel2>, CancellatorTask2>();
    ASSERT (g_ExecutedAtCatch < g_NumThreads, "Somehow worker tasks started their execution before the cancellator task");
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch, "Some tasks were executed after cancellation");
} // void Test6 ()

////////////////////////////////////////////////////////////////////////////////
// Regression test based on the contribution by the author of the following forum post:
// http://softwarecommunity.intel.com/isn/Community/en-US/forums/thread/30254959.aspx

#define LOOP_COUNT 16
#define MAX_NESTING 3
#define REDUCE_RANGE 1024
#define REDUCE_GRAIN 256

class Worker {
public:
    void DoWork (int & result, int nest);
};

class RecursiveParReduceBodyWithSharedWorker {
    Worker * m_SharedWorker;
    int m_NestingLevel;
    int m_Result;
public:
    RecursiveParReduceBodyWithSharedWorker ( RecursiveParReduceBodyWithSharedWorker& src, tbb::split )
        : m_SharedWorker(src.m_SharedWorker)
        , m_NestingLevel(src.m_NestingLevel)
        , m_Result(0)
    {}
    RecursiveParReduceBodyWithSharedWorker ( Worker *w, int nesting )
        : m_SharedWorker(w)
        , m_NestingLevel(nesting)
        , m_Result(0)
    {}

    void operator() ( const tbb::blocked_range<size_t>& r ) {
        for (size_t i = r.begin (); i != r.end (); ++i) {
            int result = 0;
            m_SharedWorker->DoWork (result, m_NestingLevel);
            m_Result += result;
        }
    }
    void join (const RecursiveParReduceBodyWithSharedWorker & x) {
        m_Result += x.m_Result;
    }
    int result () { return m_Result; }
};

void Worker::DoWork ( int& result, int nest ) {
    ++nest;
    if ( nest < MAX_NESTING ) {
        RecursiveParReduceBodyWithSharedWorker rt (this, nest);
        tbb::parallel_reduce (tbb::blocked_range<size_t>(0, REDUCE_RANGE, REDUCE_GRAIN), rt);
        result = rt.result ();
    }
    else
        ++result;
}

//! Regression test for hanging that occurred with the first version of cancellation propagation
void Test7 () {
    Worker w;
    int result = 0;
    w.DoWork (result, 0);
    ASSERT ( result == 1048576, "Wrong calculation result");
}

void RunParForAndReduceTests () {
    REMARK( "parallel for and reduce tests" );
    tbb::task_scheduler_init init (g_NumThreads);
    g_Master = Harness::CurrentTid();

    Test0();
#if !__TBB_EXCEPTION_HANDLING_BROKEN
    Test1();
    Test3();
    Test4();
#endif
    Test5();
    Test6();
    Test7();
}

////////////////////////////////////////////////////////////////////////////////
// Tests for tbb::parallel_do

#define ITER_RANGE          1000
#define ITEMS_TO_FEED       50
#define NESTED_ITER_RANGE   100
#define NESTING_ITER_RANGE  50

#define PREPARE_RANGE(Iterator, rangeSize)  \
    size_t test_vector[rangeSize + 1]; \
    for (int i =0; i < rangeSize; i++) \
        test_vector[i] = i; \
    Iterator begin(&test_vector[0]); \
    Iterator end(&test_vector[rangeSize])

void Feed ( tbb::parallel_do_feeder<size_t> &feeder, size_t val ) {
    if (g_FedTasksCount < ITEMS_TO_FEED) { 
        ++g_FedTasksCount; 
        feeder.add(val);
    }
}

#include "harness_iterator.h"

// Simple functor object with exception
class SimpleParDoBody {
public:
    void operator() ( size_t &value ) const {
        ++g_CurExecuted;
        Harness::ConcurrencyTracker ct;
        value += 1000;
        WaitUntilConcurrencyPeaks();
        ThrowTestException(1);
    }
};

// Simple functor object with exception and feeder
class SimpleParDoBodyWithFeeder : SimpleParDoBody {
public:
    void operator() ( size_t &value, tbb::parallel_do_feeder<size_t> &feeder ) const {
        Feed(feeder, 0);
        SimpleParDoBody::operator()(value);
    }
};

// Tests exceptions without nesting
template <class Iterator, class simple_body>
void Test1_parallel_do () {
    ResetGlobals();
    PREPARE_RANGE(Iterator, ITER_RANGE);
    TRY();
        tbb::parallel_do<Iterator, simple_body>(begin, end, simple_body() );
    CATCH_AND_ASSERT();
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");

} // void Test1_parallel_do ()

template <class Iterator>
class NestingParDoBody {
public:
    void operator()( size_t& /*value*/ ) const {
        ++g_CurExecuted;
        PREPARE_RANGE(Iterator, NESTED_ITER_RANGE);
        tbb::parallel_do<Iterator, SimpleParDoBody>(begin, end, SimpleParDoBody());
    }
};

template <class Iterator>
class NestingParDoBodyWithFeeder : NestingParDoBody<Iterator> {
public:
    void operator()( size_t& value, tbb::parallel_do_feeder<size_t>& feeder ) const {
        Feed(feeder, 0);
        NestingParDoBody<Iterator>::operator()(value);
    }
};

//! Uses parallel_do body containing a nested parallel_do with the default context not wrapped by a try-block.
/** Nested algorithms are spawned inside the new bound context by default. Since
    exceptions thrown from the nested parallel_do are not handled by the caller
    (nesting parallel_do body) in this test, they will cancel all the sibling nested
    algorithms. **/
template <class Iterator, class nesting_body>
void Test2_parallel_do () {
    ResetGlobals();
    PREPARE_RANGE(Iterator, ITER_RANGE);
    TRY();
        tbb::parallel_do<Iterator, nesting_body >(begin, end, nesting_body() );
    CATCH_AND_ASSERT();
    ASSERT (exceptionCaught, "No exception thrown from the nesting parallel_for");
    //if ( g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test2_parallel_do ()

template <class Iterator> 
class NestingParDoBodyWithIsolatedCtx {
public:
    void operator()( size_t& /*value*/ ) const {
        tbb::task_group_context ctx(tbb::task_group_context::isolated);
        ++g_CurExecuted;
        PREPARE_RANGE(Iterator, NESTED_ITER_RANGE);
        tbb::parallel_do<Iterator, SimpleParDoBody>(begin, end, SimpleParDoBody(), ctx);
    }
};

template <class Iterator> 
class NestingParDoBodyWithIsolatedCtxWithFeeder : NestingParDoBodyWithIsolatedCtx<Iterator> {
public:
    void operator()( size_t& value, tbb::parallel_do_feeder<size_t> &feeder ) const {
        Feed(feeder, 0);
        NestingParDoBodyWithIsolatedCtx<Iterator>::operator()(value);
    }
};

//! Uses parallel_do body invoking a nested parallel_do with an isolated context without a try-block.
/** Even though exceptions thrown from the nested parallel_do are not handled
    by the caller in this test, they will not affect sibling nested algorithms
    already running because of the isolated contexts. However because the first
    exception cancels the root parallel_do only the first g_NumThreads subranges
    will be processed (which launch nested parallel_dos) **/
template <class Iterator, class nesting_body>
void Test3_parallel_do () {
    ResetGlobals();
    PREPARE_RANGE(Iterator, NESTING_ITER_RANGE);
    intptr_t nestedCalls = NESTED_ITER_RANGE,
             minExecuted = (g_NumThreads - 1) * nestedCalls;
    TRY();
        tbb::parallel_do<Iterator, nesting_body >(begin, end, nesting_body());
    CATCH_AND_ASSERT();
    ASSERT (exceptionCaught, "No exception thrown from the nesting parallel_for");
    if ( g_SolitaryException ) {
        ASSERT (g_CurExecuted > minExecuted, "Too few tasks survived exception");
        ASSERT (g_CurExecuted <= minExecuted + (g_ExecutedAtCatch + g_NumThreads), "Too many tasks survived exception");
    }
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test3_parallel_do ()

template <class Iterator>
class NestingParDoWithEhBody {
public:
    void operator()( size_t& /*value*/ ) const {
        tbb::task_group_context ctx(tbb::task_group_context::isolated);
        PREPARE_RANGE(Iterator, NESTED_ITER_RANGE);
        TRY();
            tbb::parallel_do<Iterator, SimpleParDoBody>(begin, end, SimpleParDoBody(), ctx);
        CATCH();
    }
};

template <class Iterator>
class NestingParDoWithEhBodyWithFeeder : NoAssign, NestingParDoWithEhBody<Iterator> {
public:
    void operator()( size_t &value, tbb::parallel_do_feeder<size_t> &feeder ) const {
        Feed(feeder, 0);
        NestingParDoWithEhBody<Iterator>::operator()(value);
    }
};

//! Uses parallel_for body invoking a nested parallel_for (with default bound context) inside a try-block.
/** Since exception(s) thrown from the nested parallel_for are handled by the caller
    in this test, they do not affect neither other tasks of the the root parallel_for
    nor sibling nested algorithms. **/
template <class Iterator, class nesting_body_with_eh>
void Test4_parallel_do () {
    ResetGlobals( true, true );
    PREPARE_RANGE(Iterator, NESTING_ITER_RANGE);
    TRY();
        tbb::parallel_do<Iterator, nesting_body_with_eh>(begin, end, nesting_body_with_eh());
    CATCH();
    ASSERT (!exceptionCaught, "All exceptions must have been handled in the parallel_do body");
    intptr_t nestedCalls = NESTED_ITER_RANGE,
             nestingCalls = NESTING_ITER_RANGE + g_FedTasksCount,
             maxExecuted = nestingCalls * nestedCalls,
             minExecuted = 0;
    if ( g_SolitaryException ) {
        minExecuted = maxExecuted - nestedCalls;
        ASSERT (g_Exceptions == 1, "No exception registered");
        ASSERT (g_CurExecuted >= minExecuted, "Too few tasks executed");
        ASSERT (g_CurExecuted <= minExecuted + g_NumThreads, "Too many tasks survived exception");
    }
    else {
        minExecuted = g_Exceptions;
        ASSERT (g_Exceptions > 1 && g_Exceptions <= nestingCalls, "Unexpected actual number of exceptions");
        ASSERT (g_CurExecuted >= minExecuted, "Too many executed tasks reported");
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived multiple exceptions");
        ASSERT (g_CurExecuted <= nestingCalls * (1 + g_NumThreads), "Too many tasks survived exception");
    }
} // void Test4_parallel_do ()

class ParDoBodyToCancel {
public:
    void operator()( size_t& /*value*/ ) const {
        ++g_CurExecuted;
        CancellatorTask::WaitUntilReady();
    }
};

class ParDoBodyToCancelWithFeeder : ParDoBodyToCancel {
public:
    void operator()( size_t& value, tbb::parallel_do_feeder<size_t> &feeder ) const {
        Feed(feeder, 0);
        ParDoBodyToCancel::operator()(value);
    }
};

template<class B, class Iterator>
class ParDoWorkerTask : public tbb::task {
    tbb::task_group_context &my_ctx;

    tbb::task* execute () {
        PREPARE_RANGE(Iterator, NESTED_ITER_RANGE);
        tbb::parallel_do<Iterator, B>( begin, end, B(), my_ctx );
        return NULL;
    }
public:
    ParDoWorkerTask ( tbb::task_group_context& ctx ) : my_ctx(ctx) {}
};

//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
template <class Iterator, class body_to_cancel>
void Test5_parallel_do () {
    ResetGlobals( false );
    intptr_t  threshold = 10;
    tbb::task_group_context  ctx;
    ctx.reset();
    tbb::empty_task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
    r.set_ref_count(3);
    r.spawn( *new( r.allocate_child() ) CancellatorTask(ctx, threshold) );
    __TBB_Yield();
    r.spawn( *new( r.allocate_child() ) ParDoWorkerTask<body_to_cancel, Iterator>(ctx) );
    TRY();
        r.wait_for_all();
    CATCH();
    r.destroy(r);
    ASSERT (!exceptionCaught, "Cancelling tasks should not cause any exceptions");
    ASSERT (g_CurExecuted < g_ExecutedAtCatch + g_NumThreads, "Too many tasks were executed after cancellation");
} // void Test5_parallel_do ()

class ParDoBodyToCancel2 {
public:
    void operator()( size_t& /*value*/ ) const {
        ++g_CurExecuted;
        // The test will hang (and be timed out by the test system) if is_cancelled() is broken
        while( !tbb::task::self().is_cancelled() )
            __TBB_Yield();
    }
};

class ParDoBodyToCancel2WithFeeder : ParDoBodyToCancel2 {
public:
    void operator()( size_t& value, tbb::parallel_do_feeder<size_t> &feeder ) const {
        Feed(feeder, 0);
        ParDoBodyToCancel2::operator()(value);
    }
};

//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
/** This version also tests task::is_cancelled() method. **/
template <class Iterator, class body_to_cancel>
void Test6_parallel_do () {
    ResetGlobals();
    RunCancellationTest<ParDoWorkerTask<body_to_cancel, Iterator>, CancellatorTask2>();
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch, "Some tasks were executed after cancellation");
} // void Test6_parallel_do ()

// This body throws an exception only if the task was added by feeder
class ParDoBodyWithThrowingFeederTasks {
public:
    //! This form of the function call operator can be used when the body needs to add more work during the processing
    void operator() ( size_t &value, tbb::parallel_do_feeder<size_t> &feeder ) const {
        ++g_CurExecuted;
        Feed(feeder, 1);
        if (value == 1)
            ThrowTestException(1);
    }
}; // class ParDoBodyWithThrowingFeederTasks

// Test exception in task, which was added by feeder.
template <class Iterator>
void Test8_parallel_do () {
    ResetGlobals();
    PREPARE_RANGE(Iterator, ITER_RANGE);
    TRY();
        tbb::parallel_do<Iterator, ParDoBodyWithThrowingFeederTasks>(begin, end, ParDoBodyWithThrowingFeederTasks());
    CATCH();
    if (g_SolitaryException)
        ASSERT (exceptionCaught, "At least one exception should occur");
} // void Test8_parallel_do ()

#define RunWithSimpleBody(func, body)       \
    func<Harness::RandomIterator<size_t>, body>();           \
    func<Harness::RandomIterator<size_t>, body##WithFeeder>();  \
    func<Harness::ForwardIterator<size_t>, body>();         \
    func<Harness::ForwardIterator<size_t>, body##WithFeeder>()

#define RunWithTemplatedBody(func, body)       \
    func<Harness::RandomIterator<size_t>, body<Harness::RandomIterator<size_t> > >();           \
    func<Harness::RandomIterator<size_t>, body##WithFeeder<Harness::RandomIterator<size_t> > >();  \
    func<Harness::ForwardIterator<size_t>, body<Harness::ForwardIterator<size_t> > >();         \
    func<Harness::ForwardIterator<size_t>, body##WithFeeder<Harness::ForwardIterator<size_t> > >()

void RunParDoTests() {
    REMARK( "parallel do tests" );
    tbb::task_scheduler_init init (g_NumThreads);
    g_Master = Harness::CurrentTid();
#if !__TBB_EXCEPTION_HANDLING_BROKEN
    RunWithSimpleBody(Test1_parallel_do, SimpleParDoBody);
    RunWithTemplatedBody(Test2_parallel_do, NestingParDoBody);
    RunWithTemplatedBody(Test3_parallel_do, NestingParDoBodyWithIsolatedCtx);
    RunWithTemplatedBody(Test4_parallel_do, NestingParDoWithEhBody);
#endif
    RunWithSimpleBody(Test5_parallel_do, ParDoBodyToCancel);
    RunWithSimpleBody(Test6_parallel_do, ParDoBodyToCancel2);
#if !__TBB_EXCEPTION_HANDLING_BROKEN
    Test8_parallel_do<Harness::ForwardIterator<size_t> >();
    Test8_parallel_do<Harness::RandomIterator<size_t> >();
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Tests for tbb::pipeline

#define NUM_ITEMS   100

const size_t c_DataEndTag = size_t(~0);

size_t g_NumTokens = 0;

// Simple input filter class, it assigns 1 to all array members
// It stops when it receives item equal to -1
class InputFilter: public tbb::filter {
    tbb::atomic<size_t> m_Item;
    size_t m_Buffer[NUM_ITEMS + 1];
public:
    InputFilter() : tbb::filter(parallel) {
        m_Item = 0;
        for (size_t i = 0; i < NUM_ITEMS; ++i )
            m_Buffer[i] = 1;
        m_Buffer[NUM_ITEMS] = c_DataEndTag;
    }

    void* operator()( void* ) {
        size_t item = m_Item.fetch_and_increment();
        if ( item >= NUM_ITEMS )
            return NULL;
        m_Buffer[item] = 1;
        return &m_Buffer[item];
    }

    size_t* buffer() { return m_Buffer; }
}; // class InputFilter

// Pipeline filter, without exceptions throwing
class NoThrowFilter : public tbb::filter {
    size_t m_Value;
public:
    enum operation {
        addition,
        subtraction,
        multiplication
    } m_Operation;

    NoThrowFilter(operation _operation, size_t value, bool is_parallel)
        : filter(is_parallel? tbb::filter::parallel : tbb::filter::serial_in_order),
        m_Value(value), m_Operation(_operation)
    {}
    void* operator()(void* item) {
        size_t &value = *(size_t*)item;
        ASSERT(value != c_DataEndTag, "terminator element is being processed");
        switch (m_Operation){
            case addition:
                value += m_Value;
                break;
            case subtraction:
                value -= m_Value;
                break;
            case multiplication:
                value *= m_Value;
                break;
            default:
                ASSERT(0, "Wrong operation parameter passed to NoThrowFilter");
        } // switch (m_Operation)
        return item;
    }
};

// Test pipeline without exceptions throwing
void Test0_pipeline () {
    ResetGlobals();
    // Run test when serial filter is the first non-input filter
    InputFilter inputFilter;
    NoThrowFilter filter1(NoThrowFilter::addition, 99, false);
    NoThrowFilter filter2(NoThrowFilter::subtraction, 90, true);
    NoThrowFilter filter3(NoThrowFilter::multiplication, 5, false);
    // Result should be 50 for all items except the last
    tbb::pipeline p;
    p.add_filter(inputFilter);
    p.add_filter(filter1);
    p.add_filter(filter2);
    p.add_filter(filter3);
    p.run(8);
    for (size_t i = 0; i < NUM_ITEMS; ++i)
        ASSERT(inputFilter.buffer()[i] == 50, "pipeline didn't process items properly");
} // void Test0_pipeline ()

// Simple filter with exception throwing
class SimpleFilter : public tbb::filter {
public:
    SimpleFilter (tbb::filter::mode _mode ) : filter (_mode) {}

    void* operator()(void* item) {
        Harness::ConcurrencyTracker ct;
        ++g_CurExecuted;
        WaitUntilConcurrencyPeaks();
        ThrowTestException(1);
        return item;
    }
}; // class SimpleFilter

// This enumeration represents filters order in pipeline
enum FilterSet {
    parallel__parallel=0,
    parallel__serial=1,
    parallel__serial_out_of_order=2,
    serial__parallel=4,
    serial__serial=5,
    serial__serial_out_of_order=6,
    serial_out_of_order__parallel=8,
    serial_out_of_order__serial=9,
    serial_out_of_order__serial_out_of_order=10
};

// The function returns filter type using filter number in set
tbb::filter::mode filter_mode (FilterSet set, int number) {
    size_t tmp = set << (2 * (2 - number));
    switch (tmp&12){
        case 0:
            return tbb::filter::parallel;
        case 4:
            return tbb::filter::serial_in_order;
        case 8:
            return tbb::filter::serial_out_of_order;
    }
    ASSERT(0, "Wrong filter set passed to get_filter_type");
    return tbb::filter::parallel; // We should never get here, just to prevent compiler warnings
}

template<typename InFilter, typename Filter>
class CustomPipeline : protected tbb::pipeline {
    InFilter inputFilter;
    Filter filter1;
    Filter filter2;
public:
    CustomPipeline( FilterSet FilterSet )
        : filter1(filter_mode(FilterSet, 1))
        , filter2(filter_mode(FilterSet, 2))
    {
       add_filter(inputFilter);
       add_filter(filter1);
       add_filter(filter2);
    }
    void run () { tbb::pipeline::run(g_NumTokens); }
    void run ( tbb::task_group_context& ctx ) { tbb::pipeline::run(g_NumTokens, ctx); }

    using tbb::pipeline::add_filter;
};

typedef CustomPipeline<InputFilter, SimpleFilter> SimplePipeline;

// Tests exceptions without nesting
void Test1_pipeline ( FilterSet mode ) {
    ResetGlobals();
    SimplePipeline testPipeline(mode);
    TRY();
        testPipeline.run();
        if ( g_CurExecuted == 2 * NUM_ITEMS ) {
            // In case of all serial filters they might be all executed in the thread(s)
            // where exceptions are not allowed by the common test logic. So we just quit.
            return;
        }
    CATCH_AND_ASSERT();
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");

} // void Test1_pipeline ()

// Filter with nesting
class NestingFilter : public tbb::filter {
public:
    NestingFilter( tbb::filter::mode _mode ) : tbb::filter( _mode) {}

    void* operator()(void* item) {
        ++g_CurExecuted;
        SimplePipeline testPipeline(serial__parallel);
        testPipeline.run();
        return item;
    }
}; // class NestingFilter

//! Uses pipeline containing a nested pipeline with the default context not wrapped by a try-block.
/** Nested algorithms are spawned inside the new bound context by default. Since
    exceptions thrown from the nested pipeline are not handled by the caller
    (nesting pipeline body) in this test, they will cancel all the sibling nested
    algorithms. **/
void Test2_pipeline ( FilterSet mode ) {
    ResetGlobals();
    CustomPipeline<InputFilter, NestingFilter> testPipeline(mode);
    TRY();
        testPipeline.run();
    CATCH_AND_ASSERT();
    ASSERT (exceptionCaught, "No exception thrown from the nesting pipeline");
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test2_pipeline ()

class NestingFilterWithIsolatedCtx : public tbb::filter {
public:
    NestingFilterWithIsolatedCtx(tbb::filter::mode m ) : filter (m) {}

    void* operator()(void* item) {
        ++g_CurExecuted;
        tbb::task_group_context ctx(tbb::task_group_context::isolated);
        SimplePipeline testPipeline(serial__parallel);
        testPipeline.run(ctx);
        return item;
    }
}; // class NestingFilterWithIsolatedCtx

//! Uses pipeline invoking a nested pipeline with an isolated context without a try-block.
/** Even though exceptions thrown from the nested pipeline are not handled
    by the caller in this test, they will not affect sibling nested algorithms
    already running because of the isolated contexts. However because the first
    exception cancels the root parallel_do only the first g_NumThreads subranges
    will be processed (which launch nested pipelines) **/
void Test3_pipeline ( FilterSet mode ) {
    ResetGlobals();
    intptr_t nestedCalls = 100,
             minExecuted = (g_NumThreads - 1) * nestedCalls;
    CustomPipeline<InputFilter, NestingFilterWithIsolatedCtx> testPipeline(mode);
    TRY();
        testPipeline.run();
    CATCH_AND_ASSERT();
    ASSERT (exceptionCaught, "No exception thrown from the nesting parallel_for");
    if ( g_SolitaryException ) {
        ASSERT (g_CurExecuted > minExecuted, "Too few tasks survived exception");
        ASSERT (g_CurExecuted <= minExecuted + (g_ExecutedAtCatch + g_NumThreads), "Too many tasks survived exception");
    }
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test3_pipeline ()

class NestingFilterWithEhBody : public tbb::filter {
public:
    NestingFilterWithEhBody(tbb::filter::mode m ) : filter(m) {}

    void* operator()(void* item) {
        tbb::task_group_context ctx(tbb::task_group_context::isolated);
        SimplePipeline testPipeline(serial__parallel);
        TRY();
            testPipeline.run(ctx);
        CATCH();
        return item;
    }
}; // class NestingFilterWithEhBody

//! Uses pipeline body invoking a nested pipeline (with default bound context) inside a try-block.
/** Since exception(s) thrown from the nested pipeline are handled by the caller
    in this test, they do not affect neither other tasks of the the root pipeline
    nor sibling nested algorithms. **/

void Test4_pipeline ( FilterSet mode ) {
#if __GNUC__ && !__INTEL_COMPILER
    if ( strncmp(__VERSION__, "4.1.0", 5) == 0 ) {
        REMARK_ONCE("Warning: One of exception handling tests is skipped due to a known issue.\n");
        return;
    }
#endif
    ResetGlobals( true, true );
    intptr_t nestedCalls = NUM_ITEMS + 1,
             nestingCalls = 2 * (NUM_ITEMS + 1),
             maxExecuted = nestingCalls * nestedCalls;
    CustomPipeline<InputFilter, NestingFilterWithEhBody> testPipeline(mode);
    TRY();
        testPipeline.run();
    CATCH_AND_ASSERT();
    ASSERT (!exceptionCaught, "All exceptions must have been handled in the parallel_do body");
    intptr_t  minExecuted = 0;
    if ( g_SolitaryException ) {
        minExecuted = maxExecuted - nestedCalls;
        ASSERT (g_Exceptions == 1, "No exception registered");
        ASSERT (g_CurExecuted <= minExecuted + g_NumThreads, "Too many tasks survived exception");
    }
    else {
        minExecuted = g_Exceptions;
        ASSERT (g_Exceptions > 1 && g_Exceptions <= nestingCalls, "Unexpected actual number of exceptions");
        ASSERT (g_CurExecuted >= minExecuted, "Too many executed tasks reported");
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived multiple exceptions");
        ASSERT (g_CurExecuted <= nestingCalls * (1 + g_NumThreads), "Too many tasks survived exception");
    }
} // void Test4_pipeline ()

class FilterToCancel : public tbb::filter {
public:
    FilterToCancel(bool is_parallel)
        : filter( is_parallel ? tbb::filter::parallel : tbb::filter::serial_in_order )
    {}
    void* operator()(void* item) {
        ++g_CurExecuted;
        CancellatorTask::WaitUntilReady();
        return item;
    }
}; // class FilterToCancel

template <class Filter_to_cancel> 
class PipelineLauncherTask : public tbb::task {
    tbb::task_group_context &my_ctx;
public:
    PipelineLauncherTask ( tbb::task_group_context& ctx ) : my_ctx(ctx) {}

    tbb::task* execute () {
        // Run test when serial filter is the first non-input filter
        InputFilter inputFilter;
        Filter_to_cancel filterToCancel(true);
        tbb::pipeline p;
        p.add_filter(inputFilter);
        p.add_filter(filterToCancel);
        p.run(g_NumTokens, my_ctx);
        return NULL;
    }
};

//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
void Test5_pipeline () {
    ResetGlobals();
    g_ThrowException = false;
    intptr_t  threshold = 10;
    tbb::task_group_context ctx;
    ctx.reset();
    tbb::empty_task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
    r.set_ref_count(3);
    r.spawn( *new( r.allocate_child() ) CancellatorTask(ctx, threshold) );
    __TBB_Yield();
    r.spawn( *new( r.allocate_child() ) PipelineLauncherTask<FilterToCancel>(ctx) );
    TRY();
        r.wait_for_all();
    CATCH();
    r.destroy(r);
    ASSERT (!exceptionCaught, "Cancelling tasks should not cause any exceptions");
    ASSERT (g_CurExecuted < g_ExecutedAtCatch + g_NumThreads, "Too many tasks were executed after cancellation");
} // void Test5_pipeline ()

class FilterToCancel2 : public tbb::filter {
public:
    FilterToCancel2(bool is_parallel)
        : filter ( is_parallel ? tbb::filter::parallel : tbb::filter::serial_in_order)
    {}

    void* operator()(void* item) {
        ++g_CurExecuted;
        // The test will hang (and be timed out by the tesst system) if is_cancelled() is broken
        while( !tbb::task::self().is_cancelled() )
            __TBB_Yield();
        return item;
    }
};

//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
/** This version also tests task::is_cancelled() method. **/
void Test6_pipeline () {
    ResetGlobals();
    RunCancellationTest<PipelineLauncherTask<FilterToCancel2>, CancellatorTask2>();
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch, "Some tasks were executed after cancellation");
} // void Test6_pipeline ()

//! Testing filter::finalize method
#define BUFFER_SIZE     32
#define NUM_BUFFERS     1024

tbb::atomic<size_t> g_AllocatedCount; // Number of currently allocated buffers
tbb::atomic<size_t> g_TotalCount; // Total number of allocated buffers

//! Base class for all filters involved in finalize method testing
class FinalizationBaseFilter : public tbb::filter {
public:
    FinalizationBaseFilter ( tbb::filter::mode m ) : filter(m) {}

    // Deletes buffers if exception occured
    virtual void finalize( void* item ) {
        size_t* m_Item = (size_t*)item;
        delete[] m_Item;
        --g_AllocatedCount;
    }
};

//! Input filter to test finalize method
class InputFilterWithFinalization: public FinalizationBaseFilter {
public:
    InputFilterWithFinalization() : FinalizationBaseFilter(tbb::filter::serial) {
        g_TotalCount = 0;
    }
    void* operator()( void* ){
        if (g_TotalCount == NUM_BUFFERS)
            return NULL;
        size_t* item = new size_t[BUFFER_SIZE];
        for (int i = 0; i < BUFFER_SIZE; i++)
            item[i] = 1;
        ++g_TotalCount;
        ++g_AllocatedCount;
        return item;
    }
};

// The filter multiplies each buffer item by 10.
class ProcessingFilterWithFinalization : public FinalizationBaseFilter {
public:
    ProcessingFilterWithFinalization (tbb::filter::mode _mode) : FinalizationBaseFilter (_mode) {}

    void* operator()( void* item) {
        if (g_TotalCount > NUM_BUFFERS / 2)
            ThrowTestException(1);
        size_t* m_Item = (size_t*)item;
        for (int i = 0; i < BUFFER_SIZE; i++)
            m_Item[i] *= 10;
        return item;
    }
};

// Output filter deletes previously allocated buffer
class OutputFilterWithFinalization : public FinalizationBaseFilter {
public:
    OutputFilterWithFinalization (tbb::filter::mode m) : FinalizationBaseFilter (m) {}

    void* operator()( void* item){
        size_t* m_Item = (size_t*)item;
        delete[] m_Item;
        --g_AllocatedCount;
        return NULL;
    }
};

//! Tests filter::finalize method
void Test8_pipeline (FilterSet mode) {
    ResetGlobals();
    g_AllocatedCount = 0;
    CustomPipeline<InputFilterWithFinalization, ProcessingFilterWithFinalization> testPipeline(mode);
    OutputFilterWithFinalization my_output_filter(tbb::filter::parallel);

    testPipeline.add_filter(my_output_filter);
    TRY();
        testPipeline.run();
    CATCH();
    ASSERT (g_AllocatedCount == 0, "Memory leak: Some my_object weren't destroyed");
} // void Test8_pipeline ()

// Tests pipeline function passed with different combination of filters
template<void testFunc(FilterSet)>
void TestWithDifferentFilters() {
    testFunc(parallel__parallel);
    testFunc(parallel__serial);
    testFunc(parallel__serial_out_of_order);
    testFunc(serial__parallel);
    testFunc(serial__serial);
    testFunc(serial__serial_out_of_order);
    testFunc(serial_out_of_order__parallel);
    testFunc(serial_out_of_order__serial);
    testFunc(serial_out_of_order__serial_out_of_order);
}

void RunPipelineTests() {
    REMARK( "pipeline tests" );
    tbb::task_scheduler_init init (g_NumThreads);
    g_Master = Harness::CurrentTid();
    g_NumTokens = 2 * g_NumThreads;

    Test0_pipeline();
#if !__TBB_EXCEPTION_HANDLING_BROKEN
    TestWithDifferentFilters<Test1_pipeline>();
    TestWithDifferentFilters<Test2_pipeline>();
    TestWithDifferentFilters<Test3_pipeline>();
    TestWithDifferentFilters<Test4_pipeline>();
#endif /* !__TBB_EXCEPTION_HANDLING_BROKEN */
    Test5_pipeline();
    Test6_pipeline();
#if !__TBB_EXCEPTION_HANDLING_BROKEN
    TestWithDifferentFilters<Test8_pipeline>();
#endif
}
#endif /* __TBB_EXCEPTIONS */

////////////////////////////////////////////////////////////////////////////////
// Tests for tbb::parallel_scan

const int id = 0;
const int PSCAN_SIZE_OF_BUFFER = 100;

class PScanBodyNothrow : public tbb::internal::no_assign {
    size_t sum;
    const size_t* const x;
    size_t* const y;
public:
    PScanBodyNothrow( size_t y_[], const size_t x_[] ) : sum(id), x(x_), y(y_) {}
    size_t get_sum() const {return sum;}
    template<typename Tag>
    void operator()( const tbb::blocked_range<int>& r, Tag ) {
        size_t temp = sum;
        for( int i=r.begin(); i<r.end(); ++i ) {
            temp = temp + x[i];
            if( Tag::is_final_scan() )
                y[i] = temp;
        }
        sum = temp;
    }
    PScanBodyNothrow( PScanBodyNothrow& b, tbb::split ) :  sum(id), x(b.x), y(b.y) {}
    void reverse_join( PScanBodyNothrow& a ) { sum = a.sum + sum;}
    void assign( PScanBodyNothrow& b ) {sum = b.sum;}
};

// Test parallel_scan without exceptions throwing
void Test0_parallel_scan () {
    ResetGlobals();

    // TODO move to a function or macro
    size_t x[100], y[100], y_ref[100], sum = 0;
    for (size_t i = 0; i < 100; i ++)
    {
        x[i] = i;
        y[i] = 0;
        sum += x[i];
        y_ref[i] = sum;
    }

    PScanBodyNothrow body(y,x);
    tbb::parallel_scan( tbb::blocked_range<int>(0, 100, 1), body );
    for (size_t i = 0; i < 100; i ++)
    {
        ASSERT(y[i] == y_ref[i], "Sum got from parallel_scan is different from serial one");
    }
    ASSERT(body.get_sum() == y_ref[99], "Sum got from parallel_scan is different from serial one");

} // void Test0_parallel_scan ()

// Simple parallel_scan body which throws an exception
class SimplePscanBody {
public:
    SimplePscanBody( ) {}
    template<typename Tag>
    void operator()( const tbb::blocked_range<int>& , Tag ) {
        ++g_CurExecuted;
        Harness::ConcurrencyTracker ct;
        WaitUntilConcurrencyPeaks();
        ThrowTestException(1);
    }
    SimplePscanBody( SimplePscanBody&, tbb::split ) {}
    void reverse_join( SimplePscanBody& ) {}
    void assign( SimplePscanBody& ) {}
};

// Tests tbb::parallel_scan exceptions handling without nesting
void Test1_parallel_scan()
{
    ResetGlobals();

    TRY();
        SimplePscanBody simple_body;
        tbb::parallel_scan( tbb::blocked_range<int>(0, 100, 1), simple_body );
    CATCH_AND_ASSERT();

    ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test1_parallel_scan()

class NestingPScanBody {
public:
    NestingPScanBody( ) {}
    template<typename Tag>
    void operator()( const tbb::blocked_range<int>&, Tag ) {
        ++g_CurExecuted;
        if ( Harness::CurrentTid() == g_Master )
            __TBB_Yield();

        SimplePscanBody simple_body;
        tbb::parallel_scan( tbb::blocked_range<int>(0, 100, 1), simple_body );
    }
    NestingPScanBody( NestingPScanBody& , tbb::split ) {}
    void reverse_join( NestingPScanBody& ) {}
    void assign( NestingPScanBody& ) {}
};

//! Uses parallel_scan body containing a nested parallel_scan with the default context not wrapped by a try-block.
/** Nested algorithms are spawned inside the new bound context by default. Since
    exceptions thrown from the nested parallel_scan are not handled by the caller
    (nesting parallel_scan body) in this test, they will cancel all the sibling nested
    algorithms. **/
void Test2_parallel_scan () {
    ResetGlobals();

    TRY();
        NestingPScanBody nesting_body;
        tbb::parallel_scan( tbb::blocked_range<int>(0, 100, 1), nesting_body );
    CATCH_AND_ASSERT();

    ASSERT (g_ExceptionThrown, "No exception thrown from the nesting parallel_scan");
    ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
    ASSERT (g_Exceptions == 1, "No try_blocks in any body expected in this test");
    if ( !g_SolitaryException )
        ASSERT (g_CurExecuted <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks survived exception");
} // void Test2_parallel_scan ()

class PScanBodyToCancel {
public:
    PScanBodyToCancel( ) {}
    template<typename Tag>
    void operator()( const tbb::blocked_range<int>&, Tag ) {
        ++g_CurExecuted;
        CancellatorTask::WaitUntilReady();
    }
    PScanBodyToCancel( PScanBodyToCancel& , tbb::split ) {}
    void reverse_join( PScanBodyToCancel& ) {}
    void assign( PScanBodyToCancel& ) {}
};

typedef class EmptyClass {
} Default_partitioner;

template <typename Partitioner>
class MyWorkerPScanTask : public tbb::task
{
    tbb::task_group_context &my_ctx;

    tbb::task* execute () {
        PScanBodyToCancel body_to_cancel;
        tbb::parallel_scan( tbb::blocked_range<int>(0, 100, 1), body_to_cancel, Partitioner(), my_ctx );
        return NULL;
    }
public:
    MyWorkerPScanTask ( tbb::task_group_context& ctx ) : my_ctx(ctx) {}
};

template <>
class MyWorkerPScanTask<Default_partitioner> : public tbb::task
{
    tbb::task_group_context &my_ctx;

    tbb::task* execute () {
        PScanBodyToCancel body_to_cancel;
        tbb::parallel_scan( tbb::blocked_range<int>(0, 100, 1), body_to_cancel, my_ctx );
        return NULL;
    }
public:
    MyWorkerPScanTask ( tbb::task_group_context& ctx ) : my_ctx(ctx) {}
};


//! Test for cancelling an algorithm from outside (from a task running in parallel with the algorithm).
template <typename Partitioner>
void Test5_parallel_scan () {
    ResetGlobals( false );
    RunCancellationTest<MyWorkerPScanTask<Partitioner>, CancellatorTask>( 1 );
    ASSERT (g_CurExecuted < g_ExecutedAtCatch + g_NumThreads, "Too many tasks were executed after cancellation");
} // void Test5_parallel_scan ()


void RunPScanTests()
{
    tbb::task_scheduler_init init (g_NumThreads);
    g_Master = Harness::CurrentTid();

    Test0_parallel_scan();
#if !(__GLIBC__==2&&__GLIBC_MINOR__==3)
    Test1_parallel_scan();
    Test2_parallel_scan();
#endif /* __GLIBC__ */
    if (g_NumThreads > 2) {
        Test5_parallel_scan<tbb::simple_partitioner>();
        Test5_parallel_scan<tbb::auto_partitioner>();
        Test5_parallel_scan<Default_partitioner>(); // default partitioner
    }
}

/** If min and max thread numbers specified on the command line are different,
    the test is run only for 2 sizes of the thread pool (MinThread and MaxThread)
    to be able to test the high and low contention modes while keeping the test reasonably fast **/
__TBB_TEST_EXPORT
int main(int argc, char* argv[]) {
    ParseCommandLine( argc, argv );
    MinThread = max(2, MinThread);
    MaxThread = max(MinThread, MaxThread);
    ASSERT (FLAT_RANGE >= FLAT_GRAIN * MaxThread, "Fix defines");
#if __TBB_EXCEPTIONS
    int step = max(MaxThread - MinThread, 1);
    for ( g_NumThreads = MinThread; g_NumThreads <= MaxThread; g_NumThreads += step ) {
        REMARK ("Number of threads %d", g_NumThreads);
        // Execute in all the possible modes
        for ( size_t j = 0; j < 4; ++j ) {
            g_ExceptionInMaster = (j & 1) == 1;
            g_SolitaryException = (j & 2) == 1;
            RunParForAndReduceTests();
            RunParDoTests();
            RunPipelineTests();
            RunPScanTests();
        }
    }
#if __TBB_EXCEPTION_HANDLING_BROKEN
    REPORT("Warning: Exception handling tests are skipped due to a known issue.\n");
#endif
    REPORT("done\n");
#else  /* !__TBB_EXCEPTION_HANDLING_BROKEN */
    REPORT("skipped\n");
#endif /* !__TBB_EXCEPTIONS */
    return 0;
}
