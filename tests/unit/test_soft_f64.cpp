// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// Soft binary64 (gpu/shaders/f64soft.inc compiled as C++): exact IEEE-754 addition and multiplication in 64-bit integer arithmetic, bit-identical to the hardware double
// on random and adversarial operands, and planted mutants of the rounding code are caught. The same text is compiled as GLSL for the GPU kernels.
#include <catch2/catch_test_macros.hpp>
#include "testmeshes.h"
#include <cstring>

#define F64SOFT_C 1
int g_mut = 0;   // mutation switch used by f64soft.inc: 0 = correct code
#include "f64soft.inc"

namespace {
uint64_t bitsOf(double d) { uint64_t u; std::memcpy(&u, &d, 8); return u; }
double fromBits(uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }

struct Battery {
    size_t checked = 0, bad = 0;
    void add(double a, double b) { checked++; volatile double h = a + b; if (f64_add(bitsOf(a), bitsOf(b)) != bitsOf(h)) bad++; }
    void sub(double a, double b) { checked++; volatile double h = a - b; if (f64_sub(bitsOf(a), bitsOf(b)) != bitsOf(h)) bad++; }
    void mul(double a, double b) { checked++; volatile double h = a * b; if (f64_mul(bitsOf(a), bitsOf(b)) != bitsOf(h)) bad++; }
};

// random normal double with exponent in [-emax, emax]
double rnd(tmesh::Rng& r, int emax) {
    uint64_t m = r.g() & 0x000FFFFFFFFFFFFFull, s = r.g() & 1;
    uint64_t e = (uint64_t)(1023 + r.pick(2 * emax + 1) - emax);
    return fromBits((s << 63) | (e << 52) | m);
}

// the fixed adversarial operand sets, plus random ones; returns the number of mismatches against the hardware double
size_t runAdd(Battery& B) {
    const double u = std::ldexp(1.0, -52);
    const double sp[] = {0.0, -0.0, 1.0, -1.0, 1.0 + u, -(1.0 + u), 2.0 - u, 2.0, 0.5, 0.5 + u / 2, 3.0, 1e300, -1e300, 1e-300, 1.5, 1.0 + 3 * u, std::ldexp(1.0, -53), std::ldexp(1.0, -54), std::ldexp(1.0, -80), std::ldexp(1.0, -53) + std::ldexp(1.0, -100), std::ldexp(1.0, 52), std::ldexp(1.0, 53) - 1};
    for (double a : sp) for (double b : sp) { B.add(a, b); B.sub(a, b); }
    for (int d = 0; d <= 70; d++) for (double m : {1.0, 1.0 + u, 1.5, 2.0 - u}) { B.add(1.0, m * std::ldexp(1.0, -d)); B.add(1.0 + u, m * std::ldexp(1.0, -d)); B.add(1.0, -m * std::ldexp(1.0, -d)); B.add(-1.0, m * std::ldexp(1.0, -d)); }
    tmesh::Rng r(77);
    for (int i = 0; i < 150000; i++) {
        double a = rnd(r, 400), b = rnd(r, 400);
        int sh = r.pick(75);                       // make the exponents close most of the time (that is where rounding and cancellation happen)
        if (r.pick(4) != 0) b = std::ldexp(a, -sh) * (0.5 + 0.5 * r.u01()) * (r.pick(2) ? 1 : -1);
        if (b == 0 || !std::isnormal(b)) continue;
        B.add(a, b);
    }
    for (int i = 0; i < 20000; i++) { double a = rnd(r, 400); B.add(a, -a); B.add(a, std::nextafter(-a, 0.0)); B.add(a, std::nextafter(-a, -1e308)); }
    return B.bad;
}
size_t runMul(Battery& B) {
    const double u = std::ldexp(1.0, -52);
    const double sp[] = {0.0, -0.0, 1.0, -1.0, 1.0 + u, 2.0 - u, 0.5, 1.5, 3.0, 1e100, -1e-100, std::ldexp(1.0, 500), std::ldexp(1.0, -500), 1.0 + std::ldexp(1.0, -26), 1.0 + std::ldexp(1.0, -27), 1.4142135623730951, 1.7320508075688772};
    for (double a : sp) for (double b : sp) B.mul(a, b);
    // exact ties: (1 + i 2^-26)(1 + j 2^-27) has a remainder of exactly half an ulp for many i, j
    for (int i = 0; i < 64; i++) for (int j = 0; j < 64; j++) B.mul(1.0 + i * std::ldexp(1.0, -26), 1.0 + j * std::ldexp(1.0, -27));
    // near-ties just above half an ulp, by less than 2^-84 relative: a = 1 + p 2^-52, b = 1 + q 2^-52 with p q slightly above 2^51
    for (uint64_t p = 3; p < 3 + 2 * 400; p += 2) { uint64_t q = ((1ull << 51) + p - 1) / p; B.mul(1.0 + (double)p * u, 1.0 + (double)q * u); B.mul(1.0 + (double)p * u, 1.0 + (double)(q - 1) * u); }
    tmesh::Rng r(78);
    for (int i = 0; i < 150000; i++) B.mul(rnd(r, 250), rnd(r, 250));
    return B.bad;
}
}  // namespace

TEST_CASE("soft binary64 addition is bit-identical to the hardware double", "[unit][soft64]") {
    g_mut = 0; Battery B; runAdd(B);
    INFO("checked " << B.checked << " additions and subtractions");
    CHECK(B.checked > 100000);
    CHECK(B.bad == 0);
}

TEST_CASE("soft binary64 multiplication is bit-identical to the hardware double", "[unit][soft64]") {
    g_mut = 0; Battery B; runMul(B);
    INFO("checked " << B.checked << " multiplications");
    CHECK(B.checked > 100000);
    CHECK(B.bad == 0);
}

TEST_CASE("soft binary64 helpers: abs, neg, greater-than", "[unit][soft64]") {
    g_mut = 0;
    tmesh::Rng r(79);
    for (int i = 0; i < 50000; i++) {
        double a = rnd(r, 300), b = i % 3 ? rnd(r, 300) : a;
        if (i % 7 == 0) a = r.pick(2) ? 0.0 : -0.0;
        REQUIRE(f64_abs(bitsOf(a)) == bitsOf(std::fabs(a)));
        REQUIRE(f64_neg(bitsOf(a)) == bitsOf(-a));
        REQUIRE(f64_gt(bitsOf(a), bitsOf(b)) == (a > b));
    }
}

TEST_CASE("planted mutants of the soft binary64 rounding code are caught", "[unit][soft64][mutant]") {
    // 1: the sticky bit is dropped; 2: ties round half up; 3: ties always round down (additions).  4: ties round half up; 5: truncation; 6: low remainder bits ignored (multiplication).
    for (int m = 1; m <= 6; m++) {
        g_mut = m; Battery B;
        if (m <= 3) runAdd(B); else runMul(B);
        INFO("mutant " << m << ": " << B.bad << " of " << B.checked << " cases differ from the hardware double");
        CHECK(B.bad > 0);
    }
    g_mut = 0;
    Battery clean; runAdd(clean); runMul(clean);
    CHECK(clean.bad == 0);    // the switch is restored: the correct code is clean again
}
