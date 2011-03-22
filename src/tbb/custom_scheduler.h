/*
    Copyright 2005-2011 Intel Corporation.  All Rights Reserved.

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

#ifndef _TBB_custom_scheduler_H
#define _TBB_custom_scheduler_H

#include "scheduler.h"
#include "observer_proxy.h"
#include "itt_notify.h"

namespace tbb {
namespace internal {

//! Amount of time to pause between steals.
/** The default values below were found to be best empirically for K-Means
    on the 32-way Altix and 4-way (*2 for HT) fxqlin04. */
#if __TBB_ipf
static const long PauseTime = 1500;
#else 
static const long PauseTime = 80;
#endif

//------------------------------------------------------------------------
//! Traits classes for scheduler
//------------------------------------------------------------------------

struct DefaultSchedulerTraits {
    static const bool itt_possible = true;
    static const bool has_slow_atomic = false;
};

struct IntelSchedulerTraits {
    static const bool itt_possible = false;
#if __TBB_x86_32||__TBB_x86_64
    static const bool has_slow_atomic = true;
#else
    static const bool has_slow_atomic = false;
#endif /* __TBB_x86_32||__TBB_x86_64 */
};

//------------------------------------------------------------------------
// custom_scheduler
//------------------------------------------------------------------------

//! A scheduler with a customized evaluation loop.
/** The customization can use SchedulerTraits to make decisions without needing a run-time check. */
template<typename SchedulerTraits>
class custom_scheduler: private generic_scheduler {
    typedef custom_scheduler<SchedulerTraits> scheduler_type;

    //! Scheduler loop that dispatches tasks.
    /** If child is non-NULL, it is dispatched first.
        Then, until "parent" has a reference count of 1, other task are dispatched or stolen. */
    /*override*/
    void local_wait_for_all( task& parent, task* child );

    //! Entry point from client code to the scheduler loop that dispatches tasks. 
    /** The method is virtual, but the *this object is used only for sake of dispatching on the correct vtable,
        not necessarily the correct *this object.  The correct *this object is looked up in TLS. */
    /*override*/
    void wait_for_all( task& parent, task* child ) {
        static_cast<custom_scheduler*>(governor::local_scheduler())->scheduler_type::local_wait_for_all( parent, child );
    }

    //! Construct a custom_scheduler
    custom_scheduler( arena* a, size_t index ) : generic_scheduler(a, index) {}

    //! Decrements ref_count of a predecessor.
    /** If it achieves 0, the predecessor is scheduled for execution.
        When changing, remember that this is a hot path function. */
    void tally_completion_of_predecessor( task& s, task*& bypass_slot ) {
        task_prefix& p = s.prefix();
        if( SchedulerTraits::itt_possible )
            ITT_NOTIFY(sync_releasing, &p.ref_count);
        if( SchedulerTraits::has_slow_atomic && p.ref_count==1 ) {
            p.ref_count=0;
        } else {
            if( __TBB_FetchAndDecrementWrelease(&p.ref_count) > 1 ) // more references exist
                return;
        }
        __TBB_ASSERT(p.ref_count==0, "completion of task caused predecessor's reference count to underflow");
        if( SchedulerTraits::itt_possible )
            ITT_NOTIFY(sync_acquired, &p.ref_count);
#if TBB_USE_ASSERT
        p.extra_state &= ~es_ref_count_active;
#endif /* TBB_USE_ASSERT */

        if( bypass_slot==NULL )
            bypass_slot = &s;
        else
            local_spawn( s, s.prefix().next );
    }

public:
    static generic_scheduler* allocate_scheduler( arena* a, size_t index ) {
        scheduler_type* s = (scheduler_type*)NFS_Allocate(sizeof(scheduler_type),1,NULL);
        new( s ) scheduler_type( a, index );
        s->assert_task_pool_valid();
        ITT_SYNC_CREATE(s, SyncType_Scheduler, SyncObj_TaskPoolSpinning);
        return s;
    }

