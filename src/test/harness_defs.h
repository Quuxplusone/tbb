/*
    Copyright 2005-2012 Intel Corporation.  All Rights Reserved.

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

#ifndef __TBB_harness_defs_H
#define __TBB_harness_defs_H

#include "tbb/tbb_config.h"

#if __TBB_TEST_PIC && !__PIC__
#define __TBB_TEST_SKIP_PIC_MODE 1
#else
#define __TBB_TEST_SKIP_PIC_MODE 0
#endif

#if __TBB_TEST_GCC_BUILTINS && !__TBB_GCC_BUILTIN_ATOMICS_PRESENT
#define __TBB_TEST_SKIP_BUILTINS_MODE 1
#else
#define __TBB_TEST_SKIP_BUILTINS_MODE 0
#endif

#ifndef TBB_USE_GCC_BUILTINS
  #define TBB_USE_GCC_BUILTINS         __TBB_TEST_GCC_BUILTINS && __TBB_GCC_BUILTIN_ATOMICS_PRESENT
#endif

#if __INTEL_COMPILER
  #define __TBB_LAMBDAS_PRESENT ( _TBB_CPP0X && __INTEL_COMPILER > 1100 )
#elif __GNUC__
  #define __TBB_LAMBDAS_PRESENT ( _TBB_CPP0X && __TBB_GCC_VERSION >= 40500 )
#elif _MSC_VER
  #define __TBB_LAMBDAS_PRESENT ( _MSC_VER >= 1600 )
#endif

#if _MSC_VER
  #define __TBB_EXCEPTION_TYPE_INFO_BROKEN ( _MSC_VER < 1400 )
#else
  #define __TBB_EXCEPTION_TYPE_INFO_BROKEN 0
#endif

//! a function ptr cannot be converted to const T& template argument without explicit cast
#define __TBB_FUNC_PTR_AS_TEMPL_PARAM_BROKEN ((__linux__ || __APPLE__) && __INTEL_COMPILER && __INTEL_COMPILER < 1100) || __SUNPRO_CC
#define __TBB_UNQUALIFIED_CALL_OF_DTOR_BROKEN (__GNUC__==3 && __GNUC_MINOR__<=3)

#if __TBB_LIBSTDCPP_EXCEPTION_HEADERS_BROKEN
  #define _EXCEPTION_PTR_H /* prevents exception_ptr.h inclusion */
  #define _GLIBCXX_NESTED_EXCEPTION_H /* prevents nested_exception.h inclusion */
#endif

#endif /* __TBB_harness_defs_H */
