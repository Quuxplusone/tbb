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

#ifndef __TBB_parallel_do_H
#define __TBB_parallel_do_H

#include "task.h"
#include "aligned_space.h"
#include <iterator>

namespace tbb {

//! @cond INTERNAL
namespace internal {
    template<typename Body> class parallel_do_feeder_impl;
    template<typename Body> class do_group_task;
} // namespace internal
//! @endcond

//! Class the user supplied algorithm body uses to add new tasks
/**
    \param Item  Work item type
**/
template<typename Item>
class parallel_do_feeder: internal::no_copy
{
    parallel_do_feeder() {}
    virtual ~parallel_do_feeder () {}
    virtual void internal_add( const Item& item ) = 0;
    template<typename Body>
    friend class internal::parallel_do_feeder_impl;
public:
    //! Add a work item to a running parallel_do.
    void add( const Item& item ) {internal_add(item);}
};


//! @cond INTERNAL
namespace internal {
    //! For internal use only.
    /** Selects one of the two possible forms of function call member operator
        @ingroup algorithms */


    template<class T, typename F1, typename F2>
    class parallel_do_operator_selector
    {
        template<typename A1, typename A2 >
        static void internal_call( const T& obj, A1& arg1, A2& arg2, F2 )
        {
            obj(arg1, arg2);
        }
        template<typename A1, typename A2 >
        static void internal_call( const T& obj, A1& arg1, A2& arg2, F1 )
        {
            obj(arg1);
        }
    public:
        template<typename A1, typename A2 >
        static void call( const T& obj, A1& arg1, A2& arg2 )
        {
            internal_call( obj, arg1, arg2, &T::operator() );
        }
    };

    //! For internal use only.
    /** Executes one iteration of a do.
        @ingroup algorithms */
    template<typename Body>
    class do_iteration_task: public task
    {
        typedef typename Body::argument_type item_type;
        typedef parallel_do_feeder_impl<Body>   feeder_type;
        typedef parallel_do_feeder<item_type>   users_feeder_type;

        item_type my_value;
        parallel_do_feeder_impl<Body>& my_feeder;

        do_iteration_task( const item_type& value, feeder_type& feeder ) : 
            my_value(value), my_feeder(feeder)
        {
        }

        /*override*/ 
        task* execute()
        {
            typedef  void (Body::*pfn_one_arg_t)(item_type) const;
            typedef  void (Body::*pfn_two_args_t)(item_type, users_feeder_type&) const;

            parallel_do_operator_selector<Body, pfn_one_arg_t, pfn_two_args_t>::call(*my_feeder.my_body, my_value, my_feeder);
            return NULL;
        }
        template<typename Body_> friend class parallel_do_feeder_impl;
    }; // class do_iteration_task

    template<typename Iterator, typename Body>
    class do_iteration_task_iter: public task
    {
        typedef typename Body::argument_type    item_type;
        typedef parallel_do_feeder_impl<Body>   feeder_type;
        typedef parallel_do_feeder<item_type>   users_feeder_type;

        Iterator my_iter;
        parallel_do_feeder_impl<Body>& my_feeder;

        do_iteration_task_iter( const Iterator& iter, feeder_type& feeder ) : 
            my_iter(iter), my_feeder(feeder)
        {}

        /*override*/ 
        task* execute()
        {
            typedef  void (Body::*pfn_one_arg_t)(item_type) const;
            typedef  void (Body::*pfn_two_args_t)(item_type, users_feeder_type&) const;

            parallel_do_operator_selector<Body, pfn_one_arg_t, pfn_two_args_t>::call(*my_feeder.my_body, *my_iter, my_feeder);
            return NULL;
        }
        template<typename Iterator_, typename Body_> friend class do_group_task_forward;    
        template<typename Body_> friend class do_group_task_input;    
        template<typename Iterator_, typename Body_> friend class do_task_iter;    
    }; // class do_iteration_task_iter

