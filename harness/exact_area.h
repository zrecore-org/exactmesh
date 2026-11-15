// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12d: exact surface area of a triangle mesh, defined as the correctly rounded binary64 value (nearest, ties to even) of the true sum of the triangle areas.
// Each area is sqrt(S)/2 with S = |cross|^2 an exact integer (after scaling every coordinate to an integer with one common exponent), so the total is a sum of square roots:
// irrational in general, hence the enclosure refinement below. Single threaded and independent of triangle order (the enclosure bounds are sums of integers).
// Refusals (never a wrong rounding): non-finite coordinates, a result outside the normal binary64 range, an enclosure still undecided at k = 65536 bits.
// Planted mutants for the unit tests (-DAREA_MUT=n): 1 answer taken from the lower bound only; 2 no refinement (k fixed at 32); 3 exact (perfect-square) case truncated
// instead of rounded to nearest; 4 the factor 1/2 of the area dropped.
#pragma once
#include "exact.h"
#include <cmath>
#include <string>
#include <vector>
#ifndef AREA_MUT
#define AREA_MUT 0
#endif
namespace mesh {
// N * 2^p rounded to the nearest binary64 (ties to even). N > 0. Returns false when the result is outside the normal range.
inline bool roundDyadic(const BigInt& N, long p, double& out) {
    if (N == 0) { out = 0.0; return true; }
    long bl = (long)boost::multiprecision::msb(N) + 1, sh = bl > 53 ? bl - 53 : 0;
    BigInt q = N >> sh;
    if (sh > 0) {
        BigInt rem = N - (q << sh), half = BigInt(1) << (sh - 1);
        bool up = rem > half || (rem == half && (q & 1) != 0);
#if AREA_MUT == 3
        up = false;
#endif
        if (up) q += 1;
    }
    long e = p + sh;
    long top = e + (long)boost::multiprecision::msb(q);          // exponent of the leading bit of the result
    if (top < -1022 || top > 1023) return false;
    out = std::ldexp((double)q.convert_to<uint64_t>(), (int)e);
    return true;
}
struct AreaResult { bool ok = false; double area = 0; std::string why; int kUsed = 0; bool exactCase = false; };
inline AreaResult exactSurfaceArea(const Mesh& m) {
    AreaResult R;
    for (double x : m.v) if (!std::isfinite(x)) { R.why = "non-finite coordinate"; return R; }
    int emin = INT32_MAX; std::vector<int> ex(m.v.size()); std::vector<int64_t> mant(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        int e; double f = std::frexp(m.v[i], &e); mant[i] = (int64_t)std::ldexp(f, 53); ex[i] = mant[i] == 0 ? 0 : e - 53;
        if (mant[i] != 0 && ex[i] < emin) emin = ex[i];
    }
    if (emin == INT32_MAX) emin = 0;
    std::vector<BigInt> iv(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) { iv[i] = BigInt(mant[i]); if (mant[i] != 0) iv[i] <<= (ex[i] - emin); }
    std::vector<BigInt> S(m.nt());
    for (size_t k = 0; k < m.nt(); k++) {
        const BigInt *a = &iv[3 * (size_t)m.t[3 * k]], *b = &iv[3 * (size_t)m.t[3 * k + 1]], *c = &iv[3 * (size_t)m.t[3 * k + 2]];
        BigInt u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        BigInt cx = u[1] * w[2] - u[2] * w[1], cy = u[2] * w[0] - u[0] * w[2], cz = u[0] * w[1] - u[1] * w[0];
        S[k] = cx * cx + cy * cy + cz * cz;
    }
#if AREA_MUT == 4
    const long base = 2L * emin;                                  // dropped factor 1/2
#else
    const long base = 2L * emin - 1;                              // area = sum sqrt(S_t) * 2^(2 emin - 1)
#endif
    // all perfect squares: the sum is rational and is rounded exactly
    {
        BigInt sum = 0; bool all = true;
        for (auto& s : S) { BigInt r = boost::multiprecision::sqrt(s); if (r * r != s) { all = false; break; } sum += r; }
        if (all) { R.exactCase = true; R.ok = roundDyadic(sum, base, R.area); if (!R.ok) R.why = "result outside the normal binary64 range"; return R; }
    }
    const long n = (long)S.size();
    for (int k = 32; k <= 65536; k *= 2) {
        BigInt L = 0;
        for (auto& s : S) L += boost::multiprecision::sqrt(s << (2 * k));          // floor(sqrt(S 4^k)): each below the true value by less than 1
        double lo, hi; bool a = roundDyadic(L, base - k, lo), b = roundDyadic(L + n, base - k, hi);
#if AREA_MUT == 1
        if (a) { R.ok = true; R.area = lo; R.kUsed = k; return R; }
#endif
        if (!a && !b) { R.why = "result outside the normal binary64 range"; return R; }
        if (a && b && lo == hi) { R.ok = true; R.area = lo; R.kUsed = k; return R; }
#if AREA_MUT == 2
        if (a) { R.ok = true; R.area = lo; R.kUsed = k; return R; }
#endif
    }
    R.why = "enclosure undecided at 65536 bits"; return R;
}
}  // namespace mesh
