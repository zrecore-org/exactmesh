// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// The exact pre-validator (lib/geogram/mp21b/validator.h): accepts valid closed, manifold, outward meshes and refuses each malformed class with a reason.
// Built also with each planted mutant (-DMUT_NODUP, -DMUT_NOORIENT); the mutant builds must FAIL these tests (CTest WILL_FAIL).
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include "validator_v1.h"
#include <limits>

using namespace mesh;

static std::string why(const Mesh& m) { return mp21b::validate(m); }
static bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

TEST_CASE("valid meshes are accepted", "[unit][validator]") {
    CHECK(why(tmesh::unitCube()).empty());
    CHECK(why(tmesh::box({-3, 2, 1e-30}, {5, 9, 1e-30 + 1e-31})).empty());
    SECTION("far from the origin and thin") {
        CHECK(why(tmesh::box({std::ldexp(1.0, 40), 0, 0}, {std::ldexp(1.0, 40) + 1, 1, std::ldexp(1.0, -40)})).empty());
    }
    SECTION("two closed pieces that touch at a vertex are valid input") {
        Mesh a = tmesh::unitCube(), b = tmesh::box({1, 1, 1}, {2, 2, 2}), u = a;
        for (size_t i = 0; i < b.nv(); i++) u.addV(b.v[3 * i], b.v[3 * i + 1], b.v[3 * i + 2]);
        for (size_t k = 0; k < b.nt(); k++) u.addT(b.t[3 * k] + 8, b.t[3 * k + 1] + 8, b.t[3 * k + 2] + 8);
        CHECK(why(u).empty());
    }
}

TEST_CASE("malformed input is refused with a reason", "[unit][validator]") {
    Mesh m = tmesh::unitCube();
    SECTION("empty mesh") { CHECK(contains(why(Mesh{}), "empty mesh")); }
    SECTION("non-finite coordinate") {
        m.v[4] = std::numeric_limits<double>::infinity();
        CHECK(contains(why(m), "non-finite"));
        m.v[4] = std::numeric_limits<double>::quiet_NaN();
        CHECK(contains(why(m), "non-finite"));
    }
    SECTION("open surface (a triangle removed)") {
        m.t.resize(m.t.size() - 3);
        CHECK(contains(why(m), "open surface"));
    }
    SECTION("non-manifold edge as returned (three triangles on one edge)") {
        Mesh n; n.addV(0, 0, 0); n.addV(1, 0, 0); n.addV(0, 1, 0); n.addV(0, 0, 1); n.addV(0, -1, 0);
        n.addT(0, 1, 2); n.addT(1, 0, 3); n.addT(0, 1, 4);
        CHECK(contains(why(n), "non-manifold edge"));
    }
    SECTION("non-manifold edge that appears only after welding equal vertices (two cubes sharing an edge)") {
        Mesh a = tmesh::unitCube(), b = tmesh::box({1, 1, 0}, {2, 2, 1}), u = a;       // they share the edge x = 1, y = 1
        for (size_t i = 0; i < b.nv(); i++) u.addV(b.v[3 * i], b.v[3 * i + 1], b.v[3 * i + 2]);
        for (size_t k = 0; k < b.nt(); k++) u.addT(b.t[3 * k] + 8, b.t[3 * k + 1] + 8, b.t[3 * k + 2] + 8);
        std::string r = why(u);
        CHECK(contains(r, "non-manifold edge"));
        CHECK(contains(r, "after welding"));
    }
    SECTION("open surface that is closed only by index (duplicated corner vertices do not hide an open edge)") {
        m.addV(m.v[0], m.v[1], m.v[2]);        // a coincident extra vertex
        m.t[0] = 8;                            // one triangle uses the copy: open as returned
        CHECK(contains(why(m), "open surface"));
    }
    SECTION("zero-area triangle (exact test)") {
        Mesh z; z.addV(0, 0, 0); z.addV(1, 1, 1); z.addV(0.5, 0.5, 0.5); z.addV(0, 0, 1);
        z.addT(0, 1, 2); z.addT(0, 3, 1); z.addT(0, 2, 3); z.addT(1, 3, 2);       // a "tetrahedron" with one collapsed face
        CHECK(contains(why(z), "zero-area"));
    }
    SECTION("inverted orientation (exact volume not positive)") {
        for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3 * k + 1], m.t[3 * k + 2]);
        CHECK(contains(why(m), "inverted"));
    }
    SECTION("a mesh whose volume is positive but only by far less than the double-precision error bound is still judged exactly") {
        tmesh::Vec lo{0, 0, 0}, hi{1, 1, std::ldexp(1.0, -1000)};
        CHECK(why(tmesh::box(lo, hi)).empty());
        Mesh r = tmesh::box(lo, hi); for (size_t k = 0; k < r.nt(); k++) std::swap(r.t[3 * k + 1], r.t[3 * k + 2]);
        CHECK(contains(why(r), "inverted"));
    }
}

