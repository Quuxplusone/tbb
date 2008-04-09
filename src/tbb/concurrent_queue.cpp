/*
    Copyright 2005-2008 Intel Corporation.  All Rights Reserved.

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

#include <cstring>
#include <cstdio>
#include "tbb/tbb_machine.h"
#include "tbb/cache_aligned_allocator.h"
#include "tbb/spin_mutex.h"
#include "tbb/atomic.h"
#include "tbb/concurrent_queue.h"
#include "tbb_misc.h"
#include "itt_notify.h"
#if __SUNPRO_CC
#include <memory.h>
#endif

using namespace std;

#define __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE 1

#if defined(_MSC_VER) && defined(_Wp64)
    // Workaround for overzealous compiler warnings in /Wp64 mode
    #pragma warning (disable: 4267)
#endif /* _MSC_VER && _Wp64 */

#define RECORD_EVENTS 0


#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
#if !_WIN32&&!_WIN64&&!__TBB_USE_FUTEX
#define __TBB_USE_PTHREAD_CONDWAIT 1
#include <pthread.h>
#endif
#endif

namespace tbb {

namespace internal {

class concurrent_queue_rep;

//! A queue using simple locking.
/** For efficient, this class has no constructor.  
    The caller is expected to zero-initialize it. */
struct micro_queue {
    typedef concurrent_queue_base::page page;
    typedef size_t ticket;

    friend class micro_queue_pop_finalizer;

    atomic<page*> head_page;
    atomic<ticket> head_counter;

    atomic<page*> tail_page;
    atomic<ticket> tail_counter;

    spin_mutex page_mutex;
    
    class push_finalizer {
        ticket my_ticket;
        micro_queue& my_queue;
    public:
        push_finalizer( micro_queue& queue, ticket k ) :
            my_ticket(k), my_queue(queue)
        {}
        ~push_finalizer() {
            my_queue.tail_counter = my_ticket;
        }
    };

    void push( const void* item, ticket k, concurrent_queue_base& base );

    bool pop( void* dst, ticket k, concurrent_queue_base& base );
};

class micro_queue_pop_finalizer {
    typedef concurrent_queue_base::page page;
    typedef size_t ticket;
    ticket my_ticket;
    micro_queue& my_queue;
    page* my_page; 
    concurrent_queue_base &base;
public:
    micro_queue_pop_finalizer( micro_queue& queue, concurrent_queue_base& b, ticket k, page* p ) :
        my_ticket(k), my_queue(queue), my_page(p), base(b)
    {}
    ~micro_queue_pop_finalizer() {
        page* p = my_page;
        if( p ) {
            spin_mutex::scoped_lock lock( my_queue.page_mutex );
            page* q = p->next;
            my_queue.head_page = q;
            if( !q ) {
                my_queue.tail_page = NULL;
            }
        }
        my_queue.head_counter = my_ticket;
        if( p )
           base.deallocate_page( p );
    }
};

//! Internal representation of a ConcurrentQueue.
/** For efficient, this class has no constructor.  
    The caller is expected to zero-initialize it. */
class concurrent_queue_rep {
public:
    typedef size_t ticket;

#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE

#if _WIN32||_WIN64
    typedef HANDLE cq_cond_t;
#elif __TBB_USE_FUTEX
    typedef int cq_cond_t;
#else /* including MacOS */
    typedef pthread_cond_t  cq_cond_t;
    typedef pthread_mutex_t cq_mutex_t;
#endif

#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */

private:
    friend struct micro_queue;

    //! Approximately n_queue/golden ratio
    static const size_t phi = 3;

public:
    //! Must be power of 2
    static const size_t n_queue = 8; 

    //! Map ticket to an array index
    static size_t index( ticket k ) {
        return k*phi%n_queue;
    }

#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE

    atomic<ticket> head_counter;
#if !__TBB_USE_PTHREAD_CONDWAIT
    atomic<size_t> n_waiting_consumers;
    cq_cond_t  var_wait_for_items;
    char pad1[NFS_MaxLineSize-sizeof(size_t)-sizeof(atomic<size_t>)-sizeof(cq_cond_t)];
#else /* including MacOS */
    size_t n_waiting_consumers;
    cq_cond_t  var_wait_for_items;
    cq_mutex_t mtx_items_avail;
    char pad1[NFS_MaxLineSize-sizeof(size_t)-sizeof(atomic<size_t>)-sizeof(cq_cond_t)-sizeof(cq_mutex_t)];
#endif /* !__TBB_USE_PTHREAD_CONDWAIT */

