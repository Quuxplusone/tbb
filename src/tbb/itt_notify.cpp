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

#define INSTANTIATE_ITT_NOTIFY 1
#include "itt_notify.h"
#include "tbb_misc.h"
#include <stdlib.h>
#include <string.h>
#include "tbb/tbb_machine.h"

#if _WIN32||_WIN64
#include <windows.h>
#else
#include <dlfcn.h>
#endif /* _WIN32||_WIN64 */

namespace tbb {
namespace internal {

#if DO_ITT_NOTIFY
struct ITT_HandlerDescription {
    //! Name of the handler
    const char* name;
    //! Pointer to the handler
    PointerToITT_Handler* handler;
};

//! Table describing the __itt_notify handlers.
static const DynamicLinkDescriptor ITT_HandlerTable[] = {
    {"__itt_notify_sync_prepare",(PointerToHandler*)&ITT_Handler_sync_prepare},
    {"__itt_notify_sync_acquired",(PointerToHandler*)&ITT_Handler_sync_acquired},
    {"__itt_notify_sync_releasing",(PointerToHandler*)&ITT_Handler_sync_releasing},
    {"__itt_notify_sync_cancel",(PointerToHandler*)&ITT_Handler_sync_cancel},
# if _WIN32||_WIN64
#  ifdef UNICODE
    {"__itt_thr_name_setW",(PointerToHandler*)&ITT_Handler_thr_name_set},
#  else
    {"__itt_thr_name_setA",(PointerToHandler*)&ITT_Handler_thr_name_set},
#  endif
# else
    {"__itt_thr_name_set",(PointerToHandler*)&ITT_Handler_thr_name_set},
# endif
};

// LIBITTNOTIFY_NAME is the name of the ITT notification library 
# if _WIN32||_WIN64
#  define LIBITTNOTIFY_NAME "libittnotify.dll"
# elif __linux__
#  define LIBITTNOTIFY_NAME "libittnotify.so"
# elif __APPLE__
#  define LIBITTNOTIFY_NAME "libittnotify.dylib"
# else
#  error Unknown OS
# endif

/** Caller is responsible for ensuring this routine is called exactly once. */
bool InitializeITT() {
    bool result = false;
    // Check if we are running under control of VTune.
    if( GetBoolEnvironmentVariable("KMP_FOR_TCHECK") || GetBoolEnvironmentVariable("KMP_FOR_TPROFILE") ) {
        // Yes, we are under control of VTune.  Check for libittnotify library.
        result = FillDynamicLinks( LIBITTNOTIFY_NAME, ITT_HandlerTable, 5 );
    }
    if (!result){
        for (int i = 0; i < 5; i++)
            *ITT_HandlerTable[i].handler = NULL;
    }
    PrintExtraVersionInfo( "ITT", result?"yes":"no" );
    return result;
}

//! Defined in task.cpp
extern void DoOneTimeInitializations();

//! Executed on very first call throught ITT_Handler_sync_prepare
void dummy_sync_prepare( volatile void* ptr ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( ITT_Handler_sync_prepare!=&dummy_sync_prepare, NULL );
    if (ITT_Handler_sync_prepare)
        (*ITT_Handler_sync_prepare) (ptr);
}

//! Executed on very first call throught ITT_Handler_sync_acquired
void dummy_sync_acquired( volatile void* ptr ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( ITT_Handler_sync_acquired!=&dummy_sync_acquired, NULL );
    if (ITT_Handler_sync_acquired)
        (*ITT_Handler_sync_acquired) (ptr);
}

//! Executed on very first call throught ITT_Handler_sync_releasing
void dummy_sync_releasing( volatile void* ptr ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( ITT_Handler_sync_releasing!=&dummy_sync_releasing, NULL );
    if (ITT_Handler_sync_releasing)
        (*ITT_Handler_sync_releasing) (ptr);
}

//! Executed on very first call throught ITT_Handler_sync_cancel
void dummy_sync_cancel( volatile void* ptr ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( ITT_Handler_sync_releasing!=&dummy_sync_cancel, NULL );
    if (ITT_Handler_sync_cancel)
        (*ITT_Handler_sync_cancel) (ptr);
}

//! Executed on very first call throught ITT_Handler_thr_name_set
int dummy_thr_name_set( const char* str, int number ) {
    DoOneTimeInitializations();
    __TBB_ASSERT( ITT_Handler_thr_name_set!=&dummy_thr_name_set, NULL );
    if (ITT_Handler_thr_name_set)
        return (*ITT_Handler_thr_name_set) (str, number);
    else{// ITT_Handler_thr_name_set is NULL. It means that ITT support is disabled.
        return -1;
    }
}

#endif /* DO_ITT_NOTIFY */

} // namespace internal 

} // namespace tbb
