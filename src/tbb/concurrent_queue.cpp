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

#include "tbb/concurrent_queue.h"
#include "tbb/cache_aligned_allocator.h"
#include "tbb/spin_mutex.h"
#include "tbb/atomic.h"
#include "tbb_misc.h"
#include <cstring>
#include <stdio.h>

#define RECORD_EVENTS 0


namespace tbb {

namespace internal {

class concurrent_queue_rep;

//! A queue using simple locking.
/** For efficient, this class has no constructor.  
    The caller is expected to zero-initialize it. */
struct micro_queue {
    typedef concurrent_queue_base::page page;
    typedef size_t ticket;

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

    class pop_finalizer {
        ticket my_ticket;
        micro_queue& my_queue;
        page* my_page; 
    public:
        pop_finalizer( micro_queue& queue, ticket k, page* p ) :
            my_ticket(k), my_queue(queue), my_page(p)
        {}
        ~pop_finalizer() {
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
                operator delete(p);
        }
    };

    bool pop( void* dst, ticket k, concurrent_queue_base& base );
};

//! Internal representation of a ConcurrentQueue.
/** For efficient, this class has no constructor.  
    The caller is expected to zero-initialize it. */
class concurrent_queue_rep {
public:
    typedef size_t ticket;

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

    atomic<ticket> head_counter;
    char pad1[NFS_MaxLineSize-sizeof(size_t)];

    atomic<ticket> tail_counter;
    char pad2[NFS_MaxLineSize-sizeof(ticket)];
    micro_queue array[n_queue];    

    micro_queue& choose( size_t k ) {
        // The formula here approximates LRU in a cache-oblivious way.
        return array[index(k)];
    }

