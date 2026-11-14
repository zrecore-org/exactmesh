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

Result runPipeline(const std::string& op, const Mesh& a, const Mesh& b, const std::string& tag) {
    static int counter = 0;
    fs::path d = workDir(), A = d / (tag + "_A.msh"), B = d / (tag + "_B.msh"), O = d / (tag + "_" + op + "_" + std::to_string(counter++) + ".msh"), L = d / (tag + "_" + op + ".log");
    REQUIRE(writeMesh(A.string(), a));
    REQUIRE(writeMesh(B.string(), b));
    fs::remove(O);
    std::string cmd = "GEOGRAM_RUNNER_THREADS=1 '" + fidelityBinary() + "' boolean " + op + " '" + A.string() + "' '" + B.string() + "' '" + O.string() + "' > /dev/null 2> '" + L.string() + "'";
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
        Mesh A = tmesh::box({0, 0, 0}, {1, 1, 1}), B = tmesh::box({1, 1, 1}, {2, 2, 2});
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

SCENARIO("A unit cube nested in a cube of side 2^20 (bar 2, the nested and large-extent classes)", "[acceptance]") {
    GIVEN("a cube [0, 2^20]^3 and a unit cube [2^19, 2^19 + 1]^3 strictly inside it") {
        const double L = std::ldexp(1.0, 20), m = std::ldexp(1.0, 19);
        Mesh big = tmesh::box({0, 0, 0}, {L, L, L}), small = tmesh::box({m, m, m}, {m + 1, m + 1, m + 1});
        Dyadic bigV = mulExact(mulExact(tmesh::dy(L), tmesh::dy(L)), tmesh::dy(L));
        WHEN("the union is computed through the pipeline") {
            Result r = runPipeline("union", big, small, "nested");
            THEN("the exact volume is 2^60, the volume of the large cube") {
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
            THEN("the exact volume is 2^60 - 1") { requireVolume(r, sixOf(subExact(bigV, tmesh::dy(1.0)))); }
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

SCENARIO("The same input always gives the same bytes", "[acceptance]") {
    GIVEN("the nested-cube pair") {
        const double L = std::ldexp(1.0, 20), m = std::ldexp(1.0, 19);
        Mesh big = tmesh::box({0, 0, 0}, {L, L, L}), small = tmesh::box({m, m, m}, {m + 1, m + 1, m + 1});
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
