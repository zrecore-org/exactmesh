// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
//
// Acceptance tests in BDD style, one scenario per registered bar (docs/PLAN.md, docs/LIMITS.md). They run the pipeline CLI `geogram_fidelity2` (exact validation, exact scaling and
// re-centring, the pinned MP20+MP22-patched Geogram v1.10.1, translate back) as a black box, on small constructed inputs of the registered families, and judge the result with
// exact arithmetic (harness/exact.h). They need the pinned, patched Geogram build: set EXACTMESH_FIDELITY2=<path of geogram_fidelity2>
// (ci/geogram_xplat.sh build-chain build-fidelity2 builds it). Tagged [acceptance]; never part of the fast suite.
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include "topology.h"
#include "validator_v1.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>

using namespace mesh;
namespace fs = std::filesystem;

namespace {
struct Result {
    int exitCode = -1;
    std::string log;      // stderr of the CLI
    Mesh out;
    bool haveOut = false;
    std::string path;     // "recentred", "c0" or "refused", from the CLI's own summary line
    double translateBackUlps = -1;
};

std::string fidelityBinary() {
    const char* p = std::getenv("EXACTMESH_FIDELITY2");
    REQUIRE(p != nullptr);
    return p;
}
fs::path workDir() { fs::path d = fs::path(EXACTMESH_TEST_TMP) / "acceptance"; fs::create_directories(d); return d; }

Result runPipeline(const std::string& op, const Mesh& a, const Mesh& b, const std::string& tag, const std::string& flags = "") {
    static int counter = 0;
    fs::path d = workDir(), A = d / (tag + "_A.msh"), B = d / (tag + "_B.msh"), O = d / (tag + "_" + op + "_" + std::to_string(counter++) + ".msh"), L = d / (tag + "_" + op + ".log");
    REQUIRE(writeMesh(A.string(), a));
    REQUIRE(writeMesh(B.string(), b));
    fs::remove(O);
    std::string cmd = "GEOGRAM_RUNNER_THREADS=1 '" + fidelityBinary() + "' " + flags + (flags.empty() ? "" : " ") + "boolean " + op + " '" + A.string() + "' '" + B.string() + "' '" + O.string() + "' > /dev/null 2> '" + L.string() + "'";
    int st = std::system(cmd.c_str());
    Result r;
    r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    std::ifstream f(L); std::stringstream ss; ss << f.rdbuf(); r.log = ss.str();
    auto field = [&](const std::string& key) -> std::string {
        size_t i = r.log.rfind("fidelity2:"); if (i == std::string::npos) return "";
        size_t k = r.log.find(key + "=", i); if (k == std::string::npos) return "";
        k += key.size() + 1; size_t e = r.log.find_first_of(" \n", k); return r.log.substr(k, e - k);
    };
    r.path = field("path");
    std::string u = field("translate_back_max_err_ulps"); if (!u.empty()) r.translateBackUlps = std::atof(u.c_str());
    if (r.exitCode == 0) r.haveOut = readMesh(O.string(), r.out);
    return r;
}

void requireVolume(const Result& r, const Dyadic& sixTimesVolume) {
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.haveOut);
    Dyadic got = exactVolumeTimes6(r.out);
    INFO("exact 6*volume of the result: " << toString(got) << ", expected " << toString(sixTimesVolume));
    CHECK(equalExact(got, sixTimesVolume));
}
Dyadic six(double v) { return mulExact(tmesh::dy(v), tmesh::dy(6.0)); }
Dyadic sixOf(const Dyadic& v) { return mulExact(v, tmesh::dy(6.0)); }
}  // namespace

