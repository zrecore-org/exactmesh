// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Small helpers shared by the tests: axis-aligned boxes as triangle meshes, exact conversion of doubles to Dyadic values, a deterministic random source.
#pragma once
#include "exact.h"
#include "mesh_io.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <random>

namespace tmesh {
using Vec = std::array<double, 3>;

// Outward-oriented (counter-clockwise seen from outside) closed box mesh with 8 vertices and 12 triangles.
inline mesh::Mesh box(Vec lo, Vec hi) {
    mesh::Mesh m;
    for (int i = 0; i < 8; i++) m.addV((i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2]);
    static const uint32_t T[12][3] = {{0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4}, {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
// The same box with the vertex and triangle order of the registered corpus generator (corpus/gen_corpus.cpp): Geogram's unpatched vertex-contact defect depends on the order
// (and so on the memory layout) of the vertices, so the registered vertex-contact scenario uses this ordering.
inline mesh::Mesh boxCorpusOrder(Vec lo, Vec hi) {
    mesh::Mesh m;
    for (int z = 0; z < 2; z++) { const double Z = z ? hi[2] : lo[2];
        m.addV(lo[0], lo[1], Z); m.addV(hi[0], lo[1], Z); m.addV(hi[0], hi[1], Z); m.addV(lo[0], hi[1], Z); }
    static const uint32_t T[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
inline mesh::Mesh unitCube() { return box({0, 0, 0}, {1, 1, 1}); }

// A finite double as an exact dyadic rational num * 2^exp2.
inline mesh::Dyadic dy(double x) {
    if (x == 0) return {mesh::BigInt(0), 0};
    int e; double f = std::frexp(x, &e);
    return {mesh::BigInt((int64_t)std::ldexp(f, 53)), e - 53};
}
// exactly 6 * (product of the box side lengths): what exactVolumeTimes6 returns for an outward box
inline mesh::Dyadic boxVolumeTimes6(Vec lo, Vec hi) {
    mesh::Dyadic v = mesh::mulExact(mesh::mulExact(mesh::subExact(dy(hi[0]), dy(lo[0])), mesh::subExact(dy(hi[1]), dy(lo[1]))), mesh::subExact(dy(hi[2]), dy(lo[2])));
    return mesh::mulExact(v, dy(6.0));
}
inline mesh::Mesh translated(const mesh::Mesh& m, Vec d) {
    mesh::Mesh r = m;
    for (size_t i = 0; i < r.nv(); i++) for (int k = 0; k < 3; k++) r.v[3 * i + k] += d[k];
    return r;
}
struct Rng {
    std::mt19937_64 g;
    explicit Rng(uint64_t s) : g(s) {}
    double u01() { return (double)(g() >> 11) / 9007199254740992.0; }   // [0,1), exactly representable
    double sym() { return u01() * 2.0 - 1.0; }
    int pick(int n) { return (int)(g() % (uint64_t)n); }
};
}  // namespace tmesh
