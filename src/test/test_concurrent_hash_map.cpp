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
#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
#include "tbb/atomic.h"
#include "tbb/tick_count.h"
#include "harness.h"

/** Has tighly controlled interface so that we can verify
    that concurrent_hash_map uses only the required interface. */
class MyKey {
private:
    void operator=( const MyKey&  );    // Deny access
    int key;
    friend class MyHashCompare;
public:
    static MyKey make( int i ) {
        MyKey result;
        result.key = i;
        return result;
    }
    int value_of() const {return key;}
};

tbb::atomic<long> MyDataCount;

class MyData {
private:
    int data;
    enum state_t {
        LIVE=0x1234,
        DEAD=0x5678
    } my_state;
    void operator=( const MyData& );    // Deny acces
public:
    MyData() {
        my_state = LIVE;
        ++MyDataCount;
    }
    MyData( const MyData& other ) {
        ASSERT( other.my_state==LIVE, NULL );
        my_state = LIVE;
        data = other.data;
        ++MyDataCount;
    }
    ~MyData() {
        --MyDataCount;
        my_state = DEAD;
    }
    static MyData make( int i ) {   
        MyData result;
        result.data = i;
        return result;
    }
    int value_of() const {
        ASSERT( my_state==LIVE, NULL );
        return data;
    }
    void set_value( int i ) {
        ASSERT( my_state==LIVE, NULL );
        data = i;
    }
};

class MyHashCompare {
public:
    bool equal( const MyKey& j, const MyKey& k ) const {
        return j.key==k.key;
    }
    unsigned long hash( const MyKey& k ) const {
        return k.key;
    }   
};

typedef tbb::concurrent_hash_map<MyKey,MyData,MyHashCompare> MyTable;

inline bool UseKey( size_t i ) {
    return (i&3)!=3;
}

struct Insert {
    static void apply( MyTable& table, int i ) {
        MyTable::accessor a;    
        if( UseKey(i) ) {
            table.insert( a, MyKey::make(i) );
            if( i&1 )
                (*a).second.set_value( i*i );
            else
                a->second.set_value(i*i);
        }
    }
};

struct Find {
    static void apply( MyTable& table, int i ) {
        MyTable::accessor a;    
        const MyTable::accessor& ca = a;
        bool b = table.find( a, MyKey::make(i) );
        ASSERT( b==!a.empty(), NULL );
        if( b ) {
            if( !UseKey(i) )
                printf("Line %d: unexpected key %d present\n",__LINE__,i);
            AssertSameType( &*a, static_cast<MyTable::value_type*>(0) );
            ASSERT( ca->second.value_of()==i*i, NULL );
            ASSERT( (*ca).second.value_of()==i*i, NULL );
            if( i&1 )
                ca->second.set_value( ~ca->second.value_of() );
            else
                (*ca).second.set_value( ~ca->second.value_of() );
        } else {
            if( UseKey(i) ) 
                printf("Line %d: key %d missing\n",__LINE__,i);
        }
    }
};

struct FindConst {
    static void apply( const MyTable& table, int i ) {
        MyTable::const_accessor a;      
        const MyTable::const_accessor& ca = a;
        bool b = table.find( a, MyKey::make(i) );
        ASSERT( b==!a.empty(), NULL );
        ASSERT( b==UseKey(i), NULL );
        if( b ) {
            AssertSameType( &*ca, static_cast<const MyTable::value_type*>(0) );
            ASSERT( ca->second.value_of()==~(i*i), NULL );
            ASSERT( (*ca).second.value_of()==~(i*i), NULL );
        }
    }
};

tbb::atomic<int> EraseCount;

struct Erase {
    static void apply( MyTable& table, int i ) {
        bool b = table.erase( MyKey::make(i) );
        if( b ) ++EraseCount;
    }
};

template<typename Op>
class TableOperation {
    MyTable& my_table;
public:
    void operator()( const tbb::blocked_range<int>& range ) const {
        for( int i=range.begin(); i!=range.end(); ++i ) 
            Op::apply(my_table,i);
    }
    TableOperation( MyTable& table ) : my_table(table) {}
};

