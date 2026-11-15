// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Library scoring tool (MP02 canonical equivalence). One yardstick for every boolean engine; the pass bars are pre-registered in PLAN.md.
//   mesh_compare score <corpusdir> <caseId> <union|intersection|difference> <result.msh> [points]
//   mesh_compare selftest
// For each result it reports, all exact: (1) validity of the inputs as solids; (2) topology of the result (closed manifold? Euler characteristic, components);
// (3) the exact volume against the manifest's exact expectation (box cases) classified exact / close / wrong; (4) point-in-solid agreement of the result with
// the boolean of A and B: for box pairs EVERY cell of the arrangement is tested (the per-axis midpoints of the sorted box coordinates, plus a cell outside), for
// other pairs `points` exact dyadic sample points (default 5000); points that lie on a surface are skipped and counted.
#include "../harness/pip.h"
#include "../harness/topology.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

extern "C" void exactinit();   // Shewchuk's predicates.c: sets the splitter and the error bounds. Without it orient2d/orient3d return the plain double determinant (V3 found this; V4 fixed it).
using namespace mesh;

static bool parseDyadic(const std::string& s, Dyadic& d) {
    size_t star = s.find("*2^"); if (star == std::string::npos) return false;
    d.num = BigInt(s.substr(0, star)); d.exp2 = atoi(s.c_str() + star + 3); return true;
}
// a / b as a double (b != 0), for a = a.num * 2^a.exp2 and b likewise. V4: the old computation aligned both integers at the smaller exponent and divided their doubles; when the exponents differ by
// about 1024 or more the aligned integers overflow to inf (inf/inf = nan, x/inf = 0), which printed "relative nan" and classified a tiny relative error as "wrong". The old computation is kept
// bit-for-bit whenever both aligned integers are finite doubles; otherwise both numerators are normalised to 64 bits plus a binary exponent, divided, and scaled with ldexp (the relative error of
// that path is about 2^-63).
static double ratioOverflowing(const Dyadic& a, const Dyadic& b) {
    int e = std::min(a.exp2, b.exp2);
    return align(a, e).num.convert_to<double>() / align(b, e).num.convert_to<double>();
}
static double normalised64(const BigInt& n, long& exp2) {        // n = result * 2^exp2 (to 64 bits), result a double with 2^63 <= |result| < 2^64, or 0
    if (n == 0) { exp2 = 0; return 0.0; }
    BigInt m = n < 0 ? BigInt(-n) : n;
    long sh = (long)boost::multiprecision::msb(m) + 1 - 64;
    if (sh > 0) m >>= sh; else m <<= -sh;
    exp2 = sh;
    double d = m.convert_to<double>();
    return n < 0 ? -d : d;
}
static double ratio(const Dyadic& a, const Dyadic& b) {
#ifdef MUT_OVERFLOWRATIO
    return ratioOverflowing(a, b);                                    // planted bug: the pre-V4 computation
#else
    int e = std::min(a.exp2, b.exp2);
    double x = align(a, e).num.convert_to<double>(), y = align(b, e).num.convert_to<double>();
    if (std::isfinite(x) && std::isfinite(y)) return x / y;          // unchanged for every value that did not overflow
    long sa, sb; double da = normalised64(a.num, sa), db = normalised64(b.num, sb);
    return std::ldexp(da / db, (int)((a.exp2 + sa) - (b.exp2 + sb)));
#endif
}
static bool isAxisBox(const Mesh& m, std::vector<double> xs[3]) {
    if (m.nv() != 8 || m.nt() != 12) return false;
    for (int k = 0; k < 3; k++) { for (size_t i = 0; i < 8; i++) xs[k].push_back(m.v[3*i+k]); std::sort(xs[k].begin(), xs[k].end()); xs[k].erase(std::unique(xs[k].begin(), xs[k].end()), xs[k].end()); if (xs[k].size() > 2) return false; }
    return true;
}
static bool inOp(int op, bool a, bool b) { return op == 0 ? (a || b) : op == 1 ? (a && b) : (a && !b); }
static const char* kOpName[] = {"union", "intersection", "difference"};

struct Score { int cells = 0, cellBad = 0, cellSkipped = 0, pts = 0, ptsBad = 0, ptsSkipped = 0; };