    //! For internal use only.
    /** Implements new task adding procedure.
        @ingroup algorithms **/
    template<class Body>
    class parallel_do_feeder_impl : public parallel_do_feeder<typename Body::argument_type>
    {
        typedef typename Body::argument_type item_type;

        /*override*/ 
        void internal_add( const item_type& item )
        {
            typedef do_iteration_task<Body> iteration_type;

            iteration_type& t = *new (task::self().allocate_additional_child_of(*my_barrier)) iteration_type(item, *this);

            t.spawn( t );
        }
    public:
        const Body* my_body;
        empty_task* my_barrier;
    }; // class parallel_do_feeder_impl


    //! For internal use only
    /** Unpacks a block of iterations.
        @ingroup algorithms */
    
    template<typename Iterator,typename Body>
    class do_group_task_forward: public task
    {
        static const size_t max_arg_size = 4;         

        parallel_do_feeder_impl<Body>& my_feeder;
        Iterator my_first;
        size_t my_size;
        typedef typename Body::argument_type item_type;
        
        do_group_task_forward( Iterator first, size_t size, parallel_do_feeder_impl<Body>& feeder ) 
            : my_feeder(feeder), my_first(first), my_size(size)
        {}

        /*override*/ task* execute()
        {
            typedef do_iteration_task_iter<Iterator, Body> iteration_type;
            __TBB_ASSERT( my_size>0, NULL );
            task_list list;
            task* t; 
            size_t k=0; 
            for(;;) {
                t = new( allocate_child() ) iteration_type( my_first, my_feeder );
                ++my_first;
                if( ++k==my_size ) break;
                list.push_back(*t);
            }
            set_ref_count(int(k+1));
            spawn(list);
            spawn_and_wait_for_all(*t);
            return NULL;
        }

        template<typename Iterator_, typename Body_> friend class do_task_iter;
    }; // class do_group_task_forward

    template<typename Body>
    class do_group_task_input: public task
    {
        static const size_t max_arg_size = 4;         
        typedef typename Body::argument_type item_type;

        parallel_do_feeder_impl<Body>& my_feeder;
        size_t my_size;
        aligned_space<item_type,max_arg_size> my_arg;

        do_group_task_input( parallel_do_feeder_impl<Body>& feeder ) 
            : my_feeder(feeder), my_size(0)
        {}

        /*override*/ task* execute()
        {
            typedef do_iteration_task_iter<typename Body::argument_type*, Body> iteration_type;
            __TBB_ASSERT( my_size>0, NULL );
            task_list list;
            task* t; 
            size_t k=0; 
            for(;;) {
                t = new( allocate_child() ) iteration_type( my_arg.begin() + k, my_feeder );
                if( ++k==my_size ) break;
                list.push_back(*t);
            }
            set_ref_count(int(k+1));
            spawn(list);
            spawn_and_wait_for_all(*t);
            return NULL;
        }

        ~do_group_task_input(){
            for( size_t k=0; k<my_size; ++k)
                (my_arg.begin() + k)->~item_type();
        }

        template<typename Iterator_, typename Body_> friend class do_task_iter;
    }; // class do_group_task_input
    
    //! For internal use only.
    /** Gets block of iterations and packages them into a do_group_task.
        @ingroup algorithms */
    template<typename Iterator, typename Body>
    class do_task_iter: public task
    {
    public:
        do_task_iter( Iterator first, Iterator last , parallel_do_feeder_impl<Body>& feeder ) : 
            my_first(first), my_last(last), my_feeder(feeder)
        {}
    
    private:
        Iterator my_first;
        Iterator my_last;
        parallel_do_feeder_impl<Body>& my_feeder;

        /*override*/ task* execute()
        {
            return run( typename std::iterator_traits<Iterator>::iterator_category() );
        }
        
        inline task* run( std::input_iterator_tag ) { return run_for_input_iterator(); }
        
