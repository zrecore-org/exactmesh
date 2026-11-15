// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP11: exact closed triangle-triangle intersection on binary64 coordinates, using Shewchuk's adaptive exact orient2d/orient3d (call exactinit() once first).
// Two closed non-degenerate triangles share a point iff some edge of one meets the other closed triangle. Non-coplanar: the intersection set is a segment or point
// whose end points lie on an edge of one triangle, so the 6 edge-in-triangle tests are complete. Coplanar: an edge of one meets the other triangle or one triangle
// contains a vertex of the other, which the same 6 tests cover (a contained triangle has its vertices, hence its edges, inside).
// Degenerate (zero-area) triangles are outside the contract (callers filter them; isDegenerate() is provided).
#pragma once
#include <cmath>
#include <cstdint>
#include "../harness/exact.h"
extern "C" { void exactinit(void); double orient3d(double* pa, double* pb, double* pc, double* pd); double orient2d(double* pa, double* pb, double* pc); }
namespace tt {
inline int sg(double x) { return (x > 0) - (x < 0); }
// Shewchuk's expansion arithmetic is exact only while no intermediate underflows or overflows. Inside the window (every coordinate zero or 2^-100 <= |x| < 2^101, the
// same window as the GPU filter) it is used; outside it (e.g. a coordinate nudged from 0 to a subnormal) the slow exact big-integer predicates of harness/exact.h decide.
inline bool inWin(double x) { if (x == 0) return true; double a = std::fabs(x); return a >= 7.888609052210118e-31 /* 2^-100 */ && a < 2.535301200456459e30 /* 2^101 */; }
inline bool inWinN(const double* p, int n) { for (int i = 0; i < n; i++) if (!inWin(p[i])) return false; return true; }
inline int o3(const double* a, const double* b, const double* c, const double* d) {
    if (inWinN(a, 3) && inWinN(b, 3) && inWinN(c, 3) && inWinN(d, 3)) return sg(orient3d(const_cast<double*>(a), const_cast<double*>(b), const_cast<double*>(c), const_cast<double*>(d)));
    return mesh::exactOrient3d(a, b, c, d);
}
inline int o2(const double* a, const double* b, const double* c) {
    if (inWinN(a, 2) && inWinN(b, 2) && inWinN(c, 2)) return sg(orient2d(const_cast<double*>(a), const_cast<double*>(b), const_cast<double*>(c)));
    return mesh::exactOrient2d(a, b, c);
}
inline bool isDegenerate(const double* a, const double* b, const double* c) {   // collinear: all three axis projections have orient2d == 0
    for (int ax = 0; ax < 3; ax++) {
        int i = (ax + 1) % 3, j = (ax + 2) % 3; double p[3][2] = {{a[i], a[j]}, {b[i], b[j]}, {c[i], c[j]}};
        if (o2(p[0], p[1], p[2]) != 0) return false;
    }
    return true;
}
inline bool onSeg2(const double* p, const double* q, const double* r) {   // r collinear with pq: inside the closed segment?
    return (r[0] >= (p[0] < q[0] ? p[0] : q[0])) && (r[0] <= (p[0] > q[0] ? p[0] : q[0])) && (r[1] >= (p[1] < q[1] ? p[1] : q[1])) && (r[1] <= (p[1] > q[1] ? p[1] : q[1]));
}
inline bool segSeg2(const double* p, const double* q, const double* r, const double* s) {
    int d1 = o2(p, q, r), d2 = o2(p, q, s), d3 = o2(r, s, p), d4 = o2(r, s, q);
    if (d1 * d2 < 0 && d3 * d4 < 0) return true;
    return (d1 == 0 && onSeg2(p, q, r)) || (d2 == 0 && onSeg2(p, q, s)) || (d3 == 0 && onSeg2(r, s, p)) || (d4 == 0 && onSeg2(r, s, q));
}
inline bool inTri2(const double* p, const double* a, const double* b, const double* c, int orient) {
    int s1 = o2(a, b, p) * orient, s2 = o2(b, c, p) * orient, s3 = o2(c, a, p) * orient;
    return s1 >= 0 && s2 >= 0 && s3 >= 0;
}
// segment pq (3D) against the closed triangle abc (3D, non-degenerate)
inline bool segTri(const double* p, const double* q, const double* a, const double* b, const double* c) {
    int sp = o3(a, b, c, p), sq = o3(a, b, c, q);
    if (sp * sq > 0) return false;
    if (sp == 0 && sq == 0) {   // coplanar: project along an axis where the triangle is not degenerate, then do it in 2D
        for (int ax = 0; ax < 3; ax++) {
            int i = (ax + 1) % 3, j = (ax + 2) % 3;
            double P[2] = {p[i], p[j]}, Q[2] = {q[i], q[j]}, A[2] = {a[i], a[j]}, B[2] = {b[i], b[j]}, C[2] = {c[i], c[j]};
            int orient = o2(A, B, C);
            if (orient == 0) continue;
            if (inTri2(P, A, B, C, orient) || inTri2(Q, A, B, C, orient)) return true;
            return segSeg2(P, Q, A, B) || segSeg2(P, Q, B, C) || segSeg2(P, Q, C, A);
        }
        return false;   // degenerate triangle: outside the contract
    }
    int t1 = o3(p, q, a, b), t2 = o3(p, q, b, c), t3 = o3(p, q, c, a);
    bool pos = t1 > 0 || t2 > 0 || t3 > 0, neg = t1 < 0 || t2 < 0 || t3 < 0;
    return !(pos && neg);
}
// A and B are 9 doubles each (three xyz vertices)
inline bool triTri(const double* A, const double* B) {
    for (int e = 0; e < 3; e++) {
        if (segTri(A + 3 * e, A + 3 * ((e + 1) % 3), B, B + 3, B + 6)) return true;
        if (segTri(B + 3 * e, B + 3 * ((e + 1) % 3), A, A + 3, A + 6)) return true;
    }
    return false;
}
}  // namespace tt