template<typename Op>
void DoConcurrentOperations( MyTable& table, int n, char* what, int nthread ) {
    if( Verbose ) 
        printf("testing %s with %d threads\n",what,nthread);
    tbb::tick_count t0 = tbb::tick_count::now();
    tbb::parallel_for( tbb::blocked_range<int>(0,n,100), TableOperation<Op>(table) );
    tbb::tick_count t1 = tbb::tick_count::now();
    if( Verbose )
        printf("time for %s = %g with %d threads\n",what,(t1-t0).seconds(),nthread);
}

//! Test traversing the table with an iterator.
void TraverseTable( MyTable& table, size_t n, size_t expected_size ) {
    if( Verbose ) 
        printf("testing traversal\n");
    size_t actual_size = table.size();
    ASSERT( actual_size==expected_size, NULL );
    size_t count = 0;
    bool* array = new bool[n];
    memset( array, 0, n*sizeof(bool) );
    const MyTable& const_table = table;
    MyTable::const_iterator ci = const_table.begin();
    for( MyTable::iterator i = table.begin(); i!=table.end(); ++i ) {
        // Check iterator
        int k = i->first.value_of();
        ASSERT( UseKey(k), NULL );
        ASSERT( (*i).first.value_of()==k, NULL );
        ASSERT( 0<=k && size_t(k)<n, "out of bounds key" );
        ASSERT( !array[k], "duplicate key" );
        array[k] = true;
        ++count;

        // Check const_iterator
        ASSERT( ci->first.value_of()==k, NULL );
        ASSERT( (*ci).first.value_of()==k, NULL );
        ++ci;
    }
    ASSERT( ci==const_table.end(), NULL );
    delete[] array;
    if( count!=expected_size ) {
        printf("Line %d: count=%ld but should be %ld\n",__LINE__,long(count),long(expected_size));
    }
}

typedef tbb::atomic<unsigned char> AtomicByte;

template<typename RangeType>
struct ParallelTraverseBody {
    const size_t n;
    AtomicByte* const array;
    ParallelTraverseBody( AtomicByte array_[], size_t n_ ) : 
        n(n_), 
        array(array_)
    {}
    void operator()( const RangeType& range ) const {
        for( MyTable::iterator i = range.begin(); i!=range.end(); ++i ) {
            int k = i->first.value_of();
            ASSERT( 0<=k && size_t(k)<n, NULL ); 
            ++array[k];
        }
    }
};

void Check( AtomicByte array[], size_t n, size_t expected_size ) {
    if( expected_size )
        for( size_t k=0; k<n; ++k ) {
            if( array[k] != int(UseKey(k)) ) {
                printf("array[%d]=%d != %d=UseKey(%d)\n",
                       int(k), int(array[k]), int(UseKey(k)), int(k));
                ASSERT(false,NULL);
            }
        }
}

//! Test travering the tabel with a parallel range
void ParallelTraverseTable( MyTable& table, size_t n, size_t expected_size ) {
    if( Verbose ) 
        printf("testing parallel traversal\n");
    ASSERT( table.size()==expected_size, NULL );
    AtomicByte* array = new AtomicByte[n];

    memset( array, 0, n*sizeof(AtomicByte) );
    MyTable::range_type r = table.range(10);
    tbb::parallel_for( r, ParallelTraverseBody<MyTable::range_type>( array, n ));
    Check( array, n, expected_size );

    const MyTable& const_table = table;
    memset( array, 0, n*sizeof(AtomicByte) );
    MyTable::const_range_type cr = const_table.range(10);
    tbb::parallel_for( cr, ParallelTraverseBody<MyTable::const_range_type>( array, n ));
    Check( array, n, expected_size );

    delete[] array;
}

