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

// to avoid usage of #pragma comment
#define __TBB_NO_IMPLICIT_LINKAGE 1

#define  COUNT_TASK_NODES 1
#define __TBB_TASK_CPP_DIRECTLY_INCLUDED 1
#include "../tbb/task.cpp"

#if __TBB_EXCEPTIONS && !__TBB_EXCEPTION_HANDLING_TOTALLY_BROKEN

#include "tbb/task_scheduler_init.h"
#include "tbb/spin_mutex.h"
#include "tbb/tick_count.h"
#include <string>

#define NUM_CHILD_TASKS                 256
#define NUM_ROOT_TASKS                  32
#define NUM_ROOTS_IN_GROUP              8

//! Statistics about number of tasks in different states
class TaskStats {
    typedef tbb::spin_mutex::scoped_lock lock_t;
    //! Number of tasks allocated that was ever allocated
    volatile intptr_t m_Existed;
    //! Number of tasks executed to the moment
    volatile intptr_t m_Executed;
    //! Number of tasks allocated but not yet destroyed to the moment
    volatile intptr_t m_Existing;

    mutable tbb::spin_mutex  m_Mutex;
public:
    //! Assumes that assignment is noncontended for the left-hand operand
    const TaskStats& operator= ( const TaskStats& rhs ) {
        if ( this != &rhs ) {
            lock_t lock(rhs.m_Mutex);
            m_Existed = rhs.m_Existed;
            m_Executed = rhs.m_Executed;
            m_Existing = rhs.m_Existing;
        }
        return *this;
    }
    intptr_t Existed() const { return m_Existed; }
    intptr_t Executed() const { return m_Executed; }
    intptr_t Existing() const { return m_Existing; }
    void IncExisted() { lock_t lock(m_Mutex); ++m_Existed; ++m_Existing; }
    void IncExecuted() { lock_t lock(m_Mutex); ++m_Executed; }
    void DecExisting() { lock_t lock(m_Mutex); --m_Existing; }
    //! Assumed to be used in uncontended manner only
    void Reset() { m_Executed = m_Existing = m_Existed = 0; }
};

TaskStats g_CurStat;

inline intptr_t Existed () { return g_CurStat.Existed(); }

#include "harness_eh.h"

bool g_BoostExecutedCount = true;
volatile bool g_TaskWasCancelled = false;

inline void ResetGlobals () {
    ResetEhGlobals();
    g_BoostExecutedCount = true;
    g_TaskWasCancelled = false;
    g_CurStat.Reset();
}

inline void WaitForException () {
    while ( !g_ExceptionCaught )
        __TBB_Yield();
}

#define ASSERT_TEST_POSTCOND() \
    ASSERT (g_CurStat.Existed() >= g_CurStat.Executed(), "Total number of tasks is less than executed");  \
    ASSERT (!g_CurStat.Existing(), "Not all task objects have been destroyed"); \
    ASSERT (!tbb::task::self().is_cancelled(), "Scheduler's default context has not been cleaned up properly");


class SimpleThrowingTask : public tbb::task {
public:
    tbb::task* execute () { throw 0; }

    ~SimpleThrowingTask() {
#if !__TBB_RELAXED_OWNERSHIP
        ASSERT( tbb::task::self().is_owned_by_current_thread(), NULL );
#endif /* !__TBB_RELAXED_OWNERSHIP */
    }
};

//! Checks if innermost running task information is updated correctly during cancellation processing
void Test0 () {
    tbb::task_scheduler_init init (1);
    tbb::empty_task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
    tbb::task_list tl;
    tl.push_back( *new( r.allocate_child() ) SimpleThrowingTask );
    tl.push_back( *new( r.allocate_child() ) SimpleThrowingTask );
    r.set_ref_count( 3 );
    try {
        r.spawn_and_wait_for_all( tl );
    }
    catch (...) {}
    r.destroy( r );
}

