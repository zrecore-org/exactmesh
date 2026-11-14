// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// The pipeline's pure scale / re-centre / refuse rules, exactly as written in lib/geogram/mp24b/geogram_fidelity2.cpp (the section is extracted at configure time into
// pipeline_rules.h; no Geogram is involved). Built also with each planted mutant (-DMUT_NOSCALE, -DMUT_SCALE19, -DMUT_C0, -DMUT_TLARGE); the mutant builds must FAIL these tests.
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include "pipeline_rules.h"

using namespace mesh;

namespace {
Mesh points(std::initializer_list<std::array<double, 3>> l) { Mesh m; for (auto& p : l) m.addV(p[0], p[1], p[2]); return m; }
struct Out { Plan p; Mesh ta, tb; };
Out planOf(const Mesh& a, const Mesh& b) { Out o; o.p = plan(a, b, o.ta, o.tb); return o; }
// A cluster far from the origin on x, whose y extent has full-mantissa values: the exact re-centring cannot represent x - c in y, so only the ratio rule decides.
Mesh farCluster(double xlo, double xhi) { return points({{xlo, -0.7, 0.3}, {xhi, 0.9, 0.1}, {(xlo + xhi) / 2, 0.0, 0.5 + 1e-16}}); }
// exact check: scaled value times 2^s equals the original
bool scaledBackEquals(const Mesh& orig, const Mesh& scaled, int s) {
    for (size_t i = 0; i < orig.v.size(); i++) if (std::ldexp(scaled.v[i], s) != orig.v[i]) return false;
    return true;
}
}  // namespace

TEST_CASE("coordinates below 2^18 are not scaled", "[unit][rules]") {
    Mesh a = tmesh::unitCube(), b = tmesh::box({0.5, 0.5, 0.5}, {1.5, 1.5, 1.5});
    Out o = planOf(a, b);
    REQUIRE(o.p.ok);
    CHECK(o.p.s == 0);
    CHECK(planOf(points({{std::nextafter(std::ldexp(1.0, 18), 0.0), 0, 0}, {0, 1, 1}}), points({{0, 0, 0}, {1, 1, 1}})).p.s == 0);
}

TEST_CASE("larger coordinates are scaled by an exact power of two so that max |x| < 2^18", "[unit][rules]") {
    const double o40 = std::ldexp(1.0, 40);
    Mesh a = tmesh::box({o40, 0, 0}, {o40 + 1, 1, 1}), b = tmesh::box({o40 + 0.5, 0, 0}, {o40 + 1.5, 1, 1});
    Out o = planOf(a, b);
    REQUIRE(o.p.ok);
    CHECK(o.p.s == 23);                                // max = 2^40 + 1.5 < 2^41: 41 - 18
    double mx = 0; for (double x : o.ta.v) mx = std::max(mx, std::fabs(x)); for (double x : o.tb.v) mx = std::max(mx, std::fabs(x));
    CHECK(mx < std::ldexp(1.0, 18));
    SECTION("the scaling is lossless: scaled values times 2^s are the original values") {
        // the working meshes are re-centred, so check the scaling alone on a pair that takes the c = 0 path
        Mesh p = points({{std::ldexp(1.0, 30), 0, 0}, {0, 1, 1}}), q = points({{0, 0, 0}, {1, 1, 1}});
        Out r = planOf(p, q);
        REQUIRE(r.p.ok);
        CHECK(r.p.s == 31 - 18);
        if (std::string(r.p.path) == "c0") CHECK(scaledBackEquals(p, r.ta, r.p.s));
    }
    SECTION("the bound is sharp: exactly 2^18 is scaled, one ulp below is not") {
        CHECK(planOf(points({{std::ldexp(1.0, 18), 0, 0}, {0, 1, 1}}), points({{0, 0, 0}, {1, 1, 1}})).p.s == 1);
    }
}

TEST_CASE("an exactly re-centrable pair takes the re-centred path and loses nothing", "[unit][rules]") {
    const double o40 = std::ldexp(1.0, 40);
    Mesh a = tmesh::box({o40, 0, 0}, {o40 + 1, 1, 1}), b = tmesh::box({o40 + 0.5, 0, 0}, {o40 + 1.5, 1, 1});
    Out o = planOf(a, b);
    REQUIRE(o.p.ok);
    CHECK(std::string(o.p.path) == "recentred");
    for (int k = 0; k < 3; k++) CHECK(o.p.c[k] == std::ldexp(std::ldexp(o.p.c[k], -o.p.e), o.p.e));      // c is a multiple of 2^e
    // exactness: (scaled - c) + c == scaled, as exact rational numbers, for every coordinate of both meshes
    for (const Mesh* pair : {&a, &b}) {
        const Mesh& t = pair == &a ? o.ta : o.tb;
        for (size_t i = 0; i < pair->v.size(); i++) {
            double scaled = std::ldexp(pair->v[i], -o.p.s);
            CHECK(equalExact(addExact(tmesh::dy(t.v[i]), tmesh::dy(o.p.c[i % 3])), tmesh::dy(scaled)));
        }
    }
}

TEST_CASE("a pair that cannot be re-centred exactly but whose centre is close to the origin keeps c = 0", "[unit][rules]") {
    Mesh a = points({{0, 0, 0}, {1, 1, 1}, {std::ldexp(1.0, -60), 0, 0}}), b = a;
    Out o = planOf(a, b);
    REQUIRE(o.p.ok);
    CHECK(std::string(o.p.path) == "c0");
    CHECK(o.p.c[0] == 0); CHECK(o.p.c[1] == 0); CHECK(o.p.c[2] == 0);
    CHECK(o.ta.v == a.v);                                  // no coordinate was touched
    CHECK(o.p.ratio <= std::ldexp(1.0, 12));
}

TEST_CASE("a pair far from the origin that cannot be re-centred exactly is refused as a hard limit", "[unit][rules]") {
    Mesh a = farCluster(131071, 131073);
    Out o = planOf(a, a);
    CHECK_FALSE(o.p.ok);
    CHECK(std::string(o.p.path) == "refused");
    CHECK(o.p.reason.rfind("hard limit (far from the origin)", 0) == 0);
    CHECK(o.p.ratio > std::ldexp(1.0, 12));
}

TEST_CASE("the refusal threshold is |bbox centre| / largest side <= 2^12", "[unit][rules]") {
    Out atT = planOf(farCluster(8191, 8193), farCluster(8191, 8193));         // centre 8192, side 2: ratio exactly 4096
    REQUIRE(atT.p.ratio == 4096.0);
    CHECK(atT.p.ok);
    CHECK(std::string(atT.p.path) == "c0");
    Out over = planOf(farCluster(8192, 8194), farCluster(8192, 8194));        // ratio 4096.5
    CHECK(over.p.ratio == 4096.5);
    CHECK_FALSE(over.p.ok);
}

TEST_CASE("scaling that would underflow a nonzero coordinate is refused as a hard limit", "[unit][rules]") {
    Mesh a = points({{0, 0, 0}, {std::ldexp(1.0, 1000), 1, 1}, {std::ldexp(1.0, -100), 0, 0}});
    Out o = planOf(a, a);
    CHECK_FALSE(o.p.ok);
    CHECK(o.p.reason.rfind("hard limit (underflow)", 0) == 0);
    SECTION("zero coordinates are not an underflow") {
        Mesh z = points({{0, 0, 0}, {std::ldexp(1.0, 1000), 1.0, 0}});
        CHECK(planOf(z, z).p.ok);
    }
}
