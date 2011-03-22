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

#include "harness.h"

#if !__SUNPRO_CC

#define TBB_PREVIEW_GRAPH 1
#include "tbb/graph.h"
#include "tbb/task_scheduler_init.h"

//
// Tests
//

const int Count = 300;
const int MaxPorts = 10;
const int MaxNSources = 5; // max # of source_nodes to register for each join_node input in parallel test
bool outputCheck[MaxPorts][Count];  // for checking output

void
check_outputCheck( int nUsed, int maxCnt) {
    for(int i=0; i < nUsed; ++i) {
        for( int j = 0; j < maxCnt; ++j) {
            ASSERT(outputCheck[i][j], NULL);
        }
    }
}

void
reset_outputCheck( int nUsed, int maxCnt) {
    for(int i=0; i < nUsed; ++i) {
        for( int j = 0; j < maxCnt; ++j) {
            outputCheck[i][j] = false;
        }
    }
}

template<typename T>
class name_of {
public:
    static const char* name() { return  "Unknown"; }
};
template<>
class name_of<int> {
public:
    static const char* name() { return  "int"; }
};
template<>
class name_of<float> {
public:
    static const char* name() { return  "float"; }
};
template<>
class name_of<double> {
public:
    static const char* name() { return  "double"; }
};
template<>
class name_of<long> {
public:
    static const char* name() { return  "long"; }
};
template<>
class name_of<short> {
public:
    static const char* name() { return  "short"; }
};

// T must be arithmetic, and shouldn't wrap around for reasonable sizes of Count (which is now 1000, and maxPorts is 10,
// so the max number generated right now is 11000.)  Source will generate a series of TT with value
// (init_val + (i-1)*addend) * my_mult, where i is the i-th invocation of the body.  We are attaching addend
// source nodes to a join_port, and each will generate part of the numerical series the port is expecting
// to receive.  If there is only one source node, the series order will be maintained; if more than one,
// this is not guaranteed.
template<typename TT>
class source_body {
    const TT my_mult;
    int my_count;
    const int addend;
    source_body& operator=( const source_body& other);
public:
    source_body(TT multiplier, int init_val, int addto) : my_mult(multiplier), my_count(init_val), addend(addto) { }
    bool operator()( TT &v) {
        int lc = my_count;
        v = my_mult * (TT)my_count;
        my_count += addend;
        return lc < Count;
    }
};

// holder for source_node pointers for eventual deletion

static void* all_source_nodes[MaxPorts][MaxNSources];

template<int ELEM, typename JNT>
class source_node_helper {
public:
    typedef JNT join_node_type;
    typedef typename join_node_type::output_type TT;
    typedef typename std::tuple_element<ELEM-1,TT>::type IT;
    typedef typename tbb::source_node<IT> my_source_node_type;
    static void print_remark() {
        source_node_helper<ELEM-1,JNT>::print_remark();
        REMARK(", %s", name_of<IT>::name());
    }
    static void add_source_nodes(join_node_type &my_join, tbb::graph &g, int nInputs) {
        for(int i=0; i < nInputs; ++i) {
            my_source_node_type *new_node = new my_source_node_type(g, source_body<IT>((IT)(ELEM+1), i, nInputs));
            ASSERT(new_node->register_successor(std::get<ELEM-1>(my_join.inputs() )), NULL);
            all_source_nodes[ELEM-1][i] = (void *)new_node;
        }
        // add the next source_node
        source_node_helper<ELEM-1, JNT>::add_source_nodes(my_join, g, nInputs);
    }
    static void check_value(int i, TT &v, bool is_serial) {
        // the fetched value will match only if there is only one source_node.
        ASSERT(!is_serial || std::get<ELEM-1>(v) == (IT)(i*(ELEM+1)), NULL);
        // tally the fetched value.
        int ival = (int)std::get<ELEM-1>(v);
        ASSERT(!(ival%(ELEM+1)), NULL);
        ival /= (ELEM+1);
        ASSERT(!outputCheck[ELEM-1][ival], NULL);
        outputCheck[ELEM-1][ival] = true;
        source_node_helper<ELEM-1,JNT>::check_value(i, v, is_serial);
    }
    static void remove_source_nodes(join_node_type& my_join, int nInputs) {
        for(int i=0; i< nInputs; ++i) {
            my_source_node_type *dp = reinterpret_cast<my_source_node_type *>(all_source_nodes[ELEM-1][i]);
            dp->remove_successor(std::get<ELEM-1>(my_join.inputs()));
            delete dp;
        }
        source_node_helper<ELEM-1, JNT>::remove_source_nodes(my_join, nInputs);
    }
};

