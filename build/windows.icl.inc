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

#------------------------------------------------------------------------------
# Define compiler-specific variables.
#------------------------------------------------------------------------------


#------------------------------------------------------------------------------
# Setting default configuration to release.
#------------------------------------------------------------------------------
cfg ?= release
#------------------------------------------------------------------------------
# End of setting default configuration to release.
#------------------------------------------------------------------------------


#------------------------------------------------------------------------------
# Setting compiler flags.
#------------------------------------------------------------------------------
CPLUS = icl $(VCCOMPAT_FLAG)
LINK_FLAGS = /link /nologo
LIB_LINK_FLAGS=/nologo /link /nologo /DLL \
               /DEF:"$(TBB.DEF)" /MAP /DEBUG /fixed:no

ifeq ($(cfg), release)
        CPLUS_FLAGS = /nologo /MD /O2 /Zi /EHsc /Zc:forScope /DDO_ITT_NOTIFY
        ASM_FLAGS =
ifeq (ia32,$(arch))
        CPLUS_FLAGS += /Oy
endif
endif
ifeq ($(cfg), debug)
        CPLUS_FLAGS = /nologo /MDd /Od /Ob0 /Zi /EHsc /Zc:forScope \
                      /DTBB_DO_ASSERT /DDO_ITT_NOTIFY
        LINK_FLAGS += libmmds.lib /NODEFAULTLIB:libmmdd.lib
        ASM_FLAGS = /DUSE_FRAME_POINTER
endif


COMPILE_ONLY = /c /QMMD
PREPROC_ONLY = /E
INCLUDE_KEY = /I
DEFINE_KEY = /D
OUTPUT_KEY = /Fe
OUTPUTOBJ_KEY = /Fo
WARNING_AS_ERROR_KEY = /WX
WARNING_KEY = /W3
DYLIB_KEY = /DLL

ifeq (em64t,$(arch))
        ASM_FLAGS += /DEM64T=1
        CPLUS_FLAGS += /GS-
endif

ifeq (true,$(codecov))
    CPLUS_FLAGS += /Qprof-genx
endif

TARGET_VERSION = 0x0400
OPENMP_FLAG = /Qopenmp
CPLUS_FLAGS += /DUSE_WINTHREAD /D_CRT_SECURE_NO_DEPRECATE \
        /D_WIN32_WINNT=$(TARGET_VERSION)
C_FLAGS = $(CPLUS_FLAGS) /TC

VCVERSION:=$(runtime)
VCCOMPAT_FLAG := $(if $(findstring vc7.1, $(VCVERSION)),/Qvc7.1)
ifeq ($(VCCOMPAT_FLAG),)
        VCCOMPAT_FLAG := $(if $(findstring vc8, $(VCVERSION)),/Qvc8)
endif
ifeq ($(VCCOMPAT_FLAG),)
        $(error VC version not detected correctly: $(VCVERSION) )
endif
export VCCOMPAT_FLAG
#------------------------------------------------------------------------------
# End of setting compiler flags.
#------------------------------------------------------------------------------


#------------------------------------------------------------------------------
# Setting assembler data.
#------------------------------------------------------------------------------
ASSEMBLY_SOURCE=$(arch)-masm
ifeq (em64t,$(arch))
    ASM=ml64
    ASM_FLAGS += /c /Zi
    TBB_ASM.OBJ = atomic_support.obj
else
    ASM=ml
    ASM_FLAGS += /c /coff /Zi
    TBB_ASM.OBJ = atomic_support.obj lock_byte.obj
endif
#------------------------------------------------------------------------------
# End of setting assembler data.
#------------------------------------------------------------------------------


#------------------------------------------------------------------------------
# Setting tbbmalloc data.
#------------------------------------------------------------------------------
M_CPLUS_FLAGS = $(subst EHsc,EHs-,$(CPLUS_FLAGS))
M_INCLUDES = $(INCLUDES) /I$(MALLOC_ROOT) /I$(MALLOC_SOURCE_ROOT)
M_CPLUS_FLAGS += /EHs- /GR-
#------------------------------------------------------------------------------
# End of setting tbbmalloc data.
#------------------------------------------------------------------------------


#------------------------------------------------------------------------------
# End of define compiler-specific variables.
#------------------------------------------------------------------------------
