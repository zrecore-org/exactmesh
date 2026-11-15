// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Unit tests for harness/exact_area.h (MP12d bars): correctly rounded surface area; also built with each planted mutant (-DAREA_MUT=n), which must FAIL these tests.
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include "exact_area.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <boost/multiprecision/cpp_bin_float.hpp>

using namespace mesh;
using namespace tmesh;
using Big500 = boost::multiprecision::number<boost::multiprecision::cpp_bin_float<500, boost::multiprecision::digit_base_2>>;

namespace {
// libm-free sphere (so the test meshes are identical on every platform): uses the exact rational parametrisation of the unit circle, points of a rational unit sphere
Mesh ratSphere(int rings, int segs) {
    Mesh m; auto circle = [](int j, int segs2, double& c, double& s) {      // rational circle point via t = tan(theta/2) with t = j/segs2 style: (1-t^2)/(1+t^2), 2t/(1+t^2), t in [-1, 1) covers the right half; mirror for the left
        double t = (double)(2 * j + 1) / (double)(2 * segs2 + 2); c = (1 - t * t) / (1 + t * t); s = 2 * t / (1 + t * t); };
    m.addV(0, 0, 1);
    for (int i = 1; i < rings; i++) for (int j = 0; j < segs; j++) {
        double ct, st, cp, sp; circle(i, rings, ct, st); circle(j, segs, cp, sp);
        double sgn = (j & 1) ? -1 : 1; m.addV(st * cp * sgn, st * sp, ct);
    }
    m.addV(0, 0, -1);
    auto V = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segs + (j % segs)); };
    for (int j = 0; j < segs; j++) m.addT(0, V(1, j), V(1, j + 1));
    for (int i = 1; i + 1 < rings; i++) for (int j = 0; j < segs; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    for (int j = 0; j < segs; j++) m.addT((uint32_t)(m.nv() - 1), V(rings - 1, j + 1), V(rings - 1, j));
    return m;
}
Mesh randomSoup(int nt, uint64_t seed, double scale) {
    std::mt19937_64 g(seed); Mesh m; auto u = [&]() { return ((double)(g() >> 11) / 9007199254740992.0 * 2 - 1) * scale; };
    for (int i = 0; i < nt; i++) { uint32_t a = m.addV(u(), u(), u()), b = m.addV(u(), u(), u()), c = m.addV(u(), u(), u()); m.addT(a, b, c); }
    return m;
}
// independent oracle: 500-bit sqrt per triangle, summed, rounded to binary64
double oracleArea(const Mesh& m) {
    Big500 sum = 0;
    for (size_t k = 0; k < m.nt(); k++) {
        const double *a = &m.v[3 * (size_t)m.t[3 * k]], *b = &m.v[3 * (size_t)m.t[3 * k + 1]], *c = &m.v[3 * (size_t)m.t[3 * k + 2]];
        Big500 u[3] = {Big500(b[0]) - Big500(a[0]), Big500(b[1]) - Big500(a[1]), Big500(b[2]) - Big500(a[2])}, w[3] = {Big500(c[0]) - Big500(a[0]), Big500(c[1]) - Big500(a[1]), Big500(c[2]) - Big500(a[2])};
        Big500 cx = u[1] * w[2] - u[2] * w[1], cy = u[2] * w[0] - u[0] * w[2], cz = u[0] * w[1] - u[1] * w[0];
        sum += sqrt(cx * cx + cy * cy + cz * cz) / 2;
    }
    return sum.convert_to<double>();
}
double naiveArea(const Mesh& m) {
    double s = 0;
    for (size_t k = 0; k < m.nt(); k++) {
        const double *a = &m.v[3 * (size_t)m.t[3 * k]], *b = &m.v[3 * (size_t)m.t[3 * k + 1]], *c = &m.v[3 * (size_t)m.t[3 * k + 2]];
        double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        double cx = u[1] * w[2] - u[2] * w[1], cy = u[2] * w[0] - u[0] * w[2], cz = u[0] * w[1] - u[1] * w[0];
        s += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return s;
}
Mesh scaled(const Mesh& a, int e) { Mesh m = a; for (auto& x : m.v) x = std::ldexp(x, e); return m; }
}  // namespace

