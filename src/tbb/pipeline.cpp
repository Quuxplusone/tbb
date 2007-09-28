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

#include "tbb/pipeline.h"
#include "tbb/cache_aligned_allocator.h"
#include "itt_notify.h"


namespace tbb {

namespace internal {

//! A buffer of ordered items.
/** Each item is a task, inserted into a position in the buffer corrsponding to a Token. */
class ordered_buffer {
    typedef  Token  size_type;

    //! Array of deferred tasks that cannot yet start executing. 
    /** Element is NULL if unused. */
    task** array;

    //! Size of array
    /** Always 0 or a power of 2 */
    size_type array_size;

    //! Lowest token that can start executing.
    /** All prior Token have already been seen. */
    Token low_token;

    //! Serializes updates.
    spin_mutex array_mutex;

    //! Resize "array".
    /** Caller is responsible to acquiring a lock on "array_mutex". */
    void grow( size_type minimum_size );

    //! Initial size for "array"
    /** Must be a power of 2 */
    static const size_type initial_buffer_size = 4;
public:
    //! Construct empty buffer.
    ordered_buffer() : array(NULL), array_size(0), low_token(0) {
        grow(initial_buffer_size);
        __TBB_ASSERT( array, NULL );
    }

    //! Destroy the buffer.
    ~ordered_buffer() {
        __TBB_ASSERT( array, NULL );
        cache_aligned_allocator<task*>().deallocate(array,array_size);
        poison_pointer( array );
    }

    //! Put a token into the buffer.
    /** The putter must be in state that works if enqueued for later wakeup 
        If putter was enqueued, returns NULL.  Otherwise returns putter,
        which the caller is expected to spawn. */
    task* put_token( task& putter, Token token ) {
        task* result = &putter;
        {
            spin_mutex::scoped_lock lock( array_mutex );
            if( token!=low_token ) {
                // Trying to put token that is beyond low_token.
                // Need to wait until low_token catches up before dispatching.
                result = NULL;
                __TBB_ASSERT( token>low_token, NULL );
                if( token-low_token>=array_size ) 
                    grow( token-low_token+1 );
                ITT_NOTIFY( sync_releasing, this );
                array[token&array_size-1] = &putter;
            }
        }
        return result;
    }