template<typename JNT>
class source_node_helper<1, JNT> {
    typedef JNT join_node_type;
    typedef typename join_node_type::output_type TT;
    typedef typename std::tuple_element<0,TT>::type IT;
    typedef typename tbb::source_node<IT> my_source_node_type;
public:
    static void print_remark() {
        REMARK("Parallel test of join_node< %s", name_of<IT>::name());
    }
    static void add_source_nodes(join_node_type &my_join, tbb::graph &g, int nInputs) {
        for(int i=0; i < nInputs; ++i) {
            my_source_node_type *new_node = new my_source_node_type(g, source_body<IT>((IT)2, i, nInputs));
            ASSERT(new_node->register_successor(std::get<0>(my_join.inputs() )), NULL);
            all_source_nodes[0][i] = (void *)new_node;
        }
    }
    static void check_value(int i, TT &v, bool is_serial) {
        ASSERT(!is_serial || std::get<0>(v) == (IT)(i*(2)), NULL);
        int ival = (int)std::get<0>(v);
        ASSERT(!(ival%2), NULL);
        ival /= 2;
        ASSERT(!outputCheck[0][ival], NULL);
        outputCheck[0][ival] = true;
    }
    static void remove_source_nodes(join_node_type& my_join, int nInputs) {
        for(int i=0; i < nInputs; ++i) {
            my_source_node_type *dp = reinterpret_cast<my_source_node_type *>(all_source_nodes[0][i]);
            dp->remove_successor(std::get<0>(my_join.inputs()));
            delete dp;
        }
    }
};

template<typename JType>
class parallel_test {
public:
    typedef typename JType::output_type TType;
    static const int SIZE = std::tuple_size<TType>::value;
    static void test() {
        TType v;
        source_node_helper<SIZE,JType>::print_remark();
        REMARK(" >\n");
        for(int i=0; i < MaxPorts; ++i) {
            for(int j=0; j < MaxNSources; ++j) {
                all_source_nodes[i][j] = NULL;
            }
        }
        for(int nInputs = 1; nInputs <= MaxNSources; ++nInputs) {
            tbb::graph g;
            JType my_join(g);
            tbb::queue_node<TType> outq1(g);
            tbb::queue_node<TType> outq2(g);

            ASSERT(my_join.register_successor(outq1), NULL);  // register outputs first, so they both get all
            ASSERT(my_join.register_successor(outq2), NULL);  // the results

            source_node_helper<SIZE, JType>::add_source_nodes(my_join, g, nInputs);

            g.wait_for_all();

            reset_outputCheck(SIZE, Count);
            for(int i=0; i < Count; ++i) {
                ASSERT(outq1.try_get(v), NULL);
                source_node_helper<SIZE, JType>::check_value(i, v, nInputs == 1);
            }

            check_outputCheck(SIZE, Count);
            reset_outputCheck(SIZE, Count);

            for(int i=0; i < Count; i++) {
                ASSERT(outq2.try_get(v), NULL);;
                source_node_helper<SIZE, JType>::check_value(i, v, nInputs == 1);
            }
            check_outputCheck(SIZE, Count);

            ASSERT(!outq1.try_get(v), NULL);
            ASSERT(!outq2.try_get(v), NULL);

            source_node_helper<SIZE, JType>::remove_source_nodes(my_join, nInputs);
            my_join.remove_successor(outq1);
            my_join.remove_successor(outq2);
        }
    }
};


