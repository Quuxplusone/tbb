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

#ifndef __TBB_itt_annotate_H
#define __TBB_itt_annotate_H

#include "tbb/tbb_stddef.h"

#if __TBB_PROVIDE_VIRTUAL_SCHEDULER && DO_ITT_ANNOTATE

#ifdef ITT_ANNOTATE_GENERATE
#define _ITT_ANNOTATE_DECLARE_0(_BASENAME) \
void ITT_Annotate_##_BASENAME() \
{ _ANNOTATE_CALL_0(_BASENAME); }

#define _ITT_ANNOTATE_DECLARE_1(_BASENAME, _P1TYPE) \
void ITT_Annotate_##_BASENAME(_P1TYPE p1) \
{ _ANNOTATE_CALL_1(_BASENAME, p1); }

#define _ITT_ANNOTATE_DECLARE_2(_BASENAME, _P1TYPE, _P2TYPE) \
void ITT_Annotate_##_BASENAME(_P1TYPE p1, _P2TYPE p2) \
{ _ANNOTATE_CALL_2(_BASENAME, p1, p2); }

#else /* !ITT_ANNOTATE_GENERATE */
#define _ITT_ANNOTATE_DECLARE_0(_BASENAME) \
extern void ITT_Annotate_##_BASENAME();

#define _ITT_ANNOTATE_DECLARE_1(_BASENAME, _P1TYPE) \
extern void ITT_Annotate_##_BASENAME(_P1TYPE p1);

#define _ITT_ANNOTATE_DECLARE_2(_BASENAME, _P1TYPE, _P2TYPE) \
extern void ITT_Annotate_##_BASENAME(_P1TYPE p1, _P2TYPE p2);
#endif /* !ITT_ANNOTATE_GENERATE */

namespace tbb {
namespace internal {
_ITT_ANNOTATE_DECLARE_0(BeginConcurrencySite)
_ITT_ANNOTATE_DECLARE_0(EndConcurrencySite)
_ITT_ANNOTATE_DECLARE_0(BeginTask)
_ITT_ANNOTATE_DECLARE_0(EndTask)
_ITT_ANNOTATE_DECLARE_1(SetRecursiveLock,         void*)
_ITT_ANNOTATE_DECLARE_1(UnsetRecursiveLock,       void*)
_ITT_ANNOTATE_DECLARE_2(RecordAllocation,         void*, size_t)
_ITT_ANNOTATE_DECLARE_0(WaitForAll)
_ITT_ANNOTATE_DECLARE_2(SetSharedExclusiveLock,   void*, int)
_ITT_ANNOTATE_DECLARE_1(UnsetSharedExclusiveLock, void*)

extern bool Initialize_ITTAnnotate();

} // namespace internal
} // namespace tbb

#define ANN_CALL(name,args) tbb::internal::ITT_Annotate_##name##args

#define ITT_ANNOTATE_BEGIN_SITE()            ANN_CALL(BeginConcurrencySite,())
#define ITT_ANNOTATE_END_SITE()              ANN_CALL(EndConcurrencySite,())
#define ITT_ANNOTATE_BEGIN_TASK()            ANN_CALL(BeginTask,())
#define ITT_ANNOTATE_END_TASK()              ANN_CALL(EndTask,())
#define ITT_ANNOTATE_ACQUIRE_LOCK(obj)       ANN_CALL(SetRecursiveLock,(obj))
#define ITT_ANNOTATE_RELEASE_LOCK(obj)       ANN_CALL(UnsetRecursiveLock,(obj))
#define ITT_ANNOTATE_ACQUIRE_WRITE_LOCK(obj) ANN_CALL(SetSharedExclusiveLock,(obj, 1))
#define ITT_ANNOTATE_RELEASE_WRITE_LOCK(obj) ANN_CALL(UnsetSharedExclusiveLock,(obj))
#define ITT_ANNOTATE_ACQUIRE_READ_LOCK(obj)  ANN_CALL(SetSharedExclusiveLock,(obj, 0))
#define ITT_ANNOTATE_RELEASE_READ_LOCK(obj)  ANN_CALL(UnsetSharedExclusiveLock,(obj))
#define ITT_ANNOTATE_ALLOCATION(obj, sz)     ANN_CALL(RecordAllocation,(obj, sz))
#define ITT_ANNOTATE_WAIT_FOR_ALL()          ANN_CALL(WaitForAll,())

#undef _ITT_ANNOTATE_DECLARE_0
#undef _ITT_ANNOTATE_DECLARE_1
#undef _ITT_ANNOTATE_DECLARE_2

#else /* !(__TBB_PROVIDE_VIRTUAL_SCHEDULER && DO_ITT_ANNOTATE) */

#define ITT_ANNOTATE_BEGIN_SITE()
#define ITT_ANNOTATE_END_SITE()
#define ITT_ANNOTATE_BEGIN_TASK()
#define ITT_ANNOTATE_END_TASK()
#define ITT_ANNOTATE_ACQUIRE_LOCK(obj)
#define ITT_ANNOTATE_RELEASE_LOCK(obj)
#define ITT_ANNOTATE_ACQUIRE_WRITE_LOCK(obj)
#define ITT_ANNOTATE_RELEASE_WRITE_LOCK(obj)
#define ITT_ANNOTATE_ACQUIRE_READ_LOCK(obj)
#define ITT_ANNOTATE_RELEASE_READ_LOCK(obj)
#define ITT_ANNOTATE_ALLOCATION(obj, sz)
#define ITT_ANNOTATE_WAIT_FOR_ALL()

#endif /* !(__TBB_PROVIDE_VIRTUAL_SCHEDULER && DO_ITT_ANNOTATE) */

#endif /* __TBB_itt_annotate_H */