static void testPoint(const Mesh& A, const Mesh& B, const Mesh& R, int op, const double* p, bool cell, Score& s) {
    Pip a = classifyPoint(A, p), b = classifyPoint(B, p), r = classifyPoint(R, p);
    int& n = cell ? s.cells : s.pts; int& bad = cell ? s.cellBad : s.ptsBad; int& skip = cell ? s.cellSkipped : s.ptsSkipped;
    if (a.boundary || b.boundary || r.boundary) { skip++; return; }
    n++; if (r.inside != inOp(op, a.inside, b.inside)) bad++;
}

static int score(const std::string& dir, const std::string& id, int op, const std::string& resultPath, int npts, bool quiet = false, Mesh* givenR = nullptr, Mesh* givenA = nullptr, Mesh* givenB = nullptr, const std::string* expRow = nullptr) {
    std::vector<std::string> c;
    std::string line;
    if (expRow) line = *expRow; else {
        std::ifstream f(dir + "/manifest.tsv"); std::getline(f, line);
        while (std::getline(f, line)) { if (line.compare(0, id.size() + 1, id + "\t") == 0) break; line.clear(); }
        if (line.empty()) { printf("case %s not in manifest\n", id.c_str()); return 2; }
    }
    { std::stringstream ss(line); std::string x; while (std::getline(ss, x, '\t')) c.push_back(x); }
    Mesh A, B, R;
    if (givenA) A = *givenA; else if (!readMesh(dir + "/adversarial/" + id + "_A.msh", A)) { printf("cannot load A\n"); return 2; }
    if (givenB) B = *givenB; else if (!readMesh(dir + "/adversarial/" + id + "_B.msh", B)) { printf("cannot load B\n"); return 2; }
    if (givenR) R = *givenR; else if (!readMesh(resultPath, R)) { printf("%s %s: FAIL cannot load result\n", id.c_str(), kOpName[op]); return 1; }
    std::vector<std::string> fails;
    for (double x : R.v) if (!std::isfinite(x)) { fails.push_back("non-finite coordinate in result"); break; }
    Topology ta = analyzeTopology(A), tb = analyzeTopology(B), tr = analyzeTopology(R);
    bool validInputs = ta.closedManifold() && tb.closedManifold() && !ta.inverted && !tb.inverted;
    Dyadic vr = exactVolumeTimes6(R);
    // volume against the manifest's exact expectation
    std::string volStatus = "n/a";
    double rel = 0;
    if (c[9 + op] != "-") {
        Dyadic ex; parseDyadic(c[9 + op], ex);
        Dyadic va, vb; parseDyadic(c[7], va); parseDyadic(c[8], vb);
        if (equalExact(vr, ex)) volStatus = "exact";
        else {
            Dyadic diff = subExact(vr, ex);
            if (signExact(ex) != 0) { rel = ratio(diff, ex); volStatus = std::fabs(rel) <= 1e-9 ? "close" : "wrong"; }
            else { Dyadic scale = signExact(subExact(va, vb)) >= 0 ? va : vb; double r0 = std::fabs(ratio(diff, scale)); rel = r0; volStatus = r0 <= std::ldexp(1.0, -52) ? "close" : "wrong"; }
        }
        if (volStatus == "wrong") fails.push_back("exact volume differs from the expectation by relative " + std::to_string(rel));
    }
    // structure of the result
    bool empty = R.nt() == 0;
    // Amendments 1 and 3 (PLAN.md): a result passes the structure check if it is a closed manifold as returned (by vertex index), OR it has no boundary edge after identifying coincident
    // vertices (it may then have an edge or vertex shared by more than two faces: the exact union of two solids that touch along an edge or at a corner is geometrically non-manifold, and
    // either representation of it is accepted). Open surfaces always fail. The volume and point tests still apply.
    if (!empty && !(tr.closedManifoldAsReturned() || (!tr.anyOpen && tr.faces > 0))) fails.push_back("result is not closed (it has a boundary edge, also after identifying coincident vertices)");
    if (validInputs && !empty && tr.inverted) fails.push_back("result has negative volume (inverted)");
    if (validInputs && !empty && tr.duplicateTri) fails.push_back("result has a duplicated triangle");
    // point classification
    Score s;
    if (validInputs) {
        std::vector<double> xs[3], ys[3];
        bool boxes = isAxisBox(A, xs) && isAxisBox(B, ys);
        if (boxes) {
            std::vector<double> ax[3];
            for (int k = 0; k < 3; k++) {
                ax[k] = xs[k]; ax[k].insert(ax[k].end(), ys[k].begin(), ys[k].end());
                std::sort(ax[k].begin(), ax[k].end()); ax[k].erase(std::unique(ax[k].begin(), ax[k].end()), ax[k].end());
            }
            std::vector<double> cen[3];
            for (int k = 0; k < 3; k++) {
                cen[k].push_back(ax[k].front() - 1.0);
                for (size_t i = 0; i + 1 < ax[k].size(); i++) { double m = ax[k][i] + (ax[k][i+1] - ax[k][i]) / 2; if (m > ax[k][i] && m < ax[k][i+1]) cen[k].push_back(m); }   // skip gaps with no representable midpoint
            }
            for (double x : cen[0]) for (double y : cen[1]) for (double z : cen[2]) { double p[3] = {x, y, z}; testPoint(A, B, R, op, p, true, s); }
        }
        // dyadic samples in the expanded bounding box
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (const Mesh* mm : {&A, &B}) for (size_t i = 0; i < mm->nv(); i++) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], mm->v[3*i+k]); hi[k] = std::max(hi[k], mm->v[3*i+k]); }
        double mx = 1; for (int k = 0; k < 3; k++) mx = std::max(mx, std::max(std::fabs(lo[k]), std::fabs(hi[k])));
        double g = std::max(std::ldexp(1.0, -14), std::ldexp(1.0, (int)std::ceil(std::log2(mx)) - 50));
        std::mt19937_64 rng(12345);
        for (int i = 0; i < npts; i++) {
            double p[3];
            for (int k = 0; k < 3; k++) { double span = hi[k] - lo[k]; double v = lo[k] - 0.25 * span + (double)(rng() >> 11) / 9007199254740992.0 * 1.5 * span; p[k] = (std::floor(v / g) + 0.5) * g; }
            testPoint(A, B, R, op, p, false, s);
        }
        if (s.cellBad) fails.push_back(std::to_string(s.cellBad) + " arrangement cell(s) classified differently from the boolean of A and B");
        if (s.ptsBad) fails.push_back(std::to_string(s.ptsBad) + " sample point(s) classified differently from the boolean of A and B");
    }
    bool pass = fails.empty();
    if (!quiet) {
        printf("%s %s: %s | inputs=%s | result tri=%zu closed=%d geomClosed=%d euler=%ld components=%d zeroArea=%d | volume=%s", id.c_str(), kOpName[op], pass ? (validInputs ? "PASS" : "ACCEPTED-UNVERIFIED") : "FAIL",
               validInputs ? "valid" : "invalid", R.nt(), (int)tr.closedManifoldAsReturned(), (int)tr.closedManifold(), tr.euler(), tr.components, (int)tr.zeroArea, volStatus.c_str());
        if (volStatus == "close") printf("(rel %.3g)", rel);
        printf(" | cells %d bad %d skipped %d | samples %d bad %d skipped %d", s.cells, s.cellBad, s.cellSkipped, s.pts, s.ptsBad, s.ptsSkipped);
        for (auto& f : fails) printf(" | %s", f.c_str());
        printf("\n");
    }
    return pass ? 0 : 1;
}

