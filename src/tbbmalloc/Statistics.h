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

#define MAX_THREADS 1024
#define NUM_OF_BINS 30
#define ThreadCommonCounters NUM_OF_BINS

enum counter_type {
    allocBlockNew = 0,
    allocBlockPublic,
    allocBumpPtrUsed,
    allocFreeListUsed,
    allocPrivatized,
    examineEmptyEnough,
    examineNotEmpty,
    freeRestoreBumpPtr,
    freeByOtherThread,
    freeToActiveBlock,
    freeToInactiveBlock,
    freeBlockPublic,
    freeBlockBack,
    MaxCounters
};
enum common_counter_type {
    allocLargeSize = 0,
    freeLargeSize,
    lockPublicFreeList,
    freeToOtherThread
};

#if COLLECT_STATISTICS

struct bin_counters {
    int counter[MaxCounters];
};

static bin_counters statistic[MAX_THREADS][NUM_OF_BINS+1]; //zero-initialized;

static inline int STAT_increment(int thread, int bin, int ctr)
{
    return ++(statistic[thread][bin].counter[ctr]);
}
#else
#define STAT_increment(a,b,c) ((int)0)
#endif

static inline void STAT_print(int thread)
{
#if COLLECT_STATISTICS
    char filename[100];
    sprintf(filename, "stat_ScalableMalloc_thr%04d.log", thread);
    FILE* outfile = fopen(filename, "w");
    for(int i=0; i<NUM_OF_BINS; ++i)
    {
        bin_counters& ctrs = statistic[thread][i];
        fprintf(outfile, "Thr%04d Bin%02d", thread, i);
        fprintf(outfile, ": allocNewBlocks %5d", ctrs.counter[allocBlockNew]);
        fprintf(outfile, ", allocPublicBlocks %5d", ctrs.counter[allocBlockPublic]);
        fprintf(outfile, ", restoreBumpPtr %5d", ctrs.counter[freeRestoreBumpPtr]);
        fprintf(outfile, ", privatizeCalled %10d", ctrs.counter[allocPrivatized]);
        fprintf(outfile, ", emptyEnough %10d", ctrs.counter[examineEmptyEnough]);
        fprintf(outfile, ", notEmptyEnough %10d", ctrs.counter[examineNotEmpty]);
        fprintf(outfile, ", freeBlocksPublic %5d", ctrs.counter[freeBlockPublic]);
        fprintf(outfile, ", freeBlocksBack %5d", ctrs.counter[freeBlockBack]);
        fprintf(outfile, "\n");
    }
    for(int i=0; i<NUM_OF_BINS; ++i)
    {
        bin_counters& ctrs = statistic[thread][i];
        fprintf(outfile, "Thr%04d Bin%02d", thread, i);
        fprintf(outfile, ": allocBumpPtr %10d", ctrs.counter[allocBumpPtrUsed]);
        fprintf(outfile, ", allocFreeList %10d", ctrs.counter[allocFreeListUsed]);
        fprintf(outfile, ", freeToActiveBlk %10d", ctrs.counter[freeToActiveBlock]);
        fprintf(outfile, ", freeToInactive  %10d", ctrs.counter[freeToInactiveBlock]);
        fprintf(outfile, ", freedByOther %10d", ctrs.counter[freeByOtherThread]);
        fprintf(outfile, "\n");
    }
    bin_counters& ctrs = statistic[thread][ThreadCommonCounters];
    fprintf(outfile, "Thr%04d common counters", thread);
    fprintf(outfile, ": allocLargeObjects %5d", ctrs.counter[allocLargeSize]);
    fprintf(outfile, ", freeLargeObjects %5d", ctrs.counter[freeLargeSize]);
    fprintf(outfile, ", lockPublicFreeList %5d", ctrs.counter[lockPublicFreeList]);
    fprintf(outfile, ", freeToOtherThread %10d", ctrs.counter[freeToOtherThread]);
    fprintf(outfile, "\n");
    
    fclose(outfile);
#endif
}
