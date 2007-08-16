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

# This file should be always located and called in $(tbb_root)/examples

tbb_root?=..
BUILDING_PHASE:=0
include ../build/common.inc

ifneq ($(firstword is_abs_path$(subst /, ,$(tbb_root))),is_abs_path)
    override tbb_root := $(CWD)$(SLASH)..
    export TBB20_INSTALL_DIR := $(tbb_root)
endif
work_dir := $(tbb_build_dir)$(SLASH)$(tbb_build_prefix)

ifeq ($(tbb_os),windows)
    EXAMPLE_MAKEFILE = Makefile.windows
    EXAMPLES = $(wildcard */*/$(EXAMPLE_MAKEFILE))
    EXAMPLE_MAKEFILE := $(if $(EXAMPLES),Makefile.windows,Makefile)
    work_dir := $(subst /,$(SLASH),$(work_dir))
    export PATH := $(work_dir)_release;$(work_dir)_debug;$(PATH)
    export LIB := $(work_dir)_release;$(work_dir)_debug;$(LIB)
    export INCLUDE := $(tbb_root)$(SLASH)include;$(INCLUDE)
    export TBB_ARCH_PLATFORM = $(arch)\$(runtime)
else
    EXAMPLE_MAKEFILE = Makefile
    EXAMPLES := $(wildcard */*/$(EXAMPLE_MAKEFILE))
    # platform-specific settings
    ifeq ($(arch),itanium)
        export CXXFLAGS += $(PIC_KEY)
    endif
    ifeq ($(compiler),gcc)
        ifeq ($(arch),em64t)
            export CXXFLAGS += -m64
        endif
        ifeq ($(arch),ia32)
            export CXXFLAGS += -m32
        endif
    endif
    ifeq ($(tbb_os),macos)
        ifeq ($(arch),em64t)
            # making GUI examples work in text console due to incompatible arch
            export UI=con
        endif
        export DYLD_LIBRARY_PATH := $(DYLD_LIBRARY_PATH):$(work_dir)_release:$(work_dir)_debug
    else
        export LD_LIBRARY_PATH := $(LD_LIBRARY_PATH):$(work_dir)_release:$(work_dir)_debug
    endif
    export LIBRARY_PATH := $(LIBRARY_PATH):$(work_dir)_release:$(work_dir)_debug
    export CPATH := $(CPATH):$(tbb_root)/include
endif

COMMON_TARGETS := all clean release debug test
EXAMPLES_TARGETS := $(foreach T,$(COMMON_TARGETS),$(addsuffix _target.$(T),$(EXAMPLES)))

.PHONY: $(COMMON_TARGETS) $(EXAMPLES_TARGETS)

.DEFAULT: all

$(COMMON_TARGETS):: % : $(addsuffix _target.%,$(EXAMPLES))

$(EXAMPLES_TARGETS)::
	-$(MAKE) -C $(@D)  -f $(EXAMPLE_MAKEFILE) $(subst .,,$(suffix $@)) CXX="$(CPLUS)"

printenv:
ifeq ($(tbb_os),windows)
	@cmd /C set PATH
	@cmd /C set LIB
	@cmd /C set INCLUDE
else
	@env | grep PATH
endif

