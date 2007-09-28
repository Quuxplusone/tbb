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

#ifndef _TBB_tbb_misc_H
#define _TBB_tbb_misc_H

#include "tbb/tbb_stddef.h"
#include "tbb/tbb_machine.h"

#if __linux__
#include <sys/sysinfo.h>
#elif __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace tbb {

static volatile int number_of_workers = 0;

#if defined(__TBB_DetectNumberOfWorkers)
static inline int DetectNumberOfWorkers() {
    return __TBB_DetectNumberOfWorkers(); 
}
#else
#if _WIN32||_WIN64

static inline int DetectNumberOfWorkers() {
    if (!number_of_workers) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        number_of_workers = static_cast<int>(si.dwNumberOfProcessors);
    }
    return number_of_workers; 
}

#elif __linux__ 

static inline int DetectNumberOfWorkers( void ) {
    if (!number_of_workers) {
        number_of_workers = get_nprocs();
    }
    return number_of_workers; 
}

#elif __APPLE__

static inline int DetectNumberOfWorkers( void ) {
    if (!number_of_workers) {
        int name[2] = {CTL_HW, HW_AVAILCPU};
        int ncpu;
        size_t size = sizeof(ncpu);
        sysctl( name, 2, &ncpu, &size, NULL, 0 );
        number_of_workers = ncpu;
    }
    return number_of_workers; 
}

#else

#error Unknown OS

#endif /* os kind */

#endif

namespace internal {

// assertion_failure is declared in tbb/tbb_stddef.h because it user code
// needs to see its declaration.

//! Throw std::runtime_error of form "(what): (strerror of error_code)"
/* The "what" should be fairly short, not more than about 64 characters.
   Because we control all the call sites to handle_perror, it is pointless
   to bullet-proof it for very long strings.

   Design note: ADR put this routine off to the side in tbb_misc.cpp instead of
   Task.cpp because the throw generates a pathetic lot of code, and ADR wanted
   this large chunk of code to be placed on a cold page. */
void handle_perror( int error_code, const char* what );

//! True if environment variable with given name is set and not 0; otherwise false.
bool GetBoolEnvironmentVariable( const char * name );

//! Print TBB version information on stderr
void PrintVersion();

//! Print extra TBB version information on stderr
void PrintExtraVersionInfo( const char* category, const char* description );

//! Type definition for a pointer to a void somefunc(void)
typedef void (*PointerToHandler)();

//! The macro casts "address of a pointer to a function" to PointerToHandler*.
/** Need it because (PointerToHandler*)&ptr_to_func causes warnings from g++ 4.1 */
#define ADDRESS_OF_HANDLER(x) (PointerToHandler*)(void*)(x)

//! Association between a handler name and location of pointer to it.
struct DynamicLinkDescriptor {
    //! Name of the handler
    const char* name;
    //! Pointer to the handler
    PointerToHandler* handler;
};

//! Fill in dynamically linked handlers.
/** n is the length of array list[], must not exceed 4, which is all we currently need. 
    If the library and all of the handlers are found, then all corresponding handler pointers are set.
    Otherwise all corresponding handler pointers are untouched. */
bool FillDynamicLinks( const char* libraryname, const DynamicLinkDescriptor list[], size_t n );

//! Template functions to temporary add volatile attribute to a variable.
/** Allow to perform operations with volatile semantics on non-volatile variables
    which is useful to improve performance on IPF where Intel compiler
    translates volatile reads to "load with acquire semantics" (ld*.acq)
    and volatile writes to "store with release semantics" (st*.rel). */
template<typename T>
static inline T volatile& volatile_cast(T& location) {
    return const_cast<T volatile&>(location);
}

template<typename T>
static inline T const volatile& volatile_cast(T const& location) {
    return const_cast<T const volatile&>(location);
}

//! Class that implements exponential backoff.
/** See implementation of SpinwaitWhileEq for an example. */
class ExponentialBackoff {
    //! Time delay, in units of "pause" instructions. 
    /** Should be equal to approximately the number of "pause" instructions
        that take the same time as an context switch. */
    static const int LOOPS_BEFORE_YIELD = 0x10;
    int count;
public:
    ExponentialBackoff() : count(1) {}

    //! Pause for a while.
    void pause() {
        if( count<=LOOPS_BEFORE_YIELD ) {
            __TBB_Pause(count);
            // Pause twice as long the next time.
            count*=2;
        } else {
            // Pause is so long that we might as well yield CPU to scheduler.
            __TBB_Yield();
        }
    }
    void reset() {
        count = 1;
    }
};

//! Spin WHILE the value of the variable is equal to a given value
/** T and U should be comparable types. */
template<typename T, typename U>
static inline void SpinwaitWhileEq( const volatile T& location, U value ) {
    ExponentialBackoff backoff;
    while( location==value ) {
        backoff.pause();
    }
}

//! Spin UNTIL the value of the variable is equal to a given value
/** T and U should be comparable types. */
template<typename T, typename U>
static inline void SpinwaitUntilEq( const volatile T& location, const U value ) {
    ExponentialBackoff backoff;
    while( location!=value ) {
        backoff.pause();
    }
}

} // namespace internal

} // namespace tbb

#endif /* _TBB_tbb_misc_H */
