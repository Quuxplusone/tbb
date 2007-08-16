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

#if _WIN32 || _WIN64
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdexcept>

#ifdef _USRDLL
#include "tbb/task_scheduler_init.h"

class CModel {
public:
  CModel(void) {};
  static tbb::task_scheduler_init tbb_init;

  void init_and_terminate( int );
};

tbb::task_scheduler_init CModel::tbb_init(1);

//! Test that task::initialize and task::terminate work when doing nothing else.
/** maxthread is treated as the "maximum" number of worker threads. */
void CModel::init_and_terminate( int maxthread ) {
    for( int i=0; i<200; ++i ) {
        switch( i&3 ) {
            default: {
                tbb::task_scheduler_init init( rand() % maxthread + 1 );
                break;
            }
            case 0: {
                tbb::task_scheduler_init init;
                break;
            }
            case 1: {
                tbb::task_scheduler_init init( tbb::task_scheduler_init::automatic );
                break;
            }
            case 2: {
                tbb::task_scheduler_init init( tbb::task_scheduler_init::deferred );
                init.initialize( rand() % maxthread + 1 );
                init.terminate();
                break;
            }
        }
    }
}

extern "C"
#if _WIN32 || _WIN64
__declspec(dllexport)
#endif
void plugin_call(int maxthread)
{
    srand(2);
    try {
        CModel model;
        model.init_and_terminate(maxthread);
    } catch( std::runtime_error& error ) {
        fprintf(stderr, "ERROR: %s\n", error.what());
    }
}

#else /* _USRDLL undefined */

#include "harness.h"

extern "C" void plugin_call(int);

void report_error_in(char* function_name)
{
#if _WIN32 || _WIN64
    char* message;
    int code = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, code,MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&message, 0, NULL );
#else
    char* message = dlerror();
    int code = 0;
#endif
    fprintf(stderr,
        "%s failed with error %d: %s\n",
        function_name, code, message);

#if _WIN32 || _WIN64
    LocalFree(message);
#endif
}

typedef void (*PLUGIN_CALL)(int);

int main(int argc, char* argv[])
{
  ParseCommandLine( argc, argv );

  PLUGIN_CALL plugin_call;
  
#if _WIN32 || _WIN64
  HMODULE hLib = LoadLibrary("test_model_plugin.dll");
  if (hLib==NULL){
    report_error_in("LoadLibrary");
    return -1;
  }
  plugin_call = (PLUGIN_CALL) GetProcAddress(hLib, "plugin_call");
  if (plugin_call==NULL) {
    report_error_in("GetProcAddress");
    return -1;
  }
#else
#if __APPLE__
  char *dllname = "test_model_plugin.dylib";
#else
  char *dllname = "test_model_plugin.so";
#endif
  void* hLib = dlopen( dllname, RTLD_LAZY ); 
  if (hLib==NULL){
    report_error_in("dlopen");
    return -1;
  }
  plugin_call = PLUGIN_CALL (dlsym(hLib, "plugin_call"));
  if (plugin_call==NULL) {
    report_error_in("dlsym");
    return -1;
  }
#endif

  plugin_call(MaxThread);

#if _WIN32 || _WIN64
  FreeLibrary(hLib);
#else
  dlclose(hLib);
#endif
  printf("done\n");
  return 0;
}

#endif
