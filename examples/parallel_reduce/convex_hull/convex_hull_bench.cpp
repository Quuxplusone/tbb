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

/*
    This file contains a few implementations, so it may look overly complicated.
    The most efficient implementation is also separated into convex_hull_sample.cpp
*/
#include <cassert>
#include "convex_hull.h"

typedef util::point<double> point_t;

#define USETBB      1
#define USECONCVEC  1
#define INIT_ONCE   1

#if !USETBB // Serial implementation of Quick Hull algorithm

typedef std::vector< point_t > pointVec_t;

void serial_initialize(pointVec_t &points);

// C++ style serial code

class FindXExtremum : public std::unary_function<const point_t&, void> {
public:
    typedef enum {
        minX, maxX
    } extremumType;

    FindXExtremum(const point_t& frstPoint, extremumType exType_)
        : extrXPoint(frstPoint), exType(exType_) {}

    void operator()(const point_t& p) {
        if(closerToExtremum(p))
            extrXPoint = p;
    }

    operator point_t () {
        return extrXPoint;
    }

private:
    const extremumType   exType;
    point_t              extrXPoint;

    bool closerToExtremum(const point_t &p) const {
        switch(exType) {
        case minX:
            return p.x<extrXPoint.x; break;
        case maxX:
            return p.x>extrXPoint.x; break;
        }
        return false; // avoid warning
    }
};

template <FindXExtremum::extremumType type>
point_t extremum(const pointVec_t &points) {
    assert(!points.empty());
    return std::for_each(points.begin(), points.end(), FindXExtremum(points[0], type));
}

class SplitByCP : public std::unary_function<const point_t&, void> {
    pointVec_t          &reducedSet;
    point_t              p1, p2;
    point_t              farPoint;
    double               howFar;
public:

    SplitByCP( point_t _p1, point_t _p2, pointVec_t &_reducedSet)
        : p1(_p1), p2(_p2), reducedSet(_reducedSet), howFar(0), farPoint(p1) {}

    void operator()(const point_t& p) {
        double cp;
        if( (p != p1) && (p != p2) ) {
            cp = util::cross_product(p1, p2, p);
            if(cp>0) {
                reducedSet.push_back(p);
                if(cp>howFar) {
                    farPoint = p;
                    howFar   = cp;
                }
            }
        }
    }

    operator point_t (){
        return farPoint;
    }
};

point_t divide(const pointVec_t &P, pointVec_t &P_reduced, const point_t &p1, const point_t &p2) {
    SplitByCP splitByCP(p1, p2, P_reduced);
    point_t farPoint = std::for_each(P.begin(), P.end(), splitByCP);

    if(util::VERBOSE) {
        std::stringstream ss;
        ss << P.size() << " nodes in bucket"<< ", "
            << "dividing by: [ " << p1 << ", " << p2 << " ], "
            << "farthest node: " << farPoint;
        util::OUTPUT.push_back(ss.str());
    }

    return farPoint;
}

void divide_and_conquer(const pointVec_t &P, pointVec_t &H, point_t p1, point_t p2) {
    assert(P.size() >= 2);
    pointVec_t P_reduced;
    pointVec_t H1, H2;
    point_t p_far = divide(P, P_reduced, p1, p2);
    if (P_reduced.size()<2) {
        H.push_back(p1);
        H.insert(H.end(), P_reduced.begin(), P_reduced.end());
    }
    else {
        divide_and_conquer(P_reduced, H1, p1, p_far);
        divide_and_conquer(P_reduced, H2, p_far, p2);

        H.insert(H.end(), H1.begin(), H1.end());
        H.insert(H.end(), H2.begin(), H2.end());
    }
}

void quickhull(const pointVec_t &points, pointVec_t &hull) {
    if (points.size() < 2) {
        hull.insert(hull.end(), points.begin(), points.end());
        return;
    }
    point_t p_maxx = extremum<FindXExtremum::maxX>(points);
    point_t p_minx = extremum<FindXExtremum::minX>(points);

    pointVec_t H;

    divide_and_conquer(points, hull, p_maxx, p_minx);
    divide_and_conquer(points, H, p_minx, p_maxx);
    hull.insert(hull.end(), H.begin(), H.end());
}


