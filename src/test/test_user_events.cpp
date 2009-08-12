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

#define DO_ITT_NOTIFY 1
#define TBB_USE_ASSERT 1
#define TBB_DO_ITT_EVENTS 1

/* to do implicite linkage under Windows, but __TBB_BUILD stops it */
#include "tbb/tbb_stddef.h" 

#if _MSC_VER /* need this to overcome _declspec( dllimport ) for ITT_event */
#define __TBB_BUILD 1
#endif

#include "../tbb/itt_notify.cpp"

#undef DO_ITT_NOTIFY

#include <string>
#include <vector>
#include <sstream> 
#include <iostream> 
#include <tbb/atomic.h>
#include <tbb/tbb_thread.h>
#include <tbb/concurrent_hash_map.h>
#include "harness_assert.h"

namespace tbb {
namespace internal {

void DoOneTimeInitializations() {}
bool GetBoolEnvironmentVariable(char const*) { return true; }
bool FillDynamicLinks(const char*, const DynamicLinkDescriptor [], size_t) {
    return true;
}
void PrintExtraVersionInfo( const char* , const char*  ) {}

struct EvName {
    int event;
    char name[100];
    
    EvName(int event, const char *n) : event(event) {
        strcpy(name, n);
    }
};

struct Hash_Cmp {
    static size_t hash(int event) {
        return event;
    }
    static bool equal(int e1, int e2) {
        return e1==e2;
    }
};

typedef concurrent_hash_map<__itt_event,std::string,Hash_Cmp> EvNames;

static EvNames ev_names;

static atomic<int> uniq_itt_event;

__itt_event test_event_create(const char *name, int)
{
    __itt_event my_cnt = uniq_itt_event++;
    EvNames::accessor acc;
    std::string str(name);
    
    ev_names.insert( acc, my_cnt );
    acc->second = str;

    return my_cnt;
}

struct IdxStr {
    itt_event_t h;
    std::string str;
};

static ITT_Event_Hnd_to_Event<true> ev;
static IdxStr *idxs;
static atomic<int> waiting_threads;

class Pusher
{
    int my_num;
    int events_per_thread;
    
    void barrier() {
        waiting_threads--;
        while(waiting_threads)
            ;
    }
public:
    Pusher(int my_num, int events_per_thread) :
        my_num(my_num), events_per_thread(events_per_thread) {}
    void operator()(){
        barrier();

        for (int i=events_per_thread*my_num; 
             i<events_per_thread*(my_num+1); i++) {
            IdxStr i_s;
            char buf[100];

            sprintf(buf, "%d", i);
            i_s.str = buf;
            i_s.h = ev.add_event(buf);

            idxs[i] = i_s;
        }
    }
    void operator()(int thread_id){
        std::vector<itt_event_t> l_idx;
        barrier();
        
        for (int i=0; i<events_per_thread; i++) {
            char buf[100];

            sprintf(buf, "%d_%d", thread_id, i);

            itt_event_t idx = ev.add_event(buf);
            int event = ev.get_event_by_handler( idx );

            EvNames::const_accessor acc;
            bool ok = ev_names.find(acc, event);
            ASSERT(ok, "itt_event should exists");
            ASSERT(0==acc->second.compare(buf), "different event names");

            l_idx.push_back(idx);
        }

        for (int i=0; i<events_per_thread; i++) {
            ev.get_event_by_handler( l_idx[i] );
        }
    }
};


void Test()
{
    ITT_Handler_event_create = test_event_create;
    uniq_itt_event = 77;
    const int init_num_thr = 8;
    const int events_per_thread = 1000;

    tbb::tbb_thread threads[init_num_thr];
    waiting_threads = init_num_thr;
    idxs = new IdxStr[init_num_thr*events_per_thread];
    for (int i=0; i<init_num_thr; i++)
        new(threads+i) tbb::tbb_thread(Pusher(i, events_per_thread));

    for (int i=0; i<init_num_thr; i++)
        threads[i].join();

    ev.switch_to_event_creation();

    for (int i=0; i<init_num_thr*events_per_thread; i++) {
        std::ostringstream out;
        out << i;

        __itt_event e = ev.get_event_by_handler(idxs[i].h);

        EvNames::const_accessor acc;
        bool ok = ev_names.find(acc, e);
        ASSERT(ok, "itt_event should exists");
        ASSERT(0==acc->second.compare(out.str()), "different event names");
    }

    waiting_threads = init_num_thr;
    for (int i=0; i<init_num_thr; i++)
        new(threads+i) tbb::tbb_thread(Pusher(i, events_per_thread), i);
    for (int i=0; i<init_num_thr; i++)
        threads[i].join();
}

}  // namespace internal
}  // namespace tbb

__TBB_TEST_EXPORT
int main() {
    tbb::internal::Test();
    REPORT("done\n");
}

#define HARNESS_NO_PARSE_COMMAND_LINE 1
#include "harness.h"
