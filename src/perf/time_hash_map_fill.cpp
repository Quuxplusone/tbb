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

// configuration:

//! enable/disable std::map tests
#define STDTABLE 0

//! enable/disable old implementation tests (correct include file also)
#define OLDTABLE 1
#define OLDTABLEHEADER "tbb/concurrent_hash_map-5468.h"//-4329

//! enable/disable experimental implementation tests (correct include file also)

#define TESTTABLE 0
#define TESTTABLEHEADER "tbb/concurrent_hash_map-oneseg.h"

#define TBB_USE_THREADING_TOOLS 0
//////////////////////////////////////////////////////////////////////////////////

#include <cstdlib>
#include <math.h>
#include "tbb/tbb_stddef.h"
#include <vector>
#include <map>
// needed by hash_maps
#include <stdexcept>
#include <iterator>
#include <algorithm>                 // std::swap
#include <utility>      // Need std::pair
#include <cstring>      // Need std::memset
#include <typeinfo>
#include "tbb/cache_aligned_allocator.h"
#include "tbb/tbb_allocator.h"
#include "tbb/spin_rw_mutex.h"
#include "tbb/aligned_space.h"
#include "tbb/atomic.h"
// for test
#include "tbb/spin_mutex.h"
#include "time_framework.h"


using namespace tbb;
using namespace tbb::internal;

namespace version_current {
    namespace tbb { using namespace ::tbb; namespace internal {
        using namespace ::tbb::internal;
    } }
    namespace std { using namespace ::std; }
    #include "tbb/concurrent_hash_map.h"
}
typedef version_current::tbb::concurrent_hash_map<int,int> IntTable;

#if OLDTABLE
#undef __TBB_concurrent_hash_map_H
namespace version_base {
    namespace tbb { using namespace ::tbb; namespace internal { using namespace ::tbb::internal; } }
    using namespace ::std;
    #include OLDTABLEHEADER
}
typedef version_base::tbb::concurrent_hash_map<int,int> OldTable;
#endif

#if TESTTABLE
#undef __TBB_concurrent_hash_map_H
namespace version_new {
    namespace tbb { using namespace ::tbb; namespace internal { using namespace ::tbb::internal; } }
    #include TESTTABLEHEADER
}
typedef version_new::tbb::concurrent_hash_map<int,int> TestTable;
#define TESTTABLE 1
#endif

/////////////////////////////////////////////////////////////////////////////////////////

const int test2_size = 2000000;
int Data[test2_size];

template<typename TableType>
struct TestHashCountStrings : TesterBase {
    typedef typename TableType::accessor accessor;
    typedef typename TableType::const_accessor const_accessor;
    TableType Table;
    int n_items;

    std::string get_name(int testn) {
        return Format("%d%% uniques", ++testn*10);
    }

    TestHashCountStrings() : TesterBase(3) {}
    void init() {
        n_items = value/threads_count;
    }

    void test_prefix(int testn, int t) {
        barrier->wait();
        if( t ) return;
        Table.clear();
        int uniques = test2_size/10*(testn+1);
        srand(10101);
        for(int i = 0; i < test2_size; i++)
            Data[i] = rand()%uniques;
    }

    double test(int testn, int t)
    {
            for(int i = t*n_items, e = (t+1)*n_items; i < e; i++) {
                Table.insert( std::make_pair(Data[i],t) );
            }
        return 0;
    }
};

class test_hash_map_find : public TestProcessor {
public:
    test_hash_map_find() : TestProcessor("test_hash_map_fill") {}
    void factory(int value, int threads) {
        if(Verbose) printf("Processing with %d threads: %d...\n", threads, value);
        process( value, threads,
#if OLDTABLE
            run("old", new ValuePerSecond<TestHashCountStrings<OldTable>, 1000000/*ns*/>() ),
#endif
            run("tbb", new ValuePerSecond<TestHashCountStrings<IntTable>, 1000000/*ns*/>() ),
#if TESTTABLE
            run("new", new ValuePerSecond<TestHashCountStrings<TestTable>,1000000/*ns*/>() ),
#endif
        end );
        //stat->Print(StatisticsCollector::HTMLFile);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[]) {
    if(argc>1) Verbose = true;
    //if(argc>2) ExtraVerbose = true;
    MinThread = 1; MaxThread = task_scheduler_init::default_num_threads();
    ParseCommandLine( argc, argv );

    ASSERT(tbb_allocator<int>::allocator_type() == tbb_allocator<int>::scalable, "expecting scalable allocator library to be loaded. Please build it by:\n\t\tmake tbbmalloc");

    {
        test_hash_map_find test_find; int o = test2_size;
        for( int t=MinThread; t <= MaxThread; t++)
            test_find.factory(o, t);
        test_find.report.SetTitle("Operations per nanosecond", o);
        test_find.report.Print(StatisticsCollector::HTMLFile|StatisticsCollector::ExcelXML);
    }
    return 0;
}

