// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V1 validator (docs/PLAN.md, "V1: validator nesting and orientation"): the frozen exact pre-validator mp21b::validate, then per-component rules.
//
//   * components = edge-connected sets of triangles after welding by exactly equal coordinates (two pieces touching only at a vertex stay separate components);
//   * each component's signed volume is exact (cpp_int); a double filter may only shortcut a CERTAIN sign;
//   * component B contains component A when a vertex of A that is not on B's surface is inside B (exact ray-parity point-in-solid, the symbolic tie-break of harness/pip.h,
//     here on cpp_int predicates); the bounding box of A inside that of B is the exact prefilter;
//   * depth = number of containers, parent = container of greatest depth. Accepted iff every volume is non-zero, a component without parent is positive, and a component's
//     sign is opposite to its parent's (+ - + - ...: solid, cavity, island, ...).
// Reason strings start with a code: V1-ZERO-VOLUME-COMPONENT, V1-NEGATIVE-OUTSIDE, V1-NESTED-SAME-ORIENTATION, V1-SURFACES-SHARED, V1-NESTING-INCONSISTENT,
// V1-CAVITY-NOT-ALLOWED (only when Options::allowCavities is false). Crossing component surfaces are NOT detected (no self-intersection check): the nesting decision is exact
// only for components whose surfaces do not cross.
// Planted mutants (tests only): MUT_V1_NOSIGN skips the sign rules, MUT_V1_NONEST skips the containment test, MUT_V1_DOUBLEVOL takes component volume signs from plain doubles.
#pragma once
#include "../mp21b/validator.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace v1 {
using namespace mesh;

struct Options { bool allowCavities = true; };
struct Report { int components = 0; int cavities = 0; int maxDepth = 0; };

namespace detail {
struct Comp { std::vector<uint32_t> tris; std::vector<uint32_t> verts; double lo[3], hi[3]; int sign = 0; };

inline int sgn(double x) { return (x > 0) - (x < 0); }

// exact sign of the volume of a triangle subset: double filter (conservative bound) first, exact integers otherwise
inline int volumeSign(const Mesh& m, const std::vector<uint32_t>& tris) {
    double sum = 0, abss = 0;
    for (uint32_t k : tris) {
        const double *a = &m.v[3*m.t[3*k]], *b = &m.v[3*m.t[3*k+1]], *c = &m.v[3*m.t[3*k+2]];
        double t1 = b[1]*c[2], t2 = b[2]*c[1], t3 = b[2]*c[0], t4 = b[0]*c[2], t5 = b[0]*c[1], t6 = b[1]*c[0];
        sum += a[0]*(t1-t2) + a[1]*(t3-t4) + a[2]*(t5-t6);
        abss += std::fabs(a[0])*(std::fabs(t1)+std::fabs(t2)) + std::fabs(a[1])*(std::fabs(t3)+std::fabs(t4)) + std::fabs(a[2])*(std::fabs(t5)+std::fabs(t6));
    }
#ifdef MUT_V1_DOUBLEVOL
    return sgn(sum);
#endif
    if (abss > 1e-250 && abss < 1e250) {
        double eb = ((double)tris.size() * 4 + 64) * 1.1102230246251565e-16 * abss;
        if (sum > eb) return 1;
        if (sum < -eb) return -1;
    }
    Dyadic acc{BigInt(0), 0};
    for (uint32_t k : tris) {
        const double *a = &m.v[3*m.t[3*k]], *b = &m.v[3*m.t[3*k+1]], *c = &m.v[3*m.t[3*k+2]];
        BigInt v[9]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2]}, v);
        // common exponent of this triangle: recover it as the exponent of the integer scaling by comparing with the doubles is unnecessary: the determinant of the
        // integer vectors times 2^(3*emin) is the determinant of the doubles; emin is the smallest exponent among the nine values
        int emin = INT32_MAX;
        for (const double* p : {a, b, c}) for (int d = 0; d < 3; d++) if (p[d] != 0) { int ex; std::frexp(p[d], &ex); emin = std::min(emin, ex - 53); }
        if (emin == INT32_MAX) continue;
        BigInt det = v[0]*(v[4]*v[8] - v[5]*v[7]) + v[1]*(v[5]*v[6] - v[3]*v[8]) + v[2]*(v[3]*v[7] - v[4]*v[6]);
        acc = addExact(acc, Dyadic{det, 3 * emin});
    }
    return signExact(acc);
}

// sign of orient2d in the (y, z) plane of (u, v, p') with p' = (py + e, pz + e^2): exact value, then the first non-zero perturbation term (as harness/pip.h)
inline int orientYZ(const double* u, const double* v, const double* p) {
    double U[2] = {u[1], u[2]}, V[2] = {v[1], v[2]}, P[2] = {p[1], p[2]};
    int s = exactOrient2d(U, V, P);
    if (s != 0) return s;
    s = sgn(u[2] - v[2]);
    if (s != 0) return s;
    return sgn(v[1] - u[1]);
}

