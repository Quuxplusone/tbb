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

#include "tbb/task_scheduler_init.h"
#include "tbb/tick_count.h"
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cfloat>
#include <vector>
#include <algorithm>

#include "../src/test/harness.h"

#if  __linux__ || __APPLE__ || __FreeBSD__
    #include <sys/resource.h>
#endif /* __APPLE__ */

// The code, performance of which is to be measured, is surrounded by the StartSimpleTiming
// and StopSimpleTiming macros. It is called "target code" or "code of interest" hereafter.
//
// The target code is executed inside the nested loop. Nesting is necessary to allow
// measurements on arrays that fit cache of a particular level, while making the load
// big enough to eliminate the influence of random deviations.
//
// Macro StartSimpleTiming defines reduction variable "util::anchor", which may be modified (usually 
// by adding to) by the target code. This can be necessary to prevent optimizing compilers 
// from throwing out the code of interest. Besides, if the target code is complex enough, 
// make sure that all its branches contribute (directly or indirectly) to the value 
// being added to the "util::anchor" variable.
//
// To factor out overhead introduced by the measurement infra code it is recommended to make 
// a calibration run with target code replaced by a no-op (but still modifying "sum"), and
// store the resulting time in the "util::base" variable.
//
// A generally good approach is to make the target code use elements of a preliminary 
// initialized array. Then for calibration run you just need to add vector elements 
// to the "sum" variable. To get rid of memory access delays make the array small 
// enough to fit L2 or L1 cache (play with StartSimpleTiming arguments if necessary).
//
// Macro CalibrateSimpleTiming performs default calibration using "util::anchor += i;" operation.
//
// Macro ANCHOR_TYPE defines the type of the reduction variable. If it was not 
// defined  before including this header, it is defined as size_t. Depending on 
// the target code modern super scalar architectures may blend reduction operation
// and instructions of interest differently for different target alternatives. So
// you may play with the type to minimize out-of-order and parallel execution impact
// on the calibration time veracity. You may even end up with different reduction 
// variable types (and different calibration times) for different measurements.


namespace util {

typedef std::vector<tbb::tick_count>    cutoffs_t;

    double average ( const cutoffs_t& start, const cutoffs_t& end, 
                     double& variation_percent, double& std_dev_percent )
    {
        ASSERT (start.size() == end.size(), "");
        size_t  n = start.size();
        std::vector<double> t(n);
        for ( size_t i = 0; i < n; ++i )
            t[i] = (end[i] - start[i]).seconds();
        if ( n > 5 ) {
            t.erase(std::min_element(t.begin(), t.end()));
            t.erase(std::max_element(t.begin(), t.end()));
            n -= 2;
        }
        double  sum = 0,
                min_val = *std::min_element(t.begin(), t.end()),
                max_val = *std::max_element(t.begin(), t.end());
        for ( size_t i = 0; i < n; ++i )
            sum += t[i];
        double  avg = sum / n,
                std_dev = 0;
        for ( size_t i = 0; i < n; ++i ) {
            double    dev = fabs(t[i] - avg);
            std_dev += dev * dev;
        }
        std_dev = sqrt(std_dev / n);
        std_dev_percent = std_dev / avg * 100;
        variation_percent = 100 * (max_val - min_val) / max_val;
        return avg;
    }


    static double   base = 0,
                    base_dev = 0,
                    base_dev_percent = 0;

    static char *empty_fmt = "";

#if !defined(ANCHOR_TYPE)
    #define ANCHOR_TYPE size_t
#endif

    static ANCHOR_TYPE anchor = 0;


#define StartSimpleTiming(nOuter, nInner) {             \
    tbb::tick_count t1, t0 = tbb::tick_count::now();    \
    for ( size_t j = 0; l < nOuter; ++l ) {             \
        for ( size_t i = 0; i < nInner; ++i ) {

#define StopSimpleTiming(res)                   \
        }                                       \
        util::anchor += (ANCHOR_TYPE)l;         \
    }                                           \
    t1 = tbb::tick_count::now();                \
    printf (util::empty_fmt, util::anchor);     \
    res = (t1-t0).seconds() - util::base;       \
}

#define CalibrateSimpleTiming(T, nOuter, nInner)    \
    StartSimpleTiming(nOuter, nInner);              \
        util::anchor += (ANCHOR_TYPE)i;             \
    StopSimpleTiming(util::base);


#define StartTiming(nRuns, nOuter, nInner) {        \
    size_t n = nRuns + 2;                           \
    util::cutoffs_t  t1(n), t0(n);                  \
    for ( size_t k = 0; k < n; ++k )  {             \
        t0[k] = tbb::tick_count::now();             \
        for ( size_t l = 0; l < nOuter; ++l ) {     \
            for ( size_t i = 0; i < nInner; ++i ) {


#define StopTiming(Avg, StdDev, StdDevPercent)      \
            }                                       \
            util::anchor += (ANCHOR_TYPE)l;         \
        }                                           \
        t1[k] = tbb::tick_count::now();             \
    }                                               \
    printf (util::empty_fmt, util::anchor);                                 \
    Avg = util::average(t0, t1, StdDev, StdDevPercent);                     \
}

//ASSERT (util::base_dev <= StdDev , "Overhead deviation is too high" );

#define CalibrateTiming(nRuns, nOuter, nInner)      \
    StartTiming(nRuns, nOuter, nInner);             \
        util::anchor += (ANCHOR_TYPE)i;             \
    StopTiming(util::base, util::base_dev, util::base_dev_percent);

} // namespace util


#ifndef NRUNS
    #define NRUNS               7
#endif

#ifndef ONE_TEST_DURATION
    #define ONE_TEST_DURATION   0.01
#endif


inline 
void RunTest ( const char* title, void (*pfn)() ) {
    double  time = 0, variation = 0, deviation = 0;
    size_t nrep = 1;
    while (true) {
        CalibrateTiming(NRUNS, 1, nrep);
        StartTiming(NRUNS, 1, nrep);
        pfn();
        StopTiming(time, variation, deviation);
        time -= util::base;
        if ( time > 1e-6 )
            break;
        nrep *= 2;
    }
    
    nrep *= (size_t)ceil(ONE_TEST_DURATION/time);
    CalibrateTiming(NRUNS, 1, nrep);
    StartTiming(NRUNS, 1, nrep);
        pfn();
    StopTiming(time, variation, deviation);
    printf ("%-32s %.2e  %-9u %1.6f    %1.6f    %2.1f         %2.1f\n", title, 
            (time - util::base)/nrep, (unsigned)nrep, time - util::base, time, variation, deviation);
}

void Test();

inline
int test_main( int argc, char* argv[] ) {
    ParseCommandLine( argc, argv );
    ASSERT (MinThread>=2, "Minimal number of threads must be 2 or more");
// Boosting up test app priority improves results reproducibility (especially on Windows).
#if _WIN32 || _WIN64
    SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif (__linux__ || __FreeBSD__) && defined(_POSIX_PRIORITY_SCHEDULING)
    // Boosting up priority requires root privileges
    setpriority(PRIO_PROCESS, 0, PRIO_MIN);
    if ( errno && Verbose)
        perror("Failed to boost priority");
#else /* __APPLE__ */
    setpriority(PRIO_PROCESS, 0, PRIO_MIN);
    if ( errno && Verbose)
        perror("Failed to boost priority");
#endif /* OS */
    for ( int i = MinThread; i <= MaxThread; ++i ) {
        tbb::task_scheduler_init init (i);
        Test();
    }
    printf("done\n");
    return 0;
}