TEST_CASE("exact area: meshes whose area is rational are exact", "[unit][area]") {
    SECTION("the unit cube has area 6") { auto r = exactSurfaceArea(unitCube()); REQUIRE(r.ok); CHECK(r.area == 6.0); CHECK(r.exactCase); }
    SECTION("a 3 x 4 x 5 box has area 94") { auto r = exactSurfaceArea(box({0, 0, 0}, {3, 4, 5})); REQUIRE(r.ok); CHECK(r.area == 94.0); }
    SECTION("a box at 2^-30 scale has area 6 * 2^-60 exactly") { auto r = exactSurfaceArea(scaled(unitCube(), -30)); REQUIRE(r.ok); CHECK(r.area == std::ldexp(6.0, -60)); }
    SECTION("an exact sum that is not a binary64: 3 unit-leg triangles of area 1/2 each plus 1/10 weights are rounded to nearest, not truncated") {
        Mesh m; for (int i = 0; i < 3; i++) { uint32_t a = m.addV(0, 0, i), b = m.addV(0.1, 0, i), c = m.addV(0, 1, i); m.addT(a, b, c); }   // each area 0.05 (0.1 is not exact in binary: the true area is the product of the two binary64 legs / 2)
        auto r = exactSurfaceArea(m); REQUIRE(r.ok); CHECK(r.area == oracleArea(m));
    }
}

TEST_CASE("exact area: irrational areas agree with an independent 500-bit oracle", "[unit][area]") {
    SECTION("a triangle with area sqrt(3)/2 (cross product (1,-1,1)) is irrational") {
        Mesh m; uint32_t a = m.addV(0, 0, 0), b = m.addV(1, 1, 0), c = m.addV(0, 1, 1); m.addT(a, b, c);
        auto r = exactSurfaceArea(m); REQUIRE(r.ok); CHECK(r.area == oracleArea(m)); CHECK_FALSE(r.exactCase);
    }
    SECTION("rational-parametrised spheres, 6x12 to 40x60") {
        for (auto rs : {std::pair<int, int>{6, 12}, {10, 20}, {20, 30}, {40, 60}}) { Mesh m = ratSphere(rs.first, rs.second); auto r = exactSurfaceArea(m); REQUIRE(r.ok); CHECK(r.area == oracleArea(m)); }
    }
    SECTION("random triangle soups of 1, 2, 10, 1000 triangles") {
        for (int n : {1, 2, 10, 1000}) for (uint64_t seed = 1; seed <= 5; seed++) { Mesh m = randomSoup(n, seed * 31 + (uint64_t)n, 1.0); auto r = exactSurfaceArea(m); REQUIRE(r.ok); CHECK(r.area == oracleArea(m)); }
    }
    SECTION("mixed magnitudes: 2^40 offset soup with 2^-20 spread") {
        Mesh m = randomSoup(50, 5, std::ldexp(1.0, -20)); for (size_t i = 0; i < m.v.size(); i += 3) m.v[i] += std::ldexp(1.0, 40);
        auto r = exactSurfaceArea(m); REQUIRE(r.ok); CHECK(r.area == oracleArea(m));
    }
}

