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

tbb_root?=.
include $(tbb_root)/build/common.inc
.PHONY: all tbb tbbmalloc test debug examples clean

all: release debug examples

tbb: tbb_release tbb_debug

tbbmalloc: tbbmalloc_release tbbmalloc_debug

test: tbbmalloc_test_release test_release tbbmalloc_test_debug test_debug

test_no_depends: tbbmalloc_test_release_no_depends test_release_no_depends tbbmalloc_test_debug_no_depends test_debug_no_depends

release: tbb_release tbbmalloc_release tbbmalloc_test_release test_release
 
debug: tbb_debug tbbmalloc_debug tbbmalloc_test_debug test_debug

examples: tbb tbbmalloc examples_debug clean_examples examples_release

clean: clean_release clean_debug clean_examples
	@echo clean done


.PHONY: tbb_release tbb_debug test_release test_debug

# do not delete double-space after -C option
tbb_release: mkdir_release
	$(MAKE) -C "$(work_dir)_release"  -r -f $(tbb_root)/build/Makefile.tbb cfg=release tbb_root=$(tbb_root)

tbb_debug: mkdir_debug
	$(MAKE) -C "$(work_dir)_debug"  -r -f $(tbb_root)/build/Makefile.tbb cfg=debug tbb_root=$(tbb_root)

test_release: mkdir_release tbb_release test_release_no_depends
test_release_no_depends: 
	-$(MAKE) -C "$(work_dir)_release"  -r -f $(tbb_root)/build/Makefile.test cfg=release tbb_root=$(tbb_root) 

test_debug: tbb_debug mkdir_debug test_debug_no_depends
test_debug_no_depends:
	-$(MAKE) -C "$(work_dir)_debug"  -r -f $(tbb_root)/build/Makefile.test cfg=debug tbb_root=$(tbb_root)

.PHONY: tbbmalloc_release tbbmalloc_debug tbbmalloc_test_release tbbmalloc_test_debug

tbbmalloc_release: mkdir_release
	$(MAKE) -C "$(work_dir)_release"  -r -f $(tbb_root)/build/Makefile.tbbmalloc cfg=release malloc tbb_root=$(tbb_root)

tbbmalloc_debug: mkdir_debug
	$(MAKE) -C "$(work_dir)_debug"  -r -f $(tbb_root)/build/Makefile.tbbmalloc cfg=debug malloc tbb_root=$(tbb_root)

tbbmalloc_test_release: tbb_release tbbmalloc_release mkdir_release tbbmalloc_test_release_no_depends
tbbmalloc_test_release_no_depends:
	-$(MAKE) -C "$(work_dir)_release"  -r -f $(tbb_root)/build/Makefile.tbbmalloc cfg=release malloc_test tbb_root=$(tbb_root)

tbbmalloc_test_debug: tbb_debug tbbmalloc_debug mkdir_debug tbbmalloc_test_debug_no_depends
tbbmalloc_test_debug_no_depends:
	-$(MAKE) -C "$(work_dir)_debug"  -r -f $(tbb_root)/build/Makefile.tbbmalloc cfg=debug malloc_test tbb_root=$(tbb_root)

.PHONY: examples_release examples_debug

examples_release: tbb_release tbbmalloc_release
	$(MAKE) -C examples -r -f Makefile tbb_root=.. release test

examples_debug: tbb_debug tbbmalloc_debug
	$(MAKE) -C examples -r -f Makefile tbb_root=.. debug test

.PHONY: clean_release clean_debug clean_examples

clean_release:
	$(shell $(RM) $(work_dir)_release$(SLASH)*.* $(NUL))
	$(shell $(RD) $(work_dir)_release $(NUL))

clean_debug:
	$(shell $(RM) $(work_dir)_debug$(SLASH)*.* $(NUL))
	$(shell $(RD) $(work_dir)_debug $(NUL))
	
clean_examples:
	$(shell $(MAKE) -s -i -r -C examples -f Makefile tbb_root=.. clean $(NUL))

.PHONY: mkdir_release mkdir_debug

mkdir_release:
	$(shell $(MD) "$(work_dir)_release" $(NUL))
	$(if $(subst undefined,,$(origin_build_dir)),,cd "$(work_dir)_release" && $(MAKE_TBBVARS) $(tbb_build_prefix)_release)

mkdir_debug:
	$(shell $(MD) "$(work_dir)_debug" $(NUL))
	$(if $(subst undefined,,$(origin_build_dir)),,cd "$(work_dir)_debug" && $(MAKE_TBBVARS) $(tbb_build_prefix)_debug)

info:
	@echo OS: $(tbb_os)
	@echo arch=$(arch)
	@echo compiler=$(compiler)
	@echo runtime=$(runtime)
	@echo tbb_build_prefix=$(tbb_build_prefix)