class TaskBase : public tbb::task {
    tbb::task* execute () {
        tbb::task* t = NULL;
        try { 
            t = do_execute();
        } catch ( ... ) { 
            g_CurStat.IncExecuted(); 
            throw;
        }
        g_CurStat.IncExecuted();
        return t;
    }
protected:
    TaskBase ( bool throwException = true ) : m_Throw(throwException) { g_CurStat.IncExisted(); }
    ~TaskBase () { g_CurStat.DecExisting(); }

    virtual tbb::task* do_execute () = 0;

    bool m_Throw;
}; // class TaskBase

class LeafTask : public TaskBase
{
    tbb::task* do_execute () {
        Harness::ConcurrencyTracker ct;
        WaitUntilConcurrencyPeaks();
        if ( g_BoostExecutedCount )
            ++g_CurExecuted;
        if ( m_Throw )
            ThrowTestException(NUM_CHILD_TASKS/2);
        if ( !g_ThrowException )
            __TBB_Yield();
        return NULL;
    }
public:
    LeafTask ( bool throw_exception = true ) : TaskBase(throw_exception) {}
};

class SimpleRootTask : public TaskBase {
    tbb::task* do_execute () {
        set_ref_count(NUM_CHILD_TASKS + 1);
        for ( size_t i = 0; i < NUM_CHILD_TASKS; ++i )
            spawn( *new( allocate_child() ) LeafTask(m_Throw) );
        wait_for_all();
        return NULL;
    }
public:
    SimpleRootTask ( bool throw_exception = true ) : TaskBase(throw_exception) {}
};

//! Default exception behavior test. 
/** Allocates a root task that spawns a bunch of children, one or several of which throw 
    a test exception in a worker or master thread (depending on the global setting). **/
void Test1 () {
    ResetGlobals();
    tbb::empty_task &r = *new( tbb::task::allocate_root() ) tbb::empty_task;
    ASSERT (!g_CurStat.Existing() && !g_CurStat.Existed() && !g_CurStat.Executed(), 
            "something wrong with the task accounting");
    r.set_ref_count(NUM_CHILD_TASKS + 1);
    for ( int i = 0; i < NUM_CHILD_TASKS; ++i )
        r.spawn( *new( r.allocate_child() ) LeafTask );
    TRY();
        r.wait_for_all();
    CATCH_AND_ASSERT();
    r.destroy(r);
    ASSERT_TEST_POSTCOND();
} // void Test1 ()

//! Default exception behavior test. 
/** Allocates and spawns root task that runs a bunch of children, one of which throws
    a test exception in a worker thread. (Similar to Test1, except that the root task 
    is spawned by the test function, and children are created by the root task instead 
    of the test function body.) **/
void Test2 () {
    ResetGlobals();
    SimpleRootTask &r = *new( tbb::task::allocate_root() ) SimpleRootTask;
    ASSERT (g_CurStat.Existing() == 1 && g_CurStat.Existed() == 1 && !g_CurStat.Executed(), 
            "something wrong with the task accounting");
    TRY();
        tbb::task::spawn_root_and_wait(r);
    CATCH_AND_ASSERT();
    ASSERT (g_ExceptionCaught, "no exception occurred");
    ASSERT_TEST_POSTCOND();
} // void Test2 ()

//! The same as Test2() except the root task has explicit context.
/** The context is initialized as bound in order to check correctness of its associating 
    with a root task. **/
void Test3 () {
    ResetGlobals();
    tbb::task_group_context  ctx(tbb::task_group_context::bound);
    SimpleRootTask &r = *new( tbb::task::allocate_root(ctx) ) SimpleRootTask;
    ASSERT (g_CurStat.Existing() == 1 && g_CurStat.Existed() == 1 && !g_CurStat.Executed(), 
            "something wrong with the task accounting");
    TRY();
        tbb::task::spawn_root_and_wait(r);
    CATCH_AND_ASSERT();
    ASSERT (g_ExceptionCaught, "no exception occurred");
    ASSERT_TEST_POSTCOND();
} // void Test2 ()

class RootLauncherTask : public TaskBase {
    tbb::task_group_context::kind_type m_CtxKind;
 
