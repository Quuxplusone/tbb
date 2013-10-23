#!/bin/sh
#
# Copyright 2005-2013 Intel Corporation.  All Rights Reserved.
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
# test_launcher.sh [-v] [-q] [-s] [-r <repeats>] [-u] [-l <library>] <executable> <arg1> <arg2> <argN>
#         where: -v enables verbose output
#         where: -q enables quiet mode
#         where: -s runs the test in stress mode (until non-zero exit code or ctrl-c pressed)
#         where: -r <repeats> specifies number of times to repeat execution
#         where: -u limits stack size
#         where: -l <library> specifies the library name to be assigned to LD_PRELOAD

while getopts  "qvsr:ul:" flag #
do case $flag in #
    s )  # Stress testing mode
         run_prefix="$run_prefix stressed" ;; #
    r )  # Repeats test n times
         repeat=$OPTARG #
         run_prefix="$run_prefix repeated" ;; #
    l )  if [ `uname` != 'Linux' ] ; then #
             echo 'skip' #
             exit #
         fi #
         LD_PRELOAD=$OPTARG ;; #
    u )  # Set stack limit
         ulimit -s 10240 ;; # 
    q )  # Quiet mode, removes 'done' but prepends any other output by test name
         OUTPUT='2>&1 | sed -e "s/done//;/^[[:space:]]*$/d;s!^!$1: !"' ;; #
    v )  # Verbose mode
         verbose=1 ;; #
esac done #
shift `expr $OPTIND - 1` #
if [ $MIC_OFFLOAD_NATIVE_PATH ] ; then #
    LIB_NAME=${1/%.$TEST_EXT/_dll.$DLL} #
    if [ -f "$MIC_OFFLOAD_NATIVE_PATH/$LIB_NAME" ]; then #
       [ -z "$MIC_CARD" ] && MIC_CARD=mic0 #
        TMPDIR_HOST=`mktemp -d /tmp/tbbtestXXXXXX` #
        TMPDIR_MIC=`sudo ssh $MIC_CARD mktemp -d /tmp/tbbtestXXXXXX` #
        sudo ssh $MIC_CARD "chmod +x $TMPDIR_MIC" #
        # Test specific library may depend on libtbbmalloc*
        cp "$MIC_OFFLOAD_NATIVE_PATH/$LIB_NAME" "$MIC_OFFLOAD_NATIVE_PATH"/libtbbmalloc* "$TMPDIR_HOST" >/dev/null 2>/dev/null #
        sudo scp "$TMPDIR_HOST"/* $MIC_CARD:"$TMPDIR_MIC" >/dev/null 2>/dev/null #

        LD_LIBRARY_PATH=$TMPDIR_MIC:$LD_LIBRARY_PATH #
        export LD_LIBRARY_PATH #
    fi #
fi #
stressed() { echo Doing stress testing. Press Ctrl-C to terminate #
    while :; do $*; done;#
} #
repeated() { #
    i=0; while [ "$i" -lt $repeat ]; do i=`expr $i + 1`; echo $i of $repeat:; $*; done #
} #
# Run the command line passed via parameters
[ $verbose ] && echo Running $run_prefix $* #
export LD_PRELOAD #
exec 4>&1 # extracting exit code of the first command in pipeline needs duplicated stdout
# custom redirection needs eval, otherwise shell cannot parse it
err=`eval '( $run_prefix $* || echo \$? >&3; )' ${OUTPUT} 3>&1 >&4` #
[ -z "$err" ] || echo $1: exited with error $err #
if [ $MIC_OFFLOAD_NATIVE_PATH ] ; then #
    sudo ssh $MIC_CARD rm -fr "$TMPDIR_MIC" >/dev/null 2>/dev/null #
    rm -fr "$TMPDIR_HOST" >/dev/null 2>/dev/null #
fi #
exit $err #
