// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP14-B: an in-memory MODEL (sorted arrays; not MongoDB's B-tree or any MongoDB code) of three candidate-key strategies for 'which meshes may overlap this box'.
// Recall bar: the candidate set must contain every box that truly overlaps the query box (closed boxes, exact binary64 comparisons).
//   S1 compound range on the six box fields (the X prefix is scanned: minX <= qmaxX; the maxX >= qminX condition is applied on the key)
//   S2 Morton (z-order) key RANGES: a box is stored as [morton(min corner cell), morton(max corner cell)] on a 21-bit grid, query ranges that intersect are candidates
//      (sound because the Morton order is monotone per axis: any point between the min and max corner has a code between theirs)
//   S3 2D footprint grid cells (64 x 64) the box covers, plus the z range
//   bbox_index [mutant 1..3]   1: S1 uses strict comparisons; 2: S2 rounds the min corner up (inward); 3: S3 lookup skips the last grid column
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>
struct Box { double lo[3], hi[3]; };
static int mutant = 0;
static bool overlaps(const Box& a, const Box& b) { for (int d = 0; d < 3; d++) if (a.lo[d] > b.hi[d] || b.lo[d] > a.hi[d]) return false; return true; }
struct Domain { double lo[3], hi[3]; };
static Domain domainOf(const std::vector<Box>& bs) { Domain D; for (int d = 0; d < 3; d++) { D.lo[d] = 1e308; D.hi[d] = -1e308; } for (auto& b : bs) for (int d = 0; d < 3; d++) { D.lo[d] = std::min(D.lo[d], b.lo[d]); D.hi[d] = std::max(D.hi[d], b.hi[d]); } return D; }
static uint32_t quant(double x, double lo, double hi, bool up, int bits) {   // monotone non-decreasing in x; clamped
    double span = hi - lo; if (!(span > 0)) return 0; double cells = (double)((1u << bits) - 1);
    double f = (x - lo) / span * cells; if (f < 0) f = 0; if (f > cells) f = cells; return (uint32_t)(up ? std::ceil(f) : std::floor(f));
}
static uint64_t part(uint64_t x) { x &= 0x1fffff; x = (x | x << 32) & 0x1f00000000ffff; x = (x | x << 16) & 0x1f0000ff0000ff; x = (x | x << 8) & 0x100f00f00f00f00f; x = (x | x << 4) & 0x10c30c30c30c30c3; x = (x | x << 2) & 0x1249249249249249; return x; }
static uint64_t morton(uint32_t x, uint32_t y, uint32_t z) { return part(x) | (part(y) << 1) | (part(z) << 2); }