    tbb::task* do_execute () {
        tbb::task_group_context  ctx (tbb::task_group_context::isolated);
        SimpleRootTask &r = *new( allocate_root(ctx) ) SimpleRootTask;
        TRY();
            spawn_root_and_wait(r);
            // Give a child of our siblings a chance to throw the test exception
            WaitForException();
        CATCH();
        ASSERT (!g_UnknownException, "unknown exception was caught");
        return NULL;
    }
public:
    RootLauncherTask ( tbb::task_group_context::kind_type ctx_kind = tbb::task_group_context::isolated ) : m_CtxKind(ctx_kind) {}
};

/** Allocates and spawns a bunch of roots, which allocate and spawn new root with 
    isolated context, which at last spawns a bunch of children each, one of which 
    throws a test exception in a worker thread. **/
void Test4 () {
    ResetGlobals();
    tbb::task_list  tl;
    for ( size_t i = 0; i < NUM_ROOT_TASKS; ++i )
        tl.push_back( *new( tbb::task::allocate_root() ) RootLauncherTask );
    TRY();
        tbb::task::spawn_root_and_wait(tl);
    CATCH_AND_ASSERT();
    ASSERT (!exceptionCaught, "exception in this scope is unexpected");
    intptr_t  num_tasks_expected = NUM_ROOT_TASKS * (NUM_CHILD_TASKS + 2);
    ASSERT (g_CurStat.Existed() == num_tasks_expected, "Wrong total number of tasks");
    if ( g_SolitaryException )
        ASSERT (g_CurStat.Executed() >= num_tasks_expected - NUM_CHILD_TASKS, "Unexpected number of executed tasks");
    ASSERT_TEST_POSTCOND();
} // void Test4 ()

class RootsGroupLauncherTask : public TaskBase {
    tbb::task* do_execute () {
        tbb::task_group_context  ctx (tbb::task_group_context::isolated);
        tbb::task_list  tl;
        for ( size_t i = 0; i < NUM_ROOT_TASKS; ++i )
            tl.push_back( *new( allocate_root(ctx) ) SimpleRootTask );
        TRY();
            spawn_root_and_wait(tl);
            // Give worker a chance to throw exception
            WaitForException();
        CATCH_AND_ASSERT();
        return NULL;
    }
};

/** Allocates and spawns a bunch of roots, which allocate and spawn groups of roots 
    with an isolated context shared by all group members, which at last spawn a bunch 
    of children each, one of which throws a test exception in a worker thread. **/
void Test5 () {
    ResetGlobals();
    tbb::task_list  tl;
    for ( size_t i = 0; i < NUM_ROOTS_IN_GROUP; ++i )
        tl.push_back( *new( tbb::task::allocate_root() ) RootsGroupLauncherTask );
    TRY();
        tbb::task::spawn_root_and_wait(tl);
    CATCH_AND_ASSERT();
    ASSERT (!exceptionCaught, "unexpected exception intercepted");
    if ( g_SolitaryException )  {
        intptr_t  num_tasks_expected = NUM_ROOTS_IN_GROUP * (1 + NUM_ROOT_TASKS * (1 + NUM_CHILD_TASKS));
        intptr_t  min_num_tasks_executed = num_tasks_expected - NUM_ROOT_TASKS * (NUM_CHILD_TASKS + 1);
        ASSERT (g_CurStat.Executed() >= min_num_tasks_executed, "Too few tasks executed");
    }
    ASSERT_TEST_POSTCOND();
} // void Test5 ()

class ThrowingRootLauncherTask : public TaskBase {
    tbb::task* do_execute () {
        tbb::task_group_context  ctx (tbb::task_group_context::bound);
        SimpleRootTask &r = *new( allocate_root(ctx) ) SimpleRootTask(false);
        TRY();
            spawn_root_and_wait(r);
        CATCH();
        ASSERT (!exceptionCaught, "unexpected exception intercepted");
        ThrowTestException(NUM_CHILD_TASKS);
        g_TaskWasCancelled |= is_cancelled();
        return NULL;
    }
};

class BoundHierarchyLauncherTask : public TaskBase {
    bool m_Recover;

    void alloc_roots ( tbb::task_group_context& ctx, tbb::task_list& tl ) {
        for ( size_t i = 0; i < NUM_ROOT_TASKS; ++i )
            tl.push_back( *new( allocate_root(ctx) ) ThrowingRootLauncherTask );
    }

