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

/* This file contains the TBB task scheduler. There are many classes
   lumped together here because very few are exposed to the outside
   world, and by putting them in a single translation unit, the
   compiler's optimizer might be able to do a better job. */

#if USE_PTHREAD

    // Some pthreads documentation says that <pthreads.h> must be first header.
    #include <pthread.h>

#elif USE_WINTHREAD

    #include <windows.h>
    #include <process.h>        /* Need _beginthreadex from there */
    #include <malloc.h>         /* Need _alloca from there */
    const size_t ThreadStackSize = 1<<20;

#else

    #error Must define USE_PTHREAD or USE_WINTHREAD

#endif

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include "tbb/tbb_stddef.h"


/* Temporarily change "private" to "public" while including "tbb/task.h"
   This hack allows us to avoid publishing types Arena and CustomScheduler
   in the public header files. */
#define private public
#include "tbb/task.h"
#undef private

#include "tbb/task_scheduler_init.h"
#include "tbb/cache_aligned_allocator.h"
#include "tbb/tbb_stddef.h"
#include "tbb/tbb_machine.h"
#include "tbb_misc.h"
#include "tbb/mutex.h"
#include "tbb/atomic.h"
#include "tbb/gate.h"

#if DO_TBB_TRACE
#include <cstdio>
#define TBB_TRACE(x) ((void)std::printf x)
#else
#define TBB_TRACE(x) ((void)(0))
#endif /* DO_TBB_TRACE */

#if TBB_DO_ASSERT
#define COUNT_TASK_NODES 1
#endif /* TBB_DO_ASSERT */

/* If nonzero, then gather statistics */
#ifndef STATISTICS
#define STATISTICS 0
#endif /* STATISTICS */

#if STATISTICS
#define GATHER_STATISTIC(x) (x)
#else
#define GATHER_STATISTIC(x) ((void)0)
#endif /* STATISTICS */

namespace tbb {

using namespace std;

namespace internal {

template<typename SchedulerTraits> class CustomScheduler;

typedef task::depth_type depth_type;


//------------------------------------------------------------------------
// TaskPool
//------------------------------------------------------------------------

//! Prefix to a TaskPool
class TaskPoolPrefix {
    static const unsigned null_arena_index = ~0u;

    unsigned arena_index;

    //! Index of first non-empty element of TaskPool::array
    depth_type steal_begin;

    friend class GenericScheduler;
    friend class TaskPool;
};

//! Pool of tasks, organized as a deque.
class TaskPool {
    typedef size_t size_type;

    static const size_type min_array_size = (NFS_MaxLineSize-sizeof(TaskPoolPrefix))/sizeof(task*);

    /** Must be last field, because it is really array of indeterminate length. */
    task* array[min_array_size];

    //! Get reference to prefix portion
    TaskPoolPrefix& prefix() const {return ((TaskPoolPrefix*)(void*)this)[-1];}

    //! Return number of bytes required to allocate a pool with given number of array elements.
    static size_t byte_size( size_type array_size ) {
        return sizeof(TaskPoolPrefix)+array_size*sizeof(task*);
    }

    //! Allocate TaskPool object with given number of array elements.
    static TaskPool* allocate_task_pool( size_type array_size ) {
        __TBB_ASSERT( array_size>0, NULL );
        size_t n = byte_size(array_size);
        unsigned char* storage = (unsigned char*)NFS_Allocate( n, 1, NULL );
        memset( storage, 0, n );
        return (TaskPool*)(storage+sizeof(TaskPoolPrefix));
    }

    //! Deallocate a TaskPool that was allocated by method allocate.
    void free_task_pool() {
        __TBB_ASSERT( this, "attempt to free NULL TaskPool" );
        NFS_Free( &prefix() );
    }

    friend class GenericScheduler;
    template<typename SchedulerTraits> friend class internal::CustomScheduler;

#if TBB_DO_ASSERT
    bool assert_okay() const {
        __TBB_ASSERT( this!=NULL, NULL );
        __TBB_ASSERT( prefix().steal_begin>=-4, NULL );
        return true;
    }
#endif /* TBB_DO_ASSERT */
};

//------------------------------------------------------------------------
// Arena
//------------------------------------------------------------------------

class Arena;
class GenericScheduler;

struct WorkerDescriptor {
    Arena* arena;
    //! NULL until worker is published.
    GenericScheduler* volatile scheduler;
#if USE_WINTHREAD
    //! Handle of the owning thread.
    HANDLE thread_handle;
#elif USE_PTHREAD
    //! Handle of the owning thread.
    pthread_t thread_handle;
#else
    unsigned long dummy_handle;
#endif /* USE_WINTHREAD */

    //! Start worker thread for this descriptor.
    void start_one_worker_thread();
};

//! The useful contents of an ArenaPrefix
class UnpaddedArenaPrefix {
    friend class GenericScheduler;
    template<typename SchedulerTraits> friend class internal::CustomScheduler;
    friend class Arena;
    friend class tbb::task_scheduler_init;

    //! One more than index of highest arena slot currently in use.
    atomic<size_t> limit;

    //! Number of masters that own this arena.
    /** This may be smaller than the number of masters who have entered the arena. */
    unsigned number_of_masters;

    //! Total number of slots in the arena
    const unsigned number_of_slots;

    //! One more than number of workers that belong to this arena
    const unsigned number_of_workers;

    //! Array of workers.
    WorkerDescriptor* worker_list;

#if COUNT_TASK_NODES
    //! Net number of nodes that have been allocated from heap.
    /** Updated each time a scheduler is destroyed. */
    atomic<intptr> task_node_count;
#endif /* COUNT_TASK_NODES */

    //! Gate at which worker threads wait until a master spawns a task.
    Gate gate;
 
protected:
    UnpaddedArenaPrefix( unsigned number_of_slots_, unsigned number_of_workers_ ) :
        number_of_masters(1),
        number_of_slots(number_of_slots_),
        number_of_workers(number_of_workers_)
    {
#if COUNT_TASK_NODES
        task_node_count = 0;
#endif /* COUNT_TASK_NODES */
        limit = number_of_workers;
    }
};

//! The prefix to Arena with padding.
class ArenaPrefix: public UnpaddedArenaPrefix {
    //! Padding to fill out to multiple of cache line size.
    char pad[(sizeof(UnpaddedArenaPrefix)/NFS_MaxLineSize+1)*NFS_MaxLineSize-sizeof(UnpaddedArenaPrefix)];

public:
    ArenaPrefix( unsigned number_of_slots_, unsigned number_of_workers_ ) :
        UnpaddedArenaPrefix(number_of_slots_,number_of_workers_)
    {
    }
};

struct UnpaddedArenaSlot {
    //! Holds copy of task_pool->deepest and a lock bit
    /** Computed as 2*task_pool->deepest+(is_locked).
        I.e., the low order bit indicates whether the slot is locked. */
    volatile depth_type steal_end;
    TaskPool* task_pool;
    bool owner_waits;
};

struct ArenaSlot: UnpaddedArenaSlot {
    char pad[NFS_MaxLineSize-sizeof(UnpaddedArenaSlot)];
};

class Arena {
    friend class GenericScheduler;
    template<typename SchedulerTraits> friend class internal::CustomScheduler;
    friend class tbb::task_scheduler_init;

    //! Get reference to prefix portion
    ArenaPrefix& prefix() const {return ((ArenaPrefix*)(void*)this)[-1];}


    //! Allocate the arena
    /** Allocates an instance of Arena and sets TheArena to point to it.
        Creates the worker threads, but does not wait for them to start. */
    static Arena* allocate_arena( unsigned number_of_slots, unsigned number_of_workers );


    //! Terminate worker threads
    /** Wait for worker threads to complete. */
    void terminate_workers();

#if COUNT_TASK_NODES
    //! Returns the number of task objects "living" in worker threads
    inline intptr workers_task_node_count();
#endif

    /** Must be last field */
    ArenaSlot slot[1];
};

//------------------------------------------------------------------------
// Traits classes for scheduler
//------------------------------------------------------------------------

struct DefaultSchedulerTraits {
    static const int id = 0;
    static const bool itt_possible = true;
    static const bool has_slow_atomic = false;
};

struct IntelSchedulerTraits {
    static const int id = 1;
    static const bool itt_possible = false;
#if __TBB_x86_32||__TBB_x86_64
    static const bool has_slow_atomic = true;
#else
    static const bool has_slow_atomic = false;
#endif /* __TBB_x86_32||__TBB_x86_64 */
};

//------------------------------------------------------------------------
// Begin shared data layout.
//
// The follow global data items are read-only after initialization.
// The first item is aligned on a 128 byte boundary so that it starts a new cache line.
//------------------------------------------------------------------------

static internal::Arena *volatile TheArena;
static mutex TheArenaMutex;

//! T::id for the scheduler traits type T to use for the scheduler
/** For example, the default value is DefaultSchedulerTraits::id. */
static int SchedulerTraitsId;

} // namespace internal

} // namespace tbb

#include "itt_notify.h"