// ---------------- self-test ----------------
static Mesh box(double x0, double y0, double z0, double x1, double y1, double z1) {
    Mesh m; double lo[3] = {x0, y0, z0}, hi[3] = {x1, y1, z1};
    for (int z = 0; z < 2; z++) { double Z = z ? hi[2] : lo[2]; m.addV(lo[0], lo[1], Z); m.addV(hi[0], lo[1], Z); m.addV(hi[0], hi[1], Z); m.addV(lo[0], hi[1], Z); }
    static const uint32_t T[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
static std::string boxRow(const char* id, const Mesh& A, const Mesh& B, const Dyadic& u, const Dyadic& i, const Dyadic& d) {
    return std::string(id) + "\tx\tvalid\t8\t12\t8\t12\t" + toString(exactVolumeTimes6(A)) + "\t" + toString(exactVolumeTimes6(B)) + "\t" + toString(u) + "\t" + toString(i) + "\t" + toString(d);
}

static int selftest() {
    int bad = 0;
    auto expect = [&](const char* what, int got, int want) { bool ok = got == want; printf("  %-72s %s\n", what, ok ? "ok" : "WRONG"); if (!ok) bad++; };
    Mesh A = box(0, 0, 0, 1, 1, 1), B = box(0.5, 0.25, -0.5, 1.5, 1.25, 0.5);
    Mesh I = box(0.5, 0.25, 0, 1, 1, 0.5);              // A intersect B
    Dyadic vA = exactVolumeTimes6(A), vB = exactVolumeTimes6(B), vI = exactVolumeTimes6(I);
    Dyadic vU = subExact(addExact(vA, vB), vI), vD = subExact(vA, vI);
    std::string row = boxRow("t", A, B, vU, vI, vD);
    std::string d = "";
    printf("scoring known-good and known-bad results (every wrong one must FAIL):\n");
    expect("correct intersection result passes", score(d, "t", 1, "", 100, true, &I, &A, &B, &row), 0);
    expect("intersection result replaced by A fails", score(d, "t", 1, "", 100, true, &A, &A, &B, &row), 1);
    Mesh Rm = I; Rm.t.resize(Rm.t.size() - 3);
    expect("intersection result with one triangle missing (open) fails", score(d, "t", 1, "", 100, true, &Rm, &A, &B, &row), 1);
    Mesh Ri = I; for (size_t k = 0; k < Ri.nt(); k++) std::swap(Ri.t[3*k+1], Ri.t[3*k+2]);
    expect("intersection result with inverted orientation fails", score(d, "t", 1, "", 100, true, &Ri, &A, &B, &row), 1);
    Mesh Rs = I; Rs.v[3 * 6] += std::ldexp(1.0, -20);
    expect("intersection result with a vertex moved by 2^-20 fails", score(d, "t", 1, "", 100, true, &Rs, &A, &B, &row), 1);
    Mesh Rt = I; Rt.v[3 * 6] += std::ldexp(1.0, -45);
    expect("intersection result with a vertex moved by 2^-45 is within the stated 1e-9 volume tolerance (passes)", score(d, "t", 1, "", 100, true, &Rt, &A, &B, &row), 0);
    Mesh E;   // empty
    expect("empty result for a non-empty intersection fails", score(d, "t", 1, "", 100, true, &E, &A, &B, &row), 1);
    std::string rowSame = boxRow("s", A, A, vA, vA, Dyadic{0, 0});
    expect("A minus A = empty passes", score(d, "s", 2, "", 100, true, &E, &A, &A, &rowSame), 0);
    expect("A union A = A passes", score(d, "s", 0, "", 100, true, &A, &A, &A, &rowSame), 0);
    Mesh A2 = box(0, 0, 0, 1, 1, 2);
    expect("A union A = a different box fails", score(d, "s", 0, "", 100, true, &A2, &A, &A, &rowSame), 1);
    printf("planted bugs in the exact point-in-solid test (each must be caught on the unit cube):\n");
    // points aligned with the face diagonal (0.25, 0.25) and with an edge, where a ray hits an edge or vertex exactly
    struct P { double p[3]; bool inside; };
    std::vector<P> pts; for (double y : {0.25, 0.5, 0.75, 0.0, -0.25, 1.0}) for (double z : {0.25, 0.5, 0.75, 0.0, -0.25, 1.0}) for (double x : {-1.0, 0.125, 0.5, 2.0}) {
        bool in = x > 0 && x < 1 && y > 0 && y < 1 && z > 0 && z < 1; pts.push_back({{x, y, z}, in}); }
    int wrong0 = 0, wrong1 = 0, wrong3 = 0, bnd = 0;
    for (auto& q : pts) {
        Pip r0 = classifyPoint(A, q.p, 0), r1 = classifyPoint(A, q.p, 1), r3 = classifyPoint(A, q.p, 3);
        bool onB = (q.p[1] == 0 || q.p[1] == 1 || q.p[2] == 0 || q.p[2] == 1) && q.p[0] >= 0 && q.p[0] <= 1 && q.p[1] >= 0 && q.p[1] <= 1 && q.p[2] >= 0 && q.p[2] <= 1;
        if (r0.boundary) { bnd++; if (!onB && !(q.p[0] == 0 || q.p[0] == 1)) wrong0++; continue; }
        if (onB) { wrong0++; continue; }
        wrong0 += r0.inside != q.inside; wrong1 += r1.inside != q.inside; wrong3 += r3.inside != q.inside;
    }
    printf("  %zu points (rays through faces, edges, vertices and diagonals), %d on the surface skipped\n", pts.size(), bnd);
    expect("correct classifier agrees with the analytic answer everywhere", wrong0, 0);
    expect("mutant 1 (no symbolic tie-break) is caught", wrong1 > 0, 1);
    expect("mutant 3 (ignore the crossing direction) is caught", wrong3 > 0, 1);
    // consistency: swapping x and y changes the ray direction relative to the mesh; the classification must not change
    // consistency: permuting the coordinate axes of mesh AND point changes the ray direction relative to the mesh; the classification must not change
    {
        auto readM = [](const char* path, Mesh& m) { return readMesh(path, m); };
        Mesh sph; bool have = readM("build/corpus/primitives/icosphere_3.msh", sph) || readM("../build/corpus/primitives/icosphere_3.msh", sph);
        if (have) {
            Mesh sw = sph; for (size_t i = 0; i < sw.nv(); i++) std::swap(sw.v[3*i], sw.v[3*i+1]);
            std::mt19937_64 rng(5); int disagree = 0, n = 0, skip = 0;
            for (int i = 0; i < 600; i++) {
                double p[3]; for (int k = 0; k < 3; k++) p[k] = (std::floor(((double)(rng() >> 11) / 9007199254740992.0 * 2.4 - 1.2) * 4096) + 0.5) / 4096;
                double q[3] = {p[1], p[0], p[2]};
                Pip a = classifyPoint(sph, p), b = classifyPoint(sw, q);
                if (a.boundary || b.boundary) { skip++; continue; }
                n++; disagree += a.inside != b.inside;
            }
            printf("  icosphere_3: %d points, %d skipped on the surface\n", n, skip);
            expect("classification is identical after swapping x and y (a different ray direction)", disagree, 0);
        } else printf("  (icosphere_3 not found; run mesh/corpus/gen_corpus.sh first; swap test skipped)\n");
    }
    // V4: regression tests for the two scorer defects found in V3
    printf("V4 regression tests (each fails on a build without the fix; see MUT_NOEXACTINIT and MUT_OVERFLOWRATIO):\n");
    {
        // (1) exact predicates. R is the pipeline's output for case legit_one_ulp_cavity_051_cutB, intersection (V2 cavity run, 16 vertices, 28 triangles, closed). Every vertex has z >= 0 and the query
        // point has z = -0x1.594cp-1 < 0, so the point is outside R by a bounding-box argument that needs no predicate (the +x ray from it cannot meet a triangle). Without exactinit() the plain
        // double determinants misclassified it as inside (1 crossing): that was the scorer's one bad sample point on this row.
        static const double RV[16][3] = {
        {0x1p+0, 0x1p+0, 0x1p+0},
        {0x1p+0, 0x1.fffffffffffffp+1, 0x1p+0},
        {0x1.ced916872b021p-2, 0x1.a1cac083126e9p-3, 0x1.fced916872b02p+0},
        {0x1.69fbe76c8b439p+1, 0x1.a1cac083126e9p-3, 0x1.fced916872b02p+0},
        {0x1.ced916872b02p-2, 0x1p+2, 0x0p+0},
        {0x1.69fbe76c8b439p+1, 0x1p+2, 0x0p+0},
        {0x1.69fbe76c8b439p+1, 0x1.a1cac083126eap-3, 0x0p+0},
        {0x1.ced916872b021p-2, 0x1.a1cac083126eap-3, 0x0p+0},
        {0x1.ced916872b021p-2, 0x1p+2, 0x1.fced916872b02p+0},
        {0x1.69fbe76c8b439p+1, 0x1p+2, 0x1.fced916872b02p+0},
        {0x1.69fbe76c8b439p+1, 0x1.fffffffffffffp+1, 0x1p+0},
        {0x1.69fbe76c8b439p+1, 0x1p+0, 0x1p+0},
        {0x1.69fbe76c8b43ap+1, 0x1p+0, 0x1.fced916872b03p+0},
        {0x1p+0, 0x1p+0, 0x1.fced916872b02p+0},
        {0x1p+0, 0x1.fffffffffffffp+1, 0x1.fced916872b02p+0},
        {0x1.69fbe76c8b43ap+1, 0x1p+2, 0x1.fced916872b03p+0}};
        static const uint32_t RT[28][3] = {{6,4,5}, {7,4,6}, {9,5,4}, {9,4,8}, {11,10,1}, {11,1,0}, {12,11,0}, {12,0,13}, {15,14,1}, {15,1,10}, {13,0,1}, {14,13,1}, {14,8,13}, {13,2,3}, {13,3,12}, {8,2,13}, {14,9,8}, {9,14,15}, {2,6,3}, {7,6,2}, {7,2,8}, {4,7,8}, {12,3,11}, {10,5,9}, {11,5,10}, {6,5,11}, {3,6,11}, {9,15,10}};
        Mesh R; for (auto& v : RV) R.addV(v[0], v[1], v[2]); for (auto& t : RT) R.addT(t[0], t[1], t[2]);
        double zmin = 1e300; for (auto& v : RV) zmin = std::min(zmin, v[2]);
        const double p[3] = {0x1.0e528p+2, 0x1.f07cp-1, -0x1.594cp-1};
        Pip q = classifyPoint(R, p);
        printf("  point with z < min z of the mesh: inside=%d boundary=%d crossings=%d (min z %g, point z %g)\n", (int)q.inside, (int)q.boundary, q.crossings, zmin, p[2]);
        expect("a point below the whole mesh is outside, with 0 crossings (needs exactinit())", (int)(zmin >= 0 && p[2] < 0 && !q.inside && !q.boundary && q.crossings == 0), 1);
    }
    {
        // (2) the volume ratio near the smallest subnormal; every expected value is known exactly. The old code returned nan (inf/inf) or 0 (x/inf) on the rows marked "old".
        const double tiny = std::ldexp(1.0, -1074);
        auto D = [](long n, int e) { return Dyadic{BigInt(n), e}; };
        Dyadic big3{BigInt(3) << 1100, -1100}, big5{BigInt(5) << 1100, -1100};
        expect("ratio 3 / 5 with both aligned integers beyond 2^1024 is 3.0/5.0 (old: nan)", ratio(big3, big5) == 3.0 / 5.0, 1);
        expect("ratio -3 / 5 keeps its sign (old: nan)", ratio(Dyadic{-big3.num, -1100}, big5) == -3.0 / 5.0, 1);
        expect("ratio 2^-1074 / 1 is exactly the smallest subnormal (old: 0)", ratio(D(1, -1074), D(1, 0)) == tiny, 1);
        expect("ratio 3*2^-1074 / 2^-1074 is exactly 3", ratio(D(3, -1074), D(1, -1074)) == 3.0, 1);
        expect("ratio 0 / 2^-1074 is 0", ratio(D(0, 0), D(1, -1074)) == 0.0, 1);
        { Dyadic na{(BigInt(1) << 1022) + 1, -1074}, nb{BigInt(1) << 1074, -1074}; double r = ratio(na, nb); expect("ratio (2^1022 + 1) * 2^-1074 / 1 is 2^-52 plus 2^-1074 (correct to 2^-60; old: 0)", std::fabs(r - std::ldexp(1.0, -52)) <= std::ldexp(1.0, -52 - 60), 1); }
        // end to end: A = unit cube, B contains it, so A intersect B = A (exact 6V = 6). The result has its x0 face at the smallest subnormal and its top at 1 - 2^-52, so its exact 6V differs from 6 by a
        // relative -2^-52 (below the 1e-9 'close' bound) and the exponents of its volume and of the difference are 1100 apart. Old: "relative nan", FAIL. New: close, PASS.
        Mesh A1 = box(0, 0, 0, 1, 1, 1), B1 = box(-1, -1, -1, 2, 2, 2);
        Mesh R1 = box(tiny, 0, 0, 1, 1, 1 - std::ldexp(1.0, -52));
        Dyadic v6A = exactVolumeTimes6(A1), v6B = exactVolumeTimes6(B1);
        std::string rowT = boxRow("u", A1, B1, v6B, v6A, Dyadic{0, 0});
        expect("result volume off by a relative 2^-52 with a subnormal coordinate: close, passes (old: relative nan, fails)", score("", "u", 1, "", 100, true, &R1, &A1, &B1, &rowT), 0);
        Mesh R2 = box(tiny, 0, 0, 1, 1, 0.5);
        expect("the same with the top at 1/2: relative error 1/2, still fails", score("", "u", 1, "", 100, true, &R2, &A1, &B1, &rowT), 1);
    }
    printf("%s\n", bad ? "SELFTEST FAIL" : "SELFTEST PASS");
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
#ifndef MUT_NOEXACTINIT
    exactinit();   // V4: must run before any predicate (score and selftest alike); MUT_NOEXACTINIT plants the V3 defect
#endif
    if (argc >= 2 && std::string(argv[1]) == "selftest") return selftest();
    if (argc >= 6 && std::string(argv[1]) == "score") {
        std::string op = argv[4]; int o = op == "union" ? 0 : op == "intersection" ? 1 : op == "difference" ? 2 : -1;
        if (o < 0) { fprintf(stderr, "bad op\n"); return 2; }
        return score(argv[2], argv[3], o, argv[5], argc > 6 ? atoi(argv[6]) : 5000);
    }
    fprintf(stderr, "usage: mesh_compare score <corpusdir> <caseId> <union|intersection|difference> <result.msh> [points] | selftest\n");
    return 2;
}
