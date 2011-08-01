/*
    Copyright 2005-2011 Intel Corporation.  All Rights Reserved.

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

#define TBB_PREVIEW_CONCURRENT_PRIORITY_QUEUE 1

#include <cstdio>
#include <vector>
#include <math.h>
#include "tbb/atomic.h"
#include "tbb/tick_count.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/task_group.h"
#include "tbb/concurrent_priority_queue.h"
#include "tbb/spin_mutex.h"
#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
#include "../../common/utility/utility.h"
#include "../../common/utility/fast_random.h"

#if defined(_MSC_VER) && defined(_Wp64)
    // Workaround for overzealous compiler warnings in /Wp64 mode
    #pragma warning (disable: 4267)
#endif /* _MSC_VER && _Wp64 */

#define __TBB_LAMBDAS_PRESENT  ( _MSC_VER >= 1600 && !__INTEL_COMPILER || __INTEL_COMPILER > 1100 && _TBB_CPP0X )

using namespace std;
using namespace tbb;

template <typename T>
struct point {
    T x;
    T y;
    point() : x(T()), y(T()) {}
    point(T _x, T _y) : x(_x), y(_y) {}
    point(const point<T>& _P) : x(_P.x), y(_P.y) {} 
};

template <typename T>
double get_distance(const point<T>& p1, const point<T>& p2) {
    double xdiff=p1.x-p2.x, ydiff=p1.y-p2.y;
    return sqrt(xdiff*xdiff + ydiff*ydiff);
}

// generates random points on 2D plane within a box of maxsize width & height
point<double> generate_random_point(utility::FastRandom& mr) {
    const size_t maxsize=500;
    double x = (double)(mr.get() % maxsize);
    double y = (double)(mr.get() % maxsize);
    return point<double>(x,y);
}

// weighted toss makes closer nodes (in the point vector) heavily connected
bool die_toss(size_t a, size_t b, utility::FastRandom& mr) {
    int node_diff = std::abs((int)(a-b));
    // near nodes
    if (node_diff < 16) return true;
    // mid nodes
    if (node_diff < 64) return ((int)mr.get() % 8 == 0);
    // far nodes
    if (node_diff < 512) return ((int)mr.get() % 16 == 0);
    return false;
}

typedef point<double> point_t;
typedef vector<point_t> pointVec_t;
typedef size_t point_id;
typedef std::pair<point_id,double> point_rec;
typedef vector<vector<point_id> > idMap_t;

bool verbose = false;          // prints bin details and other diagnostics to screen
bool silent = false;           // suppress all output except for time
size_t N = 1000;               // number of nodes
size_t src = 0;          // start of path
size_t dst = N-1;          // end of path
double INF=100000.0;           // infinity
size_t grainsize = 16;         // number of nodes per task on average
size_t max_spawn;              // max tasks to spawn
atomic<size_t> num_spawn;      // number of active tasks

pointVec_t nodes;              // nodes
idMap_t edges;                 // edges
vector<point_id> predecessor;  // for recreating path from src to dst

vector<double> f_distance;     // estimated distances at particular point
vector<double> g_distance;     // current shortest distances from src point
vector<spin_mutex> lock;       // a lock for each point
task_group *g;                 // task group for tasks executing sub-problems

class compare_f {
public:
    bool operator()(const point_rec p1, const point_rec p2) const {
        return p1.second>p2.second;
    }
};

concurrent_priority_queue<point_rec, compare_f> open_set; // tentative nodes

void shortpath_helper();

#if !__TBB_LAMBDAS_PRESENT
class shortpath_helper_functor {
public:
    shortpath_helper_functor() {};
    void operator() () const { shortpath_helper(); }
};
#endif

void shortpath() {
    g_distance[src] = 0.0; // src's distance from src is zero
    f_distance[src] = get_distance(nodes[src], nodes[dst]); // estimate distance from start to end
    open_set.push(make_pair(src,f_distance[src])); // push src into open_set
#if __TBB_LAMBDAS_PRESENT    
    g->run([](){ shortpath_helper(); });
#else
    g->run( shortpath_helper_functor() );
#endif
    g->wait();
}