    tbb::task* do_execute () {
        tbb::task_group_context  ctx (tbb::task_group_context::isolated);
        tbb::task_list tl;
        alloc_roots(ctx, tl);
        TRY();
            spawn_root_and_wait(tl);
        CATCH_AND_ASSERT();
        ASSERT (exceptionCaught, "no exception occurred");
        ASSERT (!tl.empty(), "task list was cleared somehow");
        if ( g_SolitaryException )
            ASSERT (g_TaskWasCancelled, "No tasks were cancelled despite of exception");
        if ( m_Recover ) {
            // Test task_group_context::unbind and task_group_context::reset methods
            g_ThrowException = false;
            exceptionCaught = false;
            tl.clear();
            alloc_roots(ctx, tl);
            ctx.reset();
            try {
                spawn_root_and_wait(tl);
            }
            catch (...) {
                exceptionCaught = true;
            }
            ASSERT (!exceptionCaught, "unexpected exception occurred");
        }
        return NULL;
    }
public:
    BoundHierarchyLauncherTask ( bool recover = false ) : m_Recover(recover) {}

}; // class BoundHierarchyLauncherTask

//! Test for bound contexts forming 2 level tree. Exception is thrown on the 1st (root) level.
/** Allocates and spawns a root that spawns a bunch of 2nd level roots sharing 
    the same isolated context, each of which in their turn spawns a single 3rd level 
    root with  the bound context, and these 3rd level roots spawn bunches of leaves 
    in the end. Leaves do not generate exceptions. The test exception is generated 
    by one of the 2nd level roots. **/
void Test6 () {
    ResetGlobals();
    BoundHierarchyLauncherTask &r = *new( tbb::task::allocate_root() ) BoundHierarchyLauncherTask;
    TRY();
        tbb::task::spawn_root_and_wait(r);
    CATCH_AND_ASSERT();
    ASSERT (!exceptionCaught, "unexpected exception intercepted");
    // After the first of the branches (ThrowingRootLauncherTask) completes, 
    // the rest of the task tree may be collapsed before having a chance to execute leaves.
    // A number of branches running concurrently with the first one will be able to spawn leaves though.
    /// \todo: If additional checkpoints are added to scheduler the following assertion must weaken
    intptr_t  num_tasks_expected = 1 + NUM_ROOT_TASKS * (2 + NUM_CHILD_TASKS);
    intptr_t  min_num_tasks_created = 1 + g_NumThreads * 2 + NUM_CHILD_TASKS;
    // 2 stands for BoundHierarchyLauncherTask and SimpleRootTask
    // 1 corresponds to BoundHierarchyLauncherTask 
    intptr_t  min_num_tasks_executed = 2 + 1 + NUM_CHILD_TASKS;
    ASSERT (g_CurStat.Existed() <= num_tasks_expected, "Number of expected tasks is calculated incorrectly");
    ASSERT (g_CurStat.Existed() >= min_num_tasks_created, "Too few tasks created");
    ASSERT (g_CurStat.Executed() >= min_num_tasks_executed, "Too few tasks executed");
    ASSERT_TEST_POSTCOND();
} // void Test6 ()

//! Tests task_group_context::unbind and task_group_context::reset methods.
/** Allocates and spawns a root that spawns a bunch of 2nd level roots sharing 
    the same isolated context, each of which in their turn spawns a single 3rd level 
    root with  the bound context, and these 3rd level roots spawn bunches of leaves 
    in the end. Leaves do not generate exceptions. The test exception is generated 
    by one of the 2nd level roots. **/
void Test7 () {
    ResetGlobals();
    BoundHierarchyLauncherTask &r = *new( tbb::task::allocate_root() ) BoundHierarchyLauncherTask;
    TRY();
        tbb::task::spawn_root_and_wait(r);
    CATCH_AND_ASSERT();
    ASSERT (!exceptionCaught, "unexpected exception intercepted");
    ASSERT_TEST_POSTCOND();
} // void Test6 ()