    atomic<ticket> tail_counter;
#if !__TBB_USE_PTHREAD_CONDWAIT
    atomic<size_t> n_waiting_producers;
    cq_cond_t  var_wait_for_slots;
    char pad2[NFS_MaxLineSize-sizeof(size_t)-sizeof(atomic<size_t>)-sizeof(cq_cond_t)];
#else /* including MacOS */
    size_t n_waiting_producers;
    cq_cond_t  var_wait_for_slots;
    cq_mutex_t mtx_slots_avail;
    char pad2[NFS_MaxLineSize-sizeof(ticket)-sizeof(atomic<size_t>)-sizeof(cq_cond_t)-sizeof(cq_mutex_t)];
#endif /* !__TBB_USE_PTHREAD_CONDWAIT */

#else /* !__TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */

    atomic<ticket> head_counter;
    char pad1[NFS_MaxLineSize-sizeof(atomic<ticket>)];

    atomic<ticket> tail_counter;
    char pad2[NFS_MaxLineSize-sizeof(atomic<ticket>)];

#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */

    micro_queue array[n_queue];    

#if !__TBB_USE_PTHREAD_CONDWAIT
    ptrdiff_t size_to_use;
    atomic<size_t> nthreads_in_transition;
    ptrdiff_t nthreads_to_read_size;
#define __TBB_INVALID_QSIZE (concurrent_queue_rep::infinite_capacity)
    static const ptrdiff_t thread_woken = -1;
#endif /* !__TBB_USE_PTHREAD_CONDWAIT */

    micro_queue& choose( ticket k ) {
        // The formula here approximates LRU in a cache-oblivious way.
        return array[index(k)];
    }

