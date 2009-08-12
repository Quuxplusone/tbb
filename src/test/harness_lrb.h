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

#ifndef tbb_test_harness_lrb_H
#define tbb_test_harness_lrb_H

#if !(__LRB__||__TBB_LRB_HOST)
    #error test/harness_lrb.h should be included only when building for LRB platform
#endif

#define __TBB_LRB_COMM_MSG_SIZE_MAX 1024
#define __TBB_LRB_COMMUNICATOR_NAME "__TBB_LRB_COMMUNICATOR"

#define __TBB_MSG_DONE "done\n"
#define __TBB_MSG_SKIP "skip\n"
#define __TBB_MSG_ABORT "__TBB_abort__"

#if __TBB_LRB_HOST

#include "host/XN0_host.h"

#else /* !__TBB_LRB_HOST */

#include "lrb/XN0_lrb.h"
#include <assert.h>

#define __TBB_STDARGS_BROKEN 1
#define __TBB_TEST_EXPORT XNNATIVELIBEXPORT

#if XENSIM
    #define __TBB_EXCEPTION_HANDLING_BROKEN 1
    #define __TBB_PLACEMENT_NEW_EXCEPTION_SAFETY_BROKEN 1
    #define __TBB_EXCEPTION_HANDLING_TOTALLY_BROKEN 1
#endif /* XENSIM */

namespace Harness {
    namespace internal {

    class LrbReporter {
        XNCOMMUNICATOR  m_communicator;

    public:
        LrbReporter () {
            XNERROR res = XN0MessageCreateCommunicator( __TBB_LRB_COMMUNICATOR_NAME, 
                                                        __TBB_LRB_COMM_MSG_SIZE_MAX, 
                                                        &m_communicator );
            assert( XN_SUCCESS == res );
        }
        
        ~LrbReporter () {
            XN0MessageDestroyCommunicator( m_communicator );
        }

        void Report ( const char* msg ) {
            XN0MessageSend( m_communicator, msg, __TBB_LRB_COMM_MSG_SIZE_MAX );
        }
    }; // class LrbReporter

    } // namespace internal
} // namespace Harness

#define TbbHarnessReporter LrbReporter

#define REPORT_FATAL_ERROR  REPORT(__TBB_MSG_ABORT); REPORT

#if XENSIM
    #define TBB_EXIT_ON_ASSERT 1
#else
    #define TBB_TERMINATE_ON_ASSERT 1
#endif

// Suppress warnings caused by windows.h during NetSim build
#pragma warning (disable: 4005)

#endif /* !__TBB_LRB_HOST */

#endif /* tbb_test_harness_lrb_H */