TEST_CASE("a duplicated triangle inside an otherwise closed surface is refused as such", "[unit][validator]") {
    // two tetrahedra glued along nothing but with one triangle listed twice in opposite order cannot be closed-manifold: build a closed surface of two triangles (a pillow) twice
    Mesh p; p.addV(0, 0, 0); p.addV(1, 0, 0); p.addV(0, 1, 0);
    p.addT(0, 1, 2); p.addT(0, 2, 1);                      // a degenerate "pillow": two opposite triangles on the same three vertices; every edge used once per direction
    CHECK(contains(mp21b::validate(p), "duplicate"));
}

// The gap found by the real-mesh validation (docs/LIMITS.md, history in docs/RESULTS.md "V1"): the frozen validator checked only the sign of the TOTAL exact volume. The same three inputs
// as before are now judged by v1::validate (lib/geogram/v1/validator_v1.h, docs/PLAN.md "V1: validator nesting and orientation"): (b) and (c) are refused with their reason codes,
// (a) is a legitimate cavity (accepted, counted; refused only in strict mode). This was the hidden [known-limit] test; it is now an ordinary passing test.
TEST_CASE("nested and negative-volume components are judged by the V1 validator (was the known limitation)", "[unit][validator][v1]") {
    using tmesh::append; using tmesh::inverted;
    Mesh outer = tmesh::box({0, 0, 0}, {4, 4, 4});
    auto code = [](const Mesh& m, bool cavities = true) { return v1::validate(m, v1::Options{cavities}); };
    SECTION("an inverted inner shell nested in an outer shell is a cavity: accepted and counted, refused in strict mode") {
        Mesh m = append(outer, inverted(tmesh::box({1, 1, 1}, {2, 2, 2})));
        v1::Report rep; CHECK(v1::validate(m, v1::Options{true}, &rep).empty());
        CHECK(rep.components == 2); CHECK(rep.cavities == 1); CHECK(rep.maxDepth == 1);
        CHECK(contains(code(m, false), "V1-CAVITY-NOT-ALLOWED"));
    }
    SECTION("a same-orientation inner shell nested in an outer shell (winding number 2) is refused") {
        CHECK(contains(code(append(outer, tmesh::box({1, 1, 1}, {2, 2, 2}))), "V1-NESTED-SAME-ORIENTATION"));
    }
    SECTION("a negative-volume component beside a larger positive one is refused") {
        CHECK(contains(code(append(outer, inverted(tmesh::box({10, 0, 0}, {11, 1, 1})))), "V1-NEGATIVE-OUTSIDE"));
    }
}

TEST_CASE("historical: the frozen validator (mp21b, still used by the pipeline CLI) accepts the three former known-limit inputs", "[unit][validator][v1]") {
    using tmesh::append; using tmesh::inverted;
    Mesh outer = tmesh::box({0, 0, 0}, {4, 4, 4});
    CHECK(why(append(outer, inverted(tmesh::box({1, 1, 1}, {2, 2, 2})))).empty());
    CHECK(why(append(outer, tmesh::box({1, 1, 1}, {2, 2, 2}))).empty());
    CHECK(why(append(outer, inverted(tmesh::box({10, 0, 0}, {11, 1, 1})))).empty());
}
