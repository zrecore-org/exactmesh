// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V4 (docs/PLAN.md, "V4"), bar (b): runs the PRE-V4 oracle/compare.cpp (sha256 a2945560..., `git show d9b1dbb:oracle/compare.cpp`) on the V4 regression inputs. The old main() never calls exactinit().
// build: git show d9b1dbb:oracle/compare.cpp > <dir>/compare_old.cpp; c++ -std=c++17 -O3 -DNDEBUG -ffp-contract=off -I harness -I build/deps/boost_1_88_0 -DOLD_COMPARE='"<dir>/compare_old.cpp"' lib/geogram/v4/old_comparator_driver.cpp build/libshewchuk_predicates.a -o <bin>
// run from the repository root (it reads the V2 cavity run output for case 051 cutB, intersection).
#define main old_main
#include OLD_COMPARE
#undef main
int main() {
    Mesh R; if (!readMesh("build/work/v2_cavity/run/out_3/legit_one_ulp_cavity_051_cutB.intersection.msh", R)) return 2;
    const double p[3] = {0x1.0e528p+2, 0x1.f07cp-1, -0x1.594cp-1};
    Pip q = classifyPoint(R, p);
    printf("old comparator (no exactinit): inside=%d crossings=%d  (truth: outside, p.z < 0 <= every vertex z)\n", (int)q.inside, q.crossings);
    Dyadic big3{BigInt(3) << 1100, -1100}, big5{BigInt(5) << 1100, -1100};
    printf("old ratio(3,5) at exponent -1100 = %g (exact 0.6)\n", ratio(big3, big5));
    printf("old ratio(2^-1074, 1) = %g (exact 4.94066e-324)\n", ratio(Dyadic{BigInt(1), -1074}, Dyadic{BigInt(1), 0}));
}