void shortpath_helper() {
    point_rec x_rec;
    while (open_set.try_pop(x_rec)) {
        point_id x = x_rec.first;
        if (x==dst) continue;
        double f = x_rec.second;
        if (f > f_distance[x]) continue; // prune search space
        for (size_t i=0; i<edges[x].size(); ++i) {
            point_id y = edges[x][i];
            double new_g_y = g_distance[x] + get_distance(nodes[x], nodes[y]);
            // the push flag lets us move some work out of the critical section below
            bool push = false;
            {
                spin_mutex::scoped_lock l(lock[y]);
                if (new_g_y < g_distance[y]) {
                    predecessor[y] = x;
                    g_distance[y] = new_g_y;
                    f_distance[y] = g_distance[y] + get_distance(nodes[y], nodes[dst]);
                    push = true;
                }
            }
            if (push) {
                open_set.push(make_pair(y,f_distance[y]));
                size_t n_spawn = ++num_spawn;
                if (n_spawn < max_spawn) {
#if __TBB_LAMBDAS_PRESENT    
                    g->run([]{ shortpath_helper(); });
#else
                    g->run( shortpath_helper_functor() );
#endif                
                }
                else --num_spawn;
            }
        }
    }
    --num_spawn;
}

void make_path(point_id src, point_id dst, vector<point_id>& path) {
    point_id at = predecessor[dst];
    if (at == N) path.push_back(src);
    else if (at == src) { path.push_back(src); path.push_back(dst); }
    else { make_path(src, at, path); path.push_back(dst); }
}

void print_path() {
    vector<point_id> path;
    double path_length=0.0;
    make_path(src, dst, path);
    if (verbose) printf("\n      ");
    for (size_t i=0; i<path.size(); ++i) {
        if (path[i] != dst) {
            double seg_length = get_distance(nodes[path[i]], nodes[path[i+1]]);
            if (verbose) printf("%6.1f       ", seg_length);
            path_length += seg_length;
        }
        else if (verbose) printf("\n");
    }
    if (verbose) {
        for (size_t i=0; i<path.size(); ++i) {
            if (path[i] != dst) printf("(%4d)------>", (int)path[i]);
            else printf("(%4d)\n", (int)path[i]);
        }
    }
    if (verbose) printf("Total distance = %5.1f\n", path_length);
    else if (!silent) printf(" %5.1f\n", path_length);
}

int get_default_num_threads() {
    static int threads = 0;
    if (threads == 0)
        threads = tbb::task_scheduler_init::default_num_threads();
    return threads;
}

#if !__TBB_LAMBDAS_PRESENT
class gen_nodes {
public: 
    gen_nodes() {}
    void operator() (blocked_range<size_t>& r) const {
        utility::FastRandom my_random((unsigned int)r.begin());
        for (size_t i=r.begin(); i!=r.end(); ++i) {
            nodes[i] = generate_random_point(my_random);
        }
    }
};

class gen_edges {
public: 
    gen_edges() {}
    void operator() (blocked_range<size_t>& r) const {
        utility::FastRandom my_random((unsigned int)r.begin());
        for (size_t i=r.begin(); i!=r.end(); ++i) {
            for (size_t j=0; j<i; ++j) {
                if (die_toss(i, j, my_random))
                    edges[i].push_back(j);
            }
        }
    }
};

class reset_nodes {
public: 
    reset_nodes() {}
    void operator() (blocked_range<size_t>& r) const {
        for (size_t i=r.begin(); i!=r.end(); ++i) {
            f_distance[i] = g_distance[i] = INF;
            predecessor[i] = N;
        }
    }
};
#endif