        task* run_for_input_iterator() {
            typedef do_group_task_input<Body> block_type;
            typedef typename Body::argument_type item_type;

            block_type& t = *new( allocate_additional_child_of(*my_feeder.my_barrier) ) block_type(my_feeder);

            size_t k=0; 
            while( !(my_first == my_last) ) {
                new (t.my_arg.begin() + k) item_type(*my_first);
                ++my_first;
                if( ++k==block_type::max_arg_size ) {
                    // There might be more iterations.
                    recycle_to_reexecute();
                    break;
                }
            }
            if( k==0 ) {
                destroy(t);
                return NULL;
            } else {
                t.my_size = k;
                return &t;
            }
        }

        inline task* run( std::forward_iterator_tag ) { return run_for_forward_iterator(); }

        task* run_for_forward_iterator() {
            typedef do_group_task_forward<Iterator,Body> block_type;
            
            Iterator first = my_first;
            size_t k=0; 
            while( !(my_first==my_last) ) {
                ++my_first;
                if( ++k==block_type::max_arg_size ) {
                    // There might be more iterations.
                    recycle_to_reexecute();
                    break;
                }
            }
            return k==0 ? NULL : new( allocate_additional_child_of(*my_feeder.my_barrier) ) block_type(first,k,my_feeder);
        }
        
        inline task* run( std::random_access_iterator_tag ) { return run_for_random_access_iterator(); }

        task* run_for_random_access_iterator() {
            typedef do_group_task_forward<Iterator,Body> block_type;
            typedef do_iteration_task_iter<Iterator, Body> iteration_type;
            
            size_t k = static_cast<size_t>(my_last-my_first); 
            if( k > block_type::max_arg_size ) {
                Iterator middle = my_first + k/2;

                empty_task& c = *new( allocate_continuation() ) empty_task;
                do_task_iter& b = *new( c.allocate_child() ) do_task_iter(middle,my_last,my_feeder);
                recycle_as_child_of(c);

                my_last = middle;
                c.set_ref_count(2);
                c.spawn(b);
                return this;
            }else if( k != 0 ) {
                task_list list;
                task* t; 
                size_t k1=0; 
                for(;;) {
                    t = new( allocate_child() ) iteration_type( my_first, my_feeder );
                    ++my_first;
                    if( ++k1==k ) break;
                    list.push_back(*t);
                }
                set_ref_count(int(k+1));
                spawn(list);
                spawn_and_wait_for_all(*t);
            }
            return NULL;
        }
    }; // class do_task_iter
} // namespace internal
//! @endcond


/** \page parallel_do_body_req Requirements on parallel_do body
    Class \c Body implementing the concept of parallel_do body must define:
    - \code 
        B::operator()( 
                B::argument_type& item,
                parallel_do_feeder<B::argument_type>& feeder
        ) const
        
        OR

        B::operator()( B::argument_type& item ) const
      \endcode                                                      Process item. 
                                                                    May be invoked concurrently  for the same \c this but different \c item.
                                                        
    - \code B::argument_type \endcode                               Type of a work item.
    - \code B::argument_type( const B::argument_type& ) \endcode 
                                                                    Copy a work item.
    - \code ~B::argument_type() \endcode                            Destroy a work item
**/

/** \name parallel_do
    See also requirements on \ref parallel_do_body_req "parallel_do Body". **/
//@{

//! Parallel iteration over a range, with optional addition of more work.
/** @ingroup algorithms */
template<typename Iterator, typename Body> 
void parallel_do( Iterator first, Iterator last, const Body& body )
{
    using namespace internal;

    parallel_do_feeder_impl<Body>  feeder;
    feeder.my_body = &body;
    feeder.my_barrier = new( task::allocate_root() ) empty_task();
    __TBB_ASSERT(feeder.my_barrier, "root task allocation failed");

    do_task_iter<Iterator,Body>& t = *new( feeder.my_barrier->allocate_child() ) do_task_iter<Iterator,Body>( first, last, feeder );

    feeder.my_barrier->set_ref_count(2);
    feeder.my_barrier->spawn_and_wait_for_all(t);

    feeder.my_barrier->destroy(*feeder.my_barrier);
} // parallel_do
//@}

} // namespace 

#endif /* __TBB_parallel_do_H */
