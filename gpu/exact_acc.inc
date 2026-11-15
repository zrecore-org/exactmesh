// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12 part B: exact accumulation of 6 * (signed volume) = sum over triangles of det(a, b, c), one text for C++ and GLSL (include f64soft.inc first; needs 64-bit integers).
// Each of the six triple products of binary64 coordinates is an exact integer (three 53-bit mantissas, <= 159 bits) times 2^(k0+k1+k2), with k = exponent field - 1075.
// It is added, shifted to its bit position, into a 28-limb (896-bit) two's-complement accumulator whose bit 0 stands for 2^-ACC_EBASE. Integer addition is exact and
// associative, so the sum does not depend on how triangles are split across threads or in which order partial accumulators are added; only the final rounding is a choice.
// Window: every coordinate zero or with exponent field in [923, 1123] (|x| in [2^-100, 2^101), as the orient filters); then k0+k1+k2 in [-456, 144] and the top bit used is
// below 456 + 144 + 159 + 26 (for 2^26 triangles) = 785 < 896. The caller checks the window and sends other meshes to the CPU exact path.
#ifndef F64SOFT_INC_INCLUDED
#error include f64soft.inc first
#endif
#ifndef VMUT
#define VMUT 0
#endif
#define ACC_LIMBS 28
#define ACC_EBASE 456
#ifdef F64SOFT_C
#define ACC_P(n) uint (&n)[ACC_LIMBS]
#else
#define ACC_P(n) inout uint n[ACC_LIMBS]
#endif
FN bool accInWindow(uint64_t x) { return inWindow(x); }       // from orient_filter.inc
// 53-bit mantissa (with the hidden bit) as two limbs
FN void accTriple(ACC_P(acc), uint64_t x, uint64_t y, uint64_t z, bool neg) {
    if ((x & 0x7FFFFFFFFFFFFFFFul) == 0ul || (y & 0x7FFFFFFFFFFFFFFFul) == 0ul || (z & 0x7FFFFFFFFFFFFFFFul) == 0ul) return;
    neg = neg != (((x >> 63) ^ (y >> 63) ^ (z >> 63)) != 0ul);
    uint64_t mx = (x & 0xFFFFFFFFFFFFFul) | 0x10000000000000ul, my = (y & 0xFFFFFFFFFFFFFul) | 0x10000000000000ul, mz = (z & 0xFFFFFFFFFFFFFul) | 0x10000000000000ul;
    int off = int((x >> 52) & 0x7FFul) + int((y >> 52) & 0x7FFul) + int((z >> 52) & 0x7FFul) - 3 * 1075 + ACC_EBASE;
    uint a[2], b[2], c[2];
    a[0] = uint(mx); a[1] = uint(mx >> 32); b[0] = uint(my); b[1] = uint(my >> 32); c[0] = uint(mz); c[1] = uint(mz >> 32);
    uint p[4]; for (int i = 0; i < 4; i++) p[i] = 0u;
    for (int i = 0; i < 2; i++) { uint64_t carry = 0ul; for (int j = 0; j < 2; j++) { uint64_t t = uint64_t(a[i]) * uint64_t(b[j]) + uint64_t(p[i + j]) + carry; p[i + j] = uint(t); carry = t >> 32; } p[i + 2] = uint(carry); }
    uint q[6]; for (int i = 0; i < 6; i++) q[i] = 0u;
    for (int i = 0; i < 2; i++) { uint64_t carry = 0ul; for (int j = 0; j < 4; j++) { uint64_t t = uint64_t(c[i]) * uint64_t(p[j]) + uint64_t(q[i + j]) + carry; q[i + j] = uint(t); carry = t >> 32; } q[i + 4] = uint(carry); }
#if VMUT == 1
    q[0] = 0u;                                                     // planted: the lowest product limb is lost
#endif
    int ls = off >> 5, bs = off & 31;
    uint s[7];
    for (int i = 0; i < 7; i++) {
        uint cur = i < 6 ? q[i] : 0u, prev = i > 0 ? q[i - 1] : 0u;
        s[i] = bs == 0 ? cur : ((cur << uint(bs)) | (prev >> uint(32 - bs)));
    }
    uint64_t carry = 0ul;
#if VMUT == 4
    const int top = 24;                                            // planted: the accumulator is four limbs too short
#else
    const int top = ACC_LIMBS;
#endif
    for (int i = 0; ls + i < top; i++) {
        uint64_t op = i < 7 ? uint64_t(s[i]) : 0ul;
        if (i >= 7 && carry == 0ul) break;
        if (!neg) { uint64_t t = uint64_t(acc[ls + i]) + op + carry; acc[ls + i] = uint(t); carry = t >> 32; }
        else { uint64_t t = uint64_t(acc[ls + i]) - op - carry; acc[ls + i] = uint(t); carry = (t >> 32) != 0ul ? 1ul : 0ul; }
#if VMUT == 2
        if (i == 0) break;                                         // planted: no carry or borrow propagation
#endif
    }
}
FN void accTriangle(ACC_P(acc), uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz, uint64_t cx, uint64_t cy, uint64_t cz) {
    accTriple(acc, ax, by, cz, false); accTriple(acc, ax, bz, cy, true);
    accTriple(acc, ay, bx, cz, true);  accTriple(acc, ay, bz, cx, false);
    accTriple(acc, az, bx, cy, false);
#if VMUT == 3
    accTriple(acc, az, by, cx, false);                             // planted: the sixth term has the wrong sign
#else
    accTriple(acc, az, by, cx, true);
#endif
}