// p against the closed surface of the triangles `tris` of m: 1 inside, 0 outside, -1 on the surface. Ray along +x, exact.
inline int pointInComponent(const Mesh& m, const std::vector<uint32_t>& tris, const double* p) {
    int crossings = 0;
    for (uint32_t k : tris) {
        const double* a = &m.v[3*m.t[3*k]]; const double* b = &m.v[3*m.t[3*k+1]]; const double* c = &m.v[3*m.t[3*k+2]];
        // conservative exact reject on the yz bounding box: neither a crossing of the perturbed ray nor a surface hit is possible
        double ylo = std::min({a[1], b[1], c[1]}), yhi = std::max({a[1], b[1], c[1]}), zlo = std::min({a[2], b[2], c[2]}), zhi = std::max({a[2], b[2], c[2]});
        if (p[1] < ylo || p[1] > yhi || p[2] < zlo || p[2] > zhi) continue;
        int o3 = exactOrient3d(a, b, c, p);
        if (o3 == 0) {   // p in the plane of the triangle: on the closed triangle?
            BigInt v[12]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2], p[0],p[1],p[2]}, v);
            auto sub = [&](int i, int j, BigInt* o) { for (int d = 0; d < 3; d++) o[d] = v[3*i+d] - v[3*j+d]; };
            BigInt ab[3], bc[3], ca[3], ap[3], bp[3], cp[3], n[3];
            sub(1,0,ab); sub(2,1,bc); sub(0,2,ca); sub(3,0,ap); sub(3,1,bp); sub(3,2,cp);
            auto cross = [](const BigInt* x, const BigInt* y, BigInt* o) { o[0]=x[1]*y[2]-x[2]*y[1]; o[1]=x[2]*y[0]-x[0]*y[2]; o[2]=x[0]*y[1]-x[1]*y[0]; };
            auto dot = [](const BigInt* x, const BigInt* y) -> BigInt { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; };
            BigInt c0[3], c1[3], c2[3]; cross(ab, ap, c0); cross(bc, bp, c1); cross(ca, cp, c2); cross(ab, bc, n);
            bool nonDegenerate = n[0] != 0 || n[1] != 0 || n[2] != 0;
            if (nonDegenerate && dot(c0, n).sign() >= 0 && dot(c1, n).sign() >= 0 && dot(c2, n).sign() >= 0) return -1;
        }
        int s0 = orientYZ(a, b, p), s1 = orientYZ(b, c, p), s2 = orientYZ(c, a, p);
        if (s0 == 0 || s1 == 0 || s2 == 0) continue;
        if (!(s0 == s1 && s1 == s2)) continue;
        double ayz[2] = {a[1], a[2]}, byz[2] = {b[1], b[2]}, cyz[2] = {c[1], c[2]};
        int nx = exactOrient2d(ayz, byz, cyz);
        if (nx == 0) continue;
        int side = o3;
        if (side == 0) {
            BigInt v[9]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2]}, v);
            BigInt ux=v[3]-v[0], uy=v[4]-v[1], uz=v[5]-v[2], wx=v[6]-v[0], wy=v[7]-v[1], wz=v[8]-v[2];
            BigInt ny = uz*wx - ux*wz, nz = ux*wy - uy*wx;
            side = -ny.sign(); if (side == 0) side = -nz.sign();
        }
        if (side == nx) crossings++;
    }
    return (crossings & 1) ? 1 : 0;
}
}  // namespace detail

