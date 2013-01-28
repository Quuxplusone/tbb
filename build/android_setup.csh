#!/bin/tcsh -v
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

#  Script sets up
#    . ANDROID_SERIAL - the adb number of the device.  User must provide this,
#      but script may add a port #
#    . CPLUS - C++ compiler
#    . CC    - C compiler
#    . SYSROOT - custom toolchains have this at the outer level, else in the NDK
#    . CPLUS_INCLUDE_PATH
#    . CPLUS_LIB_PATH - used by android.linux.launcher.sh to find shared libraries
#      to transfer to device
#    . tbb_tool_prefix - used by Makefile includes
#    . adds the compiler location to the PATH

# command line argument default values
set abi = 14                              # Android ABI version
set cpu = x86                             # Android device CPU architecture
set device = "*unset*"                    # Android device name (according to adb)
set gcc = 4.4.3                           # Android GCC version used by NDK
set ndk = 8                               # Android NDK version
set lib = gnustl                          # C++ library name
set ndk_path_pref =        "/localdisk"   # prefix of NDK and custom toolchain paths
# default values not available from command line
set port = ":5555"                        # default port for adb use

# three possibilities:
#  .  specify NDK, cpu, abi, device.  Will use pre-built toolchain, libraries and
#     includes in NDK.
#  .  specify pre-built toolchain, abi and device.  Will figure what cpu, NDK to
#     use libs and includes from NDK.
#  .  specify custom toolchain, device.  Will figure cpu, abi, use custom toolchain
#     libs & incs.  Cannot figure NDK.
set use_custom_toolchain = 0  # 1 => prebuilt toolchain  2 => custom toolchain

set setup_args = ( $argv )  # to store successful setup parameters
set verbose_output = 0