namespace tbb {
namespace internal {

//! Flag that is set to true after one-time initializations are done.
static volatile bool OneTimeInitializationsDone;

#if USE_WINTHREAD
static CRITICAL_SECTION OneTimeInitializationCriticalSection;
//! Index for thread-local storage.
/** The original version of TBB used __declspec(thread) for thread-local storage.
    Unfortunately, __declspec(thread) does not work on pre-Vista OSes for DLLs
    called from plug-ins. */
static DWORD TLS_Index;
#else
static pthread_mutex_t OneTimeInitializationMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t TLS_Key;
#endif /* USE_WINTHREAD */

//! Table of primes used by fast random-number generator.
/** Also serves to keep anything else from being placed in the same
    cache line as the global data items preceding it. */
static const unsigned Primes[] = {
    0x9e3779b1, 0xffe6cc59, 0x2109f6dd, 0x43977ab5,
    0xba5703f5, 0xb495a877, 0xe1626741, 0x79695e6b,
    0xbc98c09f, 0xd5bee2b3, 0x287488f9, 0x3af18231,
    0x9677cd4d, 0xbe3a6929, 0xadc6a877, 0xdcf0674b,
    0xbe4d6fe9, 0x5f15e201, 0x99afc3fd, 0xf3f16801,
    0xe222cfff, 0x24ba5fdb, 0x0620452d, 0x79f149e3,
    0xc8b93f49, 0x972702cd, 0xb07dd827, 0x6c97d5ed,
    0x085a3d61, 0x46eb5ea7, 0x3d9910ed, 0x2e687b5b,
    0x29609227, 0x6eb081f1, 0x0954c4e1, 0x9d114db9,
    0x542acfa9, 0xb3e6bd7b, 0x0742d917, 0xe9f3ffa7,
    0x54581edb, 0xf2480f45, 0x0bb9288f, 0xef1affc7,
    0x85fa0ca7, 0x3ccc14db, 0xe6baf34b, 0x343377f7,
    0x5ca19031, 0xe6d9293b, 0xf0a9f391, 0x5d2e980b,
    0xfc411073, 0xc3749363, 0xb892d829, 0x3549366b,
    0x629750ad, 0xb98294e5, 0x892d9483, 0xc235baf3,
    0x3d2402a3, 0x6bdef3c9, 0xbec333cd, 0x40c9520f
};

//! Amount of time to pause between steals.
/** The default values below were found to be best empircally for K-Means
    on the 32-way Altix and 4-way (*2 for HT) fxqlin04. */
#if __TBB_ipf
static long PauseTime = 1500;
#else 
static long PauseTime = 80;
#endif

//------------------------------------------------------------------------
// End of shared data layout
//------------------------------------------------------------------------

//------------------------------------------------------------------------
// One-time Initializations
//------------------------------------------------------------------------

//! True if running on genuine Intel hardware
static inline bool IsGenuineIntel() {
    bool result = true;
#if defined(__TBB_cpuid)
    char info[16];
    char *genuine_string = "GenuntelineI";
    __TBB_x86_cpuid( reinterpret_cast<int *>(info), 0 );
    // The multibyte chars below spell "GenuineIntel".
    //if( info[1]=='uneG' && info[3]=='Ieni' && info[2]=='letn' ) {
    //    result = true;
    //}
    for (int i = 4; i < 16; ++i) {
        if ( info[i] != genuine_string[i-4] ) {
            result = false;
            break;
        }
    }
#elif __TBB_ipf
    result = true;
#else
    result = false;
#endif
    return result;
}

//! Defined in cache_aligned_allocator.cpp
extern void initialize_cache_aligned_allocator();

//! Perform lazy one-time initializations. */
void DoOneTimeInitializations() {
#if USE_PTHREAD
    int status = 0;
    pthread_mutex_lock( &OneTimeInitializationMutex );
#else
    EnterCriticalSection( &OneTimeInitializationCriticalSection );
#endif /* USE_PTHREAD */
    if( !OneTimeInitializationsDone ) {
        if( GetBoolEnvironmentVariable("TBB_VERSION") )
            PrintVersion();
#if USE_PTHREAD
        // Create key for thread-local storage
        status = pthread_key_create( &TLS_Key, NULL );
#endif /* USE_PTHREAD */
        bool have_itt = false;
#if DO_ITT_NOTIFY
        have_itt = InitializeITT();
#endif /* DO_ITT_NOTIFY */
        initialize_cache_aligned_allocator();
        if( !have_itt )
            SchedulerTraitsId = IntelSchedulerTraits::id;
        PrintExtraVersionInfo( "SCHEDULER",
                               SchedulerTraitsId==IntelSchedulerTraits::id ? "Intel" : "default" );
        OneTimeInitializationsDone = true;
    }
#if USE_PTHREAD
    pthread_mutex_unlock( &OneTimeInitializationMutex );
    if( status )
        handle_perror(status,"pthread_key_create");
#else
    LeaveCriticalSection( &OneTimeInitializationCriticalSection );
#endif /* USE_PTHREAD */
}

#if _WIN32||_WIN64
extern "C" bool WINAPI DllMain( HANDLE hinstDLL, DWORD reason, LPVOID lpvReserved );

//! Windows "DllMain" that handles startup and shutdown of dynamic library.
/** Currently, its job is to deal with initializing/deleting
    OneTimeInitializationCriticalSection and allocating/freeing TLS_Index. */
bool WINAPI DllMain( HANDLE hinstDLL, DWORD reason, LPVOID lpvReserved ) {
    switch( reason ) {
        case DLL_PROCESS_ATTACH:
            TLS_Index = TlsAlloc();
            if( TLS_Index==TLS_OUT_OF_INDEXES ) {
#if TBB_DO_ASSERT
                // Issue diagnostic here, not failing assertion, because client
                // might want to test graceful recovery from this problem.
                fprintf( stderr, "DllMain for TBB: TlsAlloc() returned TLS_OUT_OF_INDEXES\n" );
#endif /* TBB_DO_ASSERT */
                return false;
            }
            InitializeCriticalSection(&OneTimeInitializationCriticalSection);
            break;
        case DLL_PROCESS_DETACH:
            DeleteCriticalSection(&OneTimeInitializationCriticalSection);
#if TBB_DO_ASSERT
            if( TlsGetValue(TLS_Index) ) {
                fprintf( stderr, "DllMain for TBB: thread quit without destroying its tbb::task_scheduler_init object?" );
            }
#endif /* TBB_DO_ASSERT */
            TlsFree(TLS_Index);
            TLS_Index = 0;
            break;
    }
    return true;
}
#endif /* _WIN32||_WIN64 */

//------------------------------------------------------------------------
// Routines for thread-specific global data
//------------------------------------------------------------------------

static inline void SetThreadSpecific( GenericScheduler* s ) {
#if USE_WINTHREAD
    TlsSetValue( TLS_Index, s );
#else
    pthread_setspecific( TLS_Key, s );
#endif /* USE_WINTHREAD */
}

//! Get scheduler belonging to the current thread.
/** Returns NULL if this is the first time the thread has requested a scheduler.
    It's the client's responsibility to check for the NULL, because in many
    contexts, we can proved that it cannot be NULL. */
static inline GenericScheduler* GetThreadSpecific() {
    GenericScheduler *result;
    // The assertion on OneTimeInitializationsDone checks that we can safely
    // use TLS_Key/TLS_Index; i.e., that TLS_Key/TLS_Index has been initialized.
    // The assertion message is intended to help end users.  Even though
    // OneTimeInitializationsDone might be set for other reasons, if it is
    // *not* set when a thread reaches here, the reason is almost
    // certainly that the thread failed to create a task_scheduler_init object.
    __TBB_ASSERT( OneTimeInitializationsDone, "thread did not activate a task_scheduler_init object?" );
#if USE_WINTHREAD
    result = (GenericScheduler*)TlsGetValue( TLS_Index );
#else
    result = (GenericScheduler*)pthread_getspecific( TLS_Key );
#endif /* USE_WINTHREAD */
    return result;
}

//------------------------------------------------------------------------
// FastRandom
//------------------------------------------------------------------------

//! A fast random number generator.
/** Uses linear congruential method. */
class FastRandom {
    unsigned x, a;
public:
    //! Get a random number.
    unsigned short get() {
        unsigned short r = x>>16;
        x = x*a+1;
        return r;
    }
    //! Construct a random number generator.
    FastRandom( unsigned seed ) {
        x = seed;
        a = Primes[seed%(sizeof(Primes)/sizeof(Primes[0]))];
    }
};

//------------------------------------------------------------------------
// GenericScheduler
//------------------------------------------------------------------------

//  A pure virtual destructor should still have a body
//  so the one for tbb::internal::scheduler::~scheduler() is provided here
scheduler::~scheduler( ) {}

//! Cilk-style task scheduler.
/** None of the fields here are every read or written by threads other than
    the thread that creates the instance.

    Class GenericScheduler is an abstract base class that contains most of the scheduler,
    except for tweaks specific to processors and tools (e.g. VTune).
    The derived template class CustomScheduler<SchedulerTraits> fills in the tweaks. */
class GenericScheduler: public scheduler {
    typedef task::depth_type depth_type;
    friend class tbb::task;
    friend class tbb::task_scheduler_init;
    friend struct WorkerDescriptor;
    friend class Arena;
    friend class allocate_root_proxy;
    friend class scheduler;
    template<typename SchedulerTraits> friend class internal::CustomScheduler;

    //! If sizeof(task) is <=quick_task_size, it is handled on a free list instead of malloc'd.
    static const size_t quick_task_size = 256-sizeof(internal::task_prefix);

    //! Bit masks
    enum debug_state_t {
#if TBB_DO_ASSERT
        ds_debug = 1<<0,
        ds_ref_count_active = 1<<1
#else
        ds_release = 0
#endif /* TBB_DO_ASSERT */
    };

    //! Deepest non-empty level.
    /** Not read by thieves. -1 if array is empty. */
    depth_type deepest;

    //! The physical number of slots in "array".
    /** Read by thieves. */
    TaskPool::size_type array_size;

    //! Dummy slot used when scheduler is not in arena
    UnpaddedArenaSlot dummy_slot;

    //! Pointer to my slot in the arena
    mutable UnpaddedArenaSlot* arena_slot;

    //! The arena that I own (if master) or belong to (if worker)
    Arena* const arena;

    //! Random number generator used for picking a random victim from which to steal.
    FastRandom random;

    //! Free list of small tasks that can be reused.
    task* free_list;

    //! Innermost task whose task::execute() is running.
    task* innermost_running_task;

    //! Fake root task created by slave threads.
    /** The task is used as the "parent" argument to method wait_for_all. */
    task* dummy_task;