int main(int argc, char* argv[]) {
    util::ParseInputArgs(argc, argv);

    pointVec_t      points;
    pointVec_t      hull;
    util::my_time_t tm_init, tm_start, tm_end;

    std::cout << "Starting serial version of QUICK HULL algorithm" << std::endl;

    tm_init = util::gettime();
    serial_initialize(points);
    tm_start = util::gettime();
    std::cout << "Init time: " << util::time_diff(tm_init, tm_start) << "  Points in input: " << points.size() << "\n";
    tm_start = util::gettime();
    quickhull(points, hull);
    tm_end = util::gettime();
    std::cout << "Serial time: " << util::time_diff(tm_start, tm_end) << "  Points in hull: " << hull.size() << "\n";
}

#else // USETBB - parallel version of Quick Hull algorithm

#include "tbb/task_scheduler_init.h"
#include "tbb/parallel_for.h"
#include "tbb/parallel_reduce.h"
#include "tbb/blocked_range.h"

typedef tbb::blocked_range<size_t> range_t;

#if USECONCVEC
#include "tbb/concurrent_vector.h"

typedef tbb::concurrent_vector<point_t> pointVec_t;

void appendVector(const point_t* src, size_t srcSize, pointVec_t& dest) {
    std::copy(src, src + srcSize, dest.grow_by(srcSize));
}

void appendVector(const pointVec_t& src, pointVec_t& dest) {
    std::copy(src.begin(), src.end(), dest.grow_by(src.size()));
}

#else // USE STD::VECTOR - include spin_mutex.h and lock vector operations
#include "tbb/spin_mutex.h"

typedef tbb::spin_mutex      mutex_t;
typedef std::vector<point_t> pointVec_t;

void appendVector(mutex_t& insertMutex, const pointVec_t& src, pointVec_t& dest) {
    mutex_t::scoped_lock lock(insertMutex);
    dest.insert(dest.end(), src.begin(), src.end());
}

void appendVector(mutex_t& insertMutex, const point_t* src, size_t srcSize,
                  pointVec_t& dest) {
    mutex_t::scoped_lock lock(insertMutex);
    dest.insert(dest.end(), src, src + srcSize);
}

#endif // USECONCVEC

void serial_initialize(pointVec_t &points);

class FillRNDPointsVector {
    pointVec_t          &points;
    mutable unsigned int rseed;
public:
    static const size_t  grainSize = cfg::GENERATE_GS;
#if !USECONCVEC
    static mutex_t       pushBackMutex;
#endif // USECONCVEC
    FillRNDPointsVector(pointVec_t& _points)
        : points(_points), rseed(1) {}

    FillRNDPointsVector(const FillRNDPointsVector& other)
        : points(other.points), rseed(other.rseed+1) {}

    void operator()(const range_t& range) const {
        const size_t i_end = range.end();
        size_t count = 0;
        for(size_t i = range.begin(); i != i_end; ++i) {
#if USECONCVEC
            points.push_back(util::GenerateRNDPoint<double>(count, rseed));
#else // Locked push_back to a not thread-safe STD::VECTOR
            {
                mutex_t::scoped_lock lock(pushBackMutex);
                points.push_back(util::GenerateRNDPoint<double>(count, rseed));
            }
#endif // USECONCVEC
        }
    }
};

class FillRNDPointsVector_buf {
    pointVec_t          &points;
    mutable unsigned int rseed;
public:
    static const size_t  grainSize = cfg::GENERATE_GS;
#if !USECONCVEC
    static mutex_t       insertMutex;
#endif // USECONCVEC

    FillRNDPointsVector_buf(pointVec_t& _points)
        : points(_points), rseed(1) {}

    FillRNDPointsVector_buf(const FillRNDPointsVector_buf& other)
        : points(other.points), rseed(other.rseed+1) {}