    //! Note that processing of a token is finished.
    /** Fires up processing of the next token, if processing was deferred. */
    void note_done( Token token, task& spawner ) {
        task* wakee=NULL;
        {
            spin_mutex::scoped_lock lock( array_mutex );
            if( token==low_token ) {
                // Wake the next task
                task*& item = array[++low_token & array_size-1];
                ITT_NOTIFY( sync_acquired, this );
                wakee = item;
                item = NULL;
            }
        }
        if( wakee ) {
            spawner.spawn(*wakee);
        }
    }
};

void ordered_buffer::grow( size_type minimum_size ) {
    size_type old_size = array_size;
    size_type new_size = old_size ? 2*old_size : initial_buffer_size;
    while( new_size<minimum_size ) 
        new_size*=2;
    task** new_array = cache_aligned_allocator<task*>().allocate(new_size);
    task** old_array = array;
    for( size_type i=0; i<new_size; ++i )
        new_array[i] = NULL;
    long t=low_token;
    for( size_type i=0; i<old_size; ++i, ++t )
        new_array[t&new_size-1] = old_array[t&old_size-1];
    array = new_array;
    array_size = new_size;
    if( old_array )
        cache_aligned_allocator<task*>().deallocate(old_array,old_size);
}

class stage_task: public task {
private:
    friend class tbb::pipeline;
    pipeline& my_pipeline;
    void* my_object;
    filter* my_filter;  
    const Token my_token;
public:
    stage_task( pipeline& pipeline, Token token, filter* filter_list ) : 
        my_pipeline(pipeline), 
        my_filter(filter_list),
        my_token(token)
    {}
    task* execute();
};

task* stage_task::execute() {
    my_object = (*my_filter)(my_object);
    if( ordered_buffer* input_buffer = my_filter->input_buffer )
        input_buffer->note_done(my_token,*this);
    task* next = NULL;
    my_filter = my_filter->next_filter_in_pipeline; 
    if( my_filter ) {
        // There is another filter to execute.
        // Crank up priority a notch.
        add_to_depth(1);
        if( ordered_buffer* input_buffer = my_filter->input_buffer ) {
            // The next filter must execute tokens in order.
            stage_task& clone = *new( allocate_continuation() ) stage_task( my_pipeline, my_token, my_filter );
            clone.my_object = my_object;
            next = input_buffer->put_token(clone,my_token);
        } else {
            recycle_as_continuation();
            next = this;
        }
    } else {
        // Reached end of the pipe.  Inject a new token.
        // The token must be injected before execute() returns, in order to prevent the
        // end_counter task's reference count from prematurely reaching 0.
        set_depth( my_pipeline.end_counter->depth()+1 ); 
        my_pipeline.inject_token( *this );
    }
    return next;
}

} // namespace internal

void pipeline::inject_token( task& self ) {
    void* o = NULL;
    filter* f = filter_list;
    spin_mutex::scoped_lock lock( input_mutex );
    if( !end_of_input ) {
        ITT_NOTIFY(sync_acquired, this );
        o = (*f)(NULL);
        ITT_NOTIFY(sync_releasing, this );
        if( o ) {
            internal::Token token = token_counter++;
            lock.release(); // release the lock as soon as finished updating shared fields

            f = f->next_filter_in_pipeline;
            // Successfully fetched another input object.  
            // Create a stage_task to process it.
            internal::stage_task* s = new( self.allocate_additional_child_of(*end_counter) ) internal::stage_task( *this, token, f );
            s->my_object = o;
            if( internal::ordered_buffer* input_buffer = f->input_buffer ) {
                // The next filter must execute tokens in order.
                s = static_cast<internal::stage_task*>(input_buffer->put_token(*s,token));
            } 
            if( s ) {
                self.spawn(*s);
            }
        } 
        else 
            end_of_input = true;
    }
}

pipeline::pipeline() : 
    filter_list(NULL),
    filter_end(&filter_list),
    end_counter(NULL),
    token_counter(0),
    end_of_input(false)
{
}

pipeline::~pipeline() {
    clear();
}

void pipeline::clear() {
    filter* next;
    for( filter* f = filter_list; f; f=next ) {
        if( internal::ordered_buffer* b = f->input_buffer ) {
            delete b; 
            f->input_buffer = NULL;
        }
        next=f->next_filter_in_pipeline;
        f->next_filter_in_pipeline = filter::not_in_pipeline();
    }
    filter_list = NULL;
}

void pipeline::add_filter( filter& filter ) {
    __TBB_ASSERT( filter.next_filter_in_pipeline==filter::not_in_pipeline(), "filter already part of pipeline?" );
    __TBB_ASSERT( !end_counter, "invocation of add_filter on running pipeline" );
    if( filter.is_serial() ) {
        filter.input_buffer = new internal::ordered_buffer();
    }
    *filter_end = &filter;
    filter_end = &filter.next_filter_in_pipeline;
    *filter_end = NULL;
}

void pipeline::run( size_t max_number_of_live_tokens ) {
    __TBB_ASSERT( max_number_of_live_tokens>0, "pipeline::run must have at least one token" );
    __TBB_ASSERT( !end_counter, "pipeline already running?" );
    if( filter_list ) {
        if( filter_list->next_filter_in_pipeline ) {
            end_of_input = false;
            end_counter = new( task::allocate_root() ) empty_task;
            end_counter->set_ref_count(1);
            for( size_t i=0; i<max_number_of_live_tokens; ++i )
                inject_token( *end_counter );
            end_counter->wait_for_all();
            end_counter->destroy(*end_counter);
            end_counter = NULL;
        } else {
            // There are no filters, and thus no parallelism is possible.
            // Just drain the input stream.
            while( (*filter_list)(NULL) ) 
                continue;
        }
    } 
}

filter::~filter() {
    __TBB_ASSERT( next_filter_in_pipeline==filter::not_in_pipeline(), "cannot destroy filter that is part of pipeline" );
}

} // tbb

