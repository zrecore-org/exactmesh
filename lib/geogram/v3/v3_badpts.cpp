// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V3: prints the sample points of the scorer's loop (oracle/compare.cpp, copied: same seed, same arithmetic) at which result R is classified differently from the boolean of A and B, with the exact
// harness classifier (harness/pip.h); also prints ALL sample points when the 5th argument is "all" (to compare with the Python replica in v3_analyze.py).
//   v3_badpts <A.msh> <B.msh> <R.msh> <union|intersection|difference> [all]
#include "../../../harness/pip.h"
#include <algorithm>
#include <cmath>
#include <random>
extern "C" void exactinit();
using namespace mesh;
static bool inOp(int op, bool a, bool b) { return op == 0 ? (a || b) : op == 1 ? (a && b) : (a && !b); }
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    // oracle/compare.cpp never calls exactinit() (found by this tool: without it Shewchuk's error bounds are 0 and orient2d/orient3d return the plain double determinant). V3_EXACTINIT=1 calls it.
    if (getenv("V3_EXACTINIT")) exactinit();
    printf("exactinit called: %d\n", getenv("V3_EXACTINIT") ? 1 : 0);
    Mesh A, B, R; if (!readMesh(argv[1], A) || !readMesh(argv[2], B) || !readMesh(argv[3], R)) return 2;
    std::string o = argv[4]; int op = o == "union" ? 0 : o == "intersection" ? 1 : 2; bool all = argc > 5;
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (const Mesh* mm : {&A, &B}) for (size_t i = 0; i < mm->nv(); i++) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], mm->v[3*i+k]); hi[k] = std::max(hi[k], mm->v[3*i+k]); }
    double mx = 1; for (int k = 0; k < 3; k++) mx = std::max(mx, std::max(std::fabs(lo[k]), std::fabs(hi[k])));
    double g = std::max(std::ldexp(1.0, -14), std::ldexp(1.0, (int)std::ceil(std::log2(mx)) - 50));
    std::mt19937_64 rng(12345); int bad = 0, skip = 0;
    for (int i = 0; i < 5000; i++) {
        double p[3];
        for (int k = 0; k < 3; k++) { double span = hi[k] - lo[k]; double v = lo[k] - 0.25 * span + (double)(rng() >> 11) / 9007199254740992.0 * 1.5 * span; p[k] = (std::floor(v / g) + 0.5) * g; }
        Pip a = classifyPoint(A, p), b = classifyPoint(B, p), r = classifyPoint(R, p);
        if (all) printf("P %d %a %a %a\n", i, p[0], p[1], p[2]);
        if (a.boundary || b.boundary || r.boundary) { skip++; continue; }
        if (r.inside != inOp(op, a.inside, b.inside)) { bad++; printf("BAD %d %a %a %a  (%.17g %.17g %.17g) A=%d B=%d R=%d crossings R=%d\n", i, p[0], p[1], p[2], p[0], p[1], p[2], (int)a.inside, (int)b.inside, (int)r.inside, r.crossings); }
    }
    printf("bad %d skipped %d g=%.17g\n", bad, skip, g);
}