class BoundHierarchyLauncherTask2 : public TaskBase {
    tbb::task* do_execute () {
        tbb::task_group_context  ctx;
        tbb::task_list  tl;
        for ( size_t i = 0; i < NUM_ROOT_TASKS; ++i )
            tl.push_back( *new( allocate_root(ctx) ) RootLauncherTask(tbb::task_group_context::bound) );
        TRY();
            spawn_root_and_wait(tl);
        CATCH_AND_ASSERT();
        // Exception must be intercepted by RootLauncherTask
        ASSERT (!exceptionCaught, "no exception occurred");
        return NULL;
    }
}; // class BoundHierarchyLauncherTask2

//! Test for bound contexts forming 2 level tree. Exception is thrown in the 2nd (outer) level.
/** Allocates and spawns a root that spawns a bunch of 2nd level roots sharing 
    the same isolated context, each of which in their turn spawns a single 3rd level 
    root with  the bound context, and these 3rd level roots spawn bunches of leaves 
    in the end. The test exception is generated by one of the leaves. **/
void Test8 () {
    ResetGlobals();
    BoundHierarchyLauncherTask2 &r = *new( tbb::task::allocate_root() ) BoundHierarchyLauncherTask2;
    TRY();
        tbb::task::spawn_root_and_wait(r);
    CATCH_AND_ASSERT();
    ASSERT (!exceptionCaught, "unexpected exception intercepted");
    if ( g_SolitaryException )  {
        intptr_t  num_tasks_expected = 1 + NUM_ROOT_TASKS * (2 + NUM_CHILD_TASKS);
        intptr_t  min_num_tasks_created = 1 + g_NumThreads * (2 + NUM_CHILD_TASKS);
        intptr_t  min_num_tasks_executed = num_tasks_expected - (NUM_CHILD_TASKS + 1);
        ASSERT (g_CurStat.Existed() <= num_tasks_expected, "Number of expected tasks is calculated incorrectly");
        ASSERT (g_CurStat.Existed() >= min_num_tasks_created, "Too few tasks created");
        ASSERT (g_CurStat.Executed() >= min_num_tasks_executed, "Too few tasks executed");
    }
    ASSERT_TEST_POSTCOND();
} // void Test8 ()

template<class T>
class CtxLauncherTask : public tbb::task {
    tbb::task_group_context &m_Ctx;

    tbb::task* execute () {
        tbb::task::spawn_root_and_wait( *new( tbb::task::allocate_root(m_Ctx) ) T );
        return NULL;
    }
public:
    CtxLauncherTask ( tbb::task_group_context& ctx ) : m_Ctx(ctx) {}
};

//! Test for cancelling a task hierarchy from outside (from a task running in parallel with it).
void Test9 () {
    ResetGlobals();
    g_ThrowException = false;
    tbb::task_group_context  ctx;
    tbb::task_list  tl;
    tl.push_back( *new( tbb::task::allocate_root() ) CtxLauncherTask<SimpleRootTask>(ctx) );
    tl.push_back( *new( tbb::task::allocate_root() ) CancellatorTask(ctx, NUM_CHILD_TASKS / 4) );
    TRY();
        tbb::task::spawn_root_and_wait(tl);
    CATCH();
    ASSERT (!exceptionCaught, "Cancelling tasks should not cause any exceptions");
    ASSERT (g_CurStat.Executed() <= g_ExecutedAtCatch + g_NumThreads, "Too many tasks were executed after cancellation");
    ASSERT_TEST_POSTCOND();
} // void Test9 ()

template<typename T>
void ThrowMovableException ( intptr_t threshold, const T& data ) {
    if ( IsThrowingThread() )
        return; 
    if ( !g_SolitaryException ) {
        g_ExceptionThrown = 1;
        REMARK ("About to throw one of multiple movable_exceptions... :");
        throw tbb::movable_exception<T>(data);
    }
    while ( g_CurStat.Existed() < threshold )
        __TBB_Yield();
    if ( __TBB_CompareAndSwapW(&g_ExceptionThrown, 1, 0) == 0 ) {
        REMARK ("About to throw solitary movable_exception... :");
        throw tbb::movable_exception<T>(data);
    }
}