    //! Try getting a task from the mailbox or stealing from another scheduler.
    /** Returns the stolen task or NULL if all attempts fail. */
    /* override */ task* receive_or_steal_task( reference_count&, bool );

}; // class custom_scheduler<>

//------------------------------------------------------------------------
// custom_scheduler methods
//------------------------------------------------------------------------

template<typename SchedulerTraits>
task* custom_scheduler<SchedulerTraits>::receive_or_steal_task( reference_count& completion_ref_count,
                                                                bool return_if_no_work ) {
    task* t = NULL;
    my_inbox.set_is_idle( true );
    // The state "failure_count==-1" is used only when itt_possible is true,
    // and denotes that a sync_prepare has not yet been issued.
    for( int failure_count = -static_cast<int>(SchedulerTraits::itt_possible);; ++failure_count) {
        if( completion_ref_count==1 ) {
            if( SchedulerTraits::itt_possible ) {
                if( failure_count!=-1 ) {
                    ITT_NOTIFY(sync_prepare, &completion_ref_count);
                    // Notify Intel(R) Thread Profiler that thread has stopped spinning.
                    ITT_NOTIFY(sync_acquired, this);
                }
                ITT_NOTIFY(sync_acquired, &completion_ref_count);
            }
            __TBB_ASSERT( !t, NULL );
            break; // exit stealing loop and return;
        }
        __TBB_ASSERT( my_arena->my_limit > 0, NULL );
        size_t n = my_arena->my_limit;
        __TBB_ASSERT( my_arena_index < n, NULL );
        if ( n > 1 ) {
            // Check if the resource manager requires our arena to relinquish some threads 
            if ( return_if_no_work && my_arena->my_num_workers_allotted < my_arena->num_workers_active() ) {
                __TBB_ASSERT( is_worker(), NULL );
                if( SchedulerTraits::itt_possible && failure_count != -1 )
                    ITT_NOTIFY(sync_cancel, this);
                return NULL;
            }
            // Check if there are tasks mailed to this thread via task-to-thread affinity mechanism.
            if ( my_affinity_id && (t=get_mailbox_task()) ) {
                GATHER_STATISTIC( ++my_counters.mails_received );
            }
            // Check if there are tasks in starvation-resistant stream.
            // Only allowed for workers with empty stack, which is identified by return_if_no_work.
            else if ( return_if_no_work && (t=dequeue_task()) ) {
                // just proceed with the obtained task
            }
#if __TBB_TASK_PRIORITY
            // Check if any earlier offloaded non-top priority tasks become returned to the top level
            else if ( my_offloaded_tasks && (t=reload_tasks()) ) {
                // just proceed with the obtained task
            }
#endif /* __TBB_TASK_PRIORITY */
            else if ( can_steal() && n > 1 ) {
                // Try to steal a task from a random victim.
                size_t k = my_random.get() % (n - 1);
                arena_slot* victim = &my_arena->my_slots[k];
                // The following condition excludes the master that might have 
                // already taken our previous place in the arena from the list .
                // of potential victims. But since such a situation can take 
                // place only in case of significant oversubscription, keeping
                // the checks simple seems to be preferable to complicating the code.
                if( k >= my_arena_index )
                    ++victim;               // Adjusts random distribution to exclude self
                t = steal_task( *victim );
                if( !t ) goto fail;
                if( is_proxy(*t) ) {
                    t = strip_proxy((task_proxy*)t);
                    if( !t ) goto fail;
                    GATHER_STATISTIC( ++my_counters.proxies_stolen );
                }
                t->prefix().extra_state |= es_task_is_stolen;
                if( is_version_3_task(*t) ) {
                    my_innermost_running_task = t;
                    t->prefix().owner = this;
                    t->note_affinity( my_affinity_id );
                }
                GATHER_STATISTIC( ++my_counters.steals_committed );
            } // end of stealing branch
            else
                goto fail;
        } // end of nonlocal retrieval branch
        else {
            // This is the only thread in this arena, so nowhere to steal
#if __TBB_TASK_PRIORITY
            if ( !my_offloaded_tasks || !(t = reload_tasks()) )
#endif /* __TBB_TASK_PRIORITY */
                goto fail;
        }
        // A task was successfully obtained somewhere
        __TBB_ASSERT(t,NULL);
#if __TBB_SCHEDULER_OBSERVER
        // No memory fence required for read of global_last_observer_proxy, because prior fence on steal/mailbox suffices.
        if( my_local_last_observer_proxy!=global_last_observer_proxy ) {
            notify_entry_observers();
        }
#endif /* __TBB_SCHEDULER_OBSERVER */
        if ( SchedulerTraits::itt_possible && failure_count != -1 ) {
            // FIXME - might be victim, or might be selected from a mailbox
            // Notify Intel(R) Thread Profiler that thread has stopped spinning.
            ITT_NOTIFY(sync_acquired, this);
        }
        break; // exit stealing loop and return
fail:
        GATHER_STATISTIC( ++my_counters.steals_failed );
        if( SchedulerTraits::itt_possible && failure_count==-1 ) {
            // The first attempt to steal work failed, so notify Intel(R) Thread Profiler that
            // the thread has started spinning.  Ideally, we would do this notification
            // *before* the first failed attempt to steal, but at that point we do not
            // know that the steal will fail.
            ITT_NOTIFY(sync_prepare, this);
            failure_count = 0;
        }
        // Pause, even if we are going to yield, because the yield might return immediately.
        __TBB_Pause(PauseTime);
        int yield_threshold = 2*int(n);
        if( failure_count>=yield_threshold ) {
            __TBB_Yield();
#if __TBB_TASK_PRIORITY
            // Check if there are tasks abandoned by other workers
            if ( my_arena->my_orphaned_tasks ) {
                // Epoch must be advanced before seizing the list pointer
                ++my_arena->my_abandonment_epoch;
                task* orphans = (task*)__TBB_FetchAndStoreW( &my_arena->my_orphaned_tasks, 0 );
                if ( orphans ) {
                    task** link = NULL;
                    // Get local counter out of the way (we've just brought in external tasks)
                    my_local_reload_epoch = 0;
                    t = reload_tasks( orphans, link, *my_ref_top_priority );
                    if ( orphans ) {
                        *link = my_offloaded_tasks;
                        if ( !my_offloaded_tasks )
                            my_offloaded_task_list_tail_link = link;
                        my_offloaded_tasks = orphans;
                    }
                    __TBB_ASSERT( !my_offloaded_tasks == !my_offloaded_task_list_tail_link, NULL );
                    if ( t ) {
                        if( SchedulerTraits::itt_possible )
                            ITT_NOTIFY(sync_cancel, this);
                        break; // exit stealing loop and return
                    }
                }
            }
#endif /* __TBB_TASK_PRIORITY */
            if( failure_count>=yield_threshold+100 ) {
                // When a worker thread has nothing to do, return it to RML.
                // For purposes of affinity support, the thread is considered idle while in RML.
#if __TBB_TASK_PRIORITY
                if( return_if_no_work || my_arena->my_top_priority > my_arena->my_bottom_priority ) {
                    if ( my_arena->is_out_of_work() && return_if_no_work ) {
#else /* !__TBB_TASK_PRIORITY */
                    if ( return_if_no_work && my_arena->is_out_of_work() ) {
#endif /* !__TBB_TASK_PRIORITY */
                        if( SchedulerTraits::itt_possible )
                            ITT_NOTIFY(sync_cancel, this);
                        return NULL;
                    }
#if __TBB_TASK_PRIORITY
                }
                if ( my_offloaded_tasks ) {
                    // Safeguard against any sloppiness in managing reload epoch
                    // counter (e.g. on the hot path bacause of performnace reasons).
                    my_local_reload_epoch = 0;
                    // Break the deadlock caused by a higher priority dispatch loop
                    // stealing and offloading a lower priority task. Priority check
                    // at the stealing moment cannot completely preclude such cases
                    // because priorities can changes dynamically.
                    if ( !return_if_no_work && *my_ref_top_priority > my_arena->my_top_priority ) {
                        GATHER_STATISTIC( ++my_counters.prio_ref_fixups );
                        my_ref_top_priority = &my_arena->my_top_priority;
                        my_ref_reload_epoch = &my_arena->my_reload_epoch;
                    }
                }
#endif /* __TBB_TASK_PRIORITY */
                failure_count = yield_threshold;
            } // end of arena snapshot branch
        } // end of yielding branch
    } // end of nonlocal task retrieval loop
    my_inbox.set_is_idle( false );
    return t;
}

template<typename SchedulerTraits>
void custom_scheduler<SchedulerTraits>::local_wait_for_all( task& parent, task* child ) {
    __TBB_ASSERT( governor::is_set(this), NULL );
    __TBB_ASSERT( parent.ref_count() >= (child && child->parent() == &parent ? 2 : 1), "ref_count is too small" );
    assert_task_pool_valid();
    // Using parent's refcount in sync_prepare (in the stealing loop below) is 
    // a workaround for TP. We need to name it here to display correctly in Ampl.
    if( SchedulerTraits::itt_possible )
        ITT_SYNC_CREATE(&parent.prefix().ref_count, SyncType_Scheduler, SyncObj_TaskStealingLoop);
#if __TBB_TASK_GROUP_CONTEXT
    __TBB_ASSERT( parent.prefix().context || (is_worker() && &parent == my_dummy_task), "parent task does not have context" );
#endif /* __TBB_TASK_GROUP_CONTEXT */
    task* t = child;
    // Constant all_local_work_done is an unreacheable refcount value that prevents
    // early quitting the dispatch loop. It is defined to be in the middle of the range 
    // of negative values representable by the reference_count type.
    static const reference_count 
        // For normal dispatch loops
        parents_work_done = 1,
        // For termination dispatch loops in masters
        all_local_work_done = (reference_count)3 << (sizeof(reference_count) * 8 - 2);
    reference_count quit_point;
#if __TBB_TASK_PRIORITY
    __TBB_ASSERT( (uintptr_t)*my_ref_top_priority < (uintptr_t)num_priority_levels, NULL );
    volatile intptr_t *old_ref_top_priority = my_ref_top_priority;
    // When entering nested parallelism level market level counter
    // must be replaced with the one local to this arena.
    volatile uintptr_t *old_ref_reload_epoch = my_ref_reload_epoch;
    task* old_dispatching_task = my_dispatching_task;
    my_dispatching_task = my_innermost_running_task;
#else /* !__TBB_TASK_PRIORITY */
    task* old_innermost_running_task = my_innermost_running_task;
#endif /* __TBB_TASK_PRIORITY */
    if( master_outermost_level() ) {
        // We are in the outermost task dispatch loop of a master thread,
        __TBB_ASSERT( !is_worker(), NULL );
        quit_point = &parent == my_dummy_task ? all_local_work_done : parents_work_done;
    } else {
        quit_point = parents_work_done;
#if __TBB_TASK_PRIORITY
        if ( &parent != my_dummy_task ) {
            // We are in a nested dispatch loop.
            // Market or arena priority must not prevent child tasks from being
            // executed so that dynamic priority changes did not cause deadlock.
            my_ref_top_priority = &parent.prefix().context->my_priority;
            my_ref_reload_epoch = &my_arena->my_reload_epoch;
        }
#endif /* __TBB_TASK_PRIORITY */
    }
#if __TBB_TASK_GROUP_CONTEXT && TBB_USE_EXCEPTIONS
    // Infinite safeguard EH loop
    for (;;) {
    try {
#endif /* __TBB_TASK_GROUP_CONTEXT && TBB_USE_EXCEPTIONS */
    // Outer loop receives tasks from global environment (via mailbox, FIFO queue(s),
    // and by  stealing from other threads' task pools).
    // All exit points from the dispatch loop are located in its immediate scope.
    for(;;) {
        // Middle loop retrieves tasks from the local task pool.
        do {
            // Inner loop evaluates tasks coming from nesting loops and those returned 
            // by just executed tasks (bypassing spawn or enqueue calls).
            while(t) {
                __TBB_ASSERT( my_inbox.is_idle_state(false), NULL );
                __TBB_ASSERT(!is_proxy(*t),"unexpected proxy");
                __TBB_ASSERT( t->prefix().owner, NULL );
                assert_task_valid(*t);
#if __TBB_TASK_GROUP_CONTEXT && TBB_USE_ASSERT
                if ( !t->prefix().context->my_cancellation_requested ) 
#endif
                __TBB_ASSERT( 1L<<t->state() & (1L<<task::allocated|1L<<task::ready|1L<<task::reexecute), NULL );
                assert_task_pool_valid();
#if __TBB_TASK_PRIORITY
                intptr_t p = priority(*t);
                assert_priority_valid(p);
                if ( p != *my_ref_top_priority ) {
                    if ( p != my_arena->my_top_priority ) {
                        my_market->update_arena_priority( *my_arena, p );
                    }
                    if ( p < *my_ref_top_priority ) {
                        if ( !my_offloaded_tasks ) {
                            my_offloaded_task_list_tail_link = &t->prefix().next_offloaded;
                            // Erase possible reference to the owner scheduler (next_offloaded is a union member)
                            *my_offloaded_task_list_tail_link = NULL;
                        }
                        offload_task( *t, p );
                        if ( in_arena() ) {
                            t = winnow_task_pool();
                            if ( t )
                                continue;
                        }
                        else {
                            // Mark arena as full to unlock arena priority level adjustment 
                            // by arena::is_out_of_work(), and ensure worker's presence.
                            my_arena->advertise_new_work<false>();
                        }
                        goto stealing_ground;
                    }
                }
#endif /* __TBB_TASK_PRIORITY */
                task* t_next = NULL;
                my_innermost_running_task = t;
                t->prefix().owner = this;
                t->prefix().state = task::executing;
#if __TBB_TASK_GROUP_CONTEXT
                if ( !t->prefix().context->my_cancellation_requested )
#endif
                {
                    GATHER_STATISTIC( ++my_counters.tasks_executed );
                    GATHER_STATISTIC( my_counters.avg_arena_concurrency += my_arena->my_num_threads_active );
                    GATHER_STATISTIC( my_counters.avg_assigned_workers += my_arena->my_num_workers_allotted );
#if __TBB_TASK_PRIORITY
                    GATHER_STATISTIC( my_counters.avg_arena_prio += p );
                    GATHER_STATISTIC( my_counters.avg_market_prio += my_market->my_global_top_priority );
#endif /* __TBB_TASK_PRIORITY */
                    ITT_STACK(SchedulerTraits::itt_possible, callee_enter, t->prefix().context->itt_caller);
                    t_next = t->execute();
                    ITT_STACK(SchedulerTraits::itt_possible, callee_leave, t->prefix().context->itt_caller);
                    if (t_next) {
                        __TBB_ASSERT( t_next->state()==task::allocated,
                                "if task::execute() returns task, it must be marked as allocated" );
                        t_next->prefix().extra_state &= ~es_task_is_stolen;
#if TBB_USE_ASSERT
                        affinity_id next_affinity=t_next->prefix().affinity;
                        if (next_affinity != 0 && next_affinity != my_affinity_id)
                            GATHER_STATISTIC( ++my_counters.affinity_ignored );
#endif
                    }
                }
                assert_task_pool_valid();
                switch( task::state_type(t->prefix().state) ) {
                    case task::executing: {
                        task* s = t->parent();
                        __TBB_ASSERT( my_innermost_running_task==t, NULL );
                        __TBB_ASSERT( t->prefix().ref_count==0, "Task still has children after it has been executed" );
                        t->~task();
                        if( s )
                            tally_completion_of_predecessor(*s, t_next);
                        free_task<no_hint>( *t );
                        assert_task_pool_valid();
                        break;
                    }

                    case task::recycle: // set by recycle_as_safe_continuation()
                        t->prefix().state = task::allocated;
                        __TBB_ASSERT( t_next != t, "a task returned from method execute() can not be recycled in another way" );
                        t->prefix().extra_state &= ~es_task_is_stolen;
                        // for safe continuation, need atomically decrement ref_count;
                        tally_completion_of_predecessor(*t, t_next);
                        assert_task_pool_valid();
                        break;

                    case task::reexecute: // set by recycle_to_reexecute()
                        __TBB_ASSERT( t_next, "reexecution requires that method execute() return another task" );
                        __TBB_ASSERT( t_next != t, "a task returned from method execute() can not be recycled in another way" );
                        t->prefix().state = task::allocated;
                        t->prefix().extra_state &= ~es_task_is_stolen;
                        local_spawn( *t, t->prefix().next );
                        assert_task_pool_valid();
                        break;
                    case task::allocated:
                        t->prefix().extra_state &= ~es_task_is_stolen;
                        break;
#if TBB_USE_ASSERT
                    case task::ready:
                        __TBB_ASSERT( false, "task is in READY state upon return from method execute()" );
                        break;
                    default:
                        __TBB_ASSERT( false, "illegal state" );
#else
                    default: // just to shut up some compilation warnings
                        break;
#endif /* TBB_USE_ASSERT */
                }
                GATHER_STATISTIC( t_next ? ++my_counters.spawns_bypassed : 0 );
                t = t_next;
            } // end of scheduler bypass loop

            assert_task_pool_valid();
            if ( parent.prefix().ref_count == quit_point )
                break;
            if ( in_arena() )
                t = get_task();
            else
                __TBB_ASSERT( my_arena_slot->head == 0 && my_arena_slot->tail == 0, NULL );
            __TBB_ASSERT(!t || !is_proxy(*t),"unexpected proxy");
            assert_task_pool_valid();
        } while( t ); // end of local task pool retrieval loop

#if __TBB_TASK_PRIORITY
stealing_ground:
        task* &dispatching_task = my_dispatching_task;
        if ( worker_outermost_level() && my_arena->my_skipped_fifo_priority ) {
            // This thread can dequeue FIFO tasks, and some priority levels of 
            // FIFO tasks have been bypassed (to prevent deadlock caused by 
            // dynamic priority changes in nested task group hierarchy).
            intptr_t skipped_priority = my_arena->my_skipped_fifo_priority;
            if ( my_arena->my_skipped_fifo_priority.compare_and_swap(0, skipped_priority) == skipped_priority &&
                 skipped_priority > my_arena->my_top_priority )
            {
                my_market->update_arena_priority( *my_arena, skipped_priority );
            }
        }
#else /* !__TBB_TASK_PRIORITY */
        task* &dispatching_task = old_innermost_running_task;
#endif /* __TBB_TASK_PRIORITY */
        if ( quit_point == all_local_work_done ) {
            __TBB_ASSERT( my_arena_slot == &my_dummy_slot
                          && my_arena_slot->head == 0 && my_arena_slot->tail == 0, NULL );
            my_innermost_running_task = dispatching_task;
#if __TBB_TASK_PRIORITY
            my_dispatching_task = old_dispatching_task;
            my_ref_top_priority = old_ref_top_priority;
            my_ref_reload_epoch = old_ref_reload_epoch;
#endif /* __TBB_TASK_PRIORITY */
            return;
        }
        __TBB_ASSERT( my_arena->my_max_num_workers > 0 || my_market->my_ref_count > 1
                      || parent.prefix().ref_count == 1, "deadlock detected" );
        // Dispatching task pointer is NULL *iff* this is a worker thread in its outermost
        // dispatch loop (i.e. its execution stack is empty). In this case it should exit it
        // either when there is no more work in the current arena, or when revoked by the market.
        t = receive_or_steal_task( parent.prefix().ref_count, !dispatching_task );
        if (!t) {
            my_innermost_running_task = dispatching_task;
#if __TBB_TASK_PRIORITY
            my_dispatching_task = old_dispatching_task;
            my_ref_top_priority = old_ref_top_priority;
            my_ref_reload_epoch = old_ref_reload_epoch;
#endif /* __TBB_TASK_PRIORITY */
            if ( !ConcurrentWaitsEnabled(parent) ) {
                if ( parent.prefix().ref_count != parents_work_done ) {
                    // This is a worker that was revoked by the market.
                    __TBB_ASSERT( is_worker() && !dispatching_task,
                                  "Worker thread exits nested dispatch loop prematurely" );
                    return;
                }
                parent.prefix().ref_count = 0;
            }
#if TBB_USE_ASSERT
            parent.prefix().extra_state &= ~es_ref_count_active;
#endif /* TBB_USE_ASSERT */
            goto done;
        }
        __TBB_ASSERT(t,NULL);
        __TBB_ASSERT(!is_proxy(*t),"unexpected proxy");
    } // end of infinite stealing loop
    __TBB_ASSERT( false, "Must never get here" );
#if __TBB_TASK_GROUP_CONTEXT && TBB_USE_EXCEPTIONS
    } // end of try-block
    TbbCatchAll( t->prefix().context );
    // Complete post-processing ...
    if( task::state_type(t->prefix().state) == task::recycle ) {
        // ... for tasks recycled with recycle_as_safe_continuation
        t->prefix().state = task::allocated;
        // for safe continuation, need to atomically decrement ref_count;
        if( SchedulerTraits::itt_possible )
            ITT_NOTIFY(sync_releasing, &t->prefix().ref_count);
        if( __TBB_FetchAndDecrementWrelease(&t->prefix().ref_count)==1 ) {
            if( SchedulerTraits::itt_possible )
                ITT_NOTIFY(sync_acquired, &t->prefix().ref_count);
        }else{
            t = NULL;
        }
    }
    } // end of infinite EH loop
#endif /* __TBB_TASK_GROUP_CONTEXT && TBB_USE_EXCEPTIONS */
    __TBB_ASSERT( false, "Must never get here too" );
done:
#if __TBB_TASK_GROUP_CONTEXT
    __TBB_ASSERT(parent.prefix().context && default_context(), NULL);
    task_group_context* parent_ctx = parent.prefix().context;
    if ( parent_ctx->my_cancellation_requested ) {
        task_group_context::exception_container_type *pe = parent_ctx->my_exception;
        if ( master_outermost_level() && parent_ctx == default_context() ) {
            // We are in the outermost task dispatch loop of a master thread, and 
            // the whole task tree has been collapsed. So we may clear cancellation data.
            parent_ctx->my_cancellation_requested = 0;
            // TODO: Add assertion that master's dummy task context does not have children
            parent_ctx->my_state &= ~(uintptr_t)task_group_context::may_have_children;
        }
        if ( pe )
            pe->throw_self();
    }
    __TBB_ASSERT(!is_worker() || !CancellationInfoPresent(*my_dummy_task), 
        "Worker's dummy task context modified");
    __TBB_ASSERT(!master_outermost_level() || !CancellationInfoPresent(*my_dummy_task), 
        "Unexpected exception or cancellation data in the master's dummy task");
#endif /* __TBB_TASK_GROUP_CONTEXT */
    assert_task_pool_valid();
}

} // namespace internal
} // namespace tbb

#endif /* _TBB_custom_scheduler_H */