void InitializeGraph() {
    g = new task_group;
    nodes.resize(N);
    edges.resize(N);
    predecessor.resize(N);
    g_distance.resize(N);
    f_distance.resize(N);
    lock.resize(N);
    task_scheduler_init init(get_default_num_threads());
    if (verbose) printf("Generating nodes...\n");
#if __TBB_LAMBDAS_PRESENT
    parallel_for(blocked_range<size_t>(0,N,64), 
                 [&](blocked_range<size_t>& r) {
                     utility::FastRandom my_random(r.begin());
                     for (size_t i=r.begin(); i!=r.end(); ++i) {
                         nodes[i] = generate_random_point(my_random);
                     }
                 }, simple_partitioner());
#else
    parallel_for(blocked_range<size_t>(0,N,64), gen_nodes(), simple_partitioner());
#endif
    if (verbose) printf("Generating edges...\n");
#if __TBB_LAMBDAS_PRESENT
    parallel_for(blocked_range<size_t>(0,N,64), 
                 [&](blocked_range<size_t>& r) {
                     utility::FastRandom my_random(r.begin());
                     for (size_t i=r.begin(); i!=r.end(); ++i) {
                         for (size_t j=0; j<i; ++j) {
                             if (die_toss(i, j, my_random))
                                 edges[i].push_back(j);
                         }
                     }
                 }, simple_partitioner());
#else
    parallel_for(blocked_range<size_t>(0,N,64), gen_edges(), simple_partitioner());
#endif
    for (size_t i=0; i<N; ++i) {
        for (size_t j=0; j<edges[i].size(); ++j) {
            point_id k = edges[i][j];
            edges[k].push_back(i);
        }
    }
    if (verbose) printf("Done.\n");
}

void ResetGraph() {
    task_scheduler_init init(get_default_num_threads());
#if __TBB_LAMBDAS_PRESENT
    parallel_for(blocked_range<size_t>(0,N), 
                 [&](blocked_range<size_t>& r) {
                     for (size_t i=r.begin(); i!=r.end(); ++i) {
                         f_distance[i] = g_distance[i] = INF;
                         predecessor[i] = N;
                     }
                 });
#else
    parallel_for(blocked_range<size_t>(0,N), reset_nodes());
#endif
}

int main(int argc, char *argv[]) {
    try {
        utility::thread_number_range threads(get_default_num_threads);
        utility::parse_cli_arguments(argc, argv,
                                     utility::cli_argument_pack()
                                     //"-h" option for for displaying help is present implicitly
                                     .positional_arg(threads,"#threads","  number of threads to use; a range of the "
                                                     "form low[:high]\n              where low and optional high are "
                                                     "non-negative integers,\n              or 'auto' for the TBB "
                                                     "default")
                                     .arg(verbose,"verbose","   print diagnostic output to screen")
                                     .arg(silent,"silent","    limits output to timing info; overrides verbose")
                                     .arg(N,"N","         number of nodes")
                                     .arg(src,"start","      start of path")
                                     .arg(dst,"end","        end of path")
        );
        if (silent) verbose = false;  // make silent override verbose
        else
            printf("shortpath will run with %d nodes to find shortest path between nodes"
                   " %d and %d using %d:%d threads.\n", 
                   (int)N, (int)src, (int)dst, (int)threads.first, (int)threads.last);
  
        if (dst >= N) { 
            if (verbose) 
                printf("end value %d is invalid for %d nodes; correcting to %d\n", (int)dst, (int)N, (int)N-1);
            dst = N-1;
        }
        
        num_spawn = 0;
        max_spawn = N/grainsize;
        tick_count t0, t1;
        InitializeGraph();
        for (int n_thr=threads.first; n_thr<=threads.last; ++n_thr) {
            ResetGraph();
            task_scheduler_init init((size_t)n_thr);
            t0 = tick_count::now();
            shortpath();
            t1 = tick_count::now();
            if (!silent) {
                if (predecessor[dst] != N) {
                    printf("%d threads: [%6.6f] The shortest path from point %d to point %d is:", 
                           (int)n_thr, (t1-t0).seconds(), (int)src, (int)dst);
                    print_path();
                }
                else {
                    printf("%d threads: [%6.6f] There is no path from point %d to point %d\n", 
                           (int)n_thr, (t1-t0).seconds(), (int)src, (int)dst);
                }
            } else
                utility::report_elapsed_time((t1-t0).seconds());
        }
        return 0;
    } catch(std::exception& e) {
        cerr<<"error occurred. error text is :\"" <<e.what()<<"\"\n";
        return 1;
    }
}
