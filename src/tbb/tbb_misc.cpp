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

// Source file for miscellanous entities that are infrequently referenced by 
// an executing program.

#include "tbb/tbb_stddef.h"
#include "tbb_misc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#include <stdexcept>
#endif
#if _WIN32||_WIN64
#include <crtdbg.h>
#else
#include <dlfcn.h>
#endif 

#include "tbb/tbb_machine.h"

namespace tbb {

void assertion_failure( const char * filename, int line, const char * expression, const char * comment ) {
    static bool already_failed;
    if( !already_failed ) {
        already_failed = true;
        fprintf( stderr, "Assertion %s failed on line %d of file %s\n",
                 expression, line, filename );
        if( comment )
            fprintf( stderr, "Detailed description: %s\n", comment );
#if (_WIN32||_WIN64) && defined(_DEBUG)
        if(1 == _CrtDbgReport(_CRT_ASSERT, filename, line, "tbb_debug.dll", "%s\r\n%s", expression, comment?comment:""))
        	_CrtDbgBreak();
#else
        abort();
#endif
    }
}

namespace internal {

size_t get_initial_auto_partitioner_divisor() {
  const size_t X_FACTOR = 4;
  return X_FACTOR * DetectNumberOfWorkers();
}

#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
// The above preprocessor symbols are defined by compilers when exception handling is enabled.
// However, in some cases it could be disabled for this file.

void handle_perror( int error_code, const char* what ) {
    char buf[128];
    sprintf(buf,"%s: ",what);
    char* end = strchr(buf,0);
    size_t n = buf+sizeof(buf)-end;
    strncpy( end, strerror( error_code ), n );
    // Ensure that buffer ends in terminator.
    buf[sizeof(buf)-1] = 0; 
    throw std::runtime_error(buf);
}
#endif //__EXCEPTIONS || _CPPUNWIND

bool GetBoolEnvironmentVariable( const char * name ) {
    if( const char* s = getenv(name) )
        return strcmp(s,"0");
    return false;
}

bool FillDynamicLinks( const char* library, const DynamicLinkDescriptor list[], size_t n ) {
    const size_t max_n = 5;
    __TBB_ASSERT( 0<n && n<=max_n, NULL );
#if _WIN32||_WIN64
    HMODULE module = LoadLibrary( library );
#else
    void* module = dlopen( library, RTLD_LAZY ); 
#endif /* _WIN32||_WIN64 */
    size_t count = 0;
    if( module ) {
        // The library is there, so get the entry points.
        PointerToHandler h[max_n];
        for( size_t k=0; k<n; ++k ) {
#if _WIN32||_WIN64
            h[k] = (PointerToHandler) GetProcAddress( module, list[k].name );
#else
            h[k] = (PointerToHandler) dlsym( module, list[k].name );
#endif /* _WIN32||_WIN64 */
            count += h[k]!=NULL;
        }
        // Commit the entry points if they are all present.
        if( count==n ) {
            // Cannot use memset here, because the writes must be atomic.
            for( size_t k=0; k<n; ++k )
                *list[k].handler = h[k];
        }
    }
    return count==n;
}

#include "tbb_version.h"

/** The leading "\0" is here so that applying "strings" to the binary delivers a clean result. */
static const char VersionString[] = "\0" TBB_VERSION_STRINGS;

static bool PrintVersionFlag = false;

void PrintVersion() {
    PrintVersionFlag = true;
    fputs(VersionString+1,stderr);
}

void PrintExtraVersionInfo( const char* category, const char* description ) {
    if( PrintVersionFlag ) 
        fprintf(stderr, "%s: %s\t%s\n", "TBB", category, description );
}

} // namespace internal
 
} // namespace tbb

