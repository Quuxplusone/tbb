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

#include "tbb/parallel_do.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/atomic.h"
#include "harness.h"
#include "harness_cpu.h"
#include <memory>

#if defined(_MSC_VER) && defined(_Wp64)
    // Workaround for overzealous compiler warnings in /Wp64 mode
    #pragma warning (disable: 4267)
#endif /* _MSC_VER && _Wp64 */

#define N_DEPTHS     20

static tbb::atomic<int> g_values_counter;

class value_t {
    size_t x;
    value_t& operator= ( const value_t& );
public:
    value_t ( size_t xx ) : x(xx) { ++g_values_counter; }
    value_t ( const value_t& v ) : x(v.value()) { ++g_values_counter; }
    ~value_t () { --g_values_counter; }
    size_t value() const { return x; }
};

template <class T>
class InputIter {
    value_t * my_ptr;
public:
    typedef std::input_iterator_tag iterator_category;
    typedef T value_type;
    typedef typename std::allocator<T>::difference_type difference_type;
    typedef typename std::allocator<T>::pointer pointer;
    typedef typename std::allocator<T>::reference reference;
   
    InputIter( value_t * ptr): my_ptr(ptr){}
    
    value_t& operator* () { return *my_ptr; }
    
    InputIter& operator++ () { ++my_ptr; return *this; }

    bool operator== ( const InputIter& r ) { return my_ptr == r.my_ptr; }
};

template <class T>
class ForwardIter {
    value_t * my_ptr;
public:
    typedef std::forward_iterator_tag iterator_category;
    typedef T value_type;
    typedef typename std::allocator<T>::difference_type difference_type;
    typedef typename std::allocator<T>::pointer pointer;
    typedef typename std::allocator<T>::reference reference;
   
    ForwardIter ( value_t * ptr ) : my_ptr(ptr){}
    
    ForwardIter ( const ForwardIter& r ) : my_ptr(r.my_ptr){}
    
    value_t& operator* () { return *my_ptr; }
    
    ForwardIter& operator++ () { ++my_ptr; return *this; }

    bool operator== ( const ForwardIter& r ) { return my_ptr == r.my_ptr; }
};

static value_t g_depths[N_DEPTHS] = {0, 1, 2, 3, 4, 0, 1, 0, 1, 2, 0, 1, 2, 3, 0, 1, 2, 0, 1, 2};

static size_t g_tasks_expected = 0;
static tbb::atomic<size_t> g_tasks_simplest;
static tbb::atomic<size_t> g_tasks_standard;
static tbb::atomic<size_t> g_tasks_generic;

void reset_globals () {
    g_tasks_simplest = g_tasks_standard = g_tasks_generic = 0;
    g_values_counter = 1;
}

size_t FindNumOfTasks ( size_t max_depth )
{
    if( max_depth == 0 )
        return 1;
    return  max_depth * FindNumOfTasks( max_depth - 1 ) + 1;
}

//! Representation of a numeric type with only those signatures required by parallel_do
/** Also contains and extra validity checks. **/
//! Simplest form of the parallel_do functor object.
class FakeTaskGeneratorBody
{
public:
    typedef value_t argument_type;

    //! The simplest form of the function call operator
    /** It does not allow adding new tasks during its execution. **/
    void operator() ( argument_type depth ) const
    {
        g_tasks_simplest += FindNumOfTasks(depth.value());
    }
};

//! Standard form of the parallel_do functor object.
/** This body tests run-time tasks addition. **/
class TaskGeneratorBody
{
public:
    typedef value_t argument_type;

    //! This form of the function call operator can be used when the body needs to add more work during the processing
    void operator() ( argument_type depth, tbb::parallel_do_feeder<argument_type>& feeder ) const
    {
        ++g_tasks_standard;
        size_t  d=depth.value();
        --d;
        for( size_t i = 0; i < depth.value(); ++i)
            feeder.add(value_t(d));
    }
}; // class TaskGeneratorBody

//! Generic form of the parallel_do functor object.
/** This body tests run-time tasks addition. **/
class TaskGeneratorBody_GenericForm
{
public:
    typedef value_t argument_type;

