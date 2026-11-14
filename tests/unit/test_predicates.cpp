// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Shewchuk's adaptive orient2d/orient3d (the fetched public-domain predicates.c) against the exact integer implementation, on random and adversarial inputs.
// Small N here (fast suite); `predicates_check` runs the same comparison with millions of cases plus a floating-point filter and its planted mutants.
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include <cstdlib>

extern "C" {
void exactinit(void);
double orient2d(double* pa, double* pb, double* pc);
double orient3d(double* pa, double* pb, double* pc, double* pd);
}
using namespace mesh;
static int sgn(double x) { return (x > 0) - (x < 0); }

namespace {
struct Init { Init() { exactinit(); } } g_init;
constexpr int kN = 20000;

void pt2(tmesh::Rng& r, double* p, double scale) { p[0] = r.sym() * scale; p[1] = r.sym() * scale; }
void pt3(tmesh::Rng& r, double* p, double scale) { p[0] = r.sym() * scale; p[1] = r.sym() * scale; p[2] = r.sym() * scale; }
}  // namespace

TEST_CASE("orient2d agrees with the exact sign", "[unit][predicates]") {
    tmesh::Rng r(1);
    int zeros = 0, nonzero = 0;
    SECTION("random points with full mantissas") {
        for (int i = 0; i < kN; i++) {
            double a[2], b[2], c[2]; pt2(r, a, 1); pt2(r, b, 1); pt2(r, c, 1);
            REQUIRE(sgn(orient2d(a, b, c)) == exactOrient2d(a, b, c));
        }
    }
    SECTION("exactly collinear triples and their one-ulp perturbations") {
        for (int i = 0; i < kN; i++) {
            double a[2] = {(double)(r.pick(2001) - 1000), (double)(r.pick(2001) - 1000)}, b[2] = {(double)(r.pick(2001) - 1000), (double)(r.pick(2001) - 1000)};
            int k = r.pick(9) - 4;
            double c[2] = {a[0] + k * (b[0] - a[0]), a[1] + k * (b[1] - a[1])};
            int e = exactOrient2d(a, b, c); REQUIRE(e == 0); REQUIRE(sgn(orient2d(a, b, c)) == 0); zeros++;
            double d[2] = {std::nextafter(c[0], 1e30), c[1]};
            int e2 = exactOrient2d(a, b, d); REQUIRE(sgn(orient2d(a, b, d)) == e2); if (e2 != 0) nonzero++;
        }
        CHECK(zeros == kN);
        CHECK(nonzero > kN / 2);      // the perturbation is detected most of the time (it is invisible only when the segment is vertical)
    }
    SECTION("large offset with a tiny feature") {
        for (int i = 0; i < kN; i++) {
            double o = std::ldexp(1.0, 40);
            double a[2] = {o, o}, b[2] = {o + 1 + r.pick(8), o + 2 + r.pick(8)}, c[2] = {o + 0.5 * r.pick(8), o + 0.5 * r.pick(8) + std::ldexp((double)r.pick(3), -12)};
            REQUIRE(sgn(orient2d(a, b, c)) == exactOrient2d(a, b, c));
        }
    }
}

TEST_CASE("orient3d agrees with the exact sign", "[unit][predicates]") {
    tmesh::Rng r(2);
    SECTION("random points with full mantissas") {
        for (int i = 0; i < kN; i++) {
            double a[3], b[3], c[3], d[3]; pt3(r, a, 1); pt3(r, b, 1); pt3(r, c, 1); pt3(r, d, 1);
            REQUIRE(sgn(orient3d(a, b, c, d)) == exactOrient3d(a, b, c, d));
        }
    }
    SECTION("exactly coplanar integer points on the plane z = x + 2y, then one ulp off") {
        int zeros = 0;
        for (int i = 0; i < kN; i++) {
            auto on = [&](double* p) { p[0] = r.pick(41) - 20; p[1] = r.pick(41) - 20; p[2] = p[0] + 2 * p[1]; };
            double a[3], b[3], c[3], d[3]; on(a); on(b); on(c); on(d);
            REQUIRE(exactOrient3d(a, b, c, d) == 0); REQUIRE(sgn(orient3d(a, b, c, d)) == 0); zeros++;
            d[2] = std::nextafter(d[2], 1e30);
            REQUIRE(sgn(orient3d(a, b, c, d)) == exactOrient3d(a, b, c, d));
        }
        CHECK(zeros == kN);
    }
    SECTION("a far offset (2^40) with a sliver (2^-40)") {
        for (int i = 0; i < kN; i++) {
            double o = std::ldexp(1.0, 40);
            double a[3] = {o, o, o}, b[3] = {o + 1 + r.pick(4), o, o}, c[3] = {o, o + 1 + r.pick(4), o}, d[3] = {o + 0.25 * r.pick(5), o + 0.25 * r.pick(5), o + std::ldexp((double)(r.pick(3) - 1), -12)};
            REQUIRE(sgn(orient3d(a, b, c, d)) == exactOrient3d(a, b, c, d));
        }
    }
    SECTION("tiny coordinates (2^-300; Shewchuk documents that his predicates are exact only while no intermediate underflows, so 2^-500 is outside their domain)") {
        for (int i = 0; i < kN; i++) {
            double s = std::ldexp(1.0, -300);
            double a[3], b[3], c[3], d[3]; pt3(r, a, s); pt3(r, b, s); pt3(r, c, s); pt3(r, d, s);
            REQUIRE(sgn(orient3d(a, b, c, d)) == exactOrient3d(a, b, c, d));
        }
    }
}
