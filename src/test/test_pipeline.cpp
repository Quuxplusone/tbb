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
#include "tbb/tick_count.h"
#include <cstdlib>
#include <cstdio>
#include "harness.h"

struct Buffer {
    int id;
    //! True if Buffer is in use.
    bool is_busy;
    Buffer() : id(-1), is_busy(false) {}
};

static size_t InputCounter;
static const size_t MaxStreamSize = 1<<12;
//! Maximum number of filters allowed
static const int MaxFilters = 5;
static size_t StreamSize;
static const size_t MaxBuffer = 8;
static bool Done[MaxFilters][MaxStreamSize];

class MyFilter: public tbb::filter {
    bool* const my_done;
    const bool my_is_last;      
public:
    MyFilter( bool is_ordered, bool done[], bool is_last ) : 
        filter(is_ordered) ,
        my_done(done),
        my_is_last(is_last)
    {}
    /*override*/void* operator()( void* item ) {
        Buffer& b = *static_cast<Buffer*>(item);
        ASSERT( 0<=b.id && size_t(b.id)<StreamSize, NULL );
        ASSERT( !my_done[b.id], "duplicate processing of token?" );
        ASSERT( !is_serial() || b.id==0 || my_done[b.id-1], NULL );
        ASSERT( b.is_busy, NULL );
        my_done[b.id] = true;
        if( my_is_last ) {
            b.is_busy = false;
        }
        return item;
    }
};

class MyInput: public tbb::filter {
    const size_t my_number_of_filters;
    size_t next_buffer;
    Buffer buffer[MaxBuffer];
    /*override*/void* operator()( void* );
public:
    MyInput( bool is_ordered, int number_of_filters ) : 
        tbb::filter(is_ordered),
        my_number_of_filters(number_of_filters),
        next_buffer(0)
    {}
    bool last_filter_is_ordered;        
};

void* MyInput::operator()(void*) {
    if( InputCounter>=StreamSize ) 
        return NULL;
    else {
retry:
        Buffer& b = buffer[next_buffer];
        ASSERT( &buffer[0] <= &b, NULL );
        ASSERT( &b <= &buffer[MaxBuffer-1], NULL ); 
        next_buffer = (next_buffer+1) % MaxBuffer;
        if( !last_filter_is_ordered && b.is_busy ) 
            goto retry;
        ASSERT( !b.is_busy, "premature reuse of buffer");
        b.id = int(InputCounter++);
        b.is_busy = my_number_of_filters>1;
        return &b;
    }
}

//! Test pipeline that has a single stage, which is ordered, or two stages, where first is unordered.
void TestTrivialpipeline( size_t nthread, int number_of_filters ) {
    if( Verbose ) 
        printf("testing with %d filters\n", number_of_filters );
    ASSERT( number_of_filters<=MaxFilters, "too many filters" );
    // Iterate over possible ordered/unordered filter sequences
    for( int bitmask=0; bitmask<1<<number_of_filters; ++bitmask ) {
        // Build pipeline
        tbb::pipeline pipeline;
        tbb::filter* filter[MaxFilters];
        for( int i=0; i<number_of_filters; ++i ) {
            const bool is_ordered = bitmask>>i&1;
            const bool is_last = i==number_of_filters-1;
            if( i==0 )
                filter[i] = new MyInput(is_ordered,number_of_filters);
            else
                filter[i] = new MyFilter(is_ordered,Done[i],is_last);
            pipeline.add_filter(*filter[i]);
            if( is_last ) 
                static_cast<MyInput*>(filter[0])->last_filter_is_ordered = is_ordered;
        }
        for( StreamSize=0; StreamSize<=MaxStreamSize; StreamSize += StreamSize/3+1 ) {
            memset( Done, 0, sizeof(Done) );
            InputCounter = 0;
            tbb::tick_count t0 = tbb::tick_count::now();
            pipeline.run( nthread<MaxBuffer ? nthread : MaxBuffer-1 );
            tbb::tick_count t1 = tbb::tick_count::now();
            if( number_of_filters>0 ) 
                ASSERT( InputCounter==StreamSize, NULL );
            for( int i=1; i<MaxFilters; ++i )
                for( size_t j=0; j<StreamSize; ++j ) {
                    ASSERT( Done[i][j]==(i<number_of_filters), NULL );
                }
        }
        pipeline.clear();
        for( int i=number_of_filters; --i>=0; ) {
            delete filter[i];
            filter[i] = NULL;
        }
    }
}

#include "tbb/task_scheduler_init.h"
#include "harness_cpu.h"

int main( int argc, char* argv[] ) {
    // Default is at least one thread.
    MinThread = 1;
    ParseCommandLine(argc,argv);
    if( MinThread<1 ) {
        printf("must have at least one thread");
        exit(1);
    }

    // Test with varying number of threads.
    for( int nthread=MinThread; nthread<=MaxThread; ++nthread ) {
        // Initialize TBB task scheduler
        tbb::task_scheduler_init init( nthread );

        // Test pipelines with n filters
        for( int n=0; n<=5; ++n )
            TestTrivialpipeline(size_t(nthread),n);

        // Test that all workers sleep when no work
        TestCPUUserTime(nthread-1);
    }
    printf("done\n");
    return 0;
}