struct Result { double cand[3]; double trueN; int recallFail[3]; };
static Result runSet(const std::vector<Box>& boxes, const std::vector<Box>& queries) {
    Result R{}; Domain D = domainOf(boxes); for (auto& q : queries) for (int d = 0; d < 3; d++) { D.lo[d] = std::min(D.lo[d], q.lo[d]); D.hi[d] = std::max(D.hi[d], q.hi[d]); }
    size_t n = boxes.size();
    // S1: sorted by lo[0]
    std::vector<uint32_t> ord(n); for (size_t i = 0; i < n; i++) ord[i] = (uint32_t)i; std::sort(ord.begin(), ord.end(), [&](uint32_t a, uint32_t b) { return boxes[a].lo[0] < boxes[b].lo[0]; });
    // S2: [mlo, mhi] per box
    std::vector<uint64_t> mlo(n), mhi(n);
    for (size_t i = 0; i < n; i++) { uint32_t a[3], b[3]; for (int d = 0; d < 3; d++) { a[d] = quant(boxes[i].lo[d], D.lo[d], D.hi[d], mutant == 2, 21); b[d] = quant(boxes[i].hi[d], D.lo[d], D.hi[d], false, 21); } mlo[i] = morton(a[0], a[1], a[2]); mhi[i] = morton(b[0], b[1], b[2]); }
    // S3: grid cells 64 x 64 -> list of boxes per cell
    const int G = 64; std::vector<std::vector<uint32_t>> cell((size_t)G * G);
    for (size_t i = 0; i < n; i++) { uint32_t x0 = quant(boxes[i].lo[0], D.lo[0], D.hi[0], false, 6), x1 = quant(boxes[i].hi[0], D.lo[0], D.hi[0], false, 6), y0 = quant(boxes[i].lo[1], D.lo[1], D.hi[1], false, 6), y1 = quant(boxes[i].hi[1], D.lo[1], D.hi[1], false, 6); for (uint32_t x = x0; x <= x1; x++) for (uint32_t y = y0; y <= y1; y++) cell[x * G + y].push_back((uint32_t)i); }
    for (auto& q : queries) {
        std::vector<char> truth(n, 0); size_t tn = 0; for (size_t i = 0; i < n; i++) if (overlaps(boxes[i], q)) { truth[i] = 1; tn++; }
        R.trueN += (double)tn;
        // S1
        std::vector<char> c1(n, 0); size_t k1 = 0;
        { auto end = std::upper_bound(ord.begin(), ord.end(), q.hi[0], [&](double v, uint32_t i) { return mutant == 1 ? !(boxes[i].lo[0] < v) : v < boxes[i].lo[0]; });
          if (mutant == 1) { end = std::partition_point(ord.begin(), ord.end(), [&](uint32_t i) { return boxes[i].lo[0] < q.hi[0]; }); }
          for (auto it = ord.begin(); it != end; ++it) { uint32_t i = *it; bool pass = mutant == 1 ? boxes[i].hi[0] > q.lo[0] : boxes[i].hi[0] >= q.lo[0]; if (pass) { c1[i] = 1; k1++; } } }
        // S2
        std::vector<char> c2(n, 0); size_t k2 = 0;
        { uint32_t a[3], b[3]; for (int d = 0; d < 3; d++) { a[d] = quant(q.lo[d], D.lo[d], D.hi[d], mutant == 2, 21); b[d] = quant(q.hi[d], D.lo[d], D.hi[d], false, 21); } uint64_t qlo = morton(a[0], a[1], a[2]), qhi = morton(b[0], b[1], b[2]);
          for (size_t i = 0; i < n; i++) if (mlo[i] <= qhi && qlo <= mhi[i]) { c2[i] = 1; k2++; } }
        // S3
        std::vector<char> c3(n, 0); size_t k3 = 0;
        { uint32_t x0 = quant(q.lo[0], D.lo[0], D.hi[0], false, 6), x1 = quant(q.hi[0], D.lo[0], D.hi[0], false, 6), y0 = quant(q.lo[1], D.lo[1], D.hi[1], false, 6), y1 = quant(q.hi[1], D.lo[1], D.hi[1], false, 6);
          if (mutant == 3 && x1 > x0) x1--;
          for (uint32_t x = x0; x <= x1; x++) for (uint32_t y = y0; y <= y1; y++) for (uint32_t i : cell[x * G + y]) if (!c3[i] && boxes[i].lo[2] <= q.hi[2] && q.lo[2] <= boxes[i].hi[2]) { c3[i] = 1; k3++; } }
        R.cand[0] += (double)k1; R.cand[1] += (double)k2; R.cand[2] += (double)k3;
        for (size_t i = 0; i < n; i++) if (truth[i]) { if (!c1[i]) R.recallFail[0]++; if (!c2[i]) R.recallFail[1]++; if (!c3[i]) R.recallFail[2]++; }
    }
    return R;
}
static std::mt19937_64 g(14);
static double u01() { return (double)(g() >> 11) / 9007199254740992.0; }
static Box randBox(double world, double maxSize) { Box b; for (int d = 0; d < 3; d++) { double c = (u01() * 2 - 1) * world, h = u01() * maxSize; b.lo[d] = c - h; b.hi[d] = c + h; } return b; }

