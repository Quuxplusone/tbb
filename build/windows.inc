# Copyright 2005-2007 Intel Corporation.  All Rights Reserved.
#
# This file is part of Threading Building Blocks.
#
# Threading Building Blocks is free software; you can redistribute it
# and/or modify it under the terms of the GNU General Public License
# version 2 as published by the Free Software Foundation.
#
# Threading Building Blocks is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Threading Building Blocks; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As a special exception, you may use this file as part of a free software
# library without restriction.  Specifically, if other files instantiate
# templates or use macros or inline functions from this file, or you compile
# this file and link it with other files to produce an executable, this
# file does not by itself cause the resulting executable to be covered by
# the GNU General Public License.  This exception does not however
# invalidate any other reasons why the executable file might be covered by
# the GNU General Public License.

ifdef tbb_build_dir
  test_dir:=$(tbb_build_dir)
else
  test_dir:=.
endif

ifndef arch
  export arch:=$(shell cmd /C "cscript /nologo /E:jscript $(tbb_root)/build/detect.js /arch")
endif

ifndef runtime
  export runtime:=$(shell cmd /C "cscript /nologo /E:jscript $(tbb_root)/build/detect.js /runtime")
endif

native_compiler := cl
export compiler ?= cl
debugger ?= devenv /debugexe

CWD=$(shell cmd /C echo %CD%)
RM=cmd /C del /Q /F
RD=cmd /C rmdir
MD=cmd /c mkdir
SLASH=\\
NUL = >nul 2>&1

INCLUDES += /I$(tbb_root)/src /I$(tbb_root)/include
OBJ = obj
DLL = dll
LIBEXT = lib

ifeq ($(cfg),debug)
    DEBUG_SUFFIX = _debug
endif

TBB.DEF = $(tbb_root)/src/tbb/$(arch)-tbb.def
TBB.DLL = tbb$(DEBUG_SUFFIX).dll
TBB.LIB = tbb$(DEBUG_SUFFIX).lib
TBB.RES = tbb_resource.res

MALLOC.DEF = $(MALLOC_ROOT)/$(arch)-malloc.def
MALLOC.DLL = tbbmalloc$(DEBUG_SUFFIX).dll
MALLOC.LIB = tbbmalloc$(DEBUG_SUFFIX).lib
MALLOC.RES = tbbmalloc.res


MAKE_VERSIONS = cmd /C cscript /nologo /E:jscript $(tbb_root)/build/version_info_windows.js $(compiler) $(arch) "$(CPLUS) $(CPLUS_FLAGS) $(INCLUDES)" > version_string.tmp
MAKE_TBBVARS  = cmd /C "$(subst /,\,$(tbb_root))\build\generate_tbbvars.bat"