SCENARIO("Two boxes sharing one corner (the vertex-contact family, bars 1 and 4)", "[acceptance]") {
    GIVEN("box A = [0,1]^3 and box B = [1,2]^3, which touch only at the point (1,1,1)") {
        Mesh A = tmesh::boxCorpusOrder({0, 0, 0}, {1, 1, 1}), B = tmesh::boxCorpusOrder({1, 1, 1}, {2, 2, 2});
        WHEN("the union is computed through the pipeline") {
            Result r = runPipeline("union", A, B, "corner");
            THEN("the exact volume is 2") { requireVolume(r, six(2.0)); }
            THEN("the result has no zero-area triangle and no inverted component") {
                REQUIRE(r.haveOut);
                Topology t = analyzeTopology(r.out);
                CHECK_FALSE(t.zeroArea);
                CHECK_FALSE(t.inverted);
            }
        }
        WHEN("the intersection is computed") {
            Result r = runPipeline("intersection", A, B, "corner");
            THEN("the exact volume is 0") { requireVolume(r, six(0.0)); }
        }
        WHEN("the difference A - B is computed") {
            Result r = runPipeline("difference", A, B, "corner");
            THEN("the exact volume is 1") { requireVolume(r, six(1.0)); }
        }
    }
}

SCENARIO("A unit cube nested in a cube of half-width 2^20 (bar 2, the nested and large-extent classes; Geogram defect A needs a distance above 1e6)", "[acceptance]") {
    GIVEN("a cube [-2^20, 2^20]^3 and the unit cube [0, 1]^3 at its centre, more than 1e6 from the enclosing surface") {
        const double L = std::ldexp(1.0, 20);
        Mesh big = tmesh::box({-L, -L, -L}, {L, L, L}), small = tmesh::unitCube();
        Dyadic bigV = mulExact(mulExact(tmesh::dy(2 * L), tmesh::dy(2 * L)), tmesh::dy(2 * L));
        WHEN("the union is computed through the pipeline") {
            Result r = runPipeline("union", big, small, "nested");
            THEN("the exact volume is 2^63, the volume of the large cube") {
                requireVolume(r, sixOf(bigV));
                CHECK(r.translateBackUlps <= 0.5);
            }
        }
        WHEN("the intersection is computed") {
            Result r = runPipeline("intersection", big, small, "nested");
            THEN("the exact volume is 1") { requireVolume(r, six(1.0)); }
        }
        WHEN("the difference large - small is computed") {
            Result r = runPipeline("difference", big, small, "nested");
            THEN("the exact volume is 2^63 - 1") { requireVolume(r, sixOf(subExact(bigV, tmesh::dy(1.0)))); }
        }
    }
}

SCENARIO("A small box near the bottom of a container just under the 2^19 scaling bound (bar 2, the planted-bound trap)", "[acceptance]") {
    GIVEN("a container [-507904, 507904]^3 (31 * 2^14, between 2^18 and 2^19) and a 5 x 5 x 5 box near its bottom face") {
        const double h = 31.0 * 16384;
        Mesh container = tmesh::box({-h, -h, -h}, {h, h, h}), inner = tmesh::box({-2.5, -2.5, -h + 25}, {2.5, 2.5, -h + 30});
        Dyadic cv = mulExact(mulExact(tmesh::dy(2 * h), tmesh::dy(2 * h)), tmesh::dy(2 * h));
        WHEN("the intersection is computed through the pipeline") {
            Result r = runPipeline("intersection", container, inner, "bound");
            THEN("the exact volume is 125") { requireVolume(r, six(125.0)); }
        }
        WHEN("the difference container - inner is computed") {
            Result r = runPipeline("difference", container, inner, "bound");
            THEN("the exact volume is the container volume minus 125") { requireVolume(r, sixOf(subExact(cv, tmesh::dy(125.0)))); }
        }
    }
}

