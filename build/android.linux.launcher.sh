#!/bin/sh
#
# Copyright 2005-2012 Intel Corporation.  All Rights Reserved.
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

# Usage:
# android.linux.launcher.sh [-v] [-l <library>] <executable> <arg1> <arg2> <argN>
#         where: -l <library> specfies the library name to be assigned to LD_PRELOAD
#         where: -v enables verbose output when running the test (where supported)
#
# Libs and executable necessary for testing should be present in the current directory before running.
# ANDROID_SERIAL must be set to the connected Android target device name for file transfer and test runs.
# ANDROID_TEST_DIRECTORY may be set to the directory used for testing on the Android target device; otherwise, 
#                        the default directory used is "/data/tbb/$(basename $PWD)".
# Note: Do not remove the redirections to '/dev/null' in the script, otherwise the nightly test system will fail.

# This is only a wrapper for the actual launcher script
sh ../../examples/common/android.linux.launcher.sh $*