TEST_CASE("exact area: independent of triangle order, and scaling by 2^k scales the area by 2^2k", "[unit][area]") {
    Mesh base = ratSphere(20, 30); auto r0 = exactSurfaceArea(base); REQUIRE(r0.ok);
    SECTION("20 random permutations of the triangles give bit-identical results") {
        std::mt19937_64 g(99);
        for (int rep = 0; rep < 20; rep++) {
            std::vector<size_t> idx(base.nt()); for (size_t i = 0; i < idx.size(); i++) idx[i] = i; std::shuffle(idx.begin(), idx.end(), g);
            Mesh m = base; for (size_t i = 0; i < idx.size(); i++) for (int c = 0; c < 3; c++) m.t[3 * i + c] = base.t[3 * idx[i] + c];
            auto r = exactSurfaceArea(m); REQUIRE(r.ok); CHECK(r.area == r0.area);
        }
    }
    SECTION("2^-30, 2^30 and 2^200 scaled copies: the area is the original times 2^2k, or refused when outside the normal range") {
        for (int e : {-30, 30, 200, -300}) {
            auto r = exactSurfaceArea(scaled(base, e));
            if (std::fabs(std::ldexp(r0.area, 2 * e)) > 1e-300 && std::fabs(std::ldexp(r0.area, 2 * e)) < 1e300) { REQUIRE(r.ok); CHECK(r.area == std::ldexp(r0.area, 2 * e)); }
            else CHECK_FALSE(r.ok);
        }
        auto huge = exactSurfaceArea(scaled(base, 600)); CHECK_FALSE(huge.ok); CHECK(huge.why.find("range") != std::string::npos);
    }
}

TEST_CASE("exact area: a sum a hair above a rounding midpoint needs refinement", "[unit][area]") {
    // Integer scaling uses 53-bit mantissas, so the first estimate (k = 32) already resolves about 2^-138 of the area; refinement matters only for a true sum closer to a
    // rounding boundary than that, which needs a wide exponent range. T1 = (0,0,0), (t,0,0), (0,B,t) with t = 2^-1000 and B = 1 + 2^-51 has cross product (0,-t^2, t B) and area
    // t B / 2 + t^3 / (4 B) + ... : a representable value (even mantissa) plus about 2^-3000. T2 is a right triangle with exact area 2^-1054, half the spacing of binary64 there, so
    // the rational part of the sum is an exact tie and the irrational remainder (2^-3000, far below what a 500-bit oracle can see) puts the true sum just above it: the correct answer is
    // the next binary64 up; rounding the exact tie to even (what any estimate coarser than 2^-3000 sees) gives the value below.
    const double t = std::ldexp(1.0, -1000), B = 1.0 + std::ldexp(1.0, -51);
    Mesh m;
    { uint32_t a = m.addV(0, 0, 0), b = m.addV(t, 0, 0), c = m.addV(0, B, t); m.addT(a, b, c); }
    { double p = std::ldexp(1.0, -500), q = std::ldexp(1.0, -553); uint32_t a = m.addV(0, 0, 7), b = m.addV(p, 0, 7), c = m.addV(0, q, 7); m.addT(a, b, c); }      // area p q / 2 = 2^-1054
    const double base = t * B / 2;                                                   // exact: 2^-1001 (1 + 2^-51), even mantissa
    auto r = exactSurfaceArea(m); REQUIRE(r.ok);
    CHECK(r.area == std::nextafter(base, 1.0));
    CHECK(r.kUsed >= 512);
    CHECK_FALSE(r.exactCase);
    SECTION("T1 alone is the representable value plus a hair: rounds to it") {
        Mesh u; { uint32_t a = u.addV(0, 0, 0), b = u.addV(t, 0, 0), c = u.addV(0, B, t); u.addT(a, b, c); }
        auto ru = exactSurfaceArea(u); REQUIRE(ru.ok); CHECK(ru.area == base);
    }
}

TEST_CASE("exact area: refusals", "[unit][area]") {
    Mesh m = unitCube(); m.v[0] = std::numeric_limits<double>::quiet_NaN();
    auto r = exactSurfaceArea(m); CHECK_FALSE(r.ok); CHECK(r.why == "non-finite coordinate");
}

TEST_CASE("exact area: the naive floating-point sum is not always the correctly rounded value (the function is needed)", "[unit][area][report]") {
    int differ = 0, total = 0;
    for (int n : {10, 100, 1000}) for (uint64_t seed = 1; seed <= 20; seed++) { Mesh m = randomSoup(n, seed * 977 + (uint64_t)n, 1.0); auto r = exactSurfaceArea(m); REQUIRE(r.ok); total++; differ += naiveArea(m) != r.area; }
    INFO("naive differs from the correctly rounded value in " << differ << " of " << total << " random soups");
    CHECK(differ > 0);
}