    //! Value for effective_capacity that denotes unbounded queue.
    static const ptrdiff_t infinite_capacity = ptrdiff_t(~size_t(0)/2);
};

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning( push )
// unary minus operator applied to unsigned type, result still unsigned
#pragma warning( disable: 4146 )
#endif /* _MSC_VER && !defined(__INTEL_COMPILER) */


static void* invalid_page;

//------------------------------------------------------------------------
// micro_queue
//------------------------------------------------------------------------
void micro_queue::push( const void* item, ticket k, concurrent_queue_base& base ) {
    static concurrent_queue_base::page dummy = {static_cast<page*>((void*)1), 0};
    k &= -concurrent_queue_rep::n_queue;
    page* p = NULL;
    size_t index = (k/concurrent_queue_rep::n_queue & base.items_per_page-1);
    if( !index ) {
        try {
            p = base.allocate_page();
        } catch (...) {
            // mark it so that no more pushes are allowed.
            invalid_page = &dummy;
            spin_mutex::scoped_lock lock( page_mutex );
            tail_counter = k+concurrent_queue_rep::n_queue+1;
            if( page* q = tail_page )
                q->next = static_cast<page*>(invalid_page);
            else
                head_page = static_cast<page*>(invalid_page); 
            tail_page = static_cast<page*>(invalid_page);
            throw;
        }
        p->mask = 0;
        p->next = NULL;
    }
    {
        push_finalizer finalizer( *this, k+concurrent_queue_rep::n_queue ); 
        if( tail_counter!=k ) {
            ExponentialBackoff backoff;
            do {
                backoff.pause();
                // no memory. throws an exception
                if( tail_counter&0x1 ) throw bad_last_alloc();
            } while( tail_counter!=k ) ;
        }
        
        if( p ) {
            spin_mutex::scoped_lock lock( page_mutex );
            if( page* q = tail_page )
                q->next = p;
            else
                head_page = p; 
            tail_page = p;
        } else {
            p = tail_page;
        }
        ITT_NOTIFY( sync_acquired, p );
        base.copy_item( *p, index, item );
        ITT_NOTIFY( sync_releasing, p );
        // If no exception was thrown, mark item as present.
        p->mask |= uintptr(1)<<index;
    } 
}

bool micro_queue::pop( void* dst, ticket k, concurrent_queue_base& base ) {
    k &= -concurrent_queue_rep::n_queue;
    SpinwaitUntilEq( head_counter, k );
    SpinwaitWhileEq( tail_counter, k );
    page& p = *head_page;
    __TBB_ASSERT( &p, NULL );
    size_t index = (k/concurrent_queue_rep::n_queue & base.items_per_page-1);
    bool success = false; 
    {
        micro_queue_pop_finalizer finalizer( *this, base, k+concurrent_queue_rep::n_queue, index==base.items_per_page-1 ? &p : NULL ); 
        if( p.mask & uintptr(1)<<index ) {
            success = true;
#if DO_ITT_NOTIFY
            if( ((intptr_t)dst&1) ) {
                dst = (void*) ((intptr_t)dst&~1);
                ITT_NOTIFY( sync_acquired, dst );
            }
#endif
            ITT_NOTIFY( sync_acquired, head_page );
            base.assign_and_destroy_item( dst, p, index );
            ITT_NOTIFY( sync_releasing, head_page );
        }
    }
    return success;
}

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning( pop )
#endif /* _MSC_VER && !defined(__INTEL_COMPILER) */

//------------------------------------------------------------------------
// concurrent_queue_base
//------------------------------------------------------------------------
concurrent_queue_base_v3::concurrent_queue_base_v3( size_t item_size ) {
    items_per_page = item_size<=8 ? 32 :
                     item_size<=16 ? 16 : 
                     item_size<=32 ? 8 :
                     item_size<=64 ? 4 :
                     item_size<=128 ? 2 :
                     1;
    my_capacity = size_t(-1)/(item_size>1 ? item_size : 2); 
    my_rep = cache_aligned_allocator<concurrent_queue_rep>().allocate(1);
    __TBB_ASSERT( (size_t)my_rep % NFS_GetLineSize()==0, "alignment error" );
    __TBB_ASSERT( (size_t)&my_rep->head_counter % NFS_GetLineSize()==0, "alignment error" );
    __TBB_ASSERT( (size_t)&my_rep->tail_counter % NFS_GetLineSize()==0, "alignment error" );
    __TBB_ASSERT( (size_t)&my_rep->array % NFS_GetLineSize()==0, "alignment error" );
    memset(my_rep,0,sizeof(concurrent_queue_rep));
    this->item_size = item_size;
#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
#if _WIN32||_WIN64
    my_rep->size_to_use = __TBB_INVALID_QSIZE;
    my_rep->var_wait_for_items = CreateEvent( NULL, FALSE, FALSE, NULL);
    my_rep->var_wait_for_slots = CreateEvent( NULL, FALSE, FALSE, NULL);
#elif __TBB_USE_FUTEX
    my_rep->size_to_use = __TBB_INVALID_QSIZE;
    // do nothing extra
#else /* __TBB_USE_PTHREAD_CONDWAIT; including MacOS */
    // initialize pthread_mutex_t, and pthread_cond_t

    // use default mutexes
    pthread_mutexattr_t m_attr;
    pthread_mutexattr_init( &m_attr );
    pthread_mutexattr_setprotocol( &m_attr, PTHREAD_PRIO_INHERIT );
    pthread_mutex_init( &my_rep->mtx_items_avail, &m_attr );
    pthread_mutex_init( &my_rep->mtx_slots_avail, &m_attr );
    pthread_mutexattr_destroy( &m_attr );

    pthread_condattr_t c_attr;
    pthread_condattr_init( &c_attr );
    pthread_cond_init( &my_rep->var_wait_for_items, &c_attr );
    pthread_cond_init( &my_rep->var_wait_for_slots, &c_attr );
    pthread_condattr_destroy( &c_attr );

#endif
#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */
}

concurrent_queue_base_v3::~concurrent_queue_base_v3() {
    size_t nq = my_rep->n_queue;
    for( size_t i=0; i<nq; i++ )
        __TBB_ASSERT( my_rep->array[i].tail_page==NULL, "pages were not freed properly" );
#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
# if _WIN32||_WIN64
    CloseHandle( my_rep->var_wait_for_items );
    CloseHandle( my_rep->var_wait_for_slots );
#endif
# if __TBB_USE_PTHREAD_CONDWAIT
    pthread_mutex_destroy( &my_rep->mtx_items_avail );
    pthread_mutex_destroy( &my_rep->mtx_slots_avail );
    pthread_cond_destroy( &my_rep->var_wait_for_items );
    pthread_cond_destroy( &my_rep->var_wait_for_slots );
# endif
#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */
    cache_aligned_allocator<concurrent_queue_rep>().deallocate(my_rep,1);
}

void concurrent_queue_base_v3::internal_push( const void* src ) {
    concurrent_queue_rep& r = *my_rep;
#if !__TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
    concurrent_queue_rep::ticket k  = r.tail_counter++;
    ptrdiff_t e = my_capacity;
    if( e<concurrent_queue_rep::infinite_capacity ) {
        AtomicBackoff backoff;
        for(;;) {
            if( (ptrdiff_t)(k-r.head_counter)<e ) break;
            backoff.pause();
            e = const_cast<volatile ptrdiff_t&>(my_capacity);
        }
    } 
    r.choose(k).push(src,k,*this);
#else /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */

#if DO_ITT_NOTIFY
    bool sync_prepare_done = false;
#endif

# if !__TBB_USE_PTHREAD_CONDWAIT
    concurrent_queue_rep::ticket k = r.tail_counter;
    bool in_transition = false;
    ptrdiff_t e = my_capacity;
    if( e<concurrent_queue_rep::infinite_capacity ) {
        AtomicBackoff backoff;
        for( ;; ) {
            while( (ptrdiff_t)(k-r.head_counter)>=e ) {
#if DO_ITT_NOTIFY
                if( !sync_prepare_done ) {
                     ITT_NOTIFY( sync_prepare, &sync_prepare_done );
                     sync_prepare_done = true;
                }
#endif
                if( !backoff.bounded_pause() ) {
                    // queue is full.  go to sleep.
                    r.n_waiting_producers++;

                    // i created the mess. so I am the one who better clean it up.
                    // and if someone else did not clean it up yet.
                    if( in_transition ) {
                        in_transition = false;
                        __TBB_ASSERT( r.nthreads_in_transition>0, NULL );
                        --r.nthreads_in_transition; // atomic decrement
                    }
                    
                    if( (ptrdiff_t)(k-r.head_counter)>=e ) {
#if _WIN32||_WIN64
                        WaitForSingleObject( r.var_wait_for_slots, INFINITE );
#elif __TBB_USE_FUTEX
                        futex_wait( &r.var_wait_for_slots, 0 );
                        // only one thread will wake up and come here at a time
#endif
                        in_transition = true;

                        // raise barrier
                        backoff.reset();
                        while ( __TBB_CompareAndSwapW( &r.nthreads_to_read_size, r.thread_woken, 0 )!=0 )
                            backoff.pause();
                        
                        r.nthreads_in_transition++; // atomic increment

                        // lower barrier
                        r.nthreads_to_read_size = 0;
                    }
#if __TBB_USE_FUTEX
                    r.var_wait_for_slots = 0;
#endif
                    --r.n_waiting_producers;
                    k = r.tail_counter;
                    backoff.reset();
                }
                e = const_cast<volatile ptrdiff_t&>(my_capacity);
            }
            // increment the tail counter
            concurrent_queue_rep::ticket tk = k;
            k = r.tail_counter.compare_and_swap( tk+1, tk );
            if( k==tk ) {
                if( in_transition ) {
                    in_transition = false;
                    __TBB_ASSERT( r.nthreads_in_transition>0, NULL );
                    --r.nthreads_in_transition;
                }
                break;
            }
        }

#if DO_ITT_NOTIFY
        if( sync_prepare_done )
            ITT_NOTIFY( sync_acquired, &sync_prepare_done );
#endif

        r.choose( k ).push( src, k, *this );

#if _WIN32||_WIN64
        if( r.n_waiting_consumers>0 )
            SetEvent( r.var_wait_for_items );
        if( r.n_waiting_producers>0 && (ptrdiff_t)(r.tail_counter-r.head_counter)<my_capacity )
            SetEvent( r.var_wait_for_slots );
#elif __TBB_USE_FUTEX
        if( r.n_waiting_consumers>0 && __TBB_CompareAndSwapW( &r.var_wait_for_items,1,0 )==0 )
            futex_wakeup_one( &r.var_wait_for_items );
        if( r.n_waiting_producers>0 && (ptrdiff_t)(r.tail_counter-r.head_counter)<my_capacity )
            if( __TBB_CompareAndSwapW( &r.var_wait_for_slots,1,0 )==0)
                futex_wakeup_one( &r.var_wait_for_slots );
#endif
    } else {
        // in infinite capacity, no producers would ever sleep.
        r.choose(k).push(src,k,*this);

#if _WIN32||_WIN64
        if( r.n_waiting_consumers>0 )
            SetEvent( r.var_wait_for_items );
#elif __TBB_USE_FUTEX
        if( r.n_waiting_consumers>0 && __TBB_CompareAndSwapW( &r.var_wait_for_items,1,0 )==0 )
            futex_wakeup_one( &r.var_wait_for_items );
#endif
    }

# else /* __TBB_USE_PTHREAD_CONDWAIT */

    concurrent_queue_rep::ticket k = r.tail_counter;
    ptrdiff_t e = my_capacity;
    if( e<concurrent_queue_rep::infinite_capacity ) {
        AtomicBackoff backoff;
        for( ;; ) {
            while( (ptrdiff_t)(k-r.head_counter)>=e ) {
#if DO_ITT_NOTIFY
                if( !sync_prepare_done ) {
                    ITT_NOTIFY( sync_prepare, &sync_prepare_done );
                    sync_prepare_done = true;
                }
#endif
                if( !backoff.bounded_pause() ) {
                    // queue is full.  go to sleep.

                    pthread_mutex_lock( &r.mtx_slots_avail );

                    r.n_waiting_producers++;

                    while( (ptrdiff_t)(k-r.head_counter)>=e )
                        pthread_cond_wait( &r.var_wait_for_slots, &r.mtx_slots_avail );

                    --r.n_waiting_producers;

                    pthread_mutex_unlock( &r.mtx_slots_avail );

                    k = r.tail_counter;
                    backoff.reset();
                }
                e = const_cast<volatile ptrdiff_t&>(my_capacity);
            }
            // increment the tail counter
            concurrent_queue_rep::ticket tk = k;
            k = r.tail_counter.compare_and_swap( tk+1, tk );
            if( tk==k )
                break;
        }
    }
#if DO_ITT_NOTIFY
    if( sync_prepare_done )
        ITT_NOTIFY( sync_acquired, &sync_prepare_done );
#endif
    r.choose( k ).push( src, k, *this );

    if( r.n_waiting_consumers>0 ) {
        pthread_mutex_lock( &r.mtx_items_avail );
        // pthread_cond_signal() wakes up 'at least' one consumer.
        if( r.n_waiting_consumers>0 )
            pthread_cond_signal( &r.var_wait_for_items );
        pthread_mutex_unlock( &r.mtx_items_avail );
    }

# endif /* !__TBB_USE_PTHREAD_CONDWAIT */

#endif /* !__TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */
}

void concurrent_queue_base_v3::internal_pop( void* dst ) {
    concurrent_queue_rep& r = *my_rep;
#if !__TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
    concurrent_queue_rep::ticket k;
    do {
        k = r.head_counter++;
    } while( !r.choose(k).pop(dst,k,*this) );
#else /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */

#if DO_ITT_NOTIFY
    bool sync_prepare_done = false;
#endif
# if !__TBB_USE_PTHREAD_CONDWAIT
    concurrent_queue_rep::ticket k;
    bool in_transition = false;
    AtomicBackoff backoff;

    do {
        k = r.head_counter;
        for( ;; ) {
            while( r.tail_counter<=k ) {
#if DO_ITT_NOTIFY
                if( !sync_prepare_done ) {
                    ITT_NOTIFY( sync_prepare, dst );
                    dst = (void*) ((intptr_t)dst | 1);
                    sync_prepare_done = true;
                }
#endif
                // Queue is empty; pause and re-try a few times
                if( !backoff.bounded_pause() ) {
                    // it is really empty.. go to sleep
                    r.n_waiting_consumers++;

                    if( in_transition ) {
                        in_transition = false;
                        __TBB_ASSERT( r.nthreads_in_transition>0, NULL );
                        --r.nthreads_in_transition;
                    }
                    
                    if( r.tail_counter<=k ) {
#if _WIN32||_WIN64
                        WaitForSingleObject( r.var_wait_for_items, INFINITE );
#elif __TBB_USE_FUTEX
                        futex_wait( &r.var_wait_for_items, 0 );
                        r.var_wait_for_items = 0;
#endif 
                        in_transition = true;

                        // raise barrier
                        backoff.reset();
                        while ( __TBB_CompareAndSwapW( &r.nthreads_to_read_size, r.thread_woken, 0 )!=0 )
                            backoff.pause();
                        
                        ++r.nthreads_in_transition;

                        // lower barrier
                        r.nthreads_to_read_size = 0;

                        --r.n_waiting_consumers;
                        backoff.reset();
                        k = r.head_counter;
                    } else {
#if __TBB_USE_FUTEX
                        r.var_wait_for_items = 0;
#endif
                        --r.n_waiting_consumers;
                        break;
                    }
                }
            }
            // Queue had item with ticket k when we looked.  Attempt to get that item.
            concurrent_queue_rep::ticket tk=k;
            k = r.head_counter.compare_and_swap( tk+1, tk );
            if( k==tk ) {
                if( in_transition ) {
                    in_transition = false;
                    __TBB_ASSERT( r.nthreads_in_transition>0, NULL );
                    --r.nthreads_in_transition;
                }

                break; // break from the middle 'for' loop
            }
            // Another thread snatched the item, so pause and retry.
        }
    } while( !r.choose(k).pop(dst,k,*this) );

#if _WIN32||_WIN64
    if( r.n_waiting_consumers>0 && (ptrdiff_t)(r.tail_counter-r.head_counter)>0 )
        SetEvent( r.var_wait_for_items );
    if( r.n_waiting_producers>0 )
        SetEvent( r.var_wait_for_slots );
#elif __TBB_USE_FUTEX
    if( r.n_waiting_consumers>0 && (ptrdiff_t)(r.tail_counter-r.head_counter)>0 )
        if( __TBB_CompareAndSwapW( &r.var_wait_for_items, 1, 0 )==0 )
            futex_wakeup_one( &r.var_wait_for_items );
    // wake up a producer..
    if( r.n_waiting_producers>0 && __TBB_CompareAndSwapW( &r.var_wait_for_slots, 1, 0 )==0 )
        futex_wakeup_one( &r.var_wait_for_slots );
#endif

# else /* __TBB_USE_PTHREAD_CONDWAIT */

    concurrent_queue_rep::ticket k;
    AtomicBackoff backoff;

    do {
        k = r.head_counter;
        for( ;; ) {
            while( r.tail_counter<=k ) {
#if DO_ITT_NOTIFY
                if( !sync_prepare_done ) {
                    ITT_NOTIFY( sync_prepare, dst );
                    dst = (void*) ((intptr_t)dst | 1);
                    sync_prepare_done = true;
                }
#endif
                // Queue is empty; pause and re-try a few times
                if( !backoff.bounded_pause() ) {
                    // it is really empty.. go to sleep

                    pthread_mutex_lock( &r.mtx_items_avail );

                    r.n_waiting_consumers++;

                    if( r.tail_counter<=k ) {
                        while( r.tail_counter<=k )
                            pthread_cond_wait( &r.var_wait_for_items, &r.mtx_items_avail );

                        --r.n_waiting_consumers;

                        pthread_mutex_unlock( &r.mtx_items_avail );

                        backoff.reset();
                        k = r.head_counter;
                    } else {
                        --r.n_waiting_consumers;

                        pthread_mutex_unlock( &r.mtx_items_avail );
                        break;
                    }
                }
            }
            // Queue had item with ticket k when we looked.  Attempt to get that item.
            concurrent_queue_rep::ticket tk=k;
            k = r.head_counter.compare_and_swap( tk+1, tk );
            if( tk==k )
                break; // break from the middle 'for' loop
            // Another thread snatched the item, so pause and retry.
        }
    } while( !r.choose(k).pop(dst,k,*this) );

    if( r.n_waiting_producers>0 ) {
        pthread_mutex_lock( &r.mtx_slots_avail );
        if( r.n_waiting_producers>0 )
            pthread_cond_signal( &r.var_wait_for_slots );
        pthread_mutex_unlock( &r.mtx_slots_avail );
    }

# endif /* !__TBB_USE_PTHREAD_CONDWAIT */

#endif /* !__TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */
}

bool concurrent_queue_base_v3::internal_pop_if_present( void* dst ) {
    concurrent_queue_rep& r = *my_rep;
    concurrent_queue_rep::ticket k;
    do {
        k = r.head_counter;
        for(;;) {
            if( r.tail_counter<=k ) {
                // Queue is empty 
                return false;
            }
            // Queue had item with ticket k when we looked.  Attempt to get that item.
            concurrent_queue_rep::ticket tk=k;
            k = r.head_counter.compare_and_swap( tk+1, tk );
            if( k==tk )
                break;
            // Another thread snatched the item, retry.
        }
    } while( !r.choose( k ).pop( dst, k, *this ) );

#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
#if _WIN32||_WIN64
    // wake up a producer..
    if( r.n_waiting_producers>0 )
        SetEvent( r.var_wait_for_slots );
#elif __TBB_USE_FUTEX
    if( r.n_waiting_producers>0 && __TBB_CompareAndSwapW( &r.var_wait_for_slots, 1, 0 )==0 )
        futex_wakeup_one( &r.var_wait_for_slots );
#else /* including MacOS */
    if( r.n_waiting_producers>0 ) {
        pthread_mutex_lock( &r.mtx_slots_avail );
        if( r.n_waiting_producers>0 )
            pthread_cond_signal( &r.var_wait_for_slots );
        pthread_mutex_unlock( &r.mtx_slots_avail );
    }
#endif
#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */

    return true;
}

bool concurrent_queue_base_v3::internal_push_if_not_full( const void* src ) {
    concurrent_queue_rep& r = *my_rep;
    concurrent_queue_rep::ticket k = r.tail_counter;
    for(;;) {
        if( (ptrdiff_t)(k-r.head_counter)>=my_capacity ) {
            // Queue is full
            return false;
        }
        // Queue had empty slot with ticket k when we looked.  Attempt to claim that slot.
        concurrent_queue_rep::ticket tk=k;
        k = r.tail_counter.compare_and_swap( tk+1, tk );
        if( k==tk )
            break;
        // Another thread claimed the slot, so retry. 
    }
    r.choose(k).push(src,k,*this);
#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
#if _WIN32||_WIN64
    // wake up a consumer..
    if( r.n_waiting_consumers>0 )
        SetEvent( r.var_wait_for_items );
#elif __TBB_USE_FUTEX
    if( r.n_waiting_consumers>0 && __TBB_CompareAndSwapW( &r.var_wait_for_items, 1, 0 )==0 )
        futex_wakeup_one( &r.var_wait_for_items );
#else /* including MacOS */
    if( r.n_waiting_consumers>0 ) {
        pthread_mutex_lock( &r.mtx_items_avail );
        if( r.n_waiting_consumers>0 )
            pthread_cond_signal( &r.var_wait_for_items );
        pthread_mutex_unlock( &r.mtx_items_avail );
    }
#endif
#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */
    return true;
}

ptrdiff_t concurrent_queue_base_v3::internal_size() const {
    __TBB_ASSERT( sizeof(ptrdiff_t)<=sizeof(size_t), NULL );
#if __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE
# if !__TBB_USE_PTHREAD_CONDWAIT
    concurrent_queue_rep& r = *my_rep;
    ptrdiff_t sz;
    {
        ptrdiff_t n, nn;
        AtomicBackoff bo;
    restart_read_size:
        while( (n=r.nthreads_to_read_size)==r.thread_woken ) // a woken thread is incrementing nthreads_in_transition
            bo.pause();
        bo.reset();
        do {
            nn = n;
            n = __TBB_CompareAndSwapW( &r.nthreads_to_read_size, nn+1, nn );
            if( n==r.thread_woken ) // I lost to a woken thread
                goto restart_read_size;
        } while ( n!=nn );

        while( r.nthreads_in_transition>0 ) // wait until already woken threads to finish
            bo.pause();

        sz = ptrdiff_t((r.tail_counter-r.head_counter)+(r.n_waiting_producers-r.n_waiting_consumers));

        n = r.nthreads_to_read_size;
        do {
            nn = n;
            n = __TBB_CompareAndSwapW( &r.nthreads_to_read_size, nn-1, nn );
        } while ( n!=nn );
    }
    return sz;
#else /* __TBB_USE_PTHREAD_CONDWAIT */
    concurrent_queue_rep& r = *my_rep;
    pthread_mutex_lock( &r.mtx_slots_avail );
    int nwp = r.n_waiting_producers;
    pthread_mutex_unlock( &r.mtx_slots_avail );
    pthread_mutex_lock( &r.mtx_items_avail );
    int nwc = r.n_waiting_consumers;
    pthread_mutex_unlock( &r.mtx_items_avail );
    return ptrdiff_t((r.tail_counter-r.head_counter)+(nwp - nwc));
#endif /* !__TBB_USE_PTHREAD_CONDWAIT */
#else /* !__TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE || __TBB_USE_PTHREAD_CONDWAIT */
    return ptrdiff_t(my_rep->tail_counter-my_rep->head_counter);
#endif /* __TBB_NO_BUSY_WAIT_IN_CONCURRENT_QUEUE */
}

void concurrent_queue_base_v3::internal_set_capacity( ptrdiff_t capacity, size_t /*item_size*/ ) {
    my_capacity = capacity<0 ? concurrent_queue_rep::infinite_capacity : capacity;
}

void concurrent_queue_base_v3::internal_finish_clear() {
    size_t nq = my_rep->n_queue;
    for( size_t i=0; i<nq; i++ ) {
        page* tp = my_rep->array[i].tail_page;
        __TBB_ASSERT( my_rep->array[i].head_page==tp, "at most one page should remain" );
        if( tp!=NULL) {
            if( tp!=invalid_page ) deallocate_page( tp );
            my_rep->array[i].tail_page = NULL;
        }
    }
}

void concurrent_queue_base_v3::internal_throw_exception() const {
    throw bad_alloc();
}

//------------------------------------------------------------------------
// concurrent_queue_iterator_rep
//------------------------------------------------------------------------
class  concurrent_queue_iterator_rep {
public:
    typedef concurrent_queue_rep::ticket ticket;
    ticket head_counter;   
    const concurrent_queue_base& my_queue;
    concurrent_queue_base::page* array[concurrent_queue_rep::n_queue];
    concurrent_queue_iterator_rep( const concurrent_queue_base& queue ) : 
        head_counter(queue.my_rep->head_counter),
        my_queue(queue)
    {
        const concurrent_queue_rep& rep = *queue.my_rep;
        for( size_t k=0; k<concurrent_queue_rep::n_queue; ++k )
            array[k] = rep.array[k].head_page;
    }
    //! Get pointer to kth element
    void* choose( size_t k ) {
        if( k==my_queue.my_rep->tail_counter )
            return NULL;
        else {
            concurrent_queue_base::page* p = array[concurrent_queue_rep::index(k)];
            __TBB_ASSERT(p,NULL);
            size_t i = k/concurrent_queue_rep::n_queue & my_queue.items_per_page-1;
            return static_cast<unsigned char*>(static_cast<void*>(p+1)) + my_queue.item_size*i;
        }
    }
};

//------------------------------------------------------------------------
// concurrent_queue_iterator_base
//------------------------------------------------------------------------
concurrent_queue_iterator_base_v3::concurrent_queue_iterator_base_v3( const concurrent_queue_base& queue ) {
    my_rep = cache_aligned_allocator<concurrent_queue_iterator_rep>().allocate(1);
    new( my_rep ) concurrent_queue_iterator_rep(queue);
    my_item = my_rep->choose(my_rep->head_counter);
}

void concurrent_queue_iterator_base_v3::assign( const concurrent_queue_iterator_base& other ) {
    if( my_rep!=other.my_rep ) {
        if( my_rep ) {
            cache_aligned_allocator<concurrent_queue_iterator_rep>().deallocate(my_rep, 1);
            my_rep = NULL;
        }
        if( other.my_rep ) {
            my_rep = cache_aligned_allocator<concurrent_queue_iterator_rep>().allocate(1);
            new( my_rep ) concurrent_queue_iterator_rep( *other.my_rep );
        }
    }
    my_item = other.my_item;
}

void concurrent_queue_iterator_base_v3::advance() {
    __TBB_ASSERT( my_item, "attempt to increment iterator past end of queue" );  
    size_t k = my_rep->head_counter;
    const concurrent_queue_base& queue = my_rep->my_queue;
    __TBB_ASSERT( my_item==my_rep->choose(k), NULL );
    size_t i = k/concurrent_queue_rep::n_queue & queue.items_per_page-1;
    if( i==queue.items_per_page-1 ) {
        concurrent_queue_base::page*& root = my_rep->array[concurrent_queue_rep::index(k)];
        root = root->next;
    }
    my_rep->head_counter = k+1;
    my_item = my_rep->choose(k+1);
}

concurrent_queue_iterator_base_v3::~concurrent_queue_iterator_base_v3() {
    //delete my_rep;
    cache_aligned_allocator<concurrent_queue_iterator_rep>().deallocate(my_rep, 1);
    my_rep = NULL;
}

} // namespace internal

} // namespace tbb
