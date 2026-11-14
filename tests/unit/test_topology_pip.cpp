// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Topology checks (harness/topology.h) and exact point-in-solid (harness/pip.h).
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include "pip.h"
#include "topology.h"

using namespace mesh;

TEST_CASE("a unit cube is a closed, manifold, outward, single-component surface", "[unit][topology]") {
    Topology t = analyzeTopology(tmesh::unitCube());
    CHECK(t.closedManifold());
    CHECK(t.closedManifoldAsReturned());
    CHECK(t.euler() == 2);
    CHECK(t.components == 1);
    CHECK(t.vertices == 8);
    CHECK(t.edges == 18);
    CHECK(t.faces == 12);
    CHECK_FALSE(t.inverted);
    CHECK_FALSE(t.zeroArea);
    CHECK_FALSE(t.duplicateTri);
    CHECK_FALSE(t.unwelded);
}

TEST_CASE("each malformed class is detected", "[unit][topology]") {
    Mesh m = tmesh::unitCube();
    SECTION("an open surface: one triangle removed") {
        m.t.resize(m.t.size() - 3);
        Topology t = analyzeTopology(m);
        CHECK(t.anyOpen); CHECK(t.anyOpenIdx); CHECK_FALSE(t.closedManifold());
    }
    SECTION("a duplicated triangle") {
        m.addT(m.t[0], m.t[1], m.t[2]);
        CHECK(analyzeTopology(m).duplicateTri);
    }
    SECTION("an inverted orientation") {
        for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3 * k + 1], m.t[3 * k + 2]);
        CHECK(analyzeTopology(m).inverted);
    }
    SECTION("a zero-area triangle (three collinear vertices), exactly") {
        Mesh z; z.addV(0, 0, 0); z.addV(1, 1, 1); z.addV(0.5, 0.5, 0.5); z.addT(0, 1, 2);
        CHECK(analyzeTopology(z).zeroArea);
    }
    SECTION("a triangle that is thinner than any rounding error but not degenerate is not flagged") {
        Mesh z; z.addV(0, 0, 0); z.addV(1, 1, 1); z.addV(0.5, 0.5, std::nextafter(0.5, 1.0)); z.addT(0, 1, 2);
        CHECK_FALSE(analyzeTopology(z).zeroArea);
    }
    SECTION("unwelded vertices: two vertices with exactly equal coordinates") {
        m.addV(m.v[0], m.v[1], m.v[2]);
        CHECK(analyzeTopology(m).unwelded);
    }
    SECTION("a non-manifold edge: three triangles on one edge") {
        Mesh n; n.addV(0, 0, 0); n.addV(1, 0, 0); n.addV(0, 1, 0); n.addV(0, 0, 1); n.addV(0, -1, 0);
        n.addT(0, 1, 2); n.addT(1, 0, 3); n.addT(0, 1, 4);
        CHECK(analyzeTopology(n).nonManifold);
    }
}

TEST_CASE("two cubes sharing one corner form two components that touch at a vertex", "[unit][topology]") {
    Mesh a = tmesh::unitCube(), b = tmesh::box({1, 1, 1}, {2, 2, 2});
    Mesh u = a; for (size_t i = 0; i < b.nv(); i++) u.addV(b.v[3 * i], b.v[3 * i + 1], b.v[3 * i + 2]);
    for (size_t k = 0; k < b.nt(); k++) u.addT(b.t[3 * k] + 8, b.t[3 * k + 1] + 8, b.t[3 * k + 2] + 8);
    Topology t = analyzeTopology(u);
    CHECK(t.unwelded);                 // the shared corner appears twice
    CHECK(t.vertices == 15);           // after welding
    CHECK(t.components == 2);          // edge-connected components: they share no edge
    CHECK(t.closedManifold());
}

TEST_CASE("point in solid is exact, including rays through edges and vertices", "[unit][pip]") {
    const Mesh cube = tmesh::unitCube();
    auto at = [&](double x, double y, double z, int mut = 0) { double p[3] = {x, y, z}; return classifyPoint(cube, p, mut); };
    SECTION("clearly inside and outside") {
        CHECK(at(0.5, 0.5, 0.5).inside);
        CHECK_FALSE(at(2, 0.5, 0.5).inside);
        CHECK_FALSE(at(-2, 0.5, 0.5).inside);
        CHECK_FALSE(at(0.5, 3, 0.5).inside);
    }
    SECTION("points on the surface are reported as boundary") {
        CHECK(at(0.5, 0.5, 1).boundary);
        CHECK(at(0, 0, 0).boundary);
        CHECK(at(1, 0.5, 0).boundary);
    }
    SECTION("a one-ulp step across a face decides inside versus outside") {
        CHECK(at(std::nextafter(1.0, 0.0), 0.5, 0.5).inside);
        CHECK_FALSE(at(std::nextafter(1.0, 2.0), 0.5, 0.5).inside);
    }
    SECTION("the +x ray passes exactly through an edge, a vertex, or along a face plane") {
        CHECK_FALSE(at(-1, 0, 0).inside);          // through the vertex (0,0,0) and the whole bottom-front edge
        CHECK_FALSE(at(-1, 1, 1).inside);
        CHECK_FALSE(at(-1, 0.5, 0).inside);        // along the face z = 0
        CHECK_FALSE(at(-1, 0, 0.5).inside);        // along the face y = 0
        CHECK_FALSE(at(2, 1, 0.25).inside);
    }
    SECTION("an interior point whose ray leaves through an edge") {
        CHECK(at(0.5, 1.0 - std::ldexp(1.0, -53), 0.5).inside);
    }
    SECTION("planted mutants of the point-in-solid routine are caught") {
        // mutant 1: no symbolic tie-break; mutant 3: both crossing directions are counted
        auto battery = [&](int mut) {
            int wrong = 0;
            for (auto p : {std::array<double, 3>{-1, 0, 0}, {-1, 1, 1}, {-1, 0.5, 0}, {-1, 0, 0.5}, {0.5, 0.5, 0.5}, {2, 0.5, 0.5}, {-1, 1, 0.5}, {-1, 0.5, 1}}) {
                bool want = p[0] == 0.5;
                auto r = classifyPoint(cube, p.data(), mut);
                if (!r.boundary && r.inside != want) wrong++;
            }
            return wrong;
        };
        CHECK(battery(0) == 0);
        CHECK(battery(1) > 0);
        CHECK(battery(3) > 0);
    }
}