const int g_IntExceptionData = -375;
const std::string g_StringExceptionData = "My test string";

// Exception data class implementing minimal requirements of tbb::movable_exception 
class ExceptionData {
    const ExceptionData& operator = ( const ExceptionData& src );
    explicit ExceptionData ( int n ) : m_Int(n), m_String(g_StringExceptionData) {}
public:
    ExceptionData ( const ExceptionData& src ) : m_Int(src.m_Int), m_String(src.m_String) {}
    ~ExceptionData () {}

    int m_Int;
    std::string m_String;

    // Simple way to provide an instance when all initializing constructors are private
    // and to avoid memory reclamation problems.
    static ExceptionData s_data;
};

ExceptionData ExceptionData::s_data(g_IntExceptionData);

typedef tbb::movable_exception<int> SolitaryMovableException;
typedef tbb::movable_exception<ExceptionData> MultipleMovableException;

class LeafTaskWithMovableExceptions : public TaskBase {
    bool m_IntAsData;

    tbb::task* do_execute () {
        Harness::ConcurrencyTracker ct;
        WaitUntilConcurrencyPeaks();
        if ( g_SolitaryException )
            ThrowMovableException<int>(NUM_CHILD_TASKS/2, g_IntExceptionData);
        else
            ThrowMovableException<ExceptionData>(NUM_CHILD_TASKS/2, ExceptionData::s_data);
        return NULL;
    }
};

void CheckException ( tbb::tbb_exception& e ) {
    ASSERT (strcmp(e.name(), (g_SolitaryException ? typeid(SolitaryMovableException) 
                                                   : typeid(MultipleMovableException)).name() ) == 0, 
                                                   "Unexpected original exception name");
    ASSERT (strcmp(e.what(), "tbb::movable_exception") == 0, "Unexpected original exception info ");
    if ( g_SolitaryException ) {
        SolitaryMovableException& me = dynamic_cast<SolitaryMovableException&>(e);
        ASSERT (me.data() == g_IntExceptionData, "Unexpected solitary movable_exception data");
    }
    else {
        MultipleMovableException& me = dynamic_cast<MultipleMovableException&>(e);
        ASSERT (me.data().m_Int == g_IntExceptionData, "Unexpected multiple movable_exception int data");
        ASSERT (me.data().m_String == g_StringExceptionData, "Unexpected multiple movable_exception string data");
    }
}

void CheckException () {
    try {
        throw;
    } catch ( tbb::tbb_exception& e ) {
        CheckException(e);
    }
    catch ( ... ) {
    }
}

//! Test for movable_exception behavior, and external exception recording.
/** Allocates a root task that spawns a bunch of children, one or several of which throw 
    a movable exception in a worker or master thread (depending on the global settings).
    The test also checks the correctness of multiple rethrowing of the pending exception. **/
void Test10 () {
    ResetGlobals();
    tbb::task_group_context ctx;
    tbb::empty_task *r = new( tbb::task::allocate_root() ) tbb::empty_task;
    ASSERT (!g_CurStat.Existing() && !g_CurStat.Existed() && !g_CurStat.Executed(), 
            "something wrong with the task accounting");
    r->set_ref_count(NUM_CHILD_TASKS + 1);
    for ( int i = 0; i < NUM_CHILD_TASKS; ++i )
        r->spawn( *new( r->allocate_child() ) LeafTaskWithMovableExceptions );
    TRY()
        r->wait_for_all();
    } catch ( ... ) {
        ASSERT (!ctx.is_group_execution_cancelled(), "");
        CheckException();
        try {
            throw;
        } catch ( tbb::tbb_exception& e ) {
            CheckException(e);
            g_ExceptionCaught = exceptionCaught = true;
        }
        catch ( ... ) {
            g_ExceptionCaught = true;
            g_UnknownException = unknownException = true;
        }
        ctx.register_pending_exception();
        ASSERT (ctx.is_group_execution_cancelled(), "After exception registration the context must be in the cancelled state");
    }
    r->destroy(*r);
    ASSERT_EXCEPTION();
    ASSERT_TEST_POSTCOND();

    r = new( tbb::task::allocate_root(ctx) ) tbb::empty_task;
    r->set_ref_count(1);
    g_ExceptionCaught = g_UnknownException = false;
    try {
        r->wait_for_all();
    } catch ( tbb::tbb_exception& e ) {
        CheckException(e);
        g_ExceptionCaught = true;
    }
    catch ( ... ) {
        g_ExceptionCaught = true;
        g_UnknownException = true;
    }
    ASSERT (g_ExceptionCaught, "no exception occurred");
    ASSERT (!g_UnknownException, "unknown exception was caught");
    r->destroy(*r);
} // void Test10 ()


