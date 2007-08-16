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

//#define INSTANTIATE_ITT_NOTIFY 1
//#include "tbb/itt_notify.h"
#include "Customize.h"

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI DllMain( HINSTANCE hInst, DWORD callReason, LPVOID )
{
    if (callReason==DLL_THREAD_DETACH)
    {
        mallocThreadShutdownNotification(NULL);
    }
    else if (callReason==DLL_PROCESS_DETACH)
    {
        mallocProcessShutdownNotification();
    }
    return TRUE;
}
#endif //_WIN32