    void operator()(const range_t& range) const {
        const size_t i_end = range.end();
        size_t count = 0, j = 0;
        point_t tmp_vec[grainSize];
        for(size_t i=range.begin(); i!=i_end; ++i) {
            tmp_vec[j++] = util::GenerateRNDPoint<double>(count, rseed);
        }
#if USECONCVEC
        appendVector(tmp_vec, j, points);
#else // USE STD::VECTOR
        appendVector(insertMutex, tmp_vec, j, points);
#endif // USECONCVEC
    }   
};

#if !USECONCVEC
mutex_t FillRNDPointsVector::pushBackMutex   = mutex_t();
mutex_t FillRNDPointsVector_buf::insertMutex = mutex_t();
#endif

template<typename BodyType>
void initialize(pointVec_t &points) {
    // In the buffered version, a temporary storage for as much as grainSize elements 
    // is allocated inside the body. Since auto_partitioner may increase effective
    // range size which would cause a crash, simple partitioner has to be used.

    tbb::parallel_for(range_t(0, cfg::MAXPOINTS, BodyType::grainSize),
    BodyType(points), tbb::simple_partitioner());
}

class FindXExtremum {
public:
    typedef enum {
        minX, maxX
    } extremumType;

    static const size_t  grainSize = cfg::FINDEXT_GS;

    FindXExtremum(const pointVec_t& points_, extremumType exType_)
        : points(points_), exType(exType_), extrXPoint(points[0]) {}

    FindXExtremum(const FindXExtremum& fxex, tbb::split)
        : points(fxex.points), exType(fxex.exType), extrXPoint(fxex.extrXPoint) {}

    void operator()(const range_t& range) {
        const size_t i_end = range.end();
        if(!range.empty()) {
            for(size_t i = range.begin(); i != i_end; ++i) {
                if(closerToExtremum(points[i])) {
                    extrXPoint = points[i];
                }
            }
        }
    }

    void join(const FindXExtremum &rhs) {
        if(closerToExtremum(rhs.extrXPoint)) {
            extrXPoint = rhs.extrXPoint;
        }
    }

    point_t extremeXPoint() {
        return extrXPoint;
    }

private:
    const pointVec_t    &points;
    const extremumType   exType;
    point_t              extrXPoint;
    bool closerToExtremum(const point_t &p) const {
        switch(exType) {
        case minX:
            return p.x<extrXPoint.x; break;
        case maxX:
            return p.x>extrXPoint.x; break;
        }
        return false; // avoid warning
    }
};

template <FindXExtremum::extremumType type>
point_t extremum(const pointVec_t &P) {
    FindXExtremum fxBody(P, type);
    tbb::parallel_reduce(range_t(0, P.size(), FindXExtremum::grainSize), fxBody);
    return fxBody.extremeXPoint();
}

class SplitByCP {
    const pointVec_t    &initialSet;
    pointVec_t          &reducedSet;
    point_t              p1, p2;
    point_t              farPoint;
    double               howFar;
public:
    static const size_t grainSize = cfg::DIVIDE_GS;
#if !USECONCVEC
    static mutex_t      pushBackMutex;
#endif // USECONCVEC

    SplitByCP( point_t _p1, point_t _p2,
        const pointVec_t &_initialSet, pointVec_t &_reducedSet)
        : p1(_p1), p2(_p2),
        initialSet(_initialSet), reducedSet(_reducedSet),
        howFar(0), farPoint(p1) {
    }

    SplitByCP( SplitByCP& sbcp, tbb::split )
        : p1(sbcp.p1), p2(sbcp.p2),
        initialSet(sbcp.initialSet), reducedSet(sbcp.reducedSet),
        howFar(0), farPoint(p1) {}

    void operator()( const range_t& range ) {
        const size_t i_end = range.end();
        double cp;
        for(size_t i=range.begin(); i!=i_end; ++i) {
            if( (initialSet[i] != p1) && (initialSet[i] != p2) ) {
                cp = util::cross_product(p1, p2, initialSet[i]);
                if(cp>0) {
#if USECONCVEC
                    reducedSet.push_back(initialSet[i]);
#else // Locked push_back to a not thread-safe STD::VECTOR
                    {
                        mutex_t::scoped_lock lock(pushBackMutex);
                        reducedSet.push_back(initialSet[i]);
                    }
#endif // USECONCVEC
                    if(cp>howFar) {
                        farPoint = initialSet[i];
                        howFar   = cp;
                    }
                }
            }
        }
    }