    //! Reference count for scheduler
    long ref_count;

#if !IMPROVED_GATING
    //! Set to non-null temporarily when entering arena
    /** Always NULL if insert_task is not running. */
    Gate* open_gate;
#endif /* IMPROVED_GATING */


#if COUNT_TASK_NODES
    //! Net number of big task objects that have been allocated but not yet freed.
    intptr task_node_count;
#endif /* COUNT_TASK_NODES */

#if STATISTICS
    long execute_count;
    long steal_count;
    long current_active;
    long current_length;
    //! Number of big tasks that have been malloc'd.
    /** To find total number of tasks malloc'd, compute (current_big_malloc+small_task_count) */
    long current_big_malloc;
#endif /* STATISTICS */

    //! Try to enter the arena
    /** On return, guaranteess that task pool has been acquired. */
    void try_enter_arena();

    //! Leave the arena
    void leave_arena( bool compress );

    void acquire_task_pool() const;

    void release_task_pool() const;

    //! Get task from ready pool.
    /** Called only by the thread that owns *this.
        Gets task only if there is one at depth d or deeper in the pool.
        If successful, unlinks the task and returns a pointer to it.
        Otherwise returns NULL. */
    task* get_task( depth_type d );

    //! Steal task from another scheduler's ready pool.
    task* steal_task( UnpaddedArenaSlot& arena_slot, depth_type d );

    //! Grow "array" to at least "minimum_size" elements.
    /** Does nothing if array is already that big.
        Returns &array[minimum_size-1] */
    void grow( TaskPool::size_type minimum_size );

    //! Call destructor for a task and put it on the free list (or free it if it is big).
    void destroy_task( task& t ) {
        TBB_TRACE(("%p.destroy_task(%p)\n",this,&t));
        __TBB_ASSERT( t.is_owned_by_current_thread(), "task owned by different thread" );
        t.~task();
        free_task<no_hint>( t );
    }
    static GenericScheduler* create_master( Arena* a );

    /** The workers are started up as a binary tree, where each vertex in the tree
        starts any children it has.  The tree is implicitly arranged in TheWorkerList
        like a binary heap. */
    static GenericScheduler* create_worker( WorkerDescriptor& w );

    //! Top-level routine for worker threads
    /** Argument arg is a WorkerDescriptor*, cast to a (void*). */
#if USE_WINTHREAD
    static unsigned WINAPI worker_routine( void* arg );
#else
    static void* worker_routine( void* arg );
#endif /* USE_WINTHREAD */

    //! Called by slave threads to free memory and wait for other threads.
    static void cleanup_worker_thread( void* arg );

protected:
    GenericScheduler( Arena* arena );

#if TBB_DO_ASSERT || TEST_ASSEMBLY_ROUTINES
    //! Check that internal data structures are in consistent state.
    /** Raises __TBB_ASSERT failure if inconsistency is found. */
    bool assert_okay() const;
#endif /* TBB_DO_ASSERT || TEST_ASSEMBLY_ROUTINES */

public:
    /*override*/ void spawn( task& first, task*& next );

    /*override*/ void spawn_root_and_wait( task& first, task*& next );

    static GenericScheduler* allocate_scheduler( Arena* arena );

    //! Destroy and deallocate scheduler that was created with method allocate.
    void free_scheduler();

    //! Allocate task object, either from the heap or a free list.
    /** Returns uninitialized task object with initialized prefix. */
    task& allocate_task( size_t number_of_bytes, depth_type depth, task* parent );

    //! Optimization hint to free_task that enables it omit unnecessary tests and code.
    enum hint {
        //! No hint 
        no_hint=0,
        //! Task is known to have been allocated by this scheduler
        is_local=1,
        //! Task is known to be a small task.
        /** Task should be returned to the free list of *some* scheduler, possibly not this scheduler. */
        is_small=2,
        //! Bitwise-OR of is_local and is_small.  
        /** Task should be returned to free list of this scheduler. */
        is_small_local=3
    };

    //! Put task on free list.
    /** Does not call destructor. */
    template<hint h>
    void free_task( task& t );


    void deallocate_task( task& t ) {
        task_prefix& p = t.prefix();
#if TBB_DO_ASSERT
        p.state = 0xFF;
        poison_pointer(p.next);
#endif /* TBB_DO_ASSERT */
        NFS_Free(&p);
#if COUNT_TASK_NODES
        task_node_count -= 1;
#endif /* COUNT_TASK_NODES */
    }

    //! True if running on a worker thread, false otherwise.
    inline bool is_worker() {
        return (dummy_slot.task_pool->prefix().arena_index < arena->prefix().number_of_workers);
    }

#if IMPROVED_GATING
    //! No tasks to steal since last snapshot was taken
    static const Gate::state_t SNAPSHOT_EMPTY = 0;

    //! At least one task has been offered for stealing since the last snapshot started
    static const Gate::state_t SNAPSHOT_FULL = -1;

    //! Gate is permanently open
    static const Gate::state_t SNAPSHOT_PERMANENTLY_OPEN = -2;

    //! If necessary, inform gate that task was added to pool recently.
    void mark_pool_full();

    //! Wait while pool is empty
    void wait_while_pool_is_empty();
#endif /* IMPROVED_GATING */
                 
#if TEST_ASSEMBLY_ROUTINES
    /** Defined in test_assembly.cpp */
    void test_assembly_routines();
#endif /* TEST_ASSEMBLY_ROUTINES */

#if COUNT_TASK_NODES
    intptr get_task_node_count( bool count_arena_workers = false ) {
        return task_node_count + (count_arena_workers? arena->workers_task_node_count(): 0);
    }
#endif

    //! Special value used to mark return_list as not taking any more entries.
    static task* plugged_return_list() {return (task*)(intptr)(-1);}

    //! Number of small tasks that have been allocated by this scheduler. 
    intptr small_task_count;

    //! List of small tasks that have been returned to this scheduler by other schedulers.
    // FIXME - see if puting return_list on separate cache line improves performance, 
    // e.g. on a prefix of the scheduler
    task* volatile return_list;    

    //! Free a small task t that that was allocated by a different scheduler 
    void free_nonlocal_small_task( task& t ); 
};

inline task& GenericScheduler::allocate_task( size_t number_of_bytes, depth_type depth, task* parent ) {
    GATHER_STATISTIC(current_active+=1);
    task* t = free_list;
    if( number_of_bytes<=quick_task_size ) {
        if( t ) {
            GATHER_STATISTIC(current_length-=1);
            __TBB_ASSERT( t->state()==task::freed, "free list of tasks is corrupted" );
            free_list = t->prefix().next;
        } else if( return_list ) {
            t = (task*)__TBB_FetchAndStoreW( &return_list, 0 );
            __TBB_ASSERT( t, "another thread emptied the return_list" );
            __TBB_ASSERT( t->prefix().origin==this, "task returned to wrong return_list" );
            free_list = t->prefix().next;
        } else {
            t = &((task_prefix*)NFS_Allocate( sizeof(task_prefix)+quick_task_size, 1, NULL ))->task();
#if COUNT_TASK_NODES
            ++task_node_count;
#endif /* COUNT_TASK_NODES */
            t->prefix().origin = this;
            ++small_task_count;
        }
    } else {
        GATHER_STATISTIC(current_big_malloc+=1);
        t = &((task_prefix*)NFS_Allocate( sizeof(task_prefix)+number_of_bytes, 1, NULL ))->task();
#if COUNT_TASK_NODES
        ++task_node_count;
#endif /* COUNT_TASK_NODES */
        t->prefix().origin = NULL;
    }
    task_prefix& p = t->prefix();
    p.owner = this;
    p.ref_count = 0;
    p.depth = int(depth);
    p.parent = parent;
#if TBB_DO_ASSERT
    p.debug_state = GenericScheduler::ds_debug;
#else
    // Clear reserved0 so that if client compiles their code with TBB_DO_ASSERT set,
    // the assertions in task.h that inspect debug_state still work.
    p.reserved0 = GenericScheduler::ds_release;
#endif /* TBB_DO_ASSERT */
    p.state = task::allocated;
    return *t;
}

template<GenericScheduler::hint h>
inline void GenericScheduler::free_task( task& t ) {
    GATHER_STATISTIC(current_active-=1);
    task_prefix& p = t.prefix();
    // Verify that optimization hints are correct.
    __TBB_ASSERT( h!=is_small_local || p.origin==this, NULL );
    __TBB_ASSERT( !(h&is_small) || p.origin, NULL );
#if TBB_DO_ASSERT
    p.depth = 0xDEADBEEF;
    p.ref_count = 0xDEADBEEF;
    poison_pointer(p.owner);
#endif /* TBB_DO_ASSERT */
    __TBB_ASSERT( 1L<<t.state() & (1L<<task::executing|1L<<task::allocated), NULL );
    p.state = task::freed;
    if( h==is_small_local || p.origin==this ) {
        GATHER_STATISTIC(current_length+=1);
        p.next = free_list;
        free_list = &t;
    } else if( h&is_local || p.origin ) {
        free_nonlocal_small_task(t);
    } else {  
        deallocate_task(t);
    }
}

void GenericScheduler::free_nonlocal_small_task( task& t ) {
    __TBB_ASSERT( t.state()==task::freed, NULL );
    GenericScheduler& s = *static_cast<GenericScheduler*>(t.prefix().origin);
    __TBB_ASSERT( &s!=this, NULL );
    for(;;) {
        task* old = s.return_list;
        if( old==plugged_return_list() ) 
            break;
        // Atomically insert t at head of s.return_list
        t.prefix().next = old; 
        if( __TBB_CompareAndSwapW( &s.return_list, (intptr)&t, (intptr)old )==(intptr)old ) 
            return;
    }
    deallocate_task(t);
    if( __TBB_FetchAndDecrementWrelease( &s.small_task_count )==1 ) {
        // We freed the last task allocated by scheduler s, so it's our responsibility
        // to free the scheduler.
        NFS_Free( &s );
    }
}

#if IMPROVED_GATING
inline void GenericScheduler::mark_pool_full() {
     // Double-check idiom
     Gate::state_t snapshot = arena->prefix().gate.get_state();
     if( snapshot!=SNAPSHOT_FULL && snapshot!=SNAPSHOT_PERMANENTLY_OPEN ) 
         arena->prefix().gate.try_update( SNAPSHOT_FULL, SNAPSHOT_PERMANENTLY_OPEN, true );
}

void GenericScheduler::wait_while_pool_is_empty() {
    for(;;) {
        Gate::state_t snapshot = arena->prefix().gate.get_state();
        switch( snapshot ) {
            case SNAPSHOT_EMPTY:
                arena->prefix().gate.wait();
                return;
            case SNAPSHOT_FULL: {
                // Use unique id for "busy" in order to avoid ABA problems.
                const Gate::state_t busy = Gate::state_t(this);
                // Request permission to take snapshot
                arena->prefix().gate.try_update( busy, SNAPSHOT_FULL );
                if( arena->prefix().gate.get_state()==busy ) {
                    // Got permission.  Take the snapshot.
                    size_t n = arena->prefix().limit;
                    size_t k; 
                    for( k=0; k<n; ++k ) 
                        if( arena->slot[k].steal_end>=0 ) 
                            break;
                    // Test and test-and-set.
                    if( arena->prefix().gate.get_state()==busy ) {
                        if( k>=n ) {
                            arena->prefix().gate.try_update( SNAPSHOT_EMPTY, busy );
                            continue;
                        } else {
                            arena->prefix().gate.try_update( SNAPSHOT_FULL, busy );
                        }
                    }
                    return;
                }
                break;
            }
            default:
                // Another thread is taking a snapshot or gate is permanently open.
                return;
        }
    }
}
#endif /* IMPROVED_GATING */

//------------------------------------------------------------------------
// CustomScheduler
//------------------------------------------------------------------------

//! A scheduler with a customized evaluation loop.
/** The customization can use SchedulerTraits to make decisions without needing a run-time check. */
template<typename SchedulerTraits>
class CustomScheduler: private GenericScheduler {
    //! Scheduler loop that dispatches tasks.
    /** If child is non-NULL, it is dispatched first.
        Then, until "parent" has a reference count of 1, other task are dispatched or stolen. */
    /*override*/void wait_for_all( task& parent, task* child );