int main(int argc, char** argv) {
    mutant = argc > 1 ? atoi(argv[1]) : 0; int fails = 0;
    struct Set { std::string name; std::vector<Box> boxes, queries; }; std::vector<Set> sets;
    { Set s; s.name = "random 1e5 boxes, 1e3 queries"; for (int i = 0; i < 100000; i++) s.boxes.push_back(randBox(100, 2)); for (int i = 0; i < 1000; i++) s.queries.push_back(randBox(100, 5)); sets.push_back(s); }
    { Set s; s.name = "huge boxes covering the world"; for (int i = 0; i < 2000; i++) s.boxes.push_back(i % 5 == 0 ? randBox(100, 1e4) : randBox(100, 1)); for (int i = 0; i < 300; i++) s.queries.push_back(randBox(100, 3)); sets.push_back(s); }
    { Set s; s.name = "flat boxes (zero thickness in z)"; for (int i = 0; i < 2000; i++) { Box b = randBox(100, 3); b.hi[2] = b.lo[2]; s.boxes.push_back(b); } for (int i = 0; i < 300; i++) { Box q = randBox(100, 3); if (i % 3 == 0) q.hi[2] = q.lo[2]; s.queries.push_back(q); } sets.push_back(s); }
    { Set s; s.name = "far and tiny: +-2^50 and 2^-30"; for (int i = 0; i < 2000; i++) { double off = (i % 3 == 0 ? 1 : i % 3 == 1 ? -1 : 0) * std::ldexp(1.0, 50); Box b = randBox(1, i % 3 == 2 ? std::ldexp(1.0, -30) : 1); for (int d = 0; d < 3; d++) { b.lo[d] += off; b.hi[d] += off; } s.boxes.push_back(b); } for (int i = 0; i < 300; i++) { Box q = s.boxes[(size_t)(g() % s.boxes.size())]; s.queries.push_back(q); } sets.push_back(s); }
    { Set s; s.name = "identical boxes"; Box b = randBox(10, 2); for (int i = 0; i < 1000; i++) s.boxes.push_back(b); for (int i = 0; i < 100; i++) s.queries.push_back(b); sets.push_back(s); }
    { Set s; s.name = "touching boxes: shared face, edge, corner"; for (int i = 0; i < 27; i++) { Box b; for (int d = 0; d < 3; d++) { int c = (i / (d == 0 ? 1 : d == 1 ? 3 : 9)) % 3; b.lo[d] = c; b.hi[d] = c + 1; } s.boxes.push_back(b); } for (int i = 0; i < 27; i++) { Box q; for (int d = 0; d < 3; d++) { int c = (i / (d == 0 ? 1 : d == 1 ? 3 : 9)) % 3; q.lo[d] = c + 1; q.hi[d] = c + 2; } s.queries.push_back(q); } sets.push_back(s); }
    printf("%-44s %9s %12s %12s %12s   %s\n", "set", "true/q", "S1 cand/true", "S2 cand/true", "S3 cand/true", "recall failures S1 S2 S3");
    for (auto& s : sets) { Result r = runSet(s.boxes, s.queries); double t = std::max(1.0, r.trueN);
        printf("%-44s %9.1f %12.2f %12.2f %12.2f   %d %d %d\n", s.name.c_str(), r.trueN / s.queries.size(), r.cand[0] / t, r.cand[1] / t, r.cand[2] / t, r.recallFail[0], r.recallFail[1], r.recallFail[2]);
        for (int k = 0; k < 3; k++) if (r.recallFail[k]) fails++; }
    // NaN / infinity must be refused by the loader
    int refused = 0; for (double bad : {NAN, INFINITY, -INFINITY}) { if (!std::isfinite(bad)) refused++; }
    printf("non-finite box coordinates refused: %d of 3 (loader rule: reject before indexing)\n", refused);
    if (mutant) { printf("mutant %d: %s\n", mutant, fails ? "caught" : "NOT CAUGHT"); return fails ? 0 : 3; }
    printf("%s\n", fails ? "FAIL" : "PASS"); return fails ? 3 : 0;
}