    void join(const SplitByCP& rhs) {
        if(rhs.howFar>howFar) {
            howFar   = rhs.howFar;
            farPoint = rhs.farPoint;
        }
    }

    point_t farthestPoint() const {
        return farPoint;
    }
};

class SplitByCP_buf {
    const pointVec_t    &initialSet;
    pointVec_t          &reducedSet;
    point_t              p1, p2;
    point_t              farPoint;
    double               howFar;
public:
    static const size_t  grainSize = cfg::DIVIDE_GS;
#if !USECONCVEC
    static mutex_t       insertMutex;
#endif // USECONCVEC

    SplitByCP_buf( point_t _p1, point_t _p2,
        const pointVec_t &_initialSet, pointVec_t &_reducedSet)
        : p1(_p1), p2(_p2),
        initialSet(_initialSet), reducedSet(_reducedSet),
        howFar(0), farPoint(p1) {}

    SplitByCP_buf(SplitByCP_buf& sbcp, tbb::split)
        : p1(sbcp.p1), p2(sbcp.p2),
        initialSet(sbcp.initialSet), reducedSet(sbcp.reducedSet),
        howFar(0), farPoint(p1) {}

    void operator()(const range_t& range) {
        const size_t i_end = range.end();
        size_t j = 0;
        double cp;
        point_t tmp_vec[grainSize];
        for(size_t i = range.begin(); i != i_end; ++i) {
            if( (initialSet[i] != p1) && (initialSet[i] != p2) ) {            
                cp = util::cross_product(p1, p2, initialSet[i]);
                if(cp>0) {
                    tmp_vec[j++] = initialSet[i];
                    if(cp>howFar) {
                        farPoint = initialSet[i];
                        howFar   = cp;
                    }
                }
            }
        }

#if USECONCVEC
        appendVector(tmp_vec, j, reducedSet);
#else // USE STD::VECTOR
        appendVector(insertMutex, tmp_vec, j, reducedSet);
#endif // USECONCVEC
    }

    void join(const SplitByCP_buf& rhs) {
        if(rhs.howFar>howFar) {
            howFar   = rhs.howFar;
            farPoint = rhs.farPoint;
        }
    }

    point_t farthestPoint() const {
        return farPoint;
    }
};

#if !USECONCVEC
mutex_t SplitByCP::pushBackMutex   = mutex_t();
mutex_t SplitByCP_buf::insertMutex = mutex_t();
#endif

template <typename BodyType>
point_t divide(const pointVec_t &P, pointVec_t &P_reduced,
              const point_t &p1, const point_t &p2) {
    BodyType body(p1, p2, P, P_reduced);
    // Must use simple_partitioner (see the comment in initialize() above)
    tbb::parallel_reduce(range_t(0, P.size(), BodyType::grainSize),
                         body, tbb::simple_partitioner() );

    if(util::VERBOSE) {
        std::stringstream ss;
        ss << P.size() << " nodes in bucket"<< ", "
            << "dividing by: [ " << p1 << ", " << p2 << " ], "
            << "farthest node: " << body.farthestPoint();
        util::OUTPUT.push_back(ss.str());
    }

    return body.farthestPoint();
}

void divide_and_conquer(const pointVec_t &P, pointVec_t &H,
                        point_t p1, point_t p2, bool buffered) {
    assert(P.size() >= 2);
    pointVec_t P_reduced;
    pointVec_t H1, H2;
    point_t p_far;
    
    if(buffered) {
        p_far = divide<SplitByCP_buf>(P, P_reduced, p1, p2);
    } else {
        p_far = divide<SplitByCP>(P, P_reduced, p1, p2);
    }

    if (P_reduced.size()<2) {
        H.push_back(p1);
#if USECONCVEC
        appendVector(P_reduced, H);
#else // insert into STD::VECTOR
        H.insert(H.end(), P_reduced.begin(), P_reduced.end());
#endif
    }
    else {
        divide_and_conquer(P_reduced, H1, p1, p_far, buffered);
        divide_and_conquer(P_reduced, H2, p_far, p2, buffered);

#if USECONCVEC
        appendVector(H1, H);
        appendVector(H2, H);
#else // insert into STD::VECTOR
        H.insert(H.end(), H1.begin(), H1.end());
        H.insert(H.end(), H2.begin(), H2.end());
#endif
    }
}