    //! Value for effective_capacity that denotes unbounded queue.
    static const size_t infinite_capacity = ~size_t(0)/2;
};

//------------------------------------------------------------------------
// micro_queue
//------------------------------------------------------------------------
void micro_queue::push( const void* item, ticket k, concurrent_queue_base& base ) {
    k &= -concurrent_queue_rep::n_queue;
    page* p = NULL;
    unsigned index = (k/concurrent_queue_rep::n_queue & base.items_per_page-1);
    if( !index ) {
        size_t n = sizeof(page) + base.items_per_page*base.item_size;
        p = static_cast<page*>(operator new( n ));
        p->mask = 0;
        p->next = NULL;
    }
    {
        push_finalizer finalizer( *this, k+concurrent_queue_rep::n_queue ); 
        SpinwaitUntilEq( tail_counter, k );
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
        base.copy_item( *p, index, item );
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
    unsigned index = (k/concurrent_queue_rep::n_queue & base.items_per_page-1);
    bool success = false; 
    {
        pop_finalizer finalizer( *this, k+concurrent_queue_rep::n_queue, index==base.items_per_page-1 ? &p : NULL ); 
        if( p.mask & uintptr(1)<<index ) {
            success = true;
            base.assign_and_destroy_item( dst, p, index );
        }
    }
    return success;
}

//------------------------------------------------------------------------
// concurrent_queue_base
//------------------------------------------------------------------------
concurrent_queue_base::concurrent_queue_base( size_t item_size ) {
    items_per_page = item_size<=8 ? 32 :
                     item_size<=16 ? 16 : 
                     item_size<=32 ? 8 :
                     item_size<=64 ? 4 :
                     item_size<=128 ? 2 :
                     1;
    my_capacity = size_t(-1)/(item_size>1 ? item_size : 2); 
    rep = cache_aligned_allocator<concurrent_queue_rep>().allocate(1);
    __TBB_ASSERT( (size_t)rep % NFS_GetLineSize()==0, "alignment error" );
    __TBB_ASSERT( (size_t)&rep->head_counter % NFS_GetLineSize()==0, "alignment error" );
    __TBB_ASSERT( (size_t)&rep->tail_counter % NFS_GetLineSize()==0, "alignment error" );
    __TBB_ASSERT( (size_t)&rep->array % NFS_GetLineSize()==0, "alignment error" );
    memset(rep,0,sizeof(concurrent_queue_rep));
    this->item_size = item_size;
}

concurrent_queue_base::~concurrent_queue_base() {
    concurrent_queue_rep* r = (concurrent_queue_rep*)rep;
    cache_aligned_allocator<concurrent_queue_rep>().deallocate(r,1);
}

void concurrent_queue_base::internal_push( const void* src ) {
    concurrent_queue_rep& r = *(concurrent_queue_rep*)rep;
    concurrent_queue_rep::ticket k  = r.tail_counter++;
    ptrdiff_t e = my_capacity;
    if( e<(ptrdiff_t)concurrent_queue_rep::infinite_capacity ) {
        ExponentialBackoff backoff;
        for(;;) {
            if( (ptrdiff_t)(k-r.head_counter)<e ) break;
            backoff.pause();
            e = const_cast<volatile ptrdiff_t&>(my_capacity);
        }
    } 
    r.choose(k).push(src,k,*this);
}

void concurrent_queue_base::internal_pop( void* dst ) {
    concurrent_queue_rep& r = *(concurrent_queue_rep*)rep;
    concurrent_queue_rep::ticket k;
    do {
        k = r.head_counter++;
    } while( !r.choose(k).pop(dst,k,*this) );
}

bool concurrent_queue_base::internal_pop_if_present( void* dst ) {
    concurrent_queue_rep& r = *(concurrent_queue_rep*)rep;
    concurrent_queue_rep::ticket k;
    do {
        ExponentialBackoff backoff;
        for(;;) {
            k = r.head_counter;
            if( (ptrdiff_t)(r.tail_counter-k)<=0 ) {
                // Queue is empty 
                return false;
            }
            // Queue had item with ticket k when we looked.  Attempt to get that item.
            if( r.head_counter.compare_and_swap(k+1,k)==k ) {
                break;
            }
            // Another thread snatched the item, so pause and retry.
            backoff.pause();
        }
    } while( !r.choose(k).pop(dst,k,*this) );
    return true;
}

bool concurrent_queue_base::internal_push_if_not_full( const void* src ) {
    concurrent_queue_rep& r = *(concurrent_queue_rep*)rep;
    ExponentialBackoff backoff;
    concurrent_queue_rep::ticket k;
    for(;;) {
        k = r.tail_counter;
        if( (ptrdiff_t)(k-r.head_counter)>=my_capacity ) {
            // Queue is full
            return false;
        }
        // Queue had empty slot with ticket k when we looked.  Attempt to claim that slot.
        if( r.tail_counter.compare_and_swap(k+1,k)==k ) 
            break;
        // Another thread claimed the slot, so pause and retry.
        backoff.pause();
    }
    r.choose(k).push(src,k,*this);
    return true;
}

ptrdiff_t concurrent_queue_base::internal_size() const {
    concurrent_queue_rep& r = *(concurrent_queue_rep*)rep;
    __TBB_ASSERT( sizeof(ptrdiff_t)<=sizeof(size_t), NULL );
    return ptrdiff_t(r.tail_counter-r.head_counter);
}

void concurrent_queue_base::internal_set_capacity( ptrdiff_t capacity, size_t item_size ) {
    my_capacity = capacity<0 ? concurrent_queue_rep::infinite_capacity : capacity;
}

//------------------------------------------------------------------------
// concurrent_queue_iterator_rep
//------------------------------------------------------------------------
struct concurrent_queue_iterator_rep {
    typedef concurrent_queue_rep::ticket ticket;
    ticket head_counter;   
    const concurrent_queue_base& my_queue;
    concurrent_queue_base::page* array[concurrent_queue_rep::n_queue];
    concurrent_queue_iterator_rep( const concurrent_queue_base& queue ) : 
        head_counter(queue.rep->head_counter),
        my_queue(queue)
    {
        const concurrent_queue_rep& rep = *queue.rep;
        for( size_t k=0; k<concurrent_queue_rep::n_queue; ++k )
            array[k] = rep.array[k].head_page;
    }
    //! Get pointer to kth element
    void* choose( size_t k ) {
        if( k==my_queue.rep->tail_counter )
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
concurrent_queue_iterator_base::concurrent_queue_iterator_base( const concurrent_queue_base& queue ) {
    rep = new concurrent_queue_iterator_rep(queue);
    item = rep->choose(rep->head_counter);
}

void concurrent_queue_iterator_base::assign( const concurrent_queue_iterator_base& other ) {
    if( rep!=other.rep ) {
        if( rep ) {
            delete rep;
            rep = NULL;
        }
        if( other.rep ) {
            rep = new concurrent_queue_iterator_rep( *other.rep );
        }
    }
    item = other.item;
}

void concurrent_queue_iterator_base::advance() {
    __TBB_ASSERT( item, "attempt to increment iterator past end of queue" );  
    size_t k = rep->head_counter;
    const concurrent_queue_base& queue = rep->my_queue;
    __TBB_ASSERT( item==rep->choose(k), NULL );
    size_t i = k/concurrent_queue_rep::n_queue & queue.items_per_page-1;
    if( i==queue.items_per_page-1 ) {
        concurrent_queue_base::page*& root = rep->array[concurrent_queue_rep::index(k)];
        root = root->next;
    }
    rep->head_counter = k+1;
    item = rep->choose(k+1);
}

concurrent_queue_iterator_base::~concurrent_queue_iterator_base() {
    delete rep;
    rep = NULL;
}

} // namespace internal

} // namespace tbb
