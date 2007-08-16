/*
    Copyright 2005-2007 Intel Corporation.  All Rights Reserved.

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

//! Wrapper around T where all members are private.
/** Used to prove that aligned_space<T,N> never calls member of T. */
template<typename T>
class Minimal {
    Minimal();
    Minimal( Minimal& min );
    ~Minimal();
    void operator=( const Minimal& );
#if __GNUC__
    /** If compiling with -Werror, GNU C++ issues an error if all constructors of 
        a class are private.  Therefore, we add a fake friend. */
    friend class FakeFriend;
#endif /* __GNUC__ */
};

#include "tbb/aligned_space.h"
#include "harness_assert.h"

template<typename U, size_t N>
void TestAlignedSpaceN() {
    typedef Minimal<U> T;
    tbb::aligned_space< T ,N> space;
    AssertSameType( static_cast< T *>(0), space.begin() );
    AssertSameType( static_cast< T *>(0), space.end() );
    ASSERT( reinterpret_cast<void *>(space.begin())==reinterpret_cast< void *>(&space), NULL );
    ASSERT( space.end()-space.begin()==N, NULL );
    ASSERT( reinterpret_cast<void *>(space.begin())>=reinterpret_cast< void *>(&space), NULL );
    ASSERT( space.end()<=reinterpret_cast< T *>(&space+1), NULL );
}

template<typename T>
void TestAlignedSpace() {
    TestAlignedSpaceN<T,1>();
    TestAlignedSpaceN<T,2>();
    TestAlignedSpaceN<T,3>();
    TestAlignedSpaceN<T,4>();
    TestAlignedSpaceN<T,5>();
    TestAlignedSpaceN<T,6>();
    TestAlignedSpaceN<T,7>();
    TestAlignedSpaceN<T,8>();
}
 
#include <stdio.h>
#define HARNESS_NO_PARSE_COMMAND_LINE 1
#include "harness.h"

int main() {
    TestAlignedSpace<char>();
    TestAlignedSpace<short>();
    TestAlignedSpace<int>();
    TestAlignedSpace<float>();
    TestAlignedSpace<double>();
    TestAlignedSpace<long double>();
    TestAlignedSpace<size_t>();
    printf("done\n");
    return 0;
}
