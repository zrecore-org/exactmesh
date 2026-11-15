// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Shared point-in-solid test cases (moved out of mp12_pip.cpp unchanged for MP12c): meshes (uv sphere, integer block, scaled/offset copies) and the registered query classes.
#pragma once
#include "../harness/mesh_io.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>
using mesh::Mesh;

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
struct Rng { std::mt19937_64 g; explicit Rng(uint64_t s) : g(s) {} double u01() { return (double)(g() >> 11) / 9007199254740992.0; } int pick(int n) { return (int)(g() % (uint64_t)n); } };

// Platform-independent sin and cos: only IEEE + - * / (built with -ffp-contract=off), so every platform gives the same bits (libm results differ in the last bit between
// macOS, glibc and the Windows runtime). Used when PIP_CASES_DETERMINISTIC is defined before this header (MP12c); mp12_pip keeps the libm versions its recorded totals came from.
static double detSinCos(double x, bool wantCos) {
    const double PIO2 = 1.57079632679489661923, TWO_OVER_PI = 0.63661977236758134308;
    double kf = std::floor(x * TWO_OVER_PI + 0.5); long k = (long)kf; double r = x - kf * PIO2, r2 = r * r, sn = 0, cs = 0, t = r;
    sn = r; for (int n = 1; n <= 12; n++) { t = -t * r2 / ((2.0 * n) * (2.0 * n + 1.0)); sn += t; }
    t = 1.0; cs = 1.0; for (int n = 1; n <= 12; n++) { t = -t * r2 / ((2.0 * n - 1.0) * (2.0 * n)); cs += t; }
    int q = (int)(((k % 4) + 4) % 4);                       // sin(r + q pi/2), cos(r + q pi/2)
    double sv[4] = {sn, cs, -sn, -cs}, cv[4] = {cs, -sn, -cs, sn};
    return wantCos ? cv[q] : sv[q];
}
#ifdef PIP_CASES_DETERMINISTIC
#define PIP_SIN(x) detSinCos((x), false)
#define PIP_COS(x) detSinCos((x), true)
#else
#define PIP_SIN(x) std::sin(x)
#define PIP_COS(x) std::cos(x)
#endif
static Mesh uvsphere(int rings, int segs, double rad, double cx, double cy, double cz) {
    Mesh m; const double PI = 3.14159265358979323846;
    uint32_t top = m.addV(cx, cy, cz + rad);
    for (int i = 1; i < rings; i++) for (int j = 0; j < segs; j++) { double th = PI * i / rings, ph = 2 * PI * j / segs; m.addV(cx + rad * PIP_SIN(th) * PIP_COS(ph), cy + rad * PIP_SIN(th) * PIP_SIN(ph), cz + rad * PIP_COS(th)); }
    uint32_t bot = m.addV(cx, cy, cz - rad);
    auto V = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segs + (j % segs)); };
    for (int j = 0; j < segs; j++) m.addT(top, V(1, j), V(1, j + 1));
    for (int i = 1; i + 1 < rings; i++) for (int j = 0; j < segs; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    for (int j = 0; j < segs; j++) m.addT(bot, V(rings - 1, j + 1), V(rings - 1, j));
    return m;
}
// a solid block of n^3 unit cubes as ONE closed surface (outer faces only, each unit square split along one diagonal), integer coordinates
static Mesh block(int n) {
    Mesh m; auto vid = [&](int x, int y, int z) { return (uint32_t)((x * (n + 1) + y) * (n + 1) + z); };
    for (int x = 0; x <= n; x++) for (int y = 0; y <= n; y++) for (int z = 0; z <= n; z++) m.addV(x, y, z);
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, bool flip) { if (flip) { m.addT(a, c, b); m.addT(a, d, c); } else { m.addT(a, b, c); m.addT(a, c, d); } };
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
        quad(vid(0, i, j), vid(0, i, j + 1), vid(0, i + 1, j + 1), vid(0, i + 1, j), true);   quad(vid(n, i, j), vid(n, i + 1, j), vid(n, i + 1, j + 1), vid(n, i, j + 1), true);
        quad(vid(i, 0, j), vid(i + 1, 0, j), vid(i + 1, 0, j + 1), vid(i, 0, j + 1), true);   quad(vid(i, n, j), vid(i, n, j + 1), vid(i + 1, n, j + 1), vid(i + 1, n, j), true);
        quad(vid(i, j, 0), vid(i + 1, j, 0), vid(i + 1, j + 1, 0), vid(i, j + 1, 0), true);   quad(vid(i, j, n), vid(i, j + 1, n), vid(i + 1, j + 1, n), vid(i + 1, j, n), true);
    }
    return m;
}
static Mesh mapped(const Mesh& a, double scaleExp2, double ox, double oy, double oz) { Mesh m = a; for (size_t i = 0; i < m.nv(); i++) { m.v[3*i] = std::ldexp(m.v[3*i], (int)scaleExp2) + ox; m.v[3*i+1] = std::ldexp(m.v[3*i+1], (int)scaleExp2) + oy; m.v[3*i+2] = std::ldexp(m.v[3*i+2], (int)scaleExp2) + oz; } return m; }
static double ulps(double x, int k) { for (int i = 0; i < std::abs(k); i++) x = std::nextafter(x, k > 0 ? INFINITY : -INFINITY); return x; }