inline std::string validate(const Mesh& m, const Options& opt = Options(), Report* rep = nullptr) {
    using namespace detail;
    if (rep) *rep = Report();
    if (std::string r = mp21b::validate(m); !r.empty()) return r;
    // components by shared edges after welding by exactly equal coordinates
    struct K { double x, y, z; bool operator==(const K& o) const { return x == o.x && y == o.y && z == o.z; } };
    struct KH { size_t operator()(const K& k) const { uint64_t a, b, c; double x = k.x + 0.0, y = k.y + 0.0, z = k.z + 0.0; memcpy(&a, &x, 8); memcpy(&b, &y, 8); memcpy(&c, &z, 8); return (size_t)((a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full) ^ (c * 0x165667B19E3779F9ull)); } };
    std::unordered_map<K, uint32_t, KH> w; w.reserve(m.nv() * 2); std::vector<uint32_t> id(m.nv());
    for (size_t i = 0; i < m.nv(); i++) { auto it = w.emplace(K{m.v[3*i], m.v[3*i+1], m.v[3*i+2]}, (uint32_t)w.size()); id[i] = it.first->second; }
    std::vector<uint32_t> par(m.nt()); for (size_t i = 0; i < par.size(); i++) par[i] = (uint32_t)i;
    auto find = [&](uint32_t x) { while (par[x] != x) { par[x] = par[par[x]]; x = par[x]; } return x; };
    { std::unordered_map<uint64_t, uint32_t> first; first.reserve(m.nt() * 2);
      for (size_t k = 0; k < m.nt(); k++) for (int j = 0; j < 3; j++) {
          uint32_t p = id[m.t[3*k+j]], q = id[m.t[3*k+(j+1)%3]];
          auto ins = first.emplace(((uint64_t)std::min(p, q) << 32) | std::max(p, q), (uint32_t)k);
          if (!ins.second) par[find((uint32_t)k)] = find(ins.first->second);
      } }
    std::unordered_map<uint32_t, uint32_t> compOf; std::vector<Comp> C;
    for (size_t k = 0; k < m.nt(); k++) { uint32_t r = find((uint32_t)k); auto it = compOf.emplace(r, (uint32_t)C.size()); if (it.second) C.emplace_back(); C[it.first->second].tris.push_back((uint32_t)k); }
    if (rep) rep->components = (int)C.size();
    if (C.size() == 1) return "";   // the total volume was checked by mp21b::validate and is positive; nothing to nest
    for (Comp& c : C) {
        for (int d = 0; d < 3; d++) { c.lo[d] = INFINITY; c.hi[d] = -INFINITY; }
        std::vector<uint32_t> seen; for (uint32_t k : c.tris) for (int j = 0; j < 3; j++) seen.push_back(m.t[3*k+j]);
        std::sort(seen.begin(), seen.end()); seen.erase(std::unique(seen.begin(), seen.end()), seen.end()); c.verts = seen;
        for (uint32_t v : c.verts) for (int d = 0; d < 3; d++) { c.lo[d] = std::min(c.lo[d], m.v[3*v+d]); c.hi[d] = std::max(c.hi[d], m.v[3*v+d]); }
        c.sign = volumeSign(m, c.tris);
    }
    const size_t n = C.size();
    for (size_t i = 0; i < n; i++) if (C[i].sign == 0) return "V1-ZERO-VOLUME-COMPONENT: component " + std::to_string(i) + " (" + std::to_string(C[i].tris.size()) + " triangles) has exactly zero signed volume";
    // containment: contains[a] = components that contain a
    std::vector<std::vector<size_t>> cont(n);
#ifndef MUT_V1_NONEST
    for (size_t a = 0; a < n; a++) for (size_t b = 0; b < n; b++) {
        if (a == b) continue;
        bool boxIn = true; for (int d = 0; d < 3; d++) if (C[a].lo[d] < C[b].lo[d] || C[a].hi[d] > C[b].hi[d]) boxIn = false;
        if (!boxIn) continue;
        int res = -1;
        for (uint32_t v : C[a].verts) { res = pointInComponent(m, C[b].tris, &m.v[3*v]); if (res >= 0) break; }
        if (res < 0) return "V1-SURFACES-SHARED: every vertex of component " + std::to_string(a) + " lies on the surface of component " + std::to_string(b) + "; nesting cannot be decided";
        if (res == 1) cont[a].push_back(b);
    }
#endif
    std::vector<long> parent(n, -1); int maxDepth = 0;
    for (size_t a = 0; a < n; a++) {
        auto& L = cont[a]; if (L.empty()) continue;
        std::sort(L.begin(), L.end(), [&](size_t x, size_t y) { return cont[x].size() < cont[y].size(); });
        for (size_t j = 0; j + 1 < L.size(); j++) if (cont[L[j+1]].size() != cont[L[j]].size() + 1 || std::find(cont[L[j+1]].begin(), cont[L[j+1]].end(), L[j]) == cont[L[j+1]].end())
            return "V1-NESTING-INCONSISTENT: component " + std::to_string(a) + " is inside components that are not nested in each other (crossing surfaces?)";
        parent[a] = (long)L.back(); maxDepth = std::max(maxDepth, (int)L.size());
    }
    for (size_t a = 0; a < n; a++) {
#ifndef MUT_V1_NOSIGN
        if (parent[a] < 0) { if (C[a].sign < 0) return "V1-NEGATIVE-OUTSIDE: component " + std::to_string(a) + " has negative volume and is not inside any other component"; }
        else if (C[a].sign == C[(size_t)parent[a]].sign) return "V1-NESTED-SAME-ORIENTATION: component " + std::to_string(a) + " has the same orientation as its container " + std::to_string(parent[a]) + " (a solid inside a solid, or a cavity inside a cavity)";
#endif
    }
    int cav = 0; for (auto& c : C) if (c.sign < 0) cav++;
    if (!opt.allowCavities && cav) return "V1-CAVITY-NOT-ALLOWED: " + std::to_string(cav) + " negative-volume (cavity) component(s) and the caller does not allow cavities";
    if (rep) { rep->cavities = cav; rep->maxDepth = maxDepth; }
    return "";
}
}  // namespace v1
