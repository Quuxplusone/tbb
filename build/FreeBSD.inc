# Copyright 2005-2014 Intel Corporation.  All Rights Reserved.
#
# This file is part of Threading Building Blocks. Threading Building Blocks is free software;
# you can redistribute it and/or modify it under the terms of the GNU General Public License
# version 2  as  published  by  the  Free Software Foundation.  Threading Building Blocks is
# distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See  the GNU General Public License for more details.   You should have received a copy of
# the  GNU General Public License along with Threading Building Blocks; if not, write to the
# Free Software Foundation, Inc.,  51 Franklin St,  Fifth Floor,  Boston,  MA 02110-1301 USA
#
# As a special exception,  you may use this file  as part of a free software library without
# restriction.  Specifically,  if other files instantiate templates  or use macros or inline
# functions from this file, or you compile this file and link it with other files to produce
# an executable,  this file does not by itself cause the resulting executable to be covered
# by the GNU General Public License. This exception does not however invalidate any other
# reasons why the executable file might be covered by the GNU General Public License.

ifndef arch
        ifeq ($(shell uname -m),i386)
                export arch:=ia32
        endif
        ifeq ($(shell uname -m),ia64)
                export arch:=ia64
        endif
        ifeq ($(shell uname -m),amd64)
                export arch:=intel64
        endif
endif

ifndef runtime
        gcc_version:=$(shell gcc -v 2>&1 | grep 'gcc version' | sed -e 's/^gcc version //' | sed -e 's/ .*$$//')
        os_version:=$(shell uname -r)
        os_kernel_version:=$(shell uname -r | sed -e 's/-.*$$//')
        export runtime:=cc$(gcc_version)_kernel$(os_kernel_version)
endif

native_compiler := gcc
export compiler ?= gcc
debugger ?= gdb

CMD=$(SHELL) -c
CWD=$(shell pwd)
RM?=rm -f
RD?=rmdir
MD?=mkdir -p
NUL= /dev/null
SLASH=/
MAKE_VERSIONS=sh $(tbb_root)/build/version_info_linux.sh $(VERSION_FLAGS) >version_string.ver
MAKE_TBBVARS=sh $(tbb_root)/build/generate_tbbvars.sh

ifdef LD_LIBRARY_PATH
        export LD_LIBRARY_PATH := .:$(LD_LIBRARY_PATH)
else
        export LD_LIBRARY_PATH := .
endif

####### Build settings ########################################################

OBJ = o
DLL = so
LIBEXT=so

TBB.LST =
TBB.DEF =
TBB.DLL = libtbb$(CPF_SUFFIX)$(DEBUG_SUFFIX).$(DLL)
TBB.LIB = $(TBB.DLL)
LINK_TBB.LIB = $(TBB.LIB)

MALLOC.DLL = libtbbmalloc$(DEBUG_SUFFIX).$(DLL)
MALLOC.LIB = $(MALLOC.DLL)
LINK_MALLOC.LIB = $(MALLOC.LIB)

TEST_LAUNCHER=sh $(tbb_root)/build/test_launcher.sh $(largs)