SCENARIO("Two boxes far from the origin (offset 2^40, bar 2)", "[acceptance]") {
    GIVEN("a unit cube at x in [2^40, 2^40 + 1] and a copy shifted by 0.5 in x") {
        const double o = std::ldexp(1.0, 40);
        Mesh A = tmesh::box({o, 0, 0}, {o + 1, 1, 1}), B = tmesh::box({o + 0.5, 0, 0}, {o + 1.5, 1, 1});
        WHEN("the union is computed through the pipeline") {
            Result r = runPipeline("union", A, B, "far40");
            THEN("the exact volume is 1.5") { requireVolume(r, six(1.5)); }
            THEN("the pipeline took the exactly re-centred path") { CHECK(r.path == "recentred"); }
            THEN("the translate-back error is at most half an ulp") { CHECK(r.translateBackUlps <= 0.5); }
        }
        WHEN("the intersection is computed") {
            Result r = runPipeline("intersection", A, B, "far40");
            THEN("the exact volume is 0.5") { requireVolume(r, six(0.5)); }
        }
        WHEN("the difference is computed") {
            Result r = runPipeline("difference", A, B, "far40");
            THEN("the exact volume is 0.5") { requireVolume(r, six(0.5)); }
        }
    }
}

SCENARIO("A thin feature of thickness 2^-40 (bar 3, the sliver class)", "[acceptance]") {
    GIVEN("a unit cube and a plate whose lowest 2^-40 of thickness penetrates the top face") {
        const double w = std::ldexp(1.0, -40);
        Mesh cube = tmesh::unitCube(), plate = tmesh::box({0.25, 0.25, 1 - w}, {0.75, 0.75, 2});
        WHEN("the intersection is computed through the pipeline") {
            Result r = runPipeline("intersection", cube, plate, "sliver");
            THEN("the exact volume is 0.25 * 2^-40 = 2^-42") { requireVolume(r, six(std::ldexp(1.0, -42))); }
        }
        WHEN("the union is computed") {
            Result r = runPipeline("union", cube, plate, "sliver");
            THEN("the exact volume is 1.25") { requireVolume(r, six(1.25)); }
        }
        WHEN("the difference cube - plate is computed") {
            Result r = runPipeline("difference", cube, plate, "sliver");
            THEN("the exact volume is 1 - 2^-42") { requireVolume(r, sixOf(subExact(tmesh::dy(1.0), tmesh::dy(std::ldexp(1.0, -42))))); }
        }
    }
}

SCENARIO("Malformed input is refused, never repaired (bar 1, malformed rows)", "[acceptance]") {
    struct Case { const char* name; const char* reason; Mesh (*make)(); };
    static const Case cases[] = {
        {"an open surface (one triangle removed)", "open surface", [] { Mesh m = tmesh::unitCube(); m.t.resize(m.t.size() - 3); return m; }},
        {"an inverted orientation", "inverted", [] { Mesh m = tmesh::unitCube(); for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3 * k + 1], m.t[3 * k + 2]); return m; }},
        {"a non-manifold edge (two cubes sharing an edge, unwelded)", "non-manifold edge", [] {
            Mesh a = tmesh::unitCube(), b = tmesh::box({1, 1, 0}, {2, 2, 1});
            for (size_t i = 0; i < b.nv(); i++) a.addV(b.v[3 * i], b.v[3 * i + 1], b.v[3 * i + 2]);
            for (size_t k = 0; k < b.nt(); k++) a.addT(b.t[3 * k] + 8, b.t[3 * k + 1] + 8, b.t[3 * k + 2] + 8);
            return a; }},
        {"a zero-area triangle", "zero-area", [] {
            Mesh z; z.addV(0, 0, 0); z.addV(1, 1, 1); z.addV(0.5, 0.5, 0.5); z.addV(0, 0, 1);
            z.addT(0, 1, 2); z.addT(0, 3, 1); z.addT(0, 2, 3); z.addT(1, 3, 2); return z; }},
        {"a duplicated triangle (a two-triangle pillow)", "duplicate", [] {
            Mesh p; p.addV(0, 0, 0); p.addV(1, 0, 0); p.addV(0, 1, 0); p.addT(0, 1, 2); p.addT(0, 2, 1); return p; }},
    };
    for (const Case& c : cases) {
        GIVEN(std::string("a valid unit cube B and an input A with ") + c.name) {
            Mesh A = c.make(), B = tmesh::unitCube();
            WHEN("any operation is requested through the pipeline") {
                Result r = runPipeline("union", A, B, "malformed");
                THEN("it exits with status 3 (REFUSED), names the defect, and writes no result") {
                    CHECK(r.exitCode == 3);
                    CHECK(r.log.find("REFUSED: invalid input A") != std::string::npos);
                    CHECK(r.log.find(c.reason) != std::string::npos);
                    CHECK_FALSE(r.haveOut);
                }
            }
        }
    }
}

