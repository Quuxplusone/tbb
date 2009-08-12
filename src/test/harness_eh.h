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

#include <typeinfo>
#include "tbb/atomic.h"
#include "harness.h"
#include "harness_concurrency_tracker.h"

namespace Harness {
#if _WIN32 || _WIN64
    typedef DWORD tid_t;
    tid_t CurrentTid () { return GetCurrentThreadId(); }
#else /* !WIN */
    typedef pthread_t tid_t;
    tid_t CurrentTid () { return pthread_self(); }
#endif /* !WIN */
} // namespace util

int g_NumThreads = 0;
Harness::tid_t  g_Master = 0;

class test_exception : public std::exception {
    const char* my_description;
public:
    test_exception ( const char* description ) : my_description(description) {}

    const char* what() const throw() { return my_description; }
};

class solitary_test_exception : public test_exception {
public:
    solitary_test_exception ( const char* description ) : test_exception(description) {}
};

#if TBB_USE_CAPTURED_EXCEPTION
    typedef tbb::captured_exception PropagatedException;
    #define EXCEPTION_NAME(e) e.name()
#else
    typedef test_exception PropagatedException;
    #define EXCEPTION_NAME(e) typeid(e).name()
#endif

#define EXCEPTION_DESCR "Test exception"

tbb::atomic<intptr_t> g_CurExecuted,
                      g_ExecutedAtCatch;
volatile intptr_t g_ExceptionThrown = 0;
volatile bool g_ExceptionCaught = false,
              g_UnknownException = false;

volatile bool g_ThrowException = true,
              g_Flog = false;

bool    g_ExceptionInMaster = false;
bool    g_SolitaryException = false;

tbb::atomic<intptr_t> g_Exceptions; // number of exceptions propagated into the user users (i.e. intercepted by the tests)

inline void ResetEhGlobals ( bool throwException = true, bool flog = false ) {
    Harness::ConcurrencyTracker::Reset();
    g_CurExecuted = g_ExecutedAtCatch = 0;
    g_ExceptionCaught = false;
    g_UnknownException = false;
    g_ThrowException = throwException;
    g_Flog = flog;
    g_ExceptionThrown = 0;
    g_Exceptions = 0;
}

#if HARNESS_EH_SIMPLE_MODE

static void ThrowTestException () { 
    g_ExceptionThrown = 1;
    throw test_exception(EXCEPTION_DESCR);
}

#else /* !HARNESS_EH_SIMPLE_MODE */

static void ThrowTestException ( intptr_t threshold ) {
    if ( !g_ThrowException || (!g_Flog && (g_ExceptionInMaster ^ (Harness::CurrentTid() == g_Master))) )
        return; 
    while ( Existed() < threshold )
        __TBB_Yield();
    if ( !g_SolitaryException ) {
        g_ExceptionThrown = 1;
        REMARK ("About to throw one of multiple test_exceptions (thread %08x):", Harness::CurrentTid());
        throw test_exception(EXCEPTION_DESCR);
    }
    if ( __TBB_CompareAndSwapW(&g_ExceptionThrown, 1, 0) == 0 ) {
        REMARK ("About to throw solitary test_exception... :");
        throw solitary_test_exception(EXCEPTION_DESCR);
    }
}
#endif /* !HARNESS_EH_SIMPLE_MODE */

#define TRY()   \
    bool exceptionCaught = false, unknownException = false;    \
    try {

#define CATCH()     \
    } catch ( PropagatedException& e ) { \
        g_ExecutedAtCatch = g_CurExecuted; \
        ASSERT (strcmp(EXCEPTION_NAME(e), (g_SolitaryException ? typeid(solitary_test_exception) : typeid(test_exception)).name() ) == 0, "Unexpected original exception name"); \
        ASSERT (strcmp(e.what(), EXCEPTION_DESCR) == 0, "Unexpected original exception info"); \
        g_ExceptionCaught = exceptionCaught = true; \
        ++g_Exceptions; \
    } catch ( ... ) { \
        g_ExceptionCaught = exceptionCaught = true; \
        g_UnknownException = unknownException = true; \
    }

#define ASSERT_EXCEPTION() \
    ASSERT (g_ExceptionThrown ? g_ExceptionCaught : !g_ExceptionCaught, "throw without catch or catch without throw"); \
    ASSERT (g_ExceptionCaught, "no exception occurred"); \
    ASSERT (!g_UnknownException, "unknown exception was caught")

#define CATCH_AND_ASSERT() \
    CATCH() \
    ASSERT_EXCEPTION()

const int c_Timeout = 10000;

void WaitUntilConcurrencyPeaks () {
    if ( g_Flog )
        return;
    int n = 0;
    while ( ++n < c_Timeout && (int)Harness::ConcurrencyTracker::InstantParallelism() < g_NumThreads )
        __TBB_Yield();
}

inline bool IsMaster() {
    return Harness::CurrentTid() == g_Master;
}

inline bool IsThrowingThread() {
    return g_ExceptionInMaster ^ IsMaster() ? true : false;
}

class CancellatorTask : public tbb::task {
    static volatile bool s_Ready;
    tbb::task_group_context &m_groupToCancel;
    intptr_t m_cancellationThreshold;

    tbb::task* execute () {
        s_Ready = true;
        while ( g_CurExecuted < m_cancellationThreshold )
            __TBB_Yield();
        m_groupToCancel.cancel_group_execution();
        g_ExecutedAtCatch = g_CurExecuted;
        return NULL;
    }
public:
    CancellatorTask ( tbb::task_group_context& ctx, intptr_t threshold )
        : m_groupToCancel(ctx), m_cancellationThreshold(threshold)
    {
        s_Ready = false;
    }

    static void Reset () { s_Ready = false; }

    static void WaitUntilReady () {
        do {
            __TBB_Yield();
        } while( !s_Ready );
    }
};

volatile bool CancellatorTask::s_Ready = false;

template<class LauncherTaskT, class CancellatorTaskT>
void RunCancellationTest ( intptr_t threshold = 1 )
{
    tbb::task_group_context  ctx;
    tbb::empty_task &r = *new( tbb::task::allocate_root(ctx) ) tbb::empty_task;
    r.set_ref_count(3);
    r.spawn( *new( r.allocate_child() ) CancellatorTaskT(ctx, threshold) );
    __TBB_Yield();
    r.spawn( *new( r.allocate_child() ) LauncherTaskT(ctx) );
    TRY();
        r.wait_for_all();
    CATCH();
    r.destroy(r);
    ASSERT (!g_ExceptionCaught && !exceptionCaught, "Cancelling tasks should not cause any exceptions");
}
