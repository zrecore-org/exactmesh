// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V5 instrument (not part of the scorer): real_score compiled with -Dorient2d=v5_orient2d -Dorient3d=v5_orient3d calls these; each calls the exact Shewchuk predicate (exactinit() done by main) and also
// evaluates the plain double determinant exactly as predicates.c does without initialisation (orient2d: detleft - detright; orient3d: the 3x3 expansion), counting calls whose plain sign differs from the exact sign.
#include <cstdio>
#include <cstdlib>
extern "C" { double orient2d(double*, double*, double*); double orient3d(double*, double*, double*, double*); }
static unsigned long long n2, n3, d2, d3, z2, z3;
static void rep() { fprintf(stderr, "V5COUNT orient2d calls %llu plain!=exact %llu (exact zero %llu) orient3d calls %llu plain!=exact %llu (exact zero %llu)\n", n2, d2, z2, n3, d3, z3); }
static const int reg = (atexit(rep), 0);
static int sg(double x) { return (x > 0) - (x < 0); }
extern "C" double v5_orient2d(double* a, double* b, double* c) {
    double e = orient2d(a, b, c); n2++; z2 += e == 0;
    double l = (a[0] - c[0]) * (b[1] - c[1]), r = (a[1] - c[1]) * (b[0] - c[0]); d2 += sg(l - r) != sg(e); (void)reg; return e; }
extern "C" double v5_orient3d(double* pa, double* pb, double* pc, double* pd) {
    double e = orient3d(pa, pb, pc, pd); n3++; z3 += e == 0;
    double adx = pa[0] - pd[0], bdx = pb[0] - pd[0], cdx = pc[0] - pd[0], ady = pa[1] - pd[1], bdy = pb[1] - pd[1], cdy = pc[1] - pd[1], adz = pa[2] - pd[2], bdz = pb[2] - pd[2], cdz = pc[2] - pd[2];
    double det = adx * (bdy * cdz - bdz * cdy) + bdx * (cdy * adz - cdz * ady) + cdx * (ady * bdz - adz * bdy); d3 += sg(det) != sg(e); return e; }