    typedef CustomScheduler<SchedulerTraits> scheduler_type;

    //! Construct a CustomScheduler
    CustomScheduler( Arena* arena ) : GenericScheduler(arena) {}

public:
    static GenericScheduler* allocate_scheduler( Arena* arena ) {
        __TBB_ASSERT( arena, "missing arena" );
        scheduler_type* s = (scheduler_type*)NFS_Allocate(sizeof(scheduler_type),1,NULL);
        new( s ) scheduler_type(  arena );
        __TBB_ASSERT( s->dummy_slot.task_pool->assert_okay(), NULL );
        return s;
    }
};


//------------------------------------------------------------------------
// AssertOkay
//------------------------------------------------------------------------
#if TBB_DO_ASSERT
/** Logically, this method should be a member of class task.
    But we do not want to publish it, so it is here instead. */
static bool AssertOkay( const task& task ) {
    __TBB_ASSERT( &task!=NULL, NULL );
    __TBB_ASSERT((uintptr)&task % internal::NFS_GetLineSize() == sizeof(task_prefix), "misaligned task" );
    __TBB_ASSERT( (unsigned)task.state()<=(unsigned)task::recycle, "corrupt task (invalid state)" );
    __TBB_ASSERT( task.prefix().depth<1L<<30, "corrupt task (absurd depth)" );
    return true;
}
#endif /* TBB_DO_ASSERT */

//------------------------------------------------------------------------
// Methods of Arena
//------------------------------------------------------------------------
Arena* Arena::allocate_arena( unsigned number_of_slots, unsigned number_of_workers ) {
    __TBB_ASSERT( sizeof(ArenaPrefix) % NFS_GetLineSize()==0, "ArenaPrefix not multiple of cache line size" );
    size_t n = sizeof(ArenaPrefix) + number_of_slots*sizeof(ArenaSlot);

    unsigned char* storage = (unsigned char*)NFS_Allocate( n, 1, NULL );
    memset( storage, 0, n );
    Arena* a = (Arena*)(storage+sizeof(ArenaPrefix));
    __TBB_ASSERT( sizeof(a->slot[0]) % NFS_GetLineSize()==0, "Arena::slot size not multiple of cache line size" );
    __TBB_ASSERT( (uintptr)a % NFS_GetLineSize()==0, NULL );
    new( &a->prefix() ) ArenaPrefix( number_of_slots, number_of_workers );

    // Allocate the worker_list
    WorkerDescriptor * w = new WorkerDescriptor[number_of_workers];
    memset( w, 0, sizeof(WorkerDescriptor)*(number_of_workers));
    a->prefix().worker_list = w;


    size_t k;
    // Mark each worker slot as locked and unused
    for( k=0; k<number_of_workers; ++k ) {
        a->slot[k].steal_end = -3;
        w[k].arena = a;
    }
    // Mark rest of slots as unused
    for( ; k<number_of_slots; ++k )
        a->slot[k].steal_end = -4;

    // Publish the Arena
    __TBB_ASSERT( !TheArena, NULL );
    TheArena = a;

    // Attach threads to workers
    if( number_of_workers>0 ) {
        a->prefix().worker_list[0].start_one_worker_thread();
    }
    return a;
}

void Arena::terminate_workers() {
    int n = prefix().number_of_workers;
    __TBB_ASSERT( n>=0, "negative number of workers; casting error?" );
#if IMPROVED_GATING
    prefix().gate.try_update( GenericScheduler::SNAPSHOT_PERMANENTLY_OPEN, GenericScheduler::SNAPSHOT_PERMANENTLY_OPEN, true );
#else
    prefix().gate.open();
#endif /* IMPROVED_GATING */
    for( int i=n; --i>=0; ) {
        WorkerDescriptor& w = prefix().worker_list[i];
        // The following while wait_for_all protects against situation where worker has not started yet.
        // It would be more elegant to do this with condition variables, but the situation
        // is probably rare enough in practice that it might not be worth the added complexity
        // of condition variables.
        ITT_NOTIFY(sync_prepare, &w.scheduler);
        SpinwaitWhileEq( w.scheduler, (scheduler*)NULL );
        ITT_NOTIFY(sync_acquired, &w.scheduler);
        task* t = w.scheduler->dummy_task;
        ITT_NOTIFY(sync_releasing, &t->prefix().ref_count);
        t->prefix().ref_count = 1;
    }
    // Wait for all workers to quit
    for( int i=n; --i>=0; ) {
        WorkerDescriptor& w = prefix().worker_list[i];
#if USE_WINTHREAD
        DWORD status = WaitForSingleObject( w.thread_handle, INFINITE );
        if( status==WAIT_FAILED ) {
            fprintf(stderr,"Arena::terminate_workers: WaitForSingleObject failed\n");
            exit(1);
        }
        CloseHandle( w.thread_handle );
        w.thread_handle = (HANDLE)0;
#else
        int status = pthread_join( w.thread_handle, NULL );
        if( status )
            handle_perror(status,"pthread_join");
#endif /* USE_WINTHREAD */
    }
    // All workers have quit
#if !IMPROVED_GATING
    prefix().gate.close();
#endif /* !IMPROVED_GATING */
    delete[] prefix().worker_list;
    prefix().worker_list = NULL;
#if COUNT_TASK_NODES && !TEST_ASSEMBLY_ROUTINES
    if( prefix().task_node_count ) {
        fprintf(stderr,"warning: leaked %ld task objects\n", long(prefix().task_node_count));
    }
#endif /* COUNT_TASK_NODES && !TEST_ASSEMBLY_ROUTINES */
    __TBB_ASSERT( this, "attempt to free NULL Arena" );
    prefix().~ArenaPrefix();
    NFS_Free( &prefix() );
}

#if COUNT_TASK_NODES
intptr Arena::workers_task_node_count() {
    intptr result = 0;
    for( unsigned i=0; i<prefix().number_of_workers; ++i ) {
        GenericScheduler* s = prefix().worker_list[i].scheduler;
        if( s )
            result += s->task_node_count;
    }
    return result;
}
#endif

//------------------------------------------------------------------------
// Methods of GenericScheduler
//------------------------------------------------------------------------
#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(disable:4355)
#endif
GenericScheduler::GenericScheduler( Arena* arena_ ) :
#if STATISTICS
    current_active(0),
    current_length(0),
    current_big_malloc(0),
    steal_count(0),
    execute_count(0),
#endif /* STATISTICS */
    deepest(-1),
    array_size(0),
    arena_slot(&dummy_slot),
    arena(arena_),
    random( unsigned(this-(GenericScheduler*)NULL) ),
    free_list(NULL),
    innermost_running_task(NULL),
    dummy_task(NULL),
    ref_count(1)
#if !IMPROVED_GATING
   ,open_gate(NULL)
#endif /* !IMPROVE_GATING */
#if TBB_DO_ASSERT
   ,task_node_count(0)
#endif /* TBB_DO_ASSERT */
   ,small_task_count(1)   // Extra 1 is a guard reference
   ,return_list(NULL)
{
    TaskPool* t = TaskPool::allocate_task_pool(TaskPool::min_array_size);
    dummy_slot.task_pool = t;
    t->prefix().steal_begin = depth_type(array_size);
    t->prefix().arena_index = TaskPoolPrefix::null_arena_index;
    dummy_slot.steal_end = -2;
    dummy_slot.owner_waits = false;
    array_size = TaskPool::min_array_size;
    dummy_task = &allocate_task( sizeof(task), -1, NULL );
    dummy_task->prefix().ref_count = 2;
    __TBB_ASSERT( assert_okay(), "contructor error" );
    // Register scheduler in thread local storage
    SetThreadSpecific(this);
}

#if TBB_DO_ASSERT||TEST_ASSEMBLY_ROUTINES
bool GenericScheduler::assert_okay() const {
    __TBB_ASSERT( array_size>=TaskPool::min_array_size, NULL );
#if TBB_DO_ASSERT>=2||TEST_ASSEMBLY_ROUTINES
    acquire_task_pool();
    TaskPool* tp = dummy_slot.task_pool;
    __TBB_ASSERT( tp, NULL );
    for( depth_type k=0; k<depth_type(array_size); ++k ) {
        for( task* t = tp->array[k]; t; t=t->prefix().next ) {
            __TBB_ASSERT( deepest>=k, "deepest not set properly" );
            __TBB_ASSERT( t->prefix().depth==k, NULL );
            __TBB_ASSERT( t->prefix().owner==this, NULL );
        }
    }
    release_task_pool();
#endif /* TBB_DO_ASSERT>=2||TEST_ASSEMBLY_ROUTINES */
    return true;
}
#endif /* TBB_DO_ASSERT||TEST_ASSEMBLY_ROUTINES */

void GenericScheduler::grow( TaskPool::size_type minimum_size ) {
    TBB_TRACE(("%p.grow(minimum_size=%lx)\n", this, minimum_size ));
    __TBB_ASSERT( assert_okay(), NULL );

    // Might need to resize the underlying array.
    // Get a fresh zeroed array before acquiring the old array.
    TaskPool::size_type b_size = 2*minimum_size;
    __TBB_ASSERT( b_size>=TaskPool::min_array_size, NULL );
    TaskPool* new_pool = TaskPool::allocate_task_pool( b_size );
    __TBB_ASSERT( assert_okay(), NULL );
    acquire_task_pool();
    TaskPool* old_pool = dummy_slot.task_pool;
    memcpy( &new_pool->prefix(), &old_pool->prefix(), TaskPool::byte_size(array_size) );
    arena_slot->task_pool = dummy_slot.task_pool = new_pool;
    array_size = b_size;
    release_task_pool();
    old_pool->free_task_pool();
    __TBB_ASSERT( assert_okay(), NULL );
}

GenericScheduler* GenericScheduler::allocate_scheduler( Arena* arena ) {
    switch( SchedulerTraitsId ) {
        /* DefaultSchedulerTraits::id is listed explicitly as a case so that the host compiler
           will issue an error message if it is the same as another id in the list. */
        default:
        case DefaultSchedulerTraits::id:
            return CustomScheduler<DefaultSchedulerTraits>::allocate_scheduler(arena);
        case IntelSchedulerTraits::id:
            return CustomScheduler<IntelSchedulerTraits>::allocate_scheduler(arena);
    }
}

void GenericScheduler::free_scheduler() {
    if( arena_slot!=&dummy_slot ) {
        leave_arena(/*compress=*/false);
    }
    free_task<is_small_local>( *dummy_task );

    // k accounts for a guard reference and each task that we deallocate.
    intptr k = 1;
    for(;;) {
        while( task* t = free_list ) {
            free_list = t->prefix().next;
            deallocate_task(*t);
            ++k;
        }
        if( return_list==plugged_return_list() ) 
            break;
        free_list = (task*)__TBB_FetchAndStoreW( &return_list, (intptr)plugged_return_list() );
    }

#if COUNT_TASK_NODES
    arena->prefix().task_node_count += task_node_count;
#endif /* COUNT_TASK_NODES */
    dummy_slot.task_pool->free_task_pool();
    dummy_slot.task_pool = NULL;
    SetThreadSpecific( NULL );
    // Update small_task_count last.  Doing so sooner might cause another thread to free *this.
    __TBB_ASSERT( small_task_count>=k, "small_task_count corrupted" );
    if( __TBB_FetchAndAddW( &small_task_count, -k )==k ) 
        NFS_Free( this );
}

inline void GenericScheduler::acquire_task_pool() const {
    __TBB_ASSERT( arena_slot, "arena_slot not set" );
    __TBB_ASSERT( deepest>=-1, NULL );
    __TBB_ASSERT( arena_slot->owner_waits==false, "slot ownership corrupt?" );
    ExponentialBackoff backoff;
    bool sync_prepare_done = false;
    for(;;) {
#if TEST_ASSEMBLY_ROUTINES
        __TBB_ASSERT( (arena_slot->steal_end&1)==0, "already acquired" );
#endif /* TEST_ASSEMBLY_ROUTINES */
        __TBB_ASSERT( arena_slot==&dummy_slot || arena_slot==&arena->slot[dummy_slot.task_pool->prefix().arena_index], "slot ownership corrupt?" );
        __TBB_ASSERT( arena_slot->task_pool==dummy_slot.task_pool, "slot ownership corrupt?" );
        depth_type steal_end = arena_slot->steal_end;
        if( steal_end==2*deepest && (steal_end=__TBB_CompareAndSwapW( (volatile void *)&(arena_slot->steal_end), 2*deepest+1, 2*deepest ))==2*deepest ) {
            // We acquired our own slot
            ITT_NOTIFY(sync_acquired, arena_slot);
            arena_slot->owner_waits = false;
            break;
        } else {
            __TBB_ASSERT( steal_end&1, "steal_end and/or deepest corrupt?" );
            // Someone else acquired a lock, so pause and do exponential backoff.
            if( !sync_prepare_done ) {
                // Start waiting
                ITT_NOTIFY(sync_prepare, arena_slot);
                sync_prepare_done = true;
            } else {
                // after 2nd attempt, still can't acquire own pool;
                // need notify others that the owner is waiting
                arena_slot->owner_waits = true;
            }
            backoff.pause();
#if TEST_ASSEMBLY_ROUTINES
            __TBB_ASSERT( arena_slot->task_pool==dummy_slot.task_pool, NULL );
#endif /* TEST_ASSEMBLY_ROUTINES */
        }
    }
    __TBB_ASSERT( arena_slot->steal_end>>1 <= depth_type(array_size), NULL );
    __TBB_ASSERT( dummy_slot.task_pool->prefix().steal_begin<=depth_type(array_size), NULL );
    __TBB_ASSERT( deepest<=depth_type(array_size), NULL );
}

inline void GenericScheduler::release_task_pool() const {
    __TBB_ASSERT( arena_slot->steal_end>>1 <= depth_type(array_size), NULL );
    __TBB_ASSERT( dummy_slot.task_pool->prefix().steal_begin<=depth_type(array_size), NULL );
    __TBB_ASSERT( deepest<=depth_type(array_size), NULL );
    ITT_NOTIFY(sync_releasing, arena_slot);
    arena_slot->steal_end = 2*deepest;
}

/** Conceptually, this method should be a member of class scheduler.
    But doing so would force us to publish class scheduler in the headers. */
void GenericScheduler::spawn( task& first, task*& next ) {
    __TBB_ASSERT( assert_okay(), NULL );
    TBB_TRACE(("%p.internal_spawn enter\n", this ));
    for( task* t = &first; ;  t = t->prefix().next ) 
    {
        __TBB_ASSERT( t->state()==task::allocated, "attempt to spawn task that is not in 'allocated' state" );
        __TBB_ASSERT( t->prefix().depth==first.prefix().depth, "tasks must have same depth" );
        t->prefix().owner = this;
        t->prefix().state = task::ready;
#if TBB_DO_ASSERT
        if( task* parent = t->parent() ) {
            internal::reference_count ref_count = parent->prefix().ref_count;
            __TBB_ASSERT( ref_count>=0, "attempt to spawn task whose parent has a ref_count<0" );
            __TBB_ASSERT( ref_count!=0, "attempt to spawn task whose parent has a ref_count==0 (forgot to set_ref_count?)" );
            parent->prefix().debug_state |= ds_ref_count_active;
        }
#endif /* TBB_DO_ASSERT */
        if( &t->prefix().next==&next )
            break;
    }
    depth_type d = first.prefix().depth;
#if !IMPROVED_GATING
    __TBB_ASSERT( !open_gate, NULL );
#endif /* !IMPROVED_GATING */
    __TBB_ASSERT(depth_type(array_size)>0, "casting overflow?");
    if( d>=depth_type(array_size) ) {
        grow( d+1 );
    }
    __TBB_ASSERT( assert_okay(), NULL );
    if( arena_slot==&dummy_slot ) {
        try_enter_arena();
        __TBB_ASSERT( arena_slot->steal_end&1, NULL );
    } else {
        acquire_task_pool();
    }
    TaskPool* tp = dummy_slot.task_pool;

    next = tp->array[d];
    tp->array[d] = &first;

    if( d>deepest )
        deepest = d;
    if( d<tp->prefix().steal_begin )
        tp->prefix().steal_begin = d;

    release_task_pool();
#if IMPROVED_GATING
    mark_pool_full();
#else
    if( open_gate ) {
        open_gate->open();
        open_gate = NULL;
    }
#endif /* IMPROVED_GATING */
    __TBB_ASSERT( assert_okay(), NULL );

    TBB_TRACE(("%p.internal_spawn exit\n", this ));
}

void GenericScheduler::spawn_root_and_wait( task& first, task*& next ) {
    __TBB_ASSERT( &first, NULL );
    empty_task& dummy = *new(&allocate_task( sizeof(empty_task), first.prefix().depth-1, NULL )) empty_task;
    internal::reference_count n = 0;
    for( task* t=&first; ; t=t->prefix().next ) {
        ++n;
        __TBB_ASSERT( t->is_owned_by_current_thread(), "root task not owned by current thread" );
        __TBB_ASSERT( !t->prefix().parent, "not a root task, or already running" );
        t->prefix().parent = &dummy;
        if( &t->prefix().next==&next ) break;
    }
    dummy.prefix().ref_count = n+1;
    if( n>1 )
        spawn( *first.prefix().next, next );
    TBB_TRACE(("spawn_root_and_wait((task_list*)%p): calling %p.loop\n",&first,this));
    wait_for_all( dummy, &first );
    // empty_task has trivial destructor, so call free_task directly instead of going through destroy_task.
    free_task<is_small_local>( dummy );
    TBB_TRACE(("spawn_root_and_wait((task_list*)%p): return\n",&first));
}


inline task* GenericScheduler::get_task( depth_type d ) {
    task* result = NULL;
    if( deepest>=d ) {
        acquire_task_pool();
        task** a = dummy_slot.task_pool->array;
        depth_type i = deepest;
        do {
            if( (result = a[i]) ) {
                if( !(a[i] = result->prefix().next) )
                    --i;
                break;
            }
        } while( --i>=d );
        deepest = i;
        release_task_pool();
    }
    return result;
}

task* GenericScheduler::steal_task( UnpaddedArenaSlot& arena_slot, depth_type d ) {
    task* result = NULL;
    ExponentialBackoff backoff;
    bool sync_prepare_done = false;
    depth_type steal_end = arena_slot.steal_end;
    for(;;) {
        if( steal_end>>1<d ) {
            // Nothing of interest to steal
            if( sync_prepare_done )
                ITT_NOTIFY(sync_cancel, &arena_slot);
            goto done;
        }
        if( steal_end&1 ) {
            // Someone else has lock on it.
            if( arena_slot.owner_waits ) {
                // The pool owner is waiting for it, so need to abandon locking attempts
                if( sync_prepare_done )
                    ITT_NOTIFY(sync_cancel, &arena_slot);
                goto done;
            }
            if( !sync_prepare_done ) {
                ITT_NOTIFY(sync_prepare, &arena_slot);
                sync_prepare_done = true;
            }
            // While waiting, do exponential backoff
            backoff.pause();
            steal_end = arena_slot.steal_end;
        } else {
            depth_type tmp = steal_end;
            steal_end = __TBB_CompareAndSwapW( (volatile void *)&(arena_slot.steal_end), steal_end+1, steal_end );
            if( tmp==steal_end ) {
                ITT_NOTIFY(sync_acquired, &arena_slot);
                break;
            }
        }
    }
{
    TaskPool* tp = arena_slot.task_pool;
    depth_type i = tp->prefix().steal_begin;
    if( i<d )
        i = d;
    for(; i<=steal_end>>1; ++i ) {
        if( (result = tp->array[i]) ) {
            // Unlike get_task, we do not adjust i if the next pointer is NULL.
            // The reason is that it is a waste of time, because steal_task
            // is relatively infrequent compared to insert_task, and the
            // latter updates steal_begin too.
            tp->array[i] = result->prefix().next;
            break;
        }
    }
    if( tp->prefix().steal_begin>=d )
        tp->prefix().steal_begin = i;
    // Release the task pool
    ITT_NOTIFY(sync_releasing, &arena_slot);
    arena_slot.steal_end = steal_end;
}
done:
    return result;
}

template<typename SchedulerTraits>
void CustomScheduler<SchedulerTraits>::wait_for_all( task& parent, task* child ) {
    TBB_TRACE(("%p.wait_for_all(parent=%p,child=%p) enter\n", this, &parent, child));
#if TBB_DO_ASSERT
    __TBB_ASSERT( assert_okay(), NULL );
    if( child ) {
        __TBB_ASSERT( child->prefix().owner==this, NULL );
        __TBB_ASSERT( parent.ref_count()>=2, "ref_count must be at least 2" );
    } else {
        __TBB_ASSERT( parent.ref_count()>=1, "ref_count must be at least 1" );
    }
    __TBB_ASSERT( assert_okay(), NULL );
#endif /* TBB_DO_ASSERT */
    task* t = child;
    depth_type d;
    if( innermost_running_task==dummy_task ) {
        // We are in the innermost task dispatch loop of a master thread.
        __TBB_ASSERT( !is_worker(), NULL );
        // Forcefully make this loop operating on zero depth.
        d = 0;
    } else {
        d = parent.prefix().depth+1;
    }
    task* old_innermost_running_task = innermost_running_task;
    // Outer loop steals tasks when necesssary.
    for(;;) {
        // Middle loop evaluates tasks that are pulled off "array".
        do {
            // Inner loop evaluates tasks that are handed directly to us by other tasks.
            while(t) {
                task_prefix& pref = t->prefix();
                __TBB_ASSERT( pref.owner==this, NULL );
                __TBB_ASSERT( pref.depth>=d, NULL );
                __TBB_ASSERT( 1L<<t->state() & (1L<<task::allocated|1L<<task::ready|1L<<task::reexecute), NULL );
                pref.state = task::executing;
                innermost_running_task = t;
                __TBB_ASSERT(assert_okay(),NULL);
                TBB_TRACE(("%p.wait_for_all: %p.execute\n",this,t));
                GATHER_STATISTIC( ++execute_count );
                task* t_next = t->execute();
                if( t_next ) {
                    __TBB_ASSERT( t_next->state()==task::allocated,
                                "if task::execute() returns task, it must be marked as allocated" );
                    // The store here has a subtle secondary effect - it fetches *t_next into cache.
                    t_next->prefix().owner = this;
                }
                __TBB_ASSERT(assert_okay(),NULL);
                switch( task::state_type(t->prefix().state) ) {
                    case task::executing:
                        // this block was copied below to case task::recycle
                        // when making changes, check it too
                        if( task* s = t->parent() ) {
                            if( SchedulerTraits::itt_possible )
                                ITT_NOTIFY(sync_releasing, &s->prefix().ref_count);
                            if( SchedulerTraits::has_slow_atomic && s->prefix().ref_count==1 ? (s->prefix().ref_count=0, true) : __TBB_FetchAndDecrementWrelease((volatile void *)&(s->prefix().ref_count))==1 ) {
                                if( SchedulerTraits::itt_possible )
                                    ITT_NOTIFY(sync_acquired, &s->prefix().ref_count);
#if TBB_DO_ASSERT
                                s->prefix().debug_state &= ~ds_ref_count_active;
#endif /* TBB_DO_ASSERT */
                                s->prefix().owner = this;
                                depth_type s_depth = s->prefix().depth;
                                if( !t_next && s_depth>=deepest && s_depth>=d ) {
                                    // Eliminate spawn/get_task pair.
                                    // The elimination is valid because the spawn would set deepest==s_depth,
                                    // and the subsequent call to get_task(d) would grab task s and
                                    // restore deepest to its former value.
                                    t_next = s;
                                } else {
                                    CustomScheduler<SchedulerTraits>::spawn(*s, s->prefix().next );
                                    __TBB_ASSERT(assert_okay(),NULL);
                                }
                            }
                        }
                        destroy_task( *t );
                        break;

                    case task::recycle: { // state set by recycle_as_safe_continuation()
                        t->prefix().state = task::allocated;
                        // for safe continuation, need atomically decrement ref_count;
                        // the block was copied from above case task::executing, and changed.
                        task*& s = t;     // s is an alias to t, in order to make less changes
                        if( SchedulerTraits::itt_possible )
                            ITT_NOTIFY(sync_releasing, &s->prefix().ref_count);
                        if( SchedulerTraits::has_slow_atomic && s->prefix().ref_count==1 ? (s->prefix().ref_count=0, true) : __TBB_FetchAndDecrementWrelease((volatile void *)&(s->prefix().ref_count))==1 ) {
                            if( SchedulerTraits::itt_possible )
                                ITT_NOTIFY(sync_acquired, &s->prefix().ref_count);
#if TBB_DO_ASSERT
                            s->prefix().debug_state &= ~ds_ref_count_active;
                            __TBB_ASSERT( s->prefix().owner==this, "ownership corrupt?" );
                            __TBB_ASSERT( s->prefix().depth>=d, NULL );
#endif /* TBB_DO_ASSERT */
                            if( !t_next ) {
                                t_next = s;
                            } else {
                                CustomScheduler<SchedulerTraits>::spawn(*s, s->prefix().next );
                                __TBB_ASSERT(assert_okay(),NULL);
                            }
                        }
                        } break;

                    case task::reexecute: // set by recycle_to_reexecute()
                        __TBB_ASSERT( t_next, "reexecution requires that method 'execute' return a task" );
                        TBB_TRACE(("%p.wait_for_all: put task %p back into array",this,t));
                        t->prefix().state = task::allocated;
                        CustomScheduler<SchedulerTraits>::spawn( *t, t->prefix().next );
                        __TBB_ASSERT(assert_okay(),NULL);
                        break;
#if TBB_DO_ASSERT
                    case task::allocated:
                        break;
                    case task::ready:
                        __TBB_ASSERT( false, "task is in READY state upon return from method execute()" );
                        break;
                    default:
                        __TBB_ASSERT( false, "illegal state" );
#else
                    default: // just to shut up some compilation warnings
                        break;
#endif /* TBB_DO_ASSERT */
                }
                __TBB_ASSERT( !t_next||t_next->prefix().depth>=d, NULL );
                t = t_next;
            }
            __TBB_ASSERT(assert_okay(),NULL);


            t = get_task( d );
#if TBB_DO_ASSERT
            __TBB_ASSERT(assert_okay(),NULL);
            if(t) {
                AssertOkay(*t);
                __TBB_ASSERT( t->prefix().owner==this, "thread got task that it does not own" );
            }
#endif /* TBB_DO_ASSERT */
        } while( t );
        __TBB_ASSERT( arena->prefix().number_of_workers>0||parent.prefix().ref_count==1,
                    "deadlock detected" );
        // The state "failure_count==-1" is used only when itt_possible is true,
        // and denotes that a sync_prepare has not yet been issued.
        for( int failure_count = -static_cast<int>(SchedulerTraits::itt_possible);; ++failure_count) {
            if( parent.prefix().ref_count==1 ) {
                if( SchedulerTraits::itt_possible ) {
                    if( failure_count!=-1 ) {
                        ITT_NOTIFY(sync_prepare, &parent.prefix().ref_count);
                        // Notify Intel(R) Thread Profiler that thread has stopped spinning.
                        ITT_NOTIFY(sync_acquired, this);
                    }
                    ITT_NOTIFY(sync_acquired, &parent.prefix().ref_count);
                }
                goto done;
            }
            // Try to steal a task from a random victim.
            size_t n = arena->prefix().limit;
            if( n>1 ) {
                size_t k = random.get() % (n-1);
                ArenaSlot* victim = &arena->slot[k];
                if( victim>=arena_slot )
                    ++victim;               // Adjusts random distribution to exclude self
                t = steal_task( *victim, d );
                if( t ) {
                    __TBB_ASSERT( t->prefix().depth>=d, NULL );
                    if( SchedulerTraits::itt_possible ) {
                        if( failure_count!=-1 ) {
                            ITT_NOTIFY(sync_prepare, victim);
                            // Notify Intel(R) Thread Profiler that thread has stopped spinning.
                            ITT_NOTIFY(sync_acquired, this);
                            ITT_NOTIFY(sync_acquired, victim);
                        }
                    }
                    __TBB_ASSERT(t,NULL);
                    break;
                }
            }
            if( SchedulerTraits::itt_possible && failure_count==-1 ) {
                // The first attempt to steal work failed, so notify Intel(R) Thread Profiler that
                // the thread has started spinning.  Ideally, we would do this notification
                // *before* the first failed attempt to steal, but at that point we do not
                // know that the steal will fail.
                ITT_NOTIFY(sync_prepare, this);
                failure_count = 0;
            }
            // Pause, even if we are going to yield, because the yield might return immediately.
            __TBB_Pause(PauseTime);
            int yield_threshold = int(n);
            if( failure_count>=yield_threshold ) {
                if( failure_count>=2*yield_threshold ) {
                    __TBB_Yield();
#if IMPROVED_GATING
                    // Note: if d!=0 or !is_worker(), it is not safe to wait for a non-empty pool,
                    // because of the polling of parent.prefix().ref_count.
                    if( d==0 && is_worker() ) 
                        wait_while_pool_is_empty();
#else
                    arena->prefix().gate.wait();
#endif /* IMPROVED_GATING */
                    failure_count = 0;
                } else if( failure_count==yield_threshold ) {
                    // We have paused n times since last yield.
                    // Odds are that there is no other work to do.
                    __TBB_Yield();
                }
            }
        }
        __TBB_ASSERT(t,NULL);
        GATHER_STATISTIC( ++steal_count );
        t->prefix().owner = this;
    }
done:
    parent.prefix().ref_count = 0;
#if TBB_DO_ASSERT
    parent.prefix().debug_state &= ~ds_ref_count_active;
#endif /* TBB_DO_ASSERT */
    innermost_running_task = old_innermost_running_task;
    if( deepest<0 && innermost_running_task==dummy_task && arena_slot!=&dummy_slot ) {
        leave_arena(/*compress=*/true);
    }
    __TBB_ASSERT( assert_okay(), NULL );
    TBB_TRACE(("%p.wait_for_all(parent=%p): return\n",this,&parent));
}

void GenericScheduler::try_enter_arena() {
    __TBB_ASSERT( arena, NULL );
    __TBB_ASSERT( arena_slot, "arena_slot not set" );
    __TBB_ASSERT( arena_slot==&dummy_slot, "already in arena?" );
    unsigned n = arena->prefix().number_of_slots;
    unsigned j = unsigned(arena->prefix().limit);
    for( unsigned k=j; k<n; ++k ) {
        if( arena->slot[k].steal_end==-4 && __TBB_CompareAndSwapW( (volatile void *)&(arena->slot[k].steal_end), -4|1, -4 )==-4 ) {
            __TBB_ASSERT( arena->slot[k].steal_end==-3, "slot not really acquired?" );
            ITT_NOTIFY(sync_acquired,&arena->slot[k]);
            dummy_slot.task_pool->prefix().arena_index = k;
            arena->slot[k].task_pool = dummy_slot.task_pool;
            arena->slot[k].owner_waits = false;
            arena_slot = &arena->slot[k];
#if !IMPROVED_GATING
            open_gate = &arena->prefix().gate;
#endif /* !IMPROVED_GATING */
            // Successfully claimed a spot in the arena.  Update arena->prefix().limit.
            do {
                j = unsigned(arena->prefix().limit.compare_and_swap( k+1, j ));
            } while( j<=k );
            break;
        }
    }
    arena_slot->steal_end = 2*deepest+1;
}

void GenericScheduler::leave_arena( bool compress ) {
    __TBB_ASSERT( arena_slot!=&dummy_slot, "not in arena" );
    // Remove myself from the arena.
    acquire_task_pool();
#if TBB_DO_ASSERT
    for( depth_type i=0; i<deepest; ++i )
        __TBB_ASSERT( !dummy_slot.task_pool->array[i], "leaving arena, but have tasks to do" );
#endif /* TBB_DO_ASSERT */
    size_t k = dummy_slot.task_pool->prefix().arena_index;
    __TBB_ASSERT( &arena->slot[k]==arena_slot, NULL );
    arena_slot->task_pool = NULL;
    dummy_slot.task_pool->prefix().arena_index = TaskPoolPrefix::null_arena_index;
    arena_slot = &dummy_slot;
    arena_slot->owner_waits  = false;
    size_t n = arena->prefix().number_of_workers;
#if !IMPROVED_GATING
    if( k>=n )
        arena->prefix().gate.close();
#endif /* !IMPROVED_GATING */
    __TBB_ASSERT( !compress || k>=n, "must be master to compress" );
    size_t new_limit = arena->prefix().limit;
    if( compress && new_limit==k+1 ) {
        // Garbage collect some slots
        for(;;) {
            new_limit = arena->prefix().limit.compare_and_swap( k, k+1 );
            ITT_NOTIFY(sync_releasing, &arena->slot[k]);
            arena->slot[k].steal_end = -4;
            if( new_limit!=k+1 ) break;
            if( --k<n ) break;
            if( arena->slot[k].steal_end==-4 && __TBB_CompareAndSwapW( (volatile void *)&(arena->slot[k].steal_end), -4|1, -4 )==-4 ) {
                ITT_NOTIFY(sync_acquired,&arena->slot[k]);
            } else {
                break;
            }
        }
    } else {
        ITT_NOTIFY(sync_releasing, &arena->slot[k]);
        arena->slot[k].steal_end = -4;
    }
}

GenericScheduler* GenericScheduler::create_worker( WorkerDescriptor& w ) {
    __TBB_ASSERT( !w.scheduler, NULL );
    unsigned n = w.arena->prefix().number_of_workers;
    WorkerDescriptor* worker_list = w.arena->prefix().worker_list;
    __TBB_ASSERT( &w >= worker_list, NULL );
    unsigned i = unsigned(&w - worker_list);
    __TBB_ASSERT( i<n, NULL );

    // Start my children
    if( 2*i+1<n ) {
        // Have a left child, so start it.
        worker_list[2*i+1].start_one_worker_thread();
        if( 2*i+2<n ) {
            // Have a right child, so start it.
            worker_list[2*i+2].start_one_worker_thread();
        }
    }

    // Put myself into the arena
    GenericScheduler* s = GenericScheduler::allocate_scheduler(w.arena);
    ArenaSlot& slot = w.arena->slot[i];
    __TBB_ASSERT( slot.steal_end==-3, "slot not allocated as locked worker?" );
    s->arena_slot = &slot;
    TaskPool* t = s->dummy_slot.task_pool;
    t->prefix().arena_index = i;
    ITT_NOTIFY(sync_releasing, &slot);
    slot.task_pool = t;
    slot.steal_end = -2;
    slot.owner_waits = false;

    // Publish worker
    ITT_NOTIFY(sync_releasing, &w.scheduler);
    w.scheduler = s;
    return s;
}

GenericScheduler* GenericScheduler::create_master( Arena* arena ) {
    GenericScheduler* s = GenericScheduler::allocate_scheduler( arena );
    task& t = *s->dummy_task;
    s->innermost_running_task = &t;
    t.prefix().ref_count = 1;
    __TBB_ASSERT( &task::self()==&t, NULL );
    return s;
}

#if USE_WINTHREAD
unsigned WINAPI GenericScheduler::worker_routine( void* arg )
#else
void* GenericScheduler::worker_routine( void* arg )
#endif /* USE_WINTHREAD */
{
    GenericScheduler& scheduler = *create_worker(*(WorkerDescriptor*)arg);

    ITT_NAME_SET(thr_name_set, "TBB Worker Thread", 17);

#if (_WIN32||_WIN64)&&!__TBB_ipf
    // Deal with 64K aliasing.  The formula for "offset" is a Fibonacci hash function,
    // which has the desirable feature of spreading out the offsets fairly evenly
    // without knowing the total number of offsets, and furthermore unlikely to
    // accidentally cancel out other 64K aliasing schemes that Microsoft might implement later.
    // See Knuth Vol 3. "Theorem S" for details on Fibonacci hashing.
    size_t offset = (scheduler.dummy_slot.task_pool->prefix().arena_index+1) * 40503U % (1U<<16);
    void* volatile sink_for_alloca = _alloca(offset);
#else
    // Linux thread allocators avoid 64K aliasing.
#endif /* _WIN32||_WIN64 */

#if USE_PTHREAD
    pthread_cleanup_push( cleanup_worker_thread, &scheduler );
#endif /* USE_PTHREAD */

#if USE_WINTHREAD
    __try {
#endif /* USE_WINTHREAD */

    scheduler.wait_for_all(*scheduler.dummy_task,NULL);

#if USE_WINTHREAD
    } __finally {
        cleanup_worker_thread(&scheduler);
    }
    return 0;
#elif USE_PTHREAD
    pthread_cleanup_pop( true );
    return NULL;
#else
    #error Must define USE_PTHREAD or USE_WINTHREAD
#endif /* USE_PTHREAD */
}

void GenericScheduler::cleanup_worker_thread( void* arg ) {
    TBB_TRACE(("%p.cleanup_worker_thread enter\n",arg));
    GenericScheduler& s = *(GenericScheduler*)arg;
    __TBB_ASSERT( s.dummy_slot.task_pool, "cleaning up worker with missing TaskPool" );
    //Arena* a = s.arena;
    __TBB_ASSERT( s.arena_slot!=&s.dummy_slot, "worker not in arena?" );
    s.free_scheduler();
}

//------------------------------------------------------------------------
// WorkerDescsriptor
//------------------------------------------------------------------------
void WorkerDescriptor::start_one_worker_thread() {
#if USE_WINTHREAD
    unsigned thread_id;
    // The return type of _beginthreadex is "uintptr_t" on new MS compilers,
    // and 'unsigned long' on old MS compilers.  Our uintptr works for both.
    uintptr status = _beginthreadex( NULL, ThreadStackSize, GenericScheduler::worker_routine, this, 0, &thread_id );
    if( status==0 )
        handle_perror(errno,"__beginthreadex");
    else
        thread_handle = (HANDLE)status;
#else
    // This if is due to an Intel Compiler Bug, tracker # C70996
    // This #if should be removed as soon as the bug is fixed
#if __APPLE__ && __TBB_x86_64
    static void *(*r)(void*) = GenericScheduler::worker_routine;
    int status = pthread_create( &thread_handle, NULL, r, this );
#else
    int status = pthread_create( &thread_handle, NULL, GenericScheduler::worker_routine, this );
#endif
    if( status )
        handle_perror(status,"pthread_create");
#endif /* USE_WINTHREAD */
}

//------------------------------------------------------------------------
// Methods of allocate_root_proxy
//------------------------------------------------------------------------
task& allocate_root_proxy::allocate( size_t size ) {
    internal::GenericScheduler* v = GetThreadSpecific();
    __TBB_ASSERT( v, "thread did not activate a task_scheduler_init object?" );
    return v->allocate_task( size, v->innermost_running_task->prefix().depth+1, NULL );
}

void allocate_root_proxy::free( task& task ) {
    internal::GenericScheduler* v = GetThreadSpecific();
    __TBB_ASSERT( v, "thread does not have initialized task_scheduler_init object?" );
    v->free_task<GenericScheduler::is_local>( task );
}

//------------------------------------------------------------------------
// Methods of allocate_continuation_proxy
//------------------------------------------------------------------------
task& allocate_continuation_proxy::allocate( size_t size ) const {
    task& t = *((task*)this);
    __TBB_ASSERT( AssertOkay(t), NULL );
    GenericScheduler* v = static_cast<GenericScheduler*>(t.prefix().owner);
    __TBB_ASSERT( GetThreadSpecific()==v, "thread does not own this" );
    task* parent = t.parent();
    t.prefix().parent = NULL;
    return v->allocate_task( size, t.prefix().depth, parent );
}

void allocate_continuation_proxy::free( task& mytask ) const {
    task& t = *((task*)this);
    // Restore the parent as it was before the corresponding allocate was called.
    t.prefix().parent = mytask.parent();
    static_cast<GenericScheduler*>(t.prefix().owner)->free_task<GenericScheduler::is_local>(mytask);
}

//------------------------------------------------------------------------
// Methods of allocate_child_proxy
//------------------------------------------------------------------------
task& allocate_child_proxy::allocate( size_t size ) const {
    task& t = *((task*)this);
    __TBB_ASSERT( AssertOkay(t), NULL );
    GenericScheduler* v = static_cast<GenericScheduler*>(t.prefix().owner);
    __TBB_ASSERT( GetThreadSpecific()==v, "thread does not own parent" );
    return v->allocate_task( size, t.prefix().depth+1, &t );
}

void allocate_child_proxy::free( task& mytask ) const {
    task& t = *((task*)this);
    GenericScheduler* v = static_cast<GenericScheduler*>(t.prefix().owner);
    v->free_task<GenericScheduler::is_local>(mytask);
}

//------------------------------------------------------------------------
// Methods of allocate_additional_child_of_proxy
//------------------------------------------------------------------------
task& allocate_additional_child_of_proxy::allocate( size_t size ) const {
    __TBB_ASSERT( AssertOkay(self), NULL );
    __TBB_FetchAndIncrementWacquire( (volatile void *)&(parent.prefix().ref_count) );
    GenericScheduler* v = static_cast<GenericScheduler*>(self.prefix().owner);
    return v->allocate_task( size, parent.prefix().depth+1, &parent );
}

void allocate_additional_child_of_proxy::free( task& task ) const {
    // Undo the increment.  We do not check the result of the fetch-and-decrement.
    // We could consider be spawning the task if the fetch-and-decrement returns 1.
    // But we do not know that was the programmer's intention.
    // Furthermore, if it was the programmer's intention, the program has a fundamental
    // race condition (that we warn about in Reference manual), because the
    // reference count might have become zero before the corresponding call to
    // allocate_additional_child_of_proxy::allocate.
    __TBB_FetchAndDecrementWrelease( (volatile void *)&(parent.prefix().ref_count) );
    GenericScheduler* v = static_cast<GenericScheduler*>(self.prefix().owner);

    v->free_task<GenericScheduler::is_local>(task);
}

} // namespace internal

using namespace tbb;
using namespace tbb::internal;

//------------------------------------------------------------------------
// task
//------------------------------------------------------------------------

void task::internal_set_ref_count( int count ) {
    __TBB_ASSERT( count>0, "count must be positive" );
    __TBB_ASSERT( !(prefix().debug_state&GenericScheduler::ds_ref_count_active), "ref_count race detected" );
    ITT_NOTIFY(sync_releasing, &prefix().ref_count);
    prefix().ref_count = count;
}

task& task::self() {
    GenericScheduler* v = GetThreadSpecific();
    __TBB_ASSERT( v->assert_okay(), NULL );
    return *v->innermost_running_task;
}

bool task::is_owned_by_current_thread() const {
    return GetThreadSpecific()==prefix().owner;
}

void task::destroy( task& victim ) {
    __TBB_ASSERT( victim.prefix().ref_count==0, "victim must have reference count of zero" );
    __TBB_ASSERT( victim.state()==task::allocated, "illegal state for victim task" );
    if( task* parent = victim.parent() ) {
        __TBB_ASSERT( parent->state()==task::allocated, "attempt to destroy child of running or corrupted parent?" );
        ITT_NOTIFY(sync_releasing, &parent->prefix().ref_count);
        __TBB_FetchAndDecrementWrelease((volatile void *)&(parent->prefix().ref_count));
        ITT_NOTIFY(sync_acquired, &parent->prefix().ref_count);
    }
    internal::GenericScheduler* v = static_cast<internal::GenericScheduler*>(prefix().owner);
    // Victim is allowed to be owned by another thread.
    victim.prefix().owner = v;
    v->destroy_task(victim);
}

void task::spawn_and_wait_for_all( task_list& list ) {
    __TBB_ASSERT( is_owned_by_current_thread(), "'this' not owned by current thread" );
    task* t = list.first;
    if( t ) {
        if( &t->prefix().next!=list.next_ptr )
            prefix().owner->spawn( *t->prefix().next, *list.next_ptr );
        list.clear();
    }
    prefix().owner->wait_for_all( *this, t );
}

//------------------------------------------------------------------------
// task_scheduler_init
//------------------------------------------------------------------------
void task_scheduler_init::initialize( int number_of_threads ) {
    if( number_of_threads!=deferred ) {
        __TBB_ASSERT( !my_scheduler, "task_scheduler_init already initialized" );
        __TBB_ASSERT( number_of_threads==-1 || number_of_threads>=1,
                    "number_of_threads for task_scheduler_init must be -1 or positive" );
        // Double-check
        if( !OneTimeInitializationsDone ) {
            DoOneTimeInitializations();
        }
        GenericScheduler* s = GetThreadSpecific();
        if( s ) {
            s->ref_count += 1;
        } else {
            Arena* a;
            {
                mutex::scoped_lock lock( TheArenaMutex );
                a = TheArena;
                if( a ) {
                    a->prefix().number_of_masters += 1;
                } else {
                    if( number_of_threads==-1 )
                        number_of_threads = DetectNumberOfWorkers();
                    // Put cold path in separate routine.
                    a = Arena::allocate_arena( 2*number_of_threads, number_of_threads-1 );
                    __TBB_ASSERT( a->prefix().number_of_masters==1, NULL );
                    __TBB_ASSERT( TheArena==a, NULL );
                }
            }
            s = GenericScheduler::create_master( a );
        }
        my_scheduler = s;
    }
}

void task_scheduler_init::terminate() {
    GenericScheduler* s = static_cast<GenericScheduler*>(my_scheduler);
    my_scheduler = NULL;
    __TBB_ASSERT( s, "task_scheduler_init::terminate without corresponding task_scheduler_init::initialize()");
    if( !--(s->ref_count) ) {
        __TBB_ASSERT( s->dummy_slot.task_pool, "cleaning up master with missing TaskPool" );
        s->free_scheduler();
        Arena* a;
        {
            mutex::scoped_lock lock( TheArenaMutex );
            a = TheArena;
            __TBB_ASSERT( a, "TheArena is missing" );
            if( --(a->prefix().number_of_masters) ) {
                a = NULL;
            } else {
                TheArena = NULL;
            }
        }
        if( a ) {
            a->terminate_workers();
        }
    }
}



} // namespace tbb
