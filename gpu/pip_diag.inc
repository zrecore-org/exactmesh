// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12c diagnostic: one text for C++ and GLSL that evaluates pipTri (pip_filter.inc) and, field by field, its stages for one (triangle, point) pair, so that the device and
// the CPU twin can be compared value by value. Include f64soft.inc, orient_filter.inc and pip_filter.inc first. Fields:
//   0 pipTri result; 1..3 orient2d signs of the three edges (+1 so they are unsigned); 4 sign of the projected normal; 5 orient3d side (+1);
//   6..13 low/high words of detleft, detright, det, bound of the first edge's orient2d (a, b, p in the (y, z) plane).
#define DIAG_FIELDS 14
FN uint diagField(uint which, uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz, uint64_t cx, uint64_t cy, uint64_t cz, uint64_t px, uint64_t py, uint64_t pz) {
    if (which == 0u) return pipTri(ax, ay, az, bx, by, bz, cx, cy, cz, px, py, pz);
    if (which == 1u) return uint(orient2dFilterSoft(ay, az, by, bz, py, pz) + 1);
    if (which == 2u) return uint(orient2dFilterSoft(by, bz, cy, cz, py, pz) + 1);
    if (which == 3u) return uint(orient2dFilterSoft(cy, cz, ay, az, py, pz) + 1);
    if (which == 4u) return uint(orient2dFilterSoft(ay, az, by, bz, cy, cz) + 1);
    if (which == 5u) return uint(pipOrient3d(ax, ay, az, bx, by, bz, cx, cy, cz, px, py, pz) + 1);
    uint64_t detleft = f64_mul(f64_sub(ay, py), f64_sub(bz, pz));
    uint64_t detright = f64_mul(f64_sub(az, pz), f64_sub(by, py));
    uint64_t det = f64_sub(detleft, detright);
    uint64_t bound = f64_mul(ORIENT2D_ERRBOUND_A_BITS, f64_add(f64_abs(detleft), f64_abs(detright)));
    uint64_t v = which < 8u ? detleft : which < 10u ? detright : which < 12u ? det : bound;
    return (which & 1u) == 0u ? uint(v & 0xFFFFFFFFul) : uint(v >> 32);
}
