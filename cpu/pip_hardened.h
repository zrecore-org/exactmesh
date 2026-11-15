// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12: harness/pip.h's classifyPoint with every Shewchuk orient2d/orient3d call routed through tt::o2/tt::o3 (cpu/tritri_exact.h), which fall back to the big-integer
// exact predicates outside the exponent window. harness/pip.h itself is unchanged: its Shewchuk predicates are not exact on subnormal coordinates (found in MP11),
// and MP12's first run showed its 'boundary' verdict wrong for a query at z = -5e-324 against a face at z = 0.
#pragma once
#include "tritri_exact.h"
#include "../harness/pip.h"
namespace mesh {
inline int sgnD2(double x) { return (x > 0) - (x < 0); }

// sign of orient2d(u, v, p') in the (y, z) plane with p' = (py + e, pz + e^2): exact value first, then the first non-zero perturbation term
inline int orientYZh(const double* u, const double* v, const double* p, int mut) {
    double U[2] = {u[1], u[2]}, V[2] = {v[1], v[2]}, P[2] = {p[1], p[2]};
    int s = tt::o2(U, V, P);
    if (s != 0 || mut == 1) return s;
    s = sgnD2(u[2] - v[2]);                 // d/d(py): u_z - v_z   (exact: a difference of two doubles has the sign of the comparison)
    if (s != 0) return s;
    return sgnD2(v[1] - u[1]);              // d/d(pz): v_y - u_y
}

inline Pip classifyPointHardened(const Mesh& m, const double* p, int mut = 0) {
    Pip r;
    for (size_t k = 0; k < m.nt(); k++) {
        const double* a = &m.v[3 * (size_t)m.t[3*k]];
        const double* b = &m.v[3 * (size_t)m.t[3*k+1]];
        const double* c = &m.v[3 * (size_t)m.t[3*k+2]];
        double A[3] = {a[0], a[1], a[2]}, B[3] = {b[0], b[1], b[2]}, C[3] = {c[0], c[1], c[2]}, P[3] = {p[0], p[1], p[2]};
        int s0 = orientYZh(a, b, p, mut), s1 = orientYZh(b, c, p, mut), s2 = orientYZh(c, a, p, mut);
        // exact surface test: coplanar and inside or on the closed triangle (only evaluated when the plane test is exactly zero)
        int o3s = tt::o3(A, B, C, P);
        if (o3s == 0) {
            // p lies in the plane of the triangle: is it inside the closed triangle? exact 3D test with integers
            BigInt v[12]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2], p[0],p[1],p[2]}, v);
            auto sub = [&](int i, int j, BigInt* o) { for (int d = 0; d < 3; d++) o[d] = v[3*i+d] - v[3*j+d]; };
            BigInt ab[3], bc[3], ca[3], ap[3], bp[3], cp[3], n[3];
            sub(1,0,ab); sub(2,1,bc); sub(0,2,ca); sub(3,0,ap); sub(3,1,bp); sub(3,2,cp);
            auto cross = [](const BigInt* x, const BigInt* y, BigInt* o) { o[0]=x[1]*y[2]-x[2]*y[1]; o[1]=x[2]*y[0]-x[0]*y[2]; o[2]=x[0]*y[1]-x[1]*y[0]; };
            auto dot = [](const BigInt* x, const BigInt* y) -> BigInt { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; };
            BigInt c0[3], c1[3], c2[3]; cross(ab, ap, c0); cross(bc, bp, c1); cross(ca, cp, c2); cross(ab, bc, n);
            int d0 = dot(c0, n).sign(), d1 = dot(c1, n).sign(), d2 = dot(c2, n).sign();
            bool nonDegenerate = n[0] != 0 || n[1] != 0 || n[2] != 0;
            if (nonDegenerate && d0 >= 0 && d1 >= 0 && d2 >= 0) { r.boundary = true; return r; }
        }
        if (s0 == 0 || s1 == 0 || s2 == 0) continue;           // degenerate projection (edge-on triangle), no crossing
        if (!(s0 == s1 && s1 == s2)) continue;                 // p' outside the projected triangle
        double ayz[2] = {a[1], a[2]}, byz[2] = {b[1], b[2]}, cyz[2] = {c[1], c[2]};
        int nx = tt::o2(ayz, byz, cyz);                // sign of the x component of the normal (exact)
        if (nx == 0) continue;
        if (mut == 3) { r.crossings++; continue; }
        // t > 0 along +x  <=>  sign(n . (a - p')) == sign(n_x); n . (a - p) has the sign of orient3d(a,b,c,p); ties by the perturbation (-n_y, then -n_z)
        int side = o3s;
        if (side == 0) {
            BigInt v[9]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2]}, v);
            BigInt ux=v[3]-v[0], uy=v[4]-v[1], uz=v[5]-v[2], wx=v[6]-v[0], wy=v[7]-v[1], wz=v[8]-v[2];
            BigInt ny = uz*wx - ux*wz, nz = ux*wy - uy*wx;
            side = -ny.sign(); if (side == 0) side = -nz.sign();
        }
        if (side == nx) r.crossings++;
    }
    r.inside = (r.crossings & 1) != 0;
    return r;
}
}