SCENARIO("Inputs beyond a hard limit are refused with the reason (MP24b acceptance)", "[acceptance]") {
    GIVEN("two boxes about 2^17 from the origin whose full-mantissa y and z coordinates make an exact re-centring impossible") {
        Mesh A = tmesh::box({131071, -0.7, 0.1}, {131073, 0.9, 0.3}), B = tmesh::box({131071.5, -0.7, 0.1}, {131073.5, 0.9, 0.3});
        WHEN("the union is requested through the pipeline") {
            Result r = runPipeline("union", A, B, "farhard");
            THEN("it exits with status 3 and reports a hard limit: far from the origin") {
                CHECK(r.exitCode == 3);
                CHECK(r.path == "refused");
                CHECK(r.log.find("REFUSED: hard limit (far from the origin)") != std::string::npos);
                CHECK_FALSE(r.haveOut);
            }
        }
    }
    GIVEN("a box of side 2^1000 and a box with a face 2^-100 above the common corner") {
        const double E = std::ldexp(1.0, 1000), w = std::ldexp(1.0, -100);
        Mesh A = tmesh::box({0, 0, 0}, {E, E, E}), B = tmesh::box({0, 0, w}, {E, E, E});
        WHEN("the difference is requested") {
            Result r = runPipeline("difference", A, B, "uflow");
            THEN("it exits with status 3 and reports a hard limit: underflow") {
                CHECK(r.exitCode == 3);
                CHECK(r.log.find("REFUSED: hard limit (underflow)") != std::string::npos);
                CHECK_FALSE(r.haveOut);
            }
        }
    }
}

SCENARIO("Nesting and orientation defects are refused through the pipeline CLI with a V1 reason code (V1 bar 1, wired by V2)", "[acceptance][v1]") {
    // V2 (docs/PLAN.md): the CLI calls v1::validate after the frozen mp21b::validate, so the three former known-limit inputs are decided by the CLI itself.
    using tmesh::append; using tmesh::inverted;
    Mesh cube = tmesh::box({20, 0, 0}, {21, 1, 1});
    auto refusedWith = [&](const Result& r, const char* code, const char* side) {
        CHECK(r.exitCode == 3);
        CHECK(r.log.find(std::string("REFUSED: invalid input ") + side + ": " + code) != std::string::npos);
        CHECK_FALSE(r.haveOut);
    };
    GIVEN("a solid box [0,4]^3") {
        Mesh outer = tmesh::box({0, 0, 0}, {4, 4, 4});
        WHEN("a second box with the SAME orientation is nested inside it, as operand A") {
            Mesh m = append(outer, tmesh::box({1, 1, 1}, {2, 2, 2}));
            THEN("the CLI refuses it as a solid inside a solid, for every operation, and also as operand B") {
                for (const char* op : {"union", "intersection", "difference"}) refusedWith(runPipeline(op, m, cube, "nested_same"), "V1-NESTED-SAME-ORIENTATION", "A");
                refusedWith(runPipeline("union", cube, m, "nested_same_b"), "V1-NESTED-SAME-ORIENTATION", "B");
                refusedWith(runPipeline("union", m, cube, "nested_same_s", "--no-cavities"), "V1-NESTED-SAME-ORIENTATION", "A");
            }
        }
        WHEN("an inverted box is placed beside it, outside, as operand A") {
            Mesh m = append(outer, inverted(tmesh::box({10, 0, 0}, {11, 1, 1})));
            THEN("the CLI refuses it as a negative-volume component outside every solid, in both modes") {
                for (const char* op : {"union", "intersection", "difference"}) refusedWith(runPipeline(op, m, cube, "neg_beside"), "V1-NEGATIVE-OUTSIDE", "A");
                refusedWith(runPipeline("union", cube, m, "neg_beside_b"), "V1-NEGATIVE-OUTSIDE", "B");
                refusedWith(runPipeline("union", m, cube, "neg_beside_s", "--no-cavities"), "V1-NEGATIVE-OUTSIDE", "A");
            }
        }
        WHEN("an inverted box is nested inside it (a cavity)") {
            Mesh m = append(outer, inverted(tmesh::box({1, 1, 1}, {2, 2, 2})));
            THEN("by default the CLI accepts it as a hollow solid: union with a far cube = 6 * (63 + 1)") {
                Result r = runPipeline("union", m, cube, "cavity_default");
                requireVolume(r, six(64.0));
            }
            THEN("with --no-cavities the CLI refuses it with V1-CAVITY-NOT-ALLOWED, as operand A and as operand B") {
                refusedWith(runPipeline("union", m, cube, "cavity_strict", "--no-cavities"), "V1-CAVITY-NOT-ALLOWED", "A");
                refusedWith(runPipeline("union", cube, m, "cavity_strict_b", "--no-cavities"), "V1-CAVITY-NOT-ALLOWED", "B");
            }
        }
        WHEN("an option the CLI does not know is given") {
            std::string cmd = "'" + fidelityBinary() + "' --bogus boolean union a b c > /dev/null 2>&1";
            int st = std::system(cmd.c_str());
            THEN("it is a usage error, exit status 4") { CHECK((WIFEXITED(st) ? WEXITSTATUS(st) : -1) == 4); }
        }
    }
}

