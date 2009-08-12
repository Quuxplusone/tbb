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

#include "tbb/tbb_config.h"
#include "tbb/tbb_machine.h"
#include "tbb_misc.h"
#include "dynamic_link.h"
#include "tbb/cache_aligned_allocator.h" /* NFS_MaxLineSize */

#if __TBB_PROVIDE_VIRTUAL_SCHEDULER && DO_ITT_ANNOTATE

#define _ANNOTATE_DECLARE_0(_BASENAME) \
extern "C" void __Tc##_BASENAME(); \
namespace tbb { namespace internal { \
typedef void (*_ANNOTATE_##_BASENAME##_t)(); \
extern _ANNOTATE_##_BASENAME##_t _ANNOTATE_Handler_##_BASENAME; }}


#define _ANNOTATE_DECLARE_1(_BASENAME, _P1TYPE) \
extern "C" void __Tc##_BASENAME(_P1TYPE p1); \
namespace tbb { namespace internal { \
typedef void (*_ANNOTATE_##_BASENAME##_t)(_P1TYPE p1); \
extern _ANNOTATE_##_BASENAME##_t _ANNOTATE_Handler_##_BASENAME; }}

#define _ANNOTATE_DECLARE_2(_BASENAME, _P1TYPE, _P2TYPE) \
extern "C" void __Tc##_BASENAME(_P1TYPE p1, _P2TYPE p2); \
namespace tbb { namespace internal { \
typedef void (*_ANNOTATE_##_BASENAME##_t)(_P1TYPE p1, _P2TYPE p2); \
extern _ANNOTATE_##_BASENAME##_t _ANNOTATE_Handler_##_BASENAME; }}

#if _WIN32||_WIN64
    #include <windows.h>
#else /* !WIN */
    #include <dlfcn.h>
#if __TBB_WEAK_SYMBOLS
    #pragma weak __TcBeginConcurrencySite
    #pragma weak __TcEndConcurrencySite
    #pragma weak __TcBeginTask
    #pragma weak __TcEndTask
    #pragma weak __TcSetRecursiveLock
    #pragma weak __TcUnsetRecursiveLock
    #pragma weak __TcRecordAllocation
    #pragma weak __TcWaitForAll
    #pragma weak __TcSetSharedExclusiveLock
    #pragma weak __TcUnsetSharedExclusiveLock
#endif /* __TBB_WEAK_SYMBOLS */
#endif /* !WIN */

_ANNOTATE_DECLARE_0(BeginConcurrencySite)
_ANNOTATE_DECLARE_0(EndConcurrencySite)
_ANNOTATE_DECLARE_0(BeginTask)
_ANNOTATE_DECLARE_0(EndTask)
_ANNOTATE_DECLARE_1(SetRecursiveLock,         void*)
_ANNOTATE_DECLARE_1(UnsetRecursiveLock,       void*)
_ANNOTATE_DECLARE_2(RecordAllocation,         void*, size_t)
_ANNOTATE_DECLARE_0(WaitForAll)
_ANNOTATE_DECLARE_2(SetSharedExclusiveLock,   void*, int)
_ANNOTATE_DECLARE_1(UnsetSharedExclusiveLock, void*)

namespace tbb {
namespace internal {

#define _ANNOTATE_DLD(_BASENAME) \
DLD( __Tc##_BASENAME, _ANNOTATE_Handler_##_BASENAME)

//! Table describing the annotate handlers.
static const dynamic_link_descriptor Annotate_HandlerTable[] = {
    _ANNOTATE_DLD(BeginConcurrencySite),
    _ANNOTATE_DLD(EndConcurrencySite),
    _ANNOTATE_DLD(BeginTask),
    _ANNOTATE_DLD(EndTask),
    _ANNOTATE_DLD(SetRecursiveLock),
    _ANNOTATE_DLD(UnsetRecursiveLock),
    _ANNOTATE_DLD(RecordAllocation),
    _ANNOTATE_DLD(WaitForAll),
    _ANNOTATE_DLD(SetSharedExclusiveLock),
    _ANNOTATE_DLD(UnsetSharedExclusiveLock),
};

static const int Annotate_HandlerTable_size = 
    sizeof(Annotate_HandlerTable)/sizeof(dynamic_link_descriptor);

# if _WIN32||_WIN64
#  define LIB_ITT_ANNOTATE "tcwhatif.dll"
# elif __linux__
#  define LIB_ITT_ANNOTATE "libtcwhatif.so"
# else
#  error Intel(R) Threading Tools not provided for this OS
# endif

bool Initialize_ITTAnnotate() {
    bool result = false;
    if( GetBoolEnvironmentVariable("DO_WHATIF_ANALYSIS") ) {
        result = dynamic_link(LIB_ITT_ANNOTATE, Annotate_HandlerTable, Annotate_HandlerTable_size, Annotate_HandlerTable_size );
    }
    return result;
}

//! Leading padding before the area where tool notification handlers are placed.
/** Prevents cache lines where the handler pointers are stored from thrashing.
    Defined as extern to prevent compiler from placing the padding arrays separately
    from the handler pointers (which are declared as extern).
    Declared separately from definition to get rid of compiler warnings. **/
extern char __ANNOTATE_Handler_leading_padding[NFS_MaxLineSize];

//! Trailing padding after the area where tool notification handlers are placed.
extern char __ANNOTATE_Handler_trailing_padding[NFS_MaxLineSize];

#define _ANNOTATE_HANDLER(_BASENAME) \
_ANNOTATE_##_BASENAME##_t _ANNOTATE_Handler_##_BASENAME

char __ANNOTATE_Handler_leading_padding[NFS_MaxLineSize] = {0};
_ANNOTATE_HANDLER(BeginConcurrencySite);
_ANNOTATE_HANDLER(EndConcurrencySite);
_ANNOTATE_HANDLER(BeginTask);
_ANNOTATE_HANDLER(EndTask);
_ANNOTATE_HANDLER(SetRecursiveLock);
_ANNOTATE_HANDLER(UnsetRecursiveLock);
_ANNOTATE_HANDLER(RecordAllocation);
_ANNOTATE_HANDLER(WaitForAll);
_ANNOTATE_HANDLER(SetSharedExclusiveLock);
_ANNOTATE_HANDLER(UnsetSharedExclusiveLock);
char __ANNOTATE_Handler_trailing_padding[NFS_MaxLineSize] = {0};

} // namespace internal
} // namespace tbb

#define _ANNOTATE_CALL_0(_BASENAME) \
{ _ANNOTATE_##_BASENAME##_t fp = _ANNOTATE_Handler_##_BASENAME; \
  if (fp) fp(); }

#define _ANNOTATE_CALL_1(_BASENAME, _P1) \
{ _ANNOTATE_##_BASENAME##_t fp = _ANNOTATE_Handler_##_BASENAME; \
  if (fp) fp(_P1); }

#define _ANNOTATE_CALL_2(_BASENAME, _P1, _P2) \
{ _ANNOTATE_##_BASENAME##_t fp = _ANNOTATE_Handler_##_BASENAME; \
  if (fp) fp(_P1, _P2); }

#define ITT_ANNOTATE_GENERATE
#include "itt_annotate.h"

#endif /* __TBB_PROVIDE_VIRTUAL_SCHEDULER && DO_ITT_ANNOTATE */