const int MaxNestingDepth = 256;

class CtxDestroyerTask : public tbb::task {
    int m_nestingLevel;

    tbb::task* execute () {
        ASSERT ( m_nestingLevel >= 0 && m_nestingLevel < MaxNestingDepth, "Wrong nesting level. The test is broken" );
        tbb::task_group_context  ctx;
        tbb::task *t = new( tbb::task::allocate_root(ctx) ) tbb::empty_task;
        int level = ++m_nestingLevel;
        if ( level < MaxNestingDepth ) {
            execute();
        }
        else {
            CancellatorTask::WaitUntilReady();
            ++g_CurExecuted;
        }
        if ( ctx.is_group_execution_cancelled() )
            ++s_numCancelled;
        t->destroy(*t);
        return NULL;
    }
public:
    CtxDestroyerTask () : m_nestingLevel(0) { s_numCancelled = 0; }

    static int s_numCancelled;
};

int CtxDestroyerTask::s_numCancelled = 0;

//! Test for data race between cancellation propagation and context destruction.
/** If the data race ever occurs, an assertion inside TBB will be triggered. **/
void TestCtxDestruction () {
    for ( size_t i = 0; i < 10; ++i ) {
        tbb::task_group_context  ctx;
        tbb::task_list  tl;
        ResetGlobals();
        g_BoostExecutedCount = false;
        g_ThrowException = false;
        CancellatorTask::Reset();
        // CtxLauncherTask just runs some work to cancel
        //tl.push_back( *new( tbb::task::allocate_root() ) CtxLauncherTask<SimpleRootTask>(ctx) );
        tl.push_back( *new( tbb::task::allocate_root() ) CtxLauncherTask<CtxDestroyerTask>(ctx) );
        tl.push_back( *new( tbb::task::allocate_root() ) CancellatorTask(ctx, 1) );
        tbb::task::spawn_root_and_wait(tl);
        ASSERT( g_CurExecuted == 1, "Test is broken" );
        ASSERT( CtxDestroyerTask::s_numCancelled <= MaxNestingDepth, "Test is broken" );
    }
} // void TestCtxDestruction()

void RunTests ()
{
    REMARK ("Number of threads %d", g_NumThreads);
    tbb::task_scheduler_init init (g_NumThreads);
    g_Master = Harness::CurrentTid();
    Test1();
    Test2();
    Test3();
    Test4();
    Test5();
    Test6();
    Test7();
    Test8();
    Test9();
    Test10();
    TestCtxDestruction();
}
#endif /* __TBB_EXCEPTIONS */

__TBB_TEST_EXPORT
int main(int argc, char* argv[]) {
    ParseCommandLine( argc, argv );
    MinThread = min(NUM_ROOTS_IN_GROUP, max(2, MinThread));
    MaxThread = min(NUM_ROOTS_IN_GROUP, max(MinThread, MaxThread));
    ASSERT (NUM_ROOTS_IN_GROUP < NUM_ROOT_TASKS, "Fix defines");
#if __TBB_EXCEPTIONS
    // Test0 always runs on one thread
    Test0();
    for ( g_NumThreads = MinThread; g_NumThreads <= MaxThread; ++g_NumThreads ) {
        for ( size_t j = 0; j < 2; ++j ) {
            g_SolitaryException = (j & 2) == 1;
            RunTests();
        }
    }
    REPORT("done\n");
#else
    REPORT("skipped\n");
#endif /* __TBB_EXCEPTIONS */
    return 0;
}