template<int ELEM, typename JType>
class serial_queue_helper {
public:
    typedef typename JType::output_type TT;
    typedef typename std::tuple_element<ELEM-1,TT>::type IT;
    typedef typename tbb::queue_node<IT> my_queue_node_type;
    static void print_remark() {
        serial_queue_helper<ELEM-1,JType>::print_remark();
        REMARK(", %s", name_of<IT>::name());
    }
    static void add_queue_nodes(tbb::graph &g, JType &my_join) {
        serial_queue_helper<ELEM-1,JType>::add_queue_nodes(g, my_join);
        my_queue_node_type *new_node = new my_queue_node_type(g);
        ASSERT(new_node->register_successor(std::get<ELEM-1>(my_join.inputs())), NULL);
        all_source_nodes[ELEM-1][0] = (void *)new_node;
    }
    static void fill_one_queue(int maxVal) {
        // fill queue to "left" of me
        my_queue_node_type *qptr = reinterpret_cast<my_queue_node_type *>(all_source_nodes[ELEM-1][0]);
        serial_queue_helper<ELEM-1,JType>::fill_one_queue(maxVal);
        for(int i = 0; i < maxVal; ++i) {
            ASSERT(qptr->try_put((IT)(i*(ELEM+1))), NULL);
        }
    }
    static void put_one_queue_val(int myVal) {
        // put this val to my "left".
        serial_queue_helper<ELEM-1,JType>::put_one_queue_val(myVal);
        my_queue_node_type *qptr = reinterpret_cast<my_queue_node_type *>(all_source_nodes[ELEM-1][0]);
        ASSERT(qptr->try_put((IT)(myVal*(ELEM+1))), NULL);
    }
    static void check_queue_value(int i, TT &v) {
        serial_queue_helper<ELEM-1,JType>::check_queue_value(i, v);
        ASSERT( std::get<ELEM-1>(v) == (IT)(i * (ELEM+1)), NULL);
    }
    static void remove_queue_nodes(JType &my_join) {
        my_queue_node_type *vptr = reinterpret_cast<my_queue_node_type *>(all_source_nodes[ELEM-1][0]);
        vptr->remove_successor(my_join);
        serial_queue_helper<ELEM-1, JType>::remove_queue_nodes(my_join);
        delete vptr;
    }
};

template<typename JType>
class serial_queue_helper<1, JType> {
public:
    typedef typename JType::output_type TT;
    typedef typename std::tuple_element<0,TT>::type IT;
    typedef typename tbb::queue_node<IT> my_queue_node_type;
    static void print_remark() {
        REMARK("Serial test of join_node< %s", name_of<IT>::name());
    }
    static void add_queue_nodes(tbb::graph &g, JType &my_join) {
        my_queue_node_type *new_node = new my_queue_node_type(g);
        ASSERT(new_node->register_successor(std::get<0>(my_join.inputs() )), NULL);
        all_source_nodes[0][0] = (void *)new_node;
    }
    static void fill_one_queue(int maxVal) {
        my_queue_node_type *qptr = reinterpret_cast<my_queue_node_type *>(all_source_nodes[0][0]);
        for(int i = 0; i < maxVal; ++i) {
            ASSERT(qptr->try_put((IT)(i*2)), NULL);
        }
    }
    static void put_one_queue_val(int myVal) {
        my_queue_node_type *qptr = reinterpret_cast<my_queue_node_type *>(all_source_nodes[0][0]);
        ASSERT(qptr->try_put((IT)(myVal*2)), NULL);
    }
    static void check_queue_value(int i, TT &v) {
        ASSERT( std::get<0>(v) == (IT)(i*2), NULL);
    }
    static void remove_queue_nodes(JType &my_join) {
        my_queue_node_type *vptr = reinterpret_cast<my_queue_node_type *>(all_source_nodes[0][0]);
        vptr->remove_successor(my_join);
        delete vptr;
    }
};

