// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V1 validator (lib/geogram/v1/validator_v1.h; docs/PLAN.md "V1: validator nesting and orientation"): per-component exact volume sign and exact nesting.
// Built also with each planted mutant (-DMUT_V1_NOSIGN, -DMUT_V1_NONEST, -DMUT_V1_DOUBLEVOL); the mutant builds must FAIL these tests (CTest WILL_FAIL).
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include "validator_v1.h"

using namespace mesh;
using tmesh::append; using tmesh::inverted; using tmesh::box;

static std::string why(const Mesh& m, bool cavities = true, v1::Report* r = nullptr) { return v1::validate(m, v1::Options{cavities}, r); }
static bool starts(const std::string& s, const char* code) { return s.rfind(code, 0) == 0; }

namespace {
Mesh octahedron(tmesh::Vec c, double r) {
    Mesh m; const double d[6][3] = {{r,0,0},{-r,0,0},{0,r,0},{0,-r,0},{0,0,r},{0,0,-r}};
    for (auto& p : d) m.addV(c[0] + p[0], c[1] + p[1], c[2] + p[2]);
    static const uint32_t T[8][3] = {{0,2,4},{0,5,2},{0,4,3},{0,3,5},{1,4,2},{1,2,5},{1,3,4},{1,5,3}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return signExact(exactVolumeTimes6(m)) < 0 ? inverted(m) : m;
}
// four coplanar points in the plane z = o[2]: closed, manifold, every triangle has non-zero area, the volume is exactly zero
Mesh flatTetra(tmesh::Vec o, double s) {
    Mesh m; m.addV(o[0], o[1], o[2]); m.addV(o[0] + 4*s, o[1], o[2]); m.addV(o[0], o[1] + 4*s, o[2]); m.addV(o[0] + s, o[1] + s, o[2]);
    m.addT(0, 2, 1); m.addT(0, 1, 3); m.addT(1, 2, 3); m.addT(0, 3, 2);
    return m;
}
}  // namespace

TEST_CASE("legitimate hollow and multi-solid inputs are accepted and recorded", "[unit][validator][v1]") {
    v1::Report r;
    SECTION("single component: nothing to nest") { CHECK(why(tmesh::unitCube(), true, &r).empty()); CHECK(r.components == 1); CHECK(r.cavities == 0); }
    SECTION("box with a box cavity") {
        CHECK(why(append(box({0,0,0}, {4,4,4}), inverted(box({1,1,1}, {2,2,2}))), true, &r).empty());
        CHECK(r.components == 2); CHECK(r.cavities == 1); CHECK(r.maxDepth == 1);
    }
    SECTION("sphere-like (octahedron) with an octahedral cavity") {
        CHECK(why(append(octahedron({0,0,0}, 8), inverted(octahedron({0,0,0}, 3))), true, &r).empty()); CHECK(r.cavities == 1);
    }
    SECTION("two-level: solid, cavity, island inside the cavity") {
        Mesh m = append(append(box({0,0,0}, {30,30,30}), inverted(box({4,4,4}, {26,26,26}))), box({10,10,10}, {14,14,14}));
        CHECK(why(m, true, &r).empty()); CHECK(r.components == 3); CHECK(r.cavities == 1); CHECK(r.maxDepth == 2);
    }
    SECTION("four levels: + - + -") {
        Mesh m = append(append(append(box({0,0,0}, {40,40,40}), inverted(box({2,2,2}, {38,38,38}))), box({6,6,6}, {34,34,34})), inverted(box({10,10,10}, {20,20,20})));
        CHECK(why(m, true, &r).empty()); CHECK(r.cavities == 2); CHECK(r.maxDepth == 3);
    }
    SECTION("two cavities side by side; two disjoint solids; an octahedron in a bounding-box corner outside a sphere-like solid") {
        CHECK(why(append(append(box({0,0,0}, {30,10,10}), inverted(box({2,2,2}, {8,8,8}))), inverted(box({12,2,2}, {18,8,8}))), true, &r).empty()); CHECK(r.cavities == 2);
        CHECK(why(append(box({0,0,0}, {1,1,1}), box({5,0,0}, {6,1,1})), true, &r).empty()); CHECK(r.cavities == 0);
        CHECK(why(append(octahedron({0,0,0}, 8), box({5, 5, 5}, {7.5, 7.5, 7.5}))).empty());   // the corner (5,5,5) is outside |x|+|y|+|z| <= 8, inside the bounding box
    }
    SECTION("a cavity one ulp from the outer wall is decided exactly") {
        double u = std::nextafter(4.0, 0.0);
        CHECK(why(append(box({0,0,0}, {4,4,4}), inverted(box({1,1,1}, {u,u,u})))).empty());
        CHECK(why(append(box({0,0,0}, {4,4,4}), inverted(box({std::nextafter(0.0, 1.0), 1, 1}, {2,2,2})))).empty());
    }
    SECTION("pieces touching at one vertex are separate components (labelling is by shared edges)") {
        CHECK(why(append(box({0,0,0}, {1,1,1}), box({1,1,1}, {2,2,2})), true, &r).empty()); CHECK(r.components == 2);
    }
    SECTION("documented limit: a cavity whose surface touches the outer wall is accepted (touching or overlapping surfaces are not checked; another vertex of the cavity decides)") {
        CHECK(why(append(box({0,0,0}, {4,4,4}), inverted(box({0,0,0}, {2,2,2})))).empty());       // one shared vertex, three coplanar faces
        CHECK(why(append(box({0,0,0}, {4,4,4}), inverted(box({1,1,1}, {2,2,4})))).empty());       // a face in the wall plane z = 4, corners on the wall
    }
}

TEST_CASE("nesting and orientation defects are refused with their reason code", "[unit][validator][v1]") {
    Mesh outer = box({0,0,0}, {4,4,4});
    SECTION("same orientation inside (a solid inside a solid)") { CHECK(starts(why(append(outer, box({1,1,1}, {2,2,2}))), "V1-NESTED-SAME-ORIENTATION")); }
    SECTION("same orientation inside, spheres") { CHECK(starts(why(append(octahedron({0,0,0}, 8), octahedron({0,0,0}, 3))), "V1-NESTED-SAME-ORIENTATION")); }
    SECTION("negative volume beside a larger solid") { CHECK(starts(why(append(outer, inverted(box({10,0,0}, {11,1,1})))), "V1-NEGATIVE-OUTSIDE")); }
    SECTION("negative box in the bounding-box corner of an octahedron, outside it (the containment test must say no)") {
        CHECK(starts(why(append(octahedron({0,0,0}, 8), inverted(box({5,5,5}, {7.5,7.5,7.5})))), "V1-NEGATIVE-OUTSIDE"));
    }
    SECTION("a cavity inside a cavity") {
        Mesh m = append(append(box({0,0,0}, {30,30,30}), inverted(box({4,4,4}, {26,26,26}))), inverted(box({8,8,8}, {14,14,14})));
        CHECK(starts(why(m), "V1-NESTED-SAME-ORIENTATION"));
    }
    SECTION("a solid inside the island inside the cavity") {
        Mesh m = append(append(append(box({0,0,0}, {40,40,40}), inverted(box({2,2,2}, {38,38,38}))), box({6,6,6}, {34,34,34})), box({10,10,10}, {20,20,20}));
        CHECK(starts(why(m), "V1-NESTED-SAME-ORIENTATION"));
    }
    SECTION("zero-volume component (coplanar tetrahedron), beside and inside a solid") {
        CHECK(starts(why(append(outer, flatTetra({10, 0, 0}, 1))), "V1-ZERO-VOLUME-COMPONENT"));
        CHECK(starts(why(append(outer, flatTetra({1, 1, 1}, 0.5))), "V1-ZERO-VOLUME-COMPONENT"));
    }
    SECTION("all vertices of the inner component on the outer surface: the nesting cannot be decided") {
        // octahedron inscribed in the box [0,4]^3 with its six vertices at the face centres (on the diagonals of the face triangulation)
        CHECK(starts(why(append(outer, octahedron({2,2,2}, 2))), "V1-SURFACES-SHARED"));
    }
    SECTION("strict mode refuses any cavity after the other rules") {
        CHECK(starts(why(append(outer, inverted(box({1,1,1}, {2,2,2}))), false), "V1-CAVITY-NOT-ALLOWED"));
        CHECK(starts(why(append(outer, box({1,1,1}, {2,2,2})), false), "V1-NESTED-SAME-ORIENTATION"));
    }
    SECTION("frozen checks keep their reasons: a single inverted mesh is still 'inverted', a negative total volume too") {
        CHECK(why(inverted(outer)).find("inverted") != std::string::npos);
        CHECK(why(append(box({0,0,0}, {1,1,1}), inverted(box({5,0,0}, {9,4,4})))).find("inverted") != std::string::npos);
    }
}

TEST_CASE("component volume signs are exact, far from the origin and thin", "[unit][validator][v1]") {
    const double o = std::ldexp(1.0, 40), TH = std::ldexp(1.0, -20);
    Mesh big = box({o, 0, 0}, {o + 8, 8, 8});
    for (int i = 0; i < 20; i++) {
        INFO("variant " << i);
        Mesh thinNeg = inverted(box({o + 10, 1.0 + i % 4, 1}, {o + 12, 3.0 + i % 4, 1 + TH}));
        CHECK(starts(why(append(big, thinNeg)), "V1-NEGATIVE-OUTSIDE"));
        Mesh thinIn = box({o + 1, 1.0 + i % 4, 1}, {o + 3, 3.0 + i % 4, 1 + TH});
        CHECK(starts(why(append(big, thinIn)), "V1-NESTED-SAME-ORIENTATION"));
        CHECK(why(append(big, inverted(thinIn))).empty());      // the same thin box inverted, inside: a legitimate cavity
    }
}

TEST_CASE("documented limit: crossing component surfaces are not detected", "[unit][validator][v1]") {
    // two overlapping positive boxes: neither lies inside the other by the vertex test (bounding boxes are not nested), so nothing is refused. There is no self-intersection check.
    CHECK(why(append(box({0,0,0}, {4,4,4}), box({2,2,2}, {6,6,6}))).empty());
}