void TestInsertFindErase( int nthread ) {
    int n=250000; 

    // compute m = number of unique keys
    int m = 0;       
    for( int i=0; i<n; ++i )
        m += UseKey(i);
 
    ASSERT( MyDataCount==0, NULL );
    MyTable table;
    TraverseTable(table,n,0);
    ParallelTraverseTable(table,n,0);

    DoConcurrentOperations<Insert>(table,n,"insert",nthread);
    ASSERT( MyDataCount==m, NULL );
    TraverseTable(table,n,m);
    ParallelTraverseTable(table,n,m);

    DoConcurrentOperations<Find>(table,n,"find",nthread);
    ASSERT( MyDataCount==m, NULL );

    DoConcurrentOperations<FindConst>(table,n,"find(const)",nthread);
    ASSERT( MyDataCount==m, NULL );

    EraseCount=0;
    DoConcurrentOperations<Erase>(table,n,"erase",nthread);
    ASSERT( EraseCount==m, NULL );
    ASSERT( MyDataCount==0, NULL );
    TraverseTable(table,n,0);
}

volatile int Counter;

class AddToTable {
    MyTable& my_table;
    const int my_nthread;
    const int my_m;
public:
    AddToTable( MyTable& table, int nthread, int m ) : my_table(table), my_nthread(nthread), my_m(m) {}
    void operator()( const  tbb::blocked_range<int>& r ) const {
        for( int i=0; i<my_m; ++i ) {
            // Busy wait to synchronize threads
            int j = 0;
            while( Counter<i ) {
                if( ++j==1000000 ) {
                    // If Counter<i after a million iterations, then we almost surely have
                    // more logical threads than physical threads, and should yield in 
                    // order to let suspended logical threads make progress.
                    j = 0;
#if __linux__||__APPLE__
                    sched_yield();
#else
                    Sleep(0);
#endif /* __linux__ */
                }
            }
            // Now all threads attempt to simultaneously insert a key.
            int k;
            {
                MyTable::accessor a, b;
                MyKey key = MyKey::make(i);
                if( my_table.insert( a, key ) ) 
                    a->second.set_value( 1 );
                else 
                    a->second.set_value( a->second.value_of()+1 );      
                k = a->second.value_of();
            }
            if( k==my_nthread ) 
                Counter=i+1;
        }
    }
};

//! Test for memory leak in concurrent_hash_map (TR #153).
void TestMultipleInsert( int nthread ) {
    if( Verbose ) 
        printf("testing multiple insertions of same key with %d threads\n", nthread);
    {
        ASSERT( MyDataCount==0, NULL );
        MyTable table;
        const int m = 1000;
        tbb::tick_count t0 = tbb::tick_count::now();
        tbb::parallel_for( tbb::blocked_range<int>(0,nthread,1), AddToTable(table,nthread,m) );
        tbb::tick_count t1 = tbb::tick_count::now();
        if( Verbose )
            printf("time for multiple insertions = %g with %d threads\n",(t1-t0).seconds(),nthread);
        ASSERT( MyDataCount==m, "memory leak detected" );
    }
    ASSERT( MyDataCount==0, "memory leak detected" );
}

void TestTypes() {
    AssertSameType( static_cast<MyTable::key_type*>(0), static_cast<MyKey*>(0) );
    AssertSameType( static_cast<MyTable::mapped_type*>(0), static_cast<MyData*>(0) );
    AssertSameType( static_cast<MyTable::value_type*>(0), static_cast<std::pair<const MyKey,MyData>*>(0) );
    AssertSameType( static_cast<MyTable::accessor::value_type*>(0), static_cast<MyTable::value_type*>(0) );
    AssertSameType( static_cast<MyTable::const_accessor::value_type*>(0), static_cast<const MyTable::value_type*>(0) );
    AssertSameType( static_cast<MyTable::size_type*>(0), static_cast<size_t*>(0) );
    AssertSameType( static_cast<MyTable::difference_type*>(0), static_cast<ptrdiff_t*>(0) );
}

template<typename Iterator, typename T>
void TestIteratorTraits() {
    AssertSameType( static_cast<typename Iterator::difference_type*>(0), static_cast<ptrdiff_t*>(0) );
    AssertSameType( static_cast<typename Iterator::value_type*>(0), static_cast<T*>(0) );
    AssertSameType( static_cast<typename Iterator::pointer*>(0), static_cast<T**>(0) );
    AssertSameType( static_cast<typename Iterator::iterator_category*>(0), static_cast<std::forward_iterator_tag*>(0) );
    T x;
    typename Iterator::reference xr = x;
    typename Iterator::pointer xp = &x;
    ASSERT( &xr==xp, NULL );
}