struct Cls { std::string name; std::vector<double> q; bool mustReachUncertain; };    // q: xyz per query
struct Case { std::string mesh; Mesh m; std::vector<Cls> classes; };

// query classes for any mesh; `lat` true for the integer block (adds lattice / grazing / face classes)
static void addClasses(Case& c, int blockN, Rng& r, double xform2, double ox, double oy, double oz) {
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (size_t i = 0; i < c.m.nv(); i++) for (int d = 0; d < 3; d++) { lo[d] = std::min(lo[d], c.m.v[3*i+d]); hi[d] = std::max(hi[d], c.m.v[3*i+d]); }
    auto mp = [&](double x, double y, double z, double* o) { o[0] = std::ldexp(x, (int)xform2) + ox; o[1] = std::ldexp(y, (int)xform2) + oy; o[2] = std::ldexp(z, (int)xform2) + oz; };
    { Cls k{"random in and around the bbox", {}, false};
      for (int i = 0; i < 3000; i++) for (int d = 0; d < 3; d++) { double w = hi[d] - lo[d]; k.q.push_back(lo[d] - 0.25 * w + 1.5 * w * r.u01()); }
      c.classes.push_back(k); }
    { Cls k{"ray through a vertex (py,pz of a vertex, px random)", {}, false};
      for (int i = 0; i < 1500; i++) { size_t v = (size_t)r.pick((int)c.m.nv()); double w = hi[0] - lo[0]; k.q.push_back(lo[0] - 0.25 * w + 1.5 * w * r.u01()); k.q.push_back(c.m.v[3*v+1]); k.q.push_back(c.m.v[3*v+2]); }
      c.classes.push_back(k); }
    { Cls k{"on the surface (rounded barycentric points)", {}, false};
      for (int i = 0; i < 1500; i++) { size_t t = (size_t)r.pick((int)c.m.nt()); double w0 = r.u01(), w1 = r.u01(), w2 = r.u01(), sw = w0 + w1 + w2; w0 /= sw; w1 /= sw; w2 /= sw;
          for (int d = 0; d < 3; d++) k.q.push_back(w0 * c.m.v[3*c.m.t[3*t]+d] + w1 * c.m.v[3*c.m.t[3*t+1]+d] + w2 * c.m.v[3*c.m.t[3*t+2]+d]); }
      c.classes.push_back(k); }
    { Cls k{"within 1..3 ulp of a face point (each axis)", {}, true};
      for (int i = 0; i < 3000; i++) { size_t t = (size_t)r.pick((int)c.m.nt()); double w0 = r.u01(), w1 = r.u01(), w2 = r.u01(), sw = w0 + w1 + w2; w0 /= sw; w1 /= sw; w2 /= sw; double p[3];
          for (int d = 0; d < 3; d++) p[d] = w0 * c.m.v[3*c.m.t[3*t]+d] + w1 * c.m.v[3*c.m.t[3*t+1]+d] + w2 * c.m.v[3*c.m.t[3*t+2]+d];
          int ax = r.pick(3), k2 = r.pick(7) - 3; p[ax] = ulps(p[ax], k2); k.q.insert(k.q.end(), p, p + 3); }
      c.classes.push_back(k); }
    if (blockN > 0) {
        { Cls k{"lattice points step 1/2 (faces, edges, vertices, grazing rays)", {}, true};
          for (int x = -2; x <= 2 * blockN + 2; x++) for (int y = -2; y <= 2 * blockN + 2; y++) for (int z = -2; z <= 2 * blockN + 2; z++) { double o[3]; mp(x / 2.0, y / 2.0, z / 2.0, o); k.q.insert(k.q.end(), o, o + 3); }
          c.classes.push_back(k); }
        { Cls k{"on a block face, 1..3 ulp off along the normal", {}, true};
          for (int i = 0; i < 3000; i++) { double a = blockN * r.u01(), b = blockN * r.u01(); int face = r.pick(6), dk = r.pick(7) - 3; double p[3];
              int ax = face / 2; double fv = (face & 1) ? blockN : 0; int a1 = (ax + 1) % 3, a2 = (ax + 2) % 3; p[ax] = ulps(fv, dk == 0 ? 1 : dk); p[a1] = a; p[a2] = b;
              double o[3]; mp(p[0], p[1], p[2], o); k.q.insert(k.q.end(), o, o + 3); }
          c.classes.push_back(k); }
    }
}

static std::vector<Case> makeCases() {
    std::vector<Case> cs; Rng r(12);
    { Case c; c.mesh = "sphere 30x40"; c.m = uvsphere(30, 40, 1, 0, 0, 0); addClasses(c, 0, r, 0, 0, 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3"; c.m = block(3); addClasses(c, 3, r, 0, 0, 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 at +2^30"; c.m = mapped(block(3), 0, std::ldexp(1.0, 30), 0, 0); addClasses(c, 3, r, 0, std::ldexp(1.0, 30), 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 at +2^50 (x,y,z)"; double o = std::ldexp(1.0, 50); c.m = mapped(block(3), 0, o, o, o); addClasses(c, 3, r, 0, o, o, o); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 scaled 2^-30"; c.m = mapped(block(3), -30, 0, 0, 0); addClasses(c, 3, r, -30, 0, 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 scaled 2^200 (outside the window)"; c.m = mapped(block(3), 200, 0, 0, 0); addClasses(c, 3, r, 200, 0, 0, 0); cs.push_back(std::move(c)); }
    return cs;
}

