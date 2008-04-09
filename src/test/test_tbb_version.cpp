/*
    Copyright 2005-2008 Intel Corporation.  All Rights Reserved.

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

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>

#define HARNESS_NO_PARSE_COMMAND_LINE 1
#include "tbb/task_scheduler_init.h"
#include "harness.h"

#if defined (_WIN32) || defined (_WIN64)
#define TEST_SYSTEM_COMMAND "test_tbb_version.exe 1"
#define putenv _putenv
#else
#define TEST_SYSTEM_COMMAND "./test_tbb_version.exe 1"
#endif

void initialize_strings_vector(std::vector <std::string>* vector);
const char stderr_stream[] = "version_test.err";
const char stdout_stream[] = "version_test.out";

int main(int argc, char **argv)

{
    try{
        FILE *stream_out;
        FILE *stream_err;   
        char psBuffer[512];
        
        if(argc>1) {
            stream_err = freopen( stderr_stream, "w", stderr );
            if( stream_err == NULL ){
                fprintf( stderr, "Internal test error (freopen)\n" );
                exit( 1 );
            }
            stream_out = freopen( stdout_stream, "w", stdout );
            if( stream_out == NULL ){
                fprintf( stderr, "Internal test error (freopen)\n" );
                exit( 1 );
            }
            {
                tbb::task_scheduler_init init(1);
            }
            fclose( stream_out );
            fclose( stream_err );
            exit(0);
        }
        //1st step check that output is empty if TBB_VERSION is not defined.
        if ( getenv("TBB_VERSION") ){
            printf( "TBB_VERSION defined, skipping step 1 (empty output check)\n" );
        }else{
            if( ( system(TEST_SYSTEM_COMMAND) ) != 0 ){
                fprintf( stderr, "Error (step 1): Internal test error\n" );
                exit( 1 );
            }
            //Checking output streams - they should be empty
            stream_err = fopen( stderr_stream, "r" );
            if( stream_err == NULL ){
                fprintf( stderr, "Error (step 1):Internal test error (stderr open)\n" );
                exit( 1 );
            }
            while( !feof( stream_err ) ) {
                if( fgets( psBuffer, 512, stream_err ) != NULL ){
                    fprintf( stderr, "Error (step 1): stderr should be empty\n" );
                    exit( 1 );
                }
            }
            fclose( stream_err );
            stream_out = fopen( stdout_stream, "r" );
            if( stream_out == NULL ){
                fprintf( stderr, "Error (step 1):Internal test error (stdout open)\n" );
                exit( 1 );
            }
            while( !feof( stream_out ) ) {
                if( fgets( psBuffer, 512, stream_out ) != NULL ){
                    fprintf( stderr, "Error (step 1): stdout should be empty\n" );
                    exit( 1 );
                }
            }
            fclose( stream_out );
        }

        //Setting TBB_VERSION in case it is not set
        if ( !getenv("TBB_VERSION") ){
            putenv(const_cast<char*>("TBB_VERSION=1"));
        }

        if( ( system(TEST_SYSTEM_COMMAND) ) != 0 ){
            fprintf( stderr, "Error (step 2):Internal test error\n" );
            exit( 1 );
        }
        //Checking pipe - it should contain version data
        std::vector <std::string> strings_vector;
        std::vector <std::string>::iterator strings_iterator;

        initialize_strings_vector( &strings_vector );
        strings_iterator = strings_vector.begin();

        stream_out = fopen( stdout_stream, "r" );
        if( stream_out == NULL ){
            fprintf( stderr, "Error (step 2):Internal test error (stdout open)\n" );
            exit( 1 );
        }
        while( !feof( stream_out ) ) {
            if( fgets( psBuffer, 512, stream_out ) != NULL ){
                fprintf( stderr, "Error (step 2): stdout should be empty\n" );
                exit( 1 );
            }
        }
        fclose( stream_out );

        stream_err = fopen( stderr_stream, "r" );
        if( stream_err == NULL ){
            fprintf( stderr, "Error (step 1):Internal test error (stderr open)\n" );
            exit( 1 );
        }
        while( !feof( stream_err ) ) {
            if( fgets( psBuffer, 512, stream_err ) != NULL ){
                if ( strings_iterator == strings_vector.end() ){
                    fprintf( stderr, "Error: problem with version dictionary.\n" );
                    exit( 1 );
                }
                if ( strstr( psBuffer, strings_iterator->c_str() ) == NULL ){
                    fprintf( stderr, "Error: version strings do not match.\n" );
                    printf( psBuffer );
                    exit( 1 );
                }
                if ( strings_iterator != strings_vector.end() ) strings_iterator ++;
            }
        }
        fclose( stream_err );
    } catch(...) {
        ASSERT(0,"unexpected exception");
    }
    printf("done\n");
    return 0;
}


// Fill dictionary with version strings for platforms 
void initialize_strings_vector(std::vector <std::string>* vector)
{
    vector->push_back("TBB: VERSION\t\t2.0");
    vector->push_back("TBB: INTERFACE VERSION\t3010");
    vector->push_back("TBB: BUILD_DATE");
    vector->push_back("TBB: BUILD_HOST");
#if _WIN32||_WIN64
    vector->push_back("TBB: BUILD_OS");
    vector->push_back("TBB: BUILD_CL");
    vector->push_back("TBB: BUILD_COMPILER");
#elif __APPLE__
    vector->push_back("TBB: BUILD_KERNEL");
    vector->push_back("TBB: BUILD_GCC");
    if( getenv("COMPILER_VERSION") ) {
		vector->push_back("TBB: BUILD_COMPILER");
    }
    vector->push_back("TBB: BUILD_LD");
//#elif __linux__ //We use version_info_linux.sh for unsupported OSes
#elif __APPLE__
    vector->push_back("TBB: BUILD_KERNEL");
    vector->push_back("TBB: BUILD_GCC");
    if( getenv("COMPILER_VERSION") ) {
		vector->push_back("TBB: BUILD_COMPILER");
    }
    vector->push_back("TBB: BUILD_LD");
//#elif __linux__ //We use version_info_linux.sh for unsupported OSes
#elif __sun
    vector->push_back("TBB: BUILD_OS");
    vector->push_back("TBB: BUILD_KERNEL");
    vector->push_back("TBB: BUILD_SUNCC");
    if( getenv("COMPILER_VERSION") ) {
		vector->push_back("TBB: BUILD_COMPILER");
    }
//#elif __linux__ //We use version_info_linux.sh for unsupported OSes
#else
    vector->push_back("TBB: BUILD_OS");
    vector->push_back("TBB: BUILD_KERNEL");
    vector->push_back("TBB: BUILD_GCC");
    if( getenv("COMPILER_VERSION") ) {
		vector->push_back("TBB: BUILD_COMPILER");
    }
    vector->push_back("TBB: BUILD_GLIBC");
    vector->push_back("TBB: BUILD_LD");

#endif
    vector->push_back("TBB: BUILD_TARGET");
    vector->push_back("TBB: BUILD_COMMAND");
    vector->push_back("TBB: TBB_DO_ASSERT");
    vector->push_back("TBB: DO_ITT_NOTIFY");
#if !(__APPLE__)
    vector->push_back("TBB: ITT");
#endif
    vector->push_back("TBB: ALLOCATOR");
    vector->push_back("TBB: SCHEDULER");
#if !(__APPLE__)
    vector->push_back("TBB: ITT");
#endif
    return;
}