//
// Single reservable predecessor at each port, single accepting successor
//   * put to buffer before port0, then put to buffer before port1, ...
//   * fill buffer before port0 then fill buffer before port1, ...

template<typename JType>
class serial_test {
    typedef typename JType::output_type TType;
    static const int SIZE = std::tuple_size<TType>::value;
public:
static void test() {
    tbb::graph g;
    JType my_join(g);

    serial_queue_helper<SIZE, JType>::add_queue_nodes(g,my_join);
    typedef TType q3_input_type;
    tbb::queue_node< q3_input_type >  q3(g);

    serial_queue_helper<SIZE, JType>::print_remark(); REMARK(" >\n");

    ASSERT(my_join.register_successor( q3 ), NULL);

    // fill each queue with its value one-at-a-time
    for (int i = 0; i < Count; ++i ) {
        serial_queue_helper<SIZE,JType>::put_one_queue_val(i);
    }

    g.wait_for_all();
    for (int i = 0; i < Count; ++i ) {
        q3_input_type v;
        g.wait_for_all();
        ASSERT(q3.try_get( v ), "Error in try_get()");
        serial_queue_helper<SIZE,JType>::check_queue_value(i, v);
    }

    // fill each queue completely before filling the next.
    serial_queue_helper<SIZE, JType>::fill_one_queue(Count);

    g.wait_for_all();
    for (int i = 0; i < Count; ++i ) {
        q3_input_type v;
        g.wait_for_all();
        ASSERT(q3.try_get( v ), "Error in try_get()");
        serial_queue_helper<SIZE,JType>::check_queue_value(i, v);
    }
}

}; // serial_test

template<template<typename> class TestType, typename OutputTupleType, tbb::join_policy J>
class generate_test {
public:
    typedef tbb::join_node<OutputTupleType,J> join_node_type;
    static void do_test() {
        TestType<join_node_type>::test();
    }
};

int TestMain() {
#if __TBB_USE_TBB_TUPLE
    REMARK("  Using TBB tuple");
#else
    REMARK("  Using platform tuple");
#endif
#if __TBB_USE_VARIADIC_TEMPLATE
    REMARK(" with variadic template\n");
#else
    REMARK(" with explicit template\n");
#endif
   for (int p = 0; p < 2; ++p) {

       REMARK("two_phase\n");
       generate_test<serial_test, std::tuple<float, double>, tbb::two_phase >::do_test();
       generate_test<serial_test, std::tuple<float, double, int, long>, tbb::two_phase >::do_test();
       generate_test<serial_test, std::tuple<double, double, int, long, int, short>, tbb::two_phase >::do_test();
       generate_test<serial_test, std::tuple<float, double, double, double, float, int, float, long>, tbb::two_phase >::do_test();
       generate_test<serial_test, std::tuple<float, double, int, double, double, float, long, int, float, long>, tbb::two_phase >::do_test();
       generate_test<parallel_test, std::tuple<float, double>, tbb::two_phase >::do_test();
       generate_test<parallel_test, std::tuple<float, int, long>, tbb::two_phase >::do_test();
       generate_test<parallel_test, std::tuple<double, double, int, int, short>, tbb::two_phase >::do_test();
       generate_test<parallel_test, std::tuple<float, int, double, float, long, float, long>, tbb::two_phase >::do_test();
       generate_test<parallel_test, std::tuple<float, double, int, double, double, long, int, float, long>, tbb::two_phase >::do_test();

   }
   return Harness::Done;
}

#else /* __SUNPRO_CC */

int TestMain() { 
    REPORT("Known issue: test skipped because of the compiler (CC %X) bug.\n", __SUNPRO_CC);
    return Harness::Skipped;
}

#endif /* __SUNPRO_CC */