void quickhull(const pointVec_t &points, pointVec_t &hull, bool buffered) {
    if (points.size() < 2) {
#if USECONCVEC
        appendVector(points, hull);
#else // STD::VECTOR
        hull.insert(hull.end(), points.begin(), points.end());
#endif // USECONCVEC
        return;
    }

    point_t p_maxx = extremum<FindXExtremum::maxX>(points);
    point_t p_minx = extremum<FindXExtremum::minX>(points);

    pointVec_t H;

    divide_and_conquer(points, hull, p_maxx, p_minx, buffered);
    divide_and_conquer(points, H, p_minx, p_maxx, buffered);
#if USECONCVEC
    appendVector(H, hull);
#else // STD::VECTOR
    hull.insert(hull.end(), H.begin(), H.end());
#endif // USECONCVEC
}

int main(int argc, char* argv[]) {
    util::ParseInputArgs(argc, argv);

    pointVec_t      points;
    pointVec_t      hull;
    int             nthreads;
    util::my_time_t tm_init, tm_start, tm_end;

#if USECONCVEC
    std::cout << "Starting TBB unbuffered push_back version of QUICK HULL algorithm" << std::endl;
#else
    std::cout << "Starting STL locked unbuffered push_back version of QUICK HULL algorithm" << std::endl;
#endif // USECONCVEC

#if INIT_ONCE
    tm_init = util::gettime();
    serial_initialize(points);
    tm_start = util::gettime();
    std::cout << "Serial init time: " << util::time_diff(tm_init, tm_start) << "  Points in input: " << points.size() << "\n";

#endif

    for(nthreads=cfg::NUM_THREADS_START; nthreads<=cfg::NUM_THREADS_END;
        ++nthreads) {
        tbb::task_scheduler_init init(nthreads);
#if !INIT_ONCE
        points.clear();
        tm_init = util::gettime();
        initialize<FillRNDPointsVector>(points);
        tm_start = util::gettime();
        std::cout << "Parallel init time on " << nthreads << " threads: " << util::time_diff(tm_init, tm_start) << "  Points in input: " << points.size() << "\n";
#endif // !INIT_ONCE
        tm_start = util::gettime();
        quickhull(points, hull, false);
        tm_end = util::gettime();
        std::cout << "Time on " << nthreads << " threads: " << util::time_diff(tm_start, tm_end) << "  Points in hull: " << hull.size() << "\n";
        hull.clear();
    }

#if USECONCVEC 
    std::cout << "Starting TBB buffered version of QUICK HULL algorithm" << std::endl;
#else
    std::cout << "Starting STL locked buffered version of QUICK HULL algorithm" << std::endl;
#endif

    for(nthreads=cfg::NUM_THREADS_START; nthreads<=cfg::NUM_THREADS_END;
        ++nthreads) {
        tbb::task_scheduler_init init(nthreads);
#if !INIT_ONCE
        points.clear();
        tm_init = util::gettime();
        initialize<FillRNDPointsVector_buf>(points);
        tm_start = util::gettime();
        std::cout << "Init time on " << nthreads << " threads: " << util::time_diff(tm_init, tm_start) << "  Points in input: " << points.size() << "\n";
#endif // !INIT_ONCE
        tm_start = util::gettime();
        quickhull(points, hull, true);
        tm_end = util::gettime();
        std::cout << "Time on " << nthreads << " threads: " << util::time_diff(tm_start, tm_end) << "  Points in hull: " << hull.size() << "\n";
        hull.clear();
    }    

    return 0;
}

#endif // USETBB

void serial_initialize(pointVec_t &points) {
    points.resize(cfg::MAXPOINTS);

    unsigned int rseed=1;
    for(size_t i=0, count=0; long(i)<cfg::MAXPOINTS; ++i) {
        points[i] = util::GenerateRNDPoint<double>(count, rseed);
    }
}