# process command line args
while ( $#argv )
    switch ( $argv[1] )
      case -a:
        set abi = $argv[2]
        shift
        breaksw
      case -c:
        set cpu = $argv[2]
        shift
        breaksw
      case -d:
        set device = $argv[2]
        shift
        breaksw
      case -p:
        set ndk_path_pref = $argv[2]
        shift
        if ( -d ${ndk_path_pref} ) then
        else
          echo "Specified NDK path ( ${ndk_path_pref} ) does not exist"
          exit(-1)
        endif
        breaksw
      case -s:   # standalone toolchain location
        set tool_loc = $argv[2]
        shift
        set use_custom_toolchain = 1   # standalone toolchain
        if ( -d $ndk_path_pref/$tool_loc ) then
          set tool_dir = $ndk_path_pref/$tool_loc
        else if ( -d $tool_loc ) then
          set tool_dir = $tool_loc
        else
          echo "Specified standalone toolchain location ( ${ndk_path_pref}/$tool_loc or $tool_loc ) does not exist"
          exit (-1)
        endif
        breaksw
      case -g:
        set gcc = $argv[2]
        shift
        breaksw
      case -n: 
        set ndk = $argv[2]
        shift
        breaksw
      case -t:  # prebuilt toolchain
        set tool_loc = $argv[2]
        shift
        set use_custom_toolchain = 2   # prebuilt in an NDK
        if ( -d $ndk_path_pref/$tool_loc ) then
          set tool_dir = $ndk_path_pref/$tool_loc
        else if ( -d $tool_loc ) then
          set tool_dir = $tool_loc
        else
          echo "Specified prebuilt toolchain location ( ${ndk_path_pref}/$tool_loc or $tool_loc ) does not exist"
          exit (-1)
        endif
        breaksw
      case -v:
        set verbose_output = 1
        breaksw
      default:
        if ( "$argv[1]" != '-h' ) then
            echo "Error: Unknown argument: $argv[1]"
        endif
        # build a list of available NDKs.
        set all_ndks = ""
        if( -d $ndk_path_pref ) then
            pushd $ndk_path_pref > /dev/null
            set nonomatch
            set ndk_dirs = ( android-ndk-r* )
            unset nonomatch
            popd > /dev/null
            if ( "$ndk_dirs" != 'android-ndk-r*' ) then
                foreach ff ( $ndk_path_pref/android-ndk-r* )
                    if (-d $ff ) then
                        set dd = `echo "$ff" | sed 's/.*android-ndk-r//'`
                        set all_ndks = ( ${all_ndks} $dd )
                    endif
                end
            endif
            if( "$all_ndks" == '' ) then
                set errmsg = "No NDKs in ${ndk_path_pref}.  To list available NDKs, try 'source android_setup.csh -p <NDK-location> -h'"
            endif
        else
            set errmsg = "NDK directory ${ndk_path_pref} does not exist.  To list available NDKs, try 'source android_setup.csh -p <NDK-location> -h'"
        endif
        echo ""
        echo "Usage:"
        echo "  source android_setup.csh <arguments>              current values:"
        echo "     -n <ndk> : Use Android NDK version             (<ndk> = $ndk)"
        echo "     -c <cpu> : Use Android device CPU architecture (<cpu> = $cpu)"
        echo "                (arm,x86,atom)"
        echo "     -g <gcc> : Use Android GCC compiler version    (<gnu> = $gcc)"
        echo "     -p <dir> : Location of NDKs                    (<dir> = ${ndk_path_pref})"
        echo "     -s <dir> : Location of standalone toolchain (absolute or in NDK directory)"
        echo "     -t <dir> : prebuilt toolchain directory  (absolute or in NDK directory)"
        echo "     -a <abi> : Use Android ABI version             (<abi> = $abi)"
        echo "     -d <dev> : Use Android device name             (<dev> = $device)"
        echo "     -v       : Verbose output"
        echo "     -h       : Print this usage help message"
        echo " "
        if ( "${all_ndks}" == "" ) then
            echo "         *** $errmsg"
        else
            echo "         Available NDKs in ${ndk_path_pref}:"
            foreach ff ( ${all_ndks} )
                echo "           $ff"
            end
            echo " "
        endif
        echo " "
        echo "Connected devices:"
        adb devices
        exit(-1)
    endsw
    shift
end

if ( "v${device}" == 'v*unset*' ) then
    echo " *** a device must be specified"
    exit (-1)
endif

# if we are given a toolchain directory it may be a custom or a prebuilt toolchain
# if prebuilt we can get some of the values we need from the directory.  In any case,
# the prebuilt toolchain case will be the same as the NDK case, but with the compiler
# set by the directory.
if( $use_custom_toolchain > 0 ) then
    if ( $verbose_output ) echo " --- Starting with prebuilt or standalone toolchain=$tool_dir ..."
    # decipher other values from the contents of the toolchain
    # abi is included in custom toolchains, else prebuilt will be in ./platforms.
    pushd $tool_dir > /dev/null
    set nonomatch
    #  look for <cpu>-android-linux or <cpu>-linux-android in top directory of custom toolchain
    set x86_dir = i686-*
    set arm_dir = arm-*
    set mips_dir = mipsel-*
    # gcc_arch below will differ only for x86/i686, not the arm or mips toolchains
    if ( $x86_dir != 'i686-*' ) then
        set cpu = x86
        # set dev_host = `echo $x86_dir | sed 's/i686-//' | sed 's/eabi//'`
        set dev_host = `echo $x86_dir | sed 's/i686-//'`
        set arch_subdir = $x86_dir
        set gcc_arch = "x86"
        if ( $verbose_output ) echo "x86 cpu"
    else if ( $arm_dir != 'arm-*' ) then
        set cpu = arm
        set dev_host = `echo $arm_dir | sed 's/arm-//'`
        set arch_subdir = $arm_dir
        set gcc_arch = $arm_dir
        if ( $verbose_output ) echo "arm cpu"
    else if ( $mips_dir != 'mipsel-*' ) then
        set cpu = mips
        set dev_host = `echo $mips_dir | sed 's/mipsel-//'`
        set arch_subdir = $mips_dir
        set gcc_arch = $mips_dir
        if ( $verbose_output ) echo "mips cpu"
    else
        echo "Prebuilt or standalone toolchain $tool_dir does not have a known cpu type"
        exit (-1)
    endif
    unset nonomatch

    # gcc version ( from $arch_subdir/include/c++/ or parent directory of prebuilt toolchain )
    if ( -d $arch_subdir/include/c++ ) then
        set gcc = `ls $arch_subdir/include/c++ | sed 's/\///'`
        if ( $verbose_output ) echo "    standalone: recovered gcc == $gcc from subdir"
    else
        if ( $verbose_output ) echo "prebuilt toolchain"
        set use_custom_toolchain = 0  # we are setting the script switches to describe the prebuilt toolchain
        # either a prebuilt toolchain or an incorrectly-constructed toolchain.  try to take
        # the specified directory apart
        # Example of prebuilt directory:
        #     /localdisk/android-ndk-r8/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86
        set rdir1 = $tool_dir:h # /localdisk/android-ndk-r8/toolchains/arm-linux-androideabi-4.4.3/prebuilt
        if ( "$rdir1:t" != 'prebuilt' ) then
            echo " *** Unexpected directory structure for prebuilt toolchain ($tool_dir)"
            exit (-1)
        endif
        set rdir2 = $rdir1:h  # /localdisk/android-ndk-r8/toolchains/arm-linux-androideabi-4.4.3
        set tsubdir = $rdir2:h # /localdisk/android-ndk-r8/toolchains
        set tctmp = $tsubdir:t # toolchains
        set tsubdir = $tsubdir:h # /localdisk/android-ndk-r8
        if( "$tctmp" != 'toolchains' ) then
            echo "Structure of prebuilt toolchain directories not understood ($tool_dir)"
            exit (-1)
        endif
        set bdir = $rdir2:t # arm-linux-androideabi-4.4.3
        set gcc = `echo $bdir | sed "s/${gcc_arch}-//"`  # gcc_arch == arm-linux-androideabi in this case.
        if ( "$gcc" == '' ) then
            echo "Not able to decipher GCC version for prebuilt toolchain from $tool_dir (directory examined was $bdir)"
            exit (-1)
            endif
        # we reset the path to the location of the NDK given us
        set ndk_path_pref = $tsubdir:h  # /localdisk
        set ndk = `echo "$tsubdir:t" | sed 's/.*android-ndk-r//'`
        if ( $verbose_output ) echo "    prebuilt: ndk=$ndk, gcc=$gcc"
    endif
    popd > /dev/null
    if ( $verbose_output ) echo "    using abi=$abi, cpu=$cpu, dev=$device, gcc=$gcc, ndk=$ndk ndk_path_pref == $ndk_path_pref"
else
    if ( $verbose_output ) echo "    using abi=$abi, cpu=$cpu, dev=$device, gcc=$gcc, ndk=$ndk ..."
endif
if ( $verbose_output ) echo ""

if ( $use_custom_toolchain == 0 ) then
    # Set up path to NDK 
    set ndk_path = "$ndk_path_pref/android-ndk-r$ndk"
    if (-d $ndk_path) then
        setenv ANDROID_NDK_ROOT $ndk_path
        if ( $verbose_output ) echo "ANDROID_NDK_ROOT == $ndk_path"
    else
        echo "Error: Android NDK not found: $ndk_path"
        echo "...Skipping setup of Toolchain and Sysroot..."
        goto device_setup
    endif
else
    setenv ANDROID_NDK_ROOT "_custom"  # don't need this for toolchain, but value is used in directory naming
    if ( $verbose_output ) echo "    standalone: ANDROID_NDK_ROOT == $ANDROID_NDK_ROOT"
endif

# Set up internal names according to target cpu
switch ( $cpu )
  case atom*:
    set cpu = x86  # atom does not appear in directories
  case x86*:
    set arch = i686
    set toolchain = x86
    set toolname1 = $arch-linux-android
    set toolname2 = $arch-android-linux
    set libabi = x86
    set includeabi = x86
    breaksw
  case arm*:
    set arch = arm
    set toolchain = $arch-linux-androideabi
    set toolname1 = $arch-linux-androideabi
    set toolname2 = $arch-android-linuxeabi
    set libabi = ${arch}eabi-v7a
    set includeabi = ${arch}eabi
    breaksw
  case mips*:
    echo "MIPS not supported.  Use at your own risk."
    set arch = mips
    set toolchain = ${arch}el-linux-android
    set toolname1 = ${arch}el-linux-android
    set toolname2 = ${arch}el-android-linux
    set libabi = ${arch}
    set includeabi = ${arch}
    breaksw
  default:
    echo "Error: specified cpu not recognized: $cpu"
    goto set_toolchain
endsw

# Set up path to Android toolchain
# this provides the compiler we are building with
set_toolchain:
if ( $use_custom_toolchain == 0 ) then
    set toolchain_path = "$ndk_path/toolchains/$toolchain-$gcc/prebuilt/linux-x86/bin"
else
    set toolchain_path = "$tool_dir/bin"
endif
if (-d $toolchain_path) then
    if ( $verbose_output ) echo "    toolchain == $toolchain_path"
    if ("$path" !~ $toolchain_path*) then
        set path = ( $toolchain_path $path )
    else
        if ( $verbose_output ) echo "    PATH already begins with Android Toolchain:"
    endif
    if ( $verbose_output ) echo "PATH == $PATH"
else
    echo "Error: Android Toolchain not found: $toolchain_path"
    while ( "$toolchain_path" != '' )
        if ( -d $toolchain_path ) then
            pushd $toolchain_path > /dev/null
            echo "Directory $toolchain_path contains:"
            echo ""
            ls -1
            popd > /dev/null
            echo ""
            exit ( -1 )
        endif
        set toolchain_path = $toolchain_path:h
    end
    exit (-1)
endif

# Check C/C++ cross compilers to make sure they exist
# toolname1 == pre-r8b naming convention
# toolname2 == r8b and later
# dev_host will be set to the right name
if (-x $toolchain_path/$toolname1-g++) then
    set cplus = $toolname1-g++
    set ccomp = $toolname1-gcc
    set toolname = $toolname1
    set dev_host = linux-android
    setenv CPLUS $cplus
    if ( $verbose_output ) echo "CPLUS == $cplus"
else if (-x $toolchain_path/$toolname2-g++) then
    set cplus = $toolname2-g++
    set ccomp = $toolname2-gcc
    set toolname = $toolname2
    set dev_host = android-linux
    setenv CPLUS $cplus
    if ( $verbose_output ) echo "CPLUS == $cplus"
else
    echo "Error: C++ compiler not found: $toolchain_path/$cplus"
    echo "...CPLUS not changed..."
endif

if (-x $toolchain_path/$ccomp) then
    setenv CC    $ccomp
    if ( $verbose_output ) echo "CC == $ccomp"
else
    echo "Error: C compiler not found: $toolchain_path/$ccomp"
    echo "...CC not changed..."
endif

if ( $use_custom_toolchain == 0 ) then
    set sysroot_path = "$ndk_path/platforms/android-$abi/arch-$cpu"
    if (-d $sysroot_path) then
        setenv SYSROOT $sysroot_path
        if ( $verbose_output ) echo "SYSROOT == $sysroot_path"
    else
        echo "Error: Android Sysroot not found: $sysroot_path"
        echo "...Not setting SYSROOT..."
    endif
else
    if ( -d $tool_dir/sysroot ) then
        setenv SYSROOT $tool_dir/sysroot
        if ( $verbose_output ) echo "    standalone: SYSROOT == $SYSROOT"
    else
        echo "Custom toolchain does not include sysroot directory"
        exit (-1)
    endif
    if ( -e $SYSROOT/usr/include/android/api-level.h ) then
        set abi = ( `fgrep '__ANDROID_API__' $SYSROOT/usr/include/android/api-level.h` )
        set abi = $abi[3]
        if ( $verbose_output ) echo "    standalone: recovered abi == $abi"
    else
        echo "Could not find $SYSROOT/usr/include/android/api-level.h (for setting abi)"
        exit (-1)
    endif
endif

unsetenv CPLUS_INCLUDE_PATH
if ($use_custom_toolchain == 0) then 
    set libdir = gnu-libstdc++
    set include_paths = ( "$ndk_path/sources/cxx-stl/$libdir/include"           \
                          "$ndk_path/sources/cxx-stl/$libdir/$gcc/libs/$libabi/include"           \
                          "$ndk_path/sources/cxx-stl/$libdir/$gcc/include"           \
                          "$ndk_path/sources/cxx-stl/$libdir/$libdir"           \
                          "$ndk_path/sources/cxx-stl/$libdir/libs/$includeabi/include" \
                          )
    foreach include ($include_paths)
        if (-d $include) then
            if ($?CPLUS_INCLUDE_PATH) then
                setenv CPLUS_INCLUDE_PATH ${CPLUS_INCLUDE_PATH}:$include
            else
                setenv CPLUS_INCLUDE_PATH $include
            endif
        endif
    end
    #  the NDK architecture-specific include path must be defined.  Check it separately
    #  (to get the fenv_t type defined.)  This should be set by setting SYSROOT, but
    #  the gcc compiler is not currently honoring it.
    set NDK_ARCH_INC = ""
    set all_ndk_locs = ( \
         "${SYSROOT}/usr/include" \
         )
    foreach inc2 ($all_ndk_locs)
        if (-d $inc2) then
            set NDK_ARCH_INC = $inc2
            break
        endif
    end
    if ("$NDK_ARCH_INC" == "") then
        echo "  -- WARNING -- architecture-specific include subdirectory not found"
        echo "  -- WARNING -- checked $all_ndk_locs"
        exit (-1)
    else
        # shouldn't have to test CPLUS_INCLUDE_PATH; it should already have a value set.
        setenv CPLUS_INCLUDE_PATH ${CPLUS_INCLUDE_PATH}:$NDK_ARCH_INC
    endif
    if ($?CPLUS_INCLUDE_PATH) then
        if ( $verbose_output ) then
            echo "CPLUS_INCLUDE_PATH == $CPLUS_INCLUDE_PATH"
        endif
    else
        echo "Error: Cannot find paths to C++ includes: $include_paths $all_ndk_locs"
        echo "... CPLUS_INCLUDE_PATH not set ..." 
    endif
else
    if ( $verbose_output ) echo "    standalone: toolchain include path setup" 
    # compiler-specific include
    set all_ok = 1
    set d1 = "${tool_dir}/${arch}-${dev_host}/include/c++/${gcc}"
    if ( -d $d1 ) then
    else
        if ( $verbose_output ) echo " *** compiler-specific directory $d1 not found"
        set all_ok = 0
    endif
    # compiler/architecture-specific include directory
    set d2 = "${tool_dir}/${arch}-${dev_host}/include/c++/${gcc}/${arch}-${dev_host}"
    if ( -d $d2 ) then
    else
        if ( $verbose_output ) echo " *** compiler/architecture-specific directory $d2 not found"
        set all_ok = 0
    endif
    # system-specific include
    set d3 = "${SYSROOT}/usr/include"
    if ( -d $d3 ) then
    else
        if ( $verbose_output ) echo " *** SYSROOT include directory $d3 not found"
        set all_ok = 0
    endif
    if ( $all_ok == 1 ) then
        setenv CPLUS_INCLUDE_PATH "${d1}:${d2}:${d3}"
        if ( $verbose_output ) echo "    standalone: CPLUS_INCLUDE_PATH == $CPLUS_INCLUDE_PATH"
    else
        echo "    standalone: not all include paths found"
        exit (-1)
    endif
endif

unsetenv CPLUS_LIB_PATH
set library_path = ""
if ($use_custom_toolchain > 0) then 
    set library_path = ${tool_dir}/${arch}-${dev_host}/lib
    if ( $verbose_output ) echo "    standalone: library_path == $library_path"
else
    foreach libpath ( \
            $ndk_path/sources/cxx-stl/$libdir/libs/$libabi \
            $ndk_path/sources/cxx-stl/$libdir/$gcc/libs/$libabi \
            )
        if (-d $libpath ) then
            set library_path = "$libpath"
            if ( $verbose_output ) echo "    library_path == $library_path"
            break
        endif
    end

endif

# check that library_path contains the library
if ( "$library_path" == "" ) then
    echo "CPLUS_LIB_PATH not set"
else
    if (-d $library_path) then
        set nonomatch
        pushd $library_path > /dev/null
        set glib = *gnustl*.so
        popd > /dev/null
        unset nonomatch
        if ( "$glib" == '*gnustl*.so' ) then
            echo " *** Shared library directory $library_path does not contain a $lib library"
            exit (-1)
        endif
        if ($?CPLUS_LIB_PATH) then
            setenv CPLUS_LIB_PATH ${CPLUS_LIB_PATH}:${library_path}
        else
            setenv CPLUS_LIB_PATH ${library_path}
        endif
        if ( $verbose_output ) echo "CPLUS_LIB_PATH == $CPLUS_LIB_PATH"
    endif
endif

setenv tbb_tool_prefix ${toolname}-
if ( $verbose_output ) echo "tbb_tool_prefix == $tbb_tool_prefix"

# Set up "adb" for Android device
device_setup:
if ( $verbose_output ) echo ""
set device_list = `adb devices`;

if ("$device_list" !~ *$device*) then
    set adb_out = "`adb connect $device`"
    if ("$adb_out" =~ unable*) then
        echo ""
        echo "Error: $adb_out ... Exiting"
        adb devices
        echo ""
        exit(-1)
    endif
endif

if ("$device_list" =~ *$device$port:q*) set device = $device$port:q
if ( $verbose_output ) then
    echo "Connected to Android device ${device}..."
    adb devices
endif
setenv ANDROID_SERIAL $device
if ( $verbose_output ) echo "ANDROID_SERIAL = $ANDROID_SERIAL"
setenv SETUPARGS "source android_setup.csh $setup_args"
if ( $verbose_output ) echo "SETUPARGS == $SETUPARGS"
if ( $verbose_output ) echo ""
