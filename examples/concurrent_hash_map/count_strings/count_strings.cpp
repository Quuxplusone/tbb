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

#include "tbb/concurrent_hash_map.h"
#include "tbb/blocked_range.h"
#include "tbb/parallel_for.h"
#include "tbb/tick_count.h"
#include "tbb/task_scheduler_init.h"
#include <string>
#include <cctype>

using namespace tbb;
using namespace std;

//! Set to true to counts.
static bool Verbose = false;
//! Working threads count
static int NThread = 1;
//! Problem size
const size_t N = 1000000;
//! Indicates if the number of threads wasn't set explicitly
static bool is_number_of_threads_set = false;

//! Structure that defines hashing and comparison operations for user's type.
struct MyHashCompare {
    static size_t hash( const string& x ) {
        size_t h = 0;
        for( const char* s = x.c_str(); *s; s++ )
            h = (h*17)^*s;
        return h;
    }
    //! True if strings are equal
    static bool equal( const string& x, const string& y ) {
        return x==y;
    }
};

//! A concurrent hash table that maps strings to ints.
typedef concurrent_hash_map<string,int,MyHashCompare> StringTable;

//! Function object for counting occurrences of strings.
struct Tally {
    StringTable& table;
    Tally( StringTable& table_ ) : table(table_) {}
    void operator()( const blocked_range<string*> range ) const {
        for( string* p=range.begin(); p!=range.end(); ++p ) {
            StringTable::accessor a;
            table.insert( a, *p );
            a->second += 1;
        }
    }
};

static string Data[N];

static void CountOccurrences(int nthreads) {
    StringTable table;

    tick_count t0 = tick_count::now();
    parallel_for( blocked_range<string*>( Data, Data+N, 1000 ), Tally(table) );
    tick_count t1 = tick_count::now();

    int n = 0;
    for( StringTable::iterator i=table.begin(); i!=table.end(); ++i ) {
        if( Verbose )
            printf("%s %d\n",i->first.c_str(),i->second);
        n+=i->second;
    }
    if (is_number_of_threads_set) {
        printf("threads = %d  total = %d  time = %g\n", nthreads, n, (t1-t0).seconds());
    }else{
        if ( nthreads == 1 ){
            printf("serial run   total = %d  time = %g\n", n, (t1-t0).seconds());
        }else{
            printf("parallel run total = %d  time = %g\n", n, (t1-t0).seconds());
        }
    }
}

static const string Adjective[] = {
    "sour",
    "sweet",
    "bitter",
    "salty",
    "big",
    "small"
};

static const string Noun[] = {
    "apple",
    "banana",
    "cherry",
    "date",
    "eggplant",
    "fig",
    "grape",
    "honeydew",
    "icao",
    "jujube"
};

static void CreateData() {
    size_t n_adjective = sizeof(Adjective)/sizeof(Adjective[0]);
    size_t n_noun = sizeof(Noun)/sizeof(Noun[0]);
    for( int i=0; i<N; ++i ) {
        Data[i] = Adjective[rand()%n_adjective];
        Data[i] += " ";
        Data[i] += Noun[rand()%n_noun];
    }
}

static void ParseCommandLine( int argc, char* argv[] ) {
    int i = 1;
    if( i<argc && strcmp( argv[i], "verbose" )==0 ) {
        Verbose = true;
        ++i;
    }
    if( i<argc && !isdigit(argv[i][0]) ) {
        fprintf(stderr,"Usage: %s [verbose] [number-of-threads]\n",argv[0]);
        exit(1);
    }
    if( i<argc ) {
        NThread = strtol(argv[i++],0,0);
        is_number_of_threads_set = true;
    }
}

int main( int argc, char* argv[] ) {
    srand(2);
    ParseCommandLine( argc, argv );
    if (is_number_of_threads_set) {
        task_scheduler_init init(NThread);
        CreateData();
        CountOccurrences(NThread);
    } else { // Number of threads wasn't set explicitly. Run serial and parallel version
        { // serial run
            task_scheduler_init init_serial(1);
            CreateData();
            CountOccurrences(1);
        }
        { // parallel run (number of threads is selected automatically)
            task_scheduler_init init_parallel;
            CreateData();
            CountOccurrences(0);
        }
    }
}
