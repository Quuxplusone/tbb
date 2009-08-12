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

/** @file harness_lrb_host.cpp     
    This is the launcher for TBB tests compiled for LrbFSim or NetSim environments.
**/

#include <windows.h>
#include <stdio.h>
#include <assert.h>

#define __TBB_LRB_HOST 1
#include "harness_lrb.h"

#define __TBB_HOST_EXIT(status)  exitStatus = status; goto hard_stop;

bool IsSupportedTest ( int argc, char* argv[] ) {
    const char* test[] = {
        "test_model_plugin", 
        "test_tbb_version",
        "malloc_overload",
        NULL
        };
    for ( size_t i = 0; test[i]; ++i ) {
        for ( size_t j = 1; j < argc; ++j ) {
            if ( strstr(argv[j], test[i]) )
                return false;
        }
    }
    return true;
}

bool IsCompletionMsg ( const char* msg ) {
    return strncmp(msg, __TBB_MSG_DONE, __TBB_LRB_COMM_MSG_SIZE_MAX) == 0 ||
           strncmp(msg, __TBB_MSG_SKIP, __TBB_LRB_COMM_MSG_SIZE_MAX) == 0;
}

int main( int argc, char* argv[] ) {
    int exitStatus = 0;

    if (argc < 2) {
        printf( "Usage: %s test_name test_args\n", argv[0] );
        __TBB_HOST_EXIT(-1);
    }
    if ( !IsSupportedTest(argc, argv) ) {
        printf(__TBB_MSG_SKIP);
        __TBB_HOST_EXIT(0);
    }

    XNENGINE engine;
    XNERROR result = XN0EngineGetHandle(0, &engine);
    assert( XN_SUCCESS == result );

    // Try with a run schedule of one second
    XN_RUN_SCHEDULE runSchedule;
    runSchedule.executionQuantumInUsecs = 500000;
    runSchedule.frequencyInHz = 1;

    XNCONTEXT ctxHandle;
    result = XN0ContextCreate(engine, &runSchedule, &ctxHandle);
    assert( XN_SUCCESS == result );

    XNCOMMUNICATOR communicator;
    result = XN0MessageCreateCommunicator( __TBB_LRB_COMMUNICATOR_NAME, __TBB_LRB_COMM_MSG_SIZE_MAX, &communicator );
    assert( XN_SUCCESS == result );

    XNLIBRARY libHandle;
    if ( argc == 2 )
        result = XN0ContextLoadLib(ctxHandle, argv[1], &libHandle);
    else
        result = XN0ContextLoadLib1(ctxHandle, argv[1], argc - 1, argv + 1, &libHandle);
    if( result != XN_SUCCESS ) {
        printf( "ERROR: Loading module \"%s\" failed", argv[1] );
        __TBB_HOST_EXIT(-2);
    }

    char msg[__TBB_LRB_COMM_MSG_SIZE_MAX + 1] = { 0 };
    bool abort_signalled = false;
    for( ; !IsCompletionMsg(msg); ) {
        XN0MessageReceive( communicator, msg, __TBB_LRB_COMM_MSG_SIZE_MAX, NULL );
        if ( strncmp(msg, __TBB_MSG_ABORT, __TBB_LRB_COMM_MSG_SIZE_MAX ) == 0 ) {
            abort_signalled = true;
            // The next message should provide the reason
            continue;
        }
        printf("%s\n", msg); fflush(stdout);
        if ( abort_signalled ) {
            // After exit() or abort() was invoked in a LRB library, it cannot be 
            // unloaded, and the host hangs in XN0ContextDestroy. Thus we have to 
            // bypass the graceful termination code.
            __TBB_HOST_EXIT(1);
        }
    }
    XN0MessageDestroyCommunicator( communicator );

    result = XN0ContextUnloadLib(libHandle, 10 * 1000, &exitStatus);
    if( result == XN_TIME_OUT_REACHED ) {
        printf("ERROR: timed out waiting for LRB module unload\n");
    }
    else {
        result = XN0ContextDestroy(ctxHandle);
        assert( XN_SUCCESS == result );
    }
    if ( exitStatus != 0 )
        printf("ERROR: %s returned failure status %d", argv[1], exitStatus);
hard_stop:
    fflush(stdout);
    // We do not need a dump of memory leaks statistics
    TerminateProcess( GetCurrentProcess(), 0 );
    return 0;
}