    //! This form of the function call operator is used when the body needs to add more work during its execution
    /** You may use this form of the function call operator not bothering to remember
        the exact name of parallel_do's feeder class. Besides, potentially in the future
        you will be able to reuse this body class with other algorithm. **/
    template<typename T>
    void operator() ( argument_type depth, T& feeder ) const
    {
        ++g_tasks_generic;
        size_t d=depth.value();
        --d;
        for( size_t i = 0; i < depth.value(); ++i)
            feeder.add(value_t(d));
    }
private:
    // Assert that parallel_do does not access body constructors
    TaskGeneratorBody_GenericForm () {}
    TaskGeneratorBody_GenericForm ( const TaskGeneratorBody_GenericForm& );
    // Run() needs access to the default constructor
    friend void Run( int );

}; // class TaskGeneratorBody_GenericForm

void Run( int nthread )
{
    for( size_t depth = 0; depth <= N_DEPTHS; ++depth ) {
        
        g_tasks_expected = 0;
        for ( size_t i=0; i < depth; ++i )
            g_tasks_expected += FindNumOfTasks( g_depths[i].value() );

        //parallel_do<Iterator random, Body>
        reset_globals();

        FakeTaskGeneratorBody       body;
        tbb::parallel_do(g_depths, g_depths + depth , body);
        TaskGeneratorBody           body2;
        tbb::parallel_do(g_depths, g_depths + depth, body2);
        TaskGeneratorBody_GenericForm   body3;
        tbb::parallel_do(g_depths, g_depths + depth, body3);
        ASSERT (g_tasks_expected == g_tasks_simplest, NULL);
        ASSERT (g_tasks_expected == g_tasks_standard, NULL);
        ASSERT (g_tasks_expected == g_tasks_generic, NULL);
        while( g_values_counter > 1 && g_values_counter <=nthread )
            __TBB_Yield();
        ASSERT( g_values_counter==1, NULL );
        
        //parallel_do<Iterator input, Body>
        reset_globals();
        
        InputIter<value_t> iter1_b(g_depths);
        InputIter<value_t> iter1_e(g_depths+depth);
        tbb::parallel_do(iter1_b, iter1_e , body);
        
        InputIter<value_t> iter2_b(g_depths);
        InputIter<value_t> iter2_e(g_depths+depth);
        tbb::parallel_do(iter2_b, iter2_e , body2);
        
        InputIter<value_t> iter3_b(g_depths);
        InputIter<value_t> iter3_e(g_depths+depth);
        tbb::parallel_do(iter3_b, iter3_e , body3);
        ASSERT (g_tasks_expected == g_tasks_simplest, NULL);
        ASSERT (g_tasks_expected == g_tasks_standard, NULL);
        ASSERT (g_tasks_expected == g_tasks_generic, NULL);
        while( g_values_counter > 1 && (g_values_counter <= 1 + 4*(nthread-1)) )
            __TBB_Yield();
        ASSERT( g_values_counter==1, NULL );
        
        //parallel_do<Iterator forward, Body>
        reset_globals();
        
        ForwardIter<value_t> iterf1_b(g_depths);
        ForwardIter<value_t> iterf1_e(g_depths+depth);
        tbb::parallel_do(iterf1_b, iterf1_e , body);
        
        ForwardIter<value_t> iterf2_b(g_depths);
        ForwardIter<value_t> iterf2_e(g_depths+depth);
        tbb::parallel_do(iterf2_b, iterf2_e , body2);
        
        ForwardIter<value_t> iterf3_b(g_depths);
        ForwardIter<value_t> iterf3_e(g_depths+depth);
        tbb::parallel_do(iterf3_b, iterf3_e , body3);
        ASSERT (g_tasks_expected == g_tasks_simplest, NULL);
        ASSERT (g_tasks_expected == g_tasks_standard, NULL);
        ASSERT (g_tasks_expected == g_tasks_generic, NULL);
        while( g_values_counter > 1 && g_values_counter <=nthread ) 
            __TBB_Yield();
        ASSERT( g_values_counter==1, NULL );
    }
}


int main( int argc, char* argv[] ) {
    MinThread=1;
    MaxThread=2;
    ParseCommandLine( argc, argv );
    if( MinThread<1 ) {
        printf("number of threads must be positive\n");
        exit(1);
    }

    for( int p=MinThread; p<=MaxThread; ++p ) {
        tbb::task_scheduler_init init( p );
        Run(p);
        // Test that all workers sleep when no work
        TestCPUUserTime(p);
    }
    printf("done\n");
    return 0;
}
