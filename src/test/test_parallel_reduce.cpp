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

#include "tbb/parallel_reduce.h"
#include "tbb/blocked_range.h"
#include "tbb/tick_count.h"
#include "tbb/atomic.h"
#include "harness_assert.h"
#include <cstdlib>
#include <cstdio>

using namespace std;

static tbb::atomic<long> ForkCount;

class FooBody {
private:
    FooBody( const FooBody& );          // Deny access
    void operator=( const FooBody& );   // Deny access
public:
    //! Parent that created this body via split operation.  NULL if original body.
    FooBody* parent;
    //! Total number of index values processed by body and its children.
    long sum;
    //! Number of join operations done so far on this body and its children.
    long join_count;
    //! Range that has been processed so far by this body and its children.
    size_t begin, end;
    //! True if body has not yet been processed at least once by operator().
    bool is_new;
    //! 1 if body was created by split; 0 if original body.
    int forked;
 
    FooBody() : parent(reinterpret_cast<FooBody*>(-1L)), forked(0) {}
    ~FooBody() {
        forked = 0xDEADBEEF; 
        sum=0xDEADBEEF;
        join_count=0xDEADBEEF;
    } 
    FooBody( FooBody& other, tbb::split ) {
        ++ForkCount;
        sum = 0;
        parent = &other;
        join_count = 0;
        is_new = true;
        forked = 1;
    }
    void join( FooBody& s ) {
        ASSERT( s.forked==1, NULL );
        ASSERT( this!=&s, NULL );
        if( this!=s.parent ) {
            printf( "ERROR: %p.join(%p): s.parent=%p\n", this, &s, s.parent );
            exit(1);
        }
        ASSERT( end==s.begin, NULL );
        end = s.end;
        sum += s.sum;
        join_count += s.join_count + 1;
        s.forked = 2;
    }
    void operator()( tbb::blocked_range<size_t>& r ) {
        for( size_t k=r.begin(); k<r.end(); ++k )
            ++sum;
        if( is_new ) {
            is_new = false;
            begin = r.begin();
        } else
            ASSERT( end==r.begin(), NULL );
        end = r.end();
    }
};

static size_t ChunkSize = 1;

#include "harness.h"

void Flog( int nthread ) {
    FooBody f;
    tbb::tick_count T0 = tbb::tick_count::now();
    long join_count = 0;        
    for( size_t i=0; i<=1000; ++i ) {
        for (int mode = 0;  mode < 3; mode++) {
            f.sum = 0;
            f.parent = NULL;
            f.join_count = 0;
            f.is_new = true;
            f.forked = 0;
            switch (mode) {
                case 0:
                    tbb::parallel_reduce( tbb::blocked_range<size_t>(0,i,ChunkSize), f );
                break;
                case 1:
                    tbb::parallel_reduce( tbb::blocked_range<size_t>(0,i,ChunkSize), f, tbb::simple_partitioner() );
                break;
                case 2:
                    tbb::parallel_reduce( tbb::blocked_range<size_t>(0,i,ChunkSize), f, tbb::auto_partitioner() );
                break;
            }
            join_count += f.join_count;
	}
    }
    tbb::tick_count T1 = tbb::tick_count::now();
    if( Verbose )
        printf("time=%g f.sum=%ld join_count=%ld ForkCount=%ld nthread=%d\n",(T1-T0).seconds(),f.sum,join_count,long(ForkCount), nthread);
}

#include "tbb/task_scheduler_init.h"
#include "harness_cpu.h"

int main( int argc, char* argv[] ) {
    ParseCommandLine( argc, argv );
    if( MinThread<0 ) {
        printf("Usage: nthread must be positive\n");
        exit(1);
    }
    for( int p=MinThread; p<=MaxThread; ++p ) {
        tbb::task_scheduler_init init( p );
        Flog(p);

        // Test that all workers sleep when no work
        TestCPUUserTime(p-1);
    }
    printf("done\n");
    return 0;
}
