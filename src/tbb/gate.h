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

#ifndef _TBB_Gate_H
#define _TBB_Gate_H

#define IMPROVED_GATING 0
namespace tbb {

namespace internal {

#if NO_GATE

//! Dummy implementation for experiments
class Gate {
public:
    void open() {}
    void close() {}
    void wait() {}
};

#elif USE_WINTHREAD

class Gate {
#if IMPROVED_GATING
public:
    typedef intptr state_t;
private:
    //! If state==0, then thread executing wait() suspend until state becomes non-zero.
    state_t state;
#else
    intptr count;
#endif /* IMPROVED_GATING */
    CRITICAL_SECTION critical_section;
    HANDLE event;
public:
    //! Initialize with count=0
    Gate() :   
#if IMPROVED_GATING
    state(0) 
#else
    count(0) 
#endif /* IMPROVED_GATING */
    {
        event = CreateEvent( NULL, true, false, NULL );
        InitializeCriticalSection( &critical_section );
    }
    ~Gate() {
#if !IMPROVED_GATING
        __TBB_ASSERT( count==0, NULL );
#endif /* !IMPROVED_GATING */
        CloseHandle( event );
        DeleteCriticalSection( &critical_section );
    }
#if IMPROVED_GATING
    //! Get current state of gate
    state_t get_state() const {
        return state;
    }
    //! Update state=value if state==comparand (flip==false) or state!=comparand (flip==true)
    void try_update( intptr value, intptr comparand, bool flip=false ) {
        __TBB_ASSERT( comparand!=0 || value!=0, "either value or comparand must be non-zero" );
        EnterCriticalSection( &critical_section );
        state_t old = state;
        if( flip ? old!=comparand : old==comparand ) {
            state = value;
            if( !old )
                SetEvent( event );
            else if( !value )
                ResetEvent( event );
        }
        LeaveCriticalSection( &critical_section );
    }
    //! Wait for state!=0.
    void wait() {
        if( state==0 ) {
            WaitForSingleObject( event, INFINITE );
        }
    }
#else /* IMPROVED_GATING */
    //! Increment count
    void open() {
        EnterCriticalSection( &critical_section );
        if( ++count==1 )
            SetEvent( event );
        LeaveCriticalSection( &critical_section );
    }
    //! Decrement count
    void close() {
        EnterCriticalSection( &critical_section );
        if( --count==0 )
            ResetEvent( event );
        LeaveCriticalSection( &critical_section );
    }
    //! Wait for count>0.
    void wait() {
        if( count==0 )
            WaitForSingleObject( event, INFINITE );
    }
#endif /* IMPROVED_GATING */
};

#elif USE_PTHREAD

class Gate {
#if IMPROVED_GATING
public:
    typedef intptr state_t;
private:
    //! If state==0, then thread executing wait() suspend until state becomes non-zero.
    state_t state;
#else
    intptr count;
#endif /* IMPROVED_GATING */
    pthread_mutex_t mutex;
    pthread_cond_t cond;
public:
    //! Initialize with count=0
    Gate() :   
#if IMPROVED_GATING
    state(0)
#else
    count(0)
#endif /* IMPROVED_GATING */
    {
        pthread_mutex_init( &mutex, NULL );
        pthread_cond_init( &cond, NULL);
    }
    ~Gate() {
#if !IMPROVED_GATING
        __TBB_ASSERT( count==0, NULL );
#endif /* !IMPROVED_GATING */
        pthread_cond_destroy( &cond );
        pthread_mutex_destroy( &mutex );
    }
#if IMPROVED_GATING
    //! Get current state of gate
    state_t get_state() const {
        return state;
    }
    //! Update state=value if state==comparand (flip==false) or state!=comparand (flip==true)
    void try_update( intptr value, intptr comparand, bool flip=false ) {
        __TBB_ASSERT( comparand!=0 || value!=0, "either value or comparand must be non-zero" );
        pthread_mutex_lock( &mutex );
        state_t old = state;
        if( flip ? old!=comparand : old==comparand ) {
            state = value;
            if( !old )
                pthread_cond_broadcast( &cond );
        }
        pthread_mutex_unlock( &mutex );
    }
    //! Wait for state!=0.
    void wait() {
        if( state==0 ) {
            pthread_mutex_lock( &mutex );
            while( state==0 ) {
                pthread_cond_wait( &cond, &mutex );
            }
            pthread_mutex_unlock( &mutex );
        }
    }
#else /* IMPROVED_GATING */
    //! Increment count
    void open() {
        pthread_mutex_lock( &mutex );
        if( ++count==1 )
            pthread_cond_broadcast( &cond );
        pthread_mutex_unlock( &mutex );
    }
    //! Decrement count
    void close() {
        pthread_mutex_lock( &mutex );
        __TBB_ASSERT( count>0, NULL );
        --count;
        pthread_mutex_unlock( &mutex );
    }
    //! Wait for count>0.
    void wait() {
        if( count==0 ) {
            pthread_mutex_lock( &mutex );
            while( count==0 ) {
                pthread_cond_wait( &cond, &mutex );
            }
            pthread_mutex_unlock( &mutex );
        }
    }
#endif /* IMPROVED_GATING */
};

#else
#error Must define USE_PTHREAD or USE_WINTHREAD
#endif  /* threading kind */

} // namespace Internal

} // namespace ThreadingBuildingBlocks

#endif /* _TBB_Gate_H */
