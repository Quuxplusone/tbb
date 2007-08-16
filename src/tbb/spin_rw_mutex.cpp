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

#include "tbb/spin_rw_mutex.h"
#include "tbb/tbb_machine.h"
#include "tbb_misc.h"
#include "itt_notify.h"

namespace tbb {

using namespace internal;

static inline bool CAS(volatile uintptr &addr, uintptr newv, uintptr oldv) {
    return __TBB_CompareAndSwapW((volatile void *)&addr, (intptr)newv, (intptr)oldv) == (intptr)oldv;
}

//! Signal that write lock is released
void spin_rw_mutex::internal_itt_releasing(spin_rw_mutex *mutex) {
    ITT_NOTIFY(sync_releasing, mutex);
}

bool spin_rw_mutex::internal_acquire_writer(spin_rw_mutex *mutex)
{
    ITT_NOTIFY(sync_prepare, mutex);
    ExponentialBackoff backoff;
    while(true) {
        state_t s = mutex->state;
        if( !(s & ~state_t(2)) ) { // no readers, no writers; mask is 1..1101
            if( CAS(mutex->state, 1, s) )
                break; // successfully stored writer flag
            backoff.reset(); // we could be very close to complete op.
        } else if( !(s & 2) ) { //no writer pending flag
            __TBB_AtomicOR(&mutex->state, 2);
        }
        backoff.pause();
    }
    ITT_NOTIFY(sync_acquired, mutex);
    __TBB_ASSERT( (mutex->state & ~state_t(2))==1, "invalid state of a write lock" );
    return false;
}

//! Signal that write lock is released
void spin_rw_mutex::internal_release_writer(spin_rw_mutex *mutex) {
    __TBB_ASSERT( (mutex->state & ~state_t(2))==1, "invalid state of a write lock" );
    ITT_NOTIFY(sync_releasing, mutex);
    mutex->state = 0; 
}

//! Acquire lock on given mutex.
void spin_rw_mutex::internal_acquire_reader(spin_rw_mutex *mutex) {
    ITT_NOTIFY(sync_prepare, mutex);
    ExponentialBackoff backoff;
    while(true) {
        state_t s = mutex->state;
        if( !(s & 3) ) { // no any writers
            if( CAS(mutex->state, s+4, s) )
                break; // successfully stored increased number of readers
            backoff.reset(); // we could be very close to complete op.
        }
        backoff.pause();
    }
    ITT_NOTIFY(sync_acquired, mutex);
    __TBB_ASSERT( mutex->state & ~state_t(3), "invalid state of a read lock: no readers" );
    __TBB_ASSERT( (mutex->state & 1)==0, "invalid state of a read lock: active writer" );
}

//! Upgrade reader to become a writer.
/** Returns true if the upgrade happened without re-acquiring the lock and false if opposite */
bool spin_rw_mutex::internal_upgrade(spin_rw_mutex *mutex) {
    state_t s = mutex->state;
    __TBB_ASSERT( s & ~state_t(3), "invalid state before upgrade: no readers " );
    __TBB_ASSERT( (s & 1)==0, "invalid state before upgrade: active writer " );
    // check and set writer-pending flag; state_t(2) is its mask
    // if we are the only reader, (s>>2)==1, and also trying to set the flag
    if( (!(s & 2) || (s>>2)==1) && CAS(mutex->state, s|2, s) ) {
        ExponentialBackoff backoff;
        ITT_NOTIFY(sync_prepare, mutex);
        while( (mutex->state>>2) != 1 ) // more than 1 reader
            backoff.pause();
        // the state should be 0...0110, i.e. 1 reader and waiting writer;
        // both new readers and writers are blocked
        __TBB_ASSERT(mutex->state == 6,"invalid state when upgrading to writer");
        mutex->state = 1;
        ITT_NOTIFY(sync_acquired, mutex);
        __TBB_ASSERT( (mutex->state & ~state_t(2))==1, "invalid state after upgrade" );
        return true; // successfully upgraded
    } else {
        // slow reacquire
        internal_release_reader(mutex);
        return internal_acquire_writer(mutex);
    }
}

void spin_rw_mutex::internal_downgrade(spin_rw_mutex *mutex) {
    __TBB_ASSERT( (mutex->state & ~state_t(2))==1, "invalid state before downgrade" );
    ITT_NOTIFY(sync_releasing, mutex);
    mutex->state = 4; // Bit 2 - reader, 00..00100
    __TBB_ASSERT( mutex->state & ~state_t(3), "invalid state after downgrade: no readers" );
    __TBB_ASSERT( (mutex->state & 1)==0, "invalid state after dowgrade: active writer" );
}

void spin_rw_mutex::internal_release_reader(spin_rw_mutex *mutex)
{
    __TBB_ASSERT( mutex->state & ~state_t(3), "invalid state of a read lock: no readers" );
    __TBB_ASSERT( (mutex->state & 1)==0, "invalid state of a read lock: active writer" );
    ITT_NOTIFY(sync_releasing, mutex); // release reader
    __TBB_FetchAndAddWrelease((volatile void *)&(mutex->state),-state_t(4));
}

bool spin_rw_mutex::internal_try_acquire_writer( spin_rw_mutex * mutex )
{
// for a writer: only possible to acquire if no active readers or writers
    state_t s = mutex->state; // on Itanium, this volatile load has acquire semantic
    if( (s & ~state_t(2))==0 ) // no readers, no writers; mask is 1..1101
        if( CAS(mutex->state, 1, s) ) {
            ITT_NOTIFY(sync_acquired, mutex);
            return true; // successfully stored writer flag
        }
    return false;
}

bool spin_rw_mutex::internal_try_acquire_reader( spin_rw_mutex * mutex )
{
// for a reader: acquire if no active or waiting writers
    state_t s;
    // on Itanium, a load of volatile mutex->state has acquire semantic
    while( ((s=mutex->state) & 3)==0 ) // no writers
        if( CAS(mutex->state, s+4, s) ) {
            ITT_NOTIFY(sync_acquired, mutex);
            return true; // successfully stored increased number of readers
        }
    return false;
}

} // namespace tbb