//------------------------------------------------------------------------
// Test for copy constructor and assignment
//------------------------------------------------------------------------

void FillTable( MyTable& x, int n ) {
    for( int i=0; i<n; ++i ) {
        MyKey key( MyKey::make(i) );
        MyTable::accessor a;
        bool b = x.insert(a,key); 
        ASSERT(b,NULL); 
        a->second.set_value( i*i );
    }
}

void CheckTable( const MyTable& x, int n ) {
    ASSERT( x.size()==size_t(n), "table is different size than expected" );
    ASSERT( x.empty()==(n==0), NULL );
    ASSERT( x.size()<=x.max_size(), NULL );
    for( int i=0; i<n; ++i ) {
        MyKey key( MyKey::make(i) );
        MyTable::const_accessor a;
        bool b = x.find(a,key); 
        ASSERT( b, NULL ); 
        ASSERT( a->second.value_of()==i*i, NULL );
    }
    int count = 0;
    int key_sum = 0;
    for( MyTable::const_iterator i(x.begin()); i!=x.end(); ++i ) {
        ++count;
        key_sum += i->first.value_of();
    }
    ASSERT( count==n, NULL );
    ASSERT( key_sum==n*(n-1)/2, NULL );
}

void TestCopy() {
    if( Verbose )
        printf("testing copy\n");
    MyTable t1;
    for( int i=0; i<10000; i=(i<100 ? i+1 : i*3) ) {
        MyDataCount = 0;

        FillTable( t1, i );
        CheckTable(t1,i);

        MyTable t2(t1);
        // Check that copy constructor did not mangle source table.
        CheckTable(t1,i);

        // Clear original table
        t1.clear();
        CheckTable(t1,0);

        // Verify that copy of t1 is correct, even after t1 is cleared.
        CheckTable(t2,i);
        t2.clear();
        CheckTable(t2,0);
        ASSERT( MyDataCount==0, "data leak?" );
    }
}

void TestAssignment() {
    if( Verbose )
        printf("testing assignment\n");
    for( int i=0; i<1000; i=(i<30 ? i+1 : i*5) ) {
        for( int j=0; j<1000; j=(j<30 ? j+1 : j*7) ) {
            MyTable t1;
            MyTable t2;
            FillTable(t1,i);
            FillTable(t2,j);
            CheckTable(t1,i);
            CheckTable(t2,j);

            MyTable& tref = t2=t1; 
            ASSERT( &tref==&t2, NULL );
            CheckTable(t1,i);
            CheckTable(t2,i);

            t1.clear();
            CheckTable(t1,0);
            CheckTable(t2,i);
            ASSERT( MyDataCount==i, "data leak?" );

            t2.clear();
            CheckTable(t1,0);
            CheckTable(t2,0);
            ASSERT( MyDataCount==0, "data leak?" );
        }
    }
}

//------------------------------------------------------------------------
// Test driver
//------------------------------------------------------------------------

#include "tbb/task_scheduler_init.h"

//! Test driver
int main( int argc, char* argv[] ) {
    // Default minimum number of threads is 1.
    MinThread = 1;

    ParseCommandLine(argc,argv);
    if( MinThread<0 ) {
        printf("ERROR: must use at least one thread\n");
        exit(1);
    }

    // Do serial tests
    TestTypes();
    TestIteratorTraits<MyTable::iterator,MyTable::value_type>();
    TestIteratorTraits<MyTable::const_iterator,const MyTable::value_type>();
    TestCopy();
    TestAssignment();

    // Do concurrency tests.
    for( int nthread=MinThread; nthread<=MaxThread; ++nthread ) {
        tbb::task_scheduler_init init( nthread );
        TestInsertFindErase( nthread );
        TestMultipleInsert( nthread );
    }

    printf("done\n");
    return 0;
}