SCENARIO("A solid with a cavity and an island inside the cavity is accepted and its Boolean is exact (V1 bar 2; one case only)", "[acceptance][v1]") {
    using tmesh::append; using tmesh::inverted;
    GIVEN("A = box [0,4]^3 with a cavity [1,2]^3, and B = box [3,6] x [0,4] x [0,4]") {
        Mesh A = append(tmesh::box({0, 0, 0}, {4, 4, 4}), inverted(tmesh::box({1, 1, 1}, {2, 2, 2}))), B = tmesh::box({3, 0, 0}, {6, 4, 4});
        THEN("the V1 validator accepts both") { CHECK(v1::validate(A).empty()); CHECK(v1::validate(B).empty()); }
        WHEN("the union is computed through the pipeline") {
            Result r = runPipeline("union", A, B, "cavity");
            THEN("the exact volume is 63 + 48 - 16 = 95") { requireVolume(r, six(95.0)); }
        }
        WHEN("the intersection is computed") {
            Result r = runPipeline("intersection", A, B, "cavity");
            THEN("the exact volume is 16 (the cavity is outside B)") { requireVolume(r, six(16.0)); }
        }
        WHEN("the difference B - A is computed with the cavity moved into B's range") {
            Mesh A2 = append(tmesh::box({0, 0, 0}, {4, 4, 4}), inverted(tmesh::box({3.25, 1, 1}, {3.75, 2, 2})));
            Result r = runPipeline("difference", B, A2, "cavity2");
            THEN("the cavity is part of B - A: exact volume 48 - (16 - 0.5) = 32.5") { requireVolume(r, six(32.5)); }
        }
    }
}

SCENARIO("The same input always gives the same bytes", "[acceptance]") {
    GIVEN("the nested-cube pair of half-width 2^20") {
        const double L = std::ldexp(1.0, 20);
        Mesh big = tmesh::box({-L, -L, -L}, {L, L, L}), small = tmesh::unitCube();
        WHEN("the union is computed twice") {
            Result a = runPipeline("union", big, small, "det"), b = runPipeline("union", big, small, "det");
            THEN("both results are identical, vertex for vertex and triangle for triangle") {
                REQUIRE(a.haveOut); REQUIRE(b.haveOut);
                CHECK(a.out.v == b.out.v);
                CHECK(a.out.t == b.out.t);
            }
        }
    }
}
