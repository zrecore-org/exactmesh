// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Unit tests for harness/exact.h: exact orientation signs, dyadic arithmetic, exact signed volume.
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include <limits>

using namespace mesh;

TEST_CASE("exact orient2d gives the sign of the determinant", "[unit][exact]") {
    const double a[2] = {0, 0}, b[2] = {1, 0}, c[2] = {0, 1};
    CHECK(exactOrient2d(a, b, c) == 1);       // counter-clockwise
    CHECK(exactOrient2d(a, c, b) == -1);      // clockwise
    const double d[2] = {2, 0};
    CHECK(exactOrient2d(a, b, d) == 0);       // collinear
    SECTION("a point one unit in the last place off a diagonal is still decided") {
        const double p[2] = {0, 0}, q[2] = {1, 1}, above[2] = {0.5, std::nextafter(0.5, 1.0)}, below[2] = {0.5, std::nextafter(0.5, 0.0)};
        CHECK(exactOrient2d(p, q, above) == 1);
        CHECK(exactOrient2d(p, q, below) == -1);
    }
    SECTION("non-finite input is reported, not decided") {
        const double nan[2] = {std::numeric_limits<double>::quiet_NaN(), 0};
        CHECK(exactOrient2d(nan, b, c) == 2);
    }
}

TEST_CASE("exact orient3d follows Shewchuk's convention", "[unit][exact]") {
    const double a[3] = {0, 0, 0}, b[3] = {1, 0, 0}, c[3] = {0, 1, 0};
    const double below[3] = {0.25, 0.25, -1}, above[3] = {0.25, 0.25, 1}, on[3] = {0.25, 0.25, 0};
    CHECK(exactOrient3d(a, b, c, below) == 1);    // positive when d lies below the plane abc, abc counter-clockwise from above
    CHECK(exactOrient3d(a, b, c, above) == -1);
    CHECK(exactOrient3d(a, b, c, on) == 0);
    SECTION("a point 2^-60 off the plane is decided exactly") {
        const double tiny[3] = {0.25, 0.25, std::ldexp(1.0, -60)};
        CHECK(exactOrient3d(a, b, c, tiny) == -1);
    }
    SECTION("mixed magnitudes: 2^40 offset and 2^-40 sliver") {
        const double A[3] = {std::ldexp(1.0, 40), 0, 0}, B[3] = {std::ldexp(1.0, 40) + 1, 0, 0}, C[3] = {std::ldexp(1.0, 40), 1, 0}, D[3] = {std::ldexp(1.0, 40) + 0.25, 0.25, -std::ldexp(1.0, -40)};
        CHECK(exactOrient3d(A, B, C, D) == 1);
    }
}

TEST_CASE("dyadic arithmetic is exact", "[unit][exact]") {
    Dyadic a = tmesh::dy(0.1), b = tmesh::dy(0.2);
    Dyadic s = addExact(a, b);
    CHECK_FALSE(equalExact(s, tmesh::dy(0.3)));                    // 0.1 + 0.2 != 0.3 exactly (they are different doubles)
    CHECK(equalExact(subExact(s, b), a));
    CHECK(equalExact(mulExact(tmesh::dy(std::ldexp(1.0, -1000)), tmesh::dy(std::ldexp(1.0, 1000))), tmesh::dy(1.0)));
    CHECK(signExact(subExact(tmesh::dy(1.0), tmesh::dy(std::nextafter(1.0, 2.0)))) == -1);
    CHECK(toString(tmesh::dy(1.0)).find("*2^") != std::string::npos);
}

TEST_CASE("exact signed volume of closed meshes", "[unit][exact]") {
    CHECK(equalExact(exactVolumeTimes6(tmesh::unitCube()), tmesh::dy(6.0)));
    SECTION("a box with a side of 2^-40") {
        tmesh::Vec lo{0, 0, 0}, hi{1, 1, std::ldexp(1.0, -40)};
        CHECK(equalExact(exactVolumeTimes6(tmesh::box(lo, hi)), tmesh::boxVolumeTimes6(lo, hi)));
    }
    SECTION("a box far from the origin keeps its exact volume") {
        tmesh::Vec lo{std::ldexp(1.0, 40), 0, 0}, hi{std::ldexp(1.0, 40) + 1, 1, 1};
        CHECK(equalExact(exactVolumeTimes6(tmesh::box(lo, hi)), tmesh::dy(6.0)));
    }
    SECTION("reversing the orientation negates the volume") {
        Mesh m = tmesh::unitCube();
        for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3 * k + 1], m.t[3 * k + 2]);
        CHECK(signExact(exactVolumeTimes6(m)) == -1);
    }
}

TEST_CASE("the binary mesh format round-trips and rejects bad indices", "[unit][io]") {
    Mesh m = tmesh::box({-1.5, 0, 0.25}, {2, 3, 4});
    const std::string p = std::string(EXACTMESH_TEST_TMP) + "roundtrip.msh";
    REQUIRE(writeMesh(p, m));
    Mesh r; REQUIRE(readMesh(p, r));
    CHECK(r.v == m.v);
    CHECK(r.t == m.t);
    m.t[0] = 99;                                       // index outside the vertex array
    REQUIRE(writeMesh(p, m));
    Mesh bad; CHECK_FALSE(readMesh(p, bad));
    std::remove(p.c_str());
}
