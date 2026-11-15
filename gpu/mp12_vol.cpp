// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12 part B: exact volume. The GPU adds the six triple products of every triangle into private 896-bit integer accumulators (gpu/volume_reduce.comp, gpu/exact_acc.inc);
// the host adds the partials exactly and compares the integer with the big-integer oracle mesh::exactVolumeTimes6 (harness/exact.h). The quantity is 6 * volume, EXACTLY
// (no rounding at all is needed to compare), plus its single correct rounding to binary64.
//   mp12_vol host              the CPU twin of the kernel (same exact_acc.inc compiled as C++)
//   mp12_vol device [1..4]     the kernel (or a planted-mutant build) on the Vulkan device, with 3 different thread counts per mesh
#include "vkctx.h"
#include "../harness/exact.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#define F64SOFT_C 1
int g_mut = 0;
#include "shaders/f64soft.inc"
#include "orient_filter.inc"
#include "exact_acc.inc"
using mesh::Mesh; using mesh::BigInt;

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static Mesh uvsphere(int rings, int segs, double rad) {
    Mesh m; const double PI = 3.14159265358979323846;
    uint32_t top = m.addV(0, 0, rad);
    for (int i = 1; i < rings; i++) for (int j = 0; j < segs; j++) { double th = PI * i / rings, ph = 2 * PI * j / segs; m.addV(rad * std::sin(th) * std::cos(ph), rad * std::sin(th) * std::sin(ph), rad * std::cos(th)); }
    uint32_t bot = m.addV(0, 0, -rad);
    auto V = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segs + (j % segs)); };
    for (int j = 0; j < segs; j++) m.addT(top, V(1, j), V(1, j + 1));
    for (int i = 1; i + 1 < rings; i++) for (int j = 0; j < segs; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    for (int j = 0; j < segs; j++) m.addT(bot, V(rings - 1, j + 1), V(rings - 1, j));
    return m;
}
static Mesh mapped(const Mesh& a, int e2, double ox, double oy, double oz) { Mesh m = a; for (size_t i = 0; i < m.nv(); i++) { m.v[3*i] = std::ldexp(m.v[3*i], e2) + ox; m.v[3*i+1] = std::ldexp(m.v[3*i+1], e2) + oy; m.v[3*i+2] = std::ldexp(m.v[3*i+2], e2) + oz; } return m; }
static Mesh reversed(const Mesh& a) { Mesh m = a; for (size_t i = 0; i < m.nt(); i++) std::swap(m.t[3*i+1], m.t[3*i+2]); return m; }
static Mesh merged(const Mesh& a, const Mesh& b) { Mesh m = a; uint32_t base = (uint32_t)m.nv(); m.v.insert(m.v.end(), b.v.begin(), b.v.end()); for (uint32_t t : b.t) m.t.push_back(t + base); return m; }
static Mesh cube(double o) { Mesh m; for (int i = 0; i < 8; i++) m.addV(o + (i & 1), o + ((i >> 1) & 1), o + ((i >> 2) & 1)); static const int t[12][3] = {{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}}; for (auto& q : t) m.addT(t[0][0] * 0 + q[0], q[1], q[2]); return m; }

struct Case { std::string name; Mesh m; bool plainMustDiffer; };
static std::vector<Case> makeCases() {
    std::vector<Case> c; Mesh s = uvsphere(30, 40, 1.0), s2 = uvsphere(12, 16, 1.0);
    const double B30 = std::ldexp(1.0, 30), B40 = std::ldexp(1.0, 40), B50 = std::ldexp(1.0, 50);
    c.push_back({"unit cube (exact in binary64)", cube(0), false});
    c.push_back({"sphere 30x40", s, false});
    c.push_back({"sphere at +2^30", mapped(s, 0, B30, B30, B30), true});
    c.push_back({"sphere at +2^50", mapped(s, 0, B50, B50, B50), true});
    c.push_back({"sphere scaled 2^-30", mapped(s, -30, 0, 0, 0), false});
    c.push_back({"sphere scaled 2^99 (near the top of the window)", mapped(s, 99, 0, 0, 0), false});
    c.push_back({"sphere A at +2^40 plus its reversed copy (exact volume 0)", merged(mapped(s2, 0, B40, 0, 0), reversed(mapped(s2, 0, B40, 0, 0))), true});
    { Mesh a = mapped(s2, 0, B40, 0, 0), b = reversed(a); b.v[3 * 5] = std::nextafter(b.v[3 * 5], INFINITY); c.push_back({"as above, one vertex of the copy moved by 1 ulp", merged(a, b), true}); }
    c.push_back({"sphere scaled 2^-99 (tiny coordinates: outside the window, CPU exact path)", mapped(s, -99, 0, 0, 0), false});
    c.push_back({"sphere scaled 2^200 (outside the window, CPU exact path)", mapped(s, 200, 0, 0, 0), false});
    return c;
}
static bool meshInWindow(const Mesh& m) { for (double x : m.v) if (!inWindow(bits(x))) return false; return true; }

static BigInt limbsToInt(const uint32_t* l) {
    BigInt r = 0; for (int i = ACC_LIMBS - 1; i >= 0; i--) { r <<= 32; r += l[i]; }
    if (l[ACC_LIMBS - 1] & 0x80000000u) r -= (BigInt(1) << (32 * ACC_LIMBS));
    return r;
}
static void twinPartials(const Mesh& m, uint32_t nth, std::vector<uint32_t>& out) {
    out.assign((size_t)nth * ACC_LIMBS, 0);
    for (uint32_t g = 0; g < nth; g++) {
        uint32_t acc[ACC_LIMBS] = {0};
        for (size_t k = g; k < m.nt(); k += nth) {
            uint64_t u[9]; for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) u[3 * c + d] = bits(m.v[3 * (size_t)m.t[3 * k + c] + d]);
            accTriangle(acc, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8]);
        }
        memcpy(&out[(size_t)g * ACC_LIMBS], acc, sizeof acc);
    }
}
static BigInt sumPartials(const std::vector<uint32_t>& p, uint32_t nth) { BigInt s = 0; for (uint32_t g = 0; g < nth; g++) s += limbsToInt(&p[(size_t)g * ACC_LIMBS]); return s; }
static double roundedFromDyadic(const BigInt& num, int exp2) {      // correct rounding to binary64 of num * 2^exp2 (round to nearest even)
    if (num == 0) return 0.0;
    bool neg = num < 0; BigInt a = neg ? BigInt(-num) : num;
    unsigned bl = (unsigned)boost::multiprecision::msb(a) + 1; double r;
    if (bl <= 53) r = std::ldexp((double)(unsigned long long)a, exp2);
    else { unsigned sh = bl - 53; BigInt q = a >> sh, rem = a - (q << sh), half = BigInt(1) << (sh - 1);
           if (rem > half || (rem == half && (q & 1) != 0)) q += 1;
           r = std::ldexp((double)(unsigned long long)q, exp2 + (int)sh); }
    return neg ? -r : r;
}
// the plain binary64 sum of the same terms in the natural order: used only to show that the class separates exact accumulation from it
static double plainSix(const Mesh& m) {
    double s = 0; for (size_t k = 0; k < m.nt(); k++) { const double *a = &m.v[3*(size_t)m.t[3*k]], *b = &m.v[3*(size_t)m.t[3*k+1]], *c = &m.v[3*(size_t)m.t[3*k+2]];
        s += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]); } return s; }

static int run(bool device, int mutant) {
    std::unique_ptr<gpu::Ctx> ctx; gpu::Kernel k{};
    if (device) {
        ctx.reset(new gpu::Ctx());
        if (!ctx->init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx->error.c_str()); return 2; }
        std::string spv = mutant ? "volume_reduce_m" + std::to_string(mutant) : "volume_reduce";
        k = ctx->makeKernel(gpu::shaderDir() + "/" + spv + ".spv", 3, 8);
        printf("device=%s memory=%s shader=%s\n", ctx->props.deviceName, ctx->memMode(), spv.c_str());
    } else printf("host: CPU twin of the kernel\n");
    printf("%-78s %9s %6s %8s %10s %8s %10s\n", "mesh", "triangles", "path", "exact=", "6V (rounded)", "plain=", "thr-indep");
    int wrongExact = 0, indepFail = 0, plainNotSeparating = 0, nthrAll = 0; (void)nthrAll;
    for (auto& cs : makeCases()) {
        const Mesh& m = cs.m; mesh::Dyadic oracle = mesh::exactVolumeTimes6(m);
        bool inWin = meshInWindow(m);
        BigInt gpuInt = 0; bool ok = true, indep = true; int exp2 = -ACC_EBASE;
        if (inWin) {
            BigInt first = 0; bool have = false;
            for (uint32_t nth : {1u, 7u, 64u, 1000u}) {
                std::vector<uint32_t> part;
                if (ctx) {
                    auto bV = ctx->alloc(m.v.size() * 8), bT = ctx->alloc(m.t.size() * 4), bO = ctx->alloc((size_t)nth * ACC_LIMBS * 4);
                    memcpy(bV.ptr, m.v.data(), m.v.size() * 8); memcpy(bT.ptr, m.t.data(), m.t.size() * 4);
                    uint32_t pc[2] = {(uint32_t)m.nt(), nth};
                    ctx->begin(); ctx->upload(bV); ctx->upload(bT); ctx->dispatch(k, {&bV, &bT, &bO}, pc, (nth + 63) / 64); ctx->download(bO); ctx->submitWait();
                    part.assign((uint32_t*)bO.ptr, (uint32_t*)bO.ptr + (size_t)nth * ACC_LIMBS);
                    for (gpu::Buf* b : {&bV, &bT, &bO}) ctx->release(*b);
                } else twinPartials(m, nth, part);
                BigInt s = sumPartials(part, nth);
                if (!have) { first = s; have = true; } else if (s != first) indep = false;
                gpuInt = s;
            }
            mesh::Dyadic got{gpuInt, exp2};
            ok = mesh::equalExact(got, oracle);
        } else { gpuInt = oracle.num; exp2 = oracle.exp2; }          // CPU exact path: the oracle itself
        double rounded = roundedFromDyadic(gpuInt, exp2), plain = plainSix(m);
        double roundedOracle = roundedFromDyadic(oracle.num, oracle.exp2);
        bool plainEq = plain == roundedOracle;
        if (!ok || (inWin && rounded != roundedOracle)) wrongExact++;
        if (!indep) indepFail++;
        if (cs.plainMustDiffer && plainEq) plainNotSeparating++;
        printf("%-78s %9zu %6s %8s %12.17g %8s %10s%s\n", cs.name.c_str(), m.nt(), inWin ? "device" : "cpu", ok ? "yes" : "NO", rounded, plainEq ? "same" : "differs", indep ? "yes" : "NO",
               cs.plainMustDiffer && plainEq ? "   CLASS FAIL: plain sum equals exact here" : "");
    }
    printf("summary: meshes with a result different from the exact integer %d; meshes where partial-count changes the sum %d; classes that should separate plain from exact but do not %d\n", wrongExact, indepFail, plainNotSeparating);
    int rc;
    if (!mutant) { rc = !(wrongExact == 0 && indepFail == 0 && plainNotSeparating == 0); printf("%s\n", rc ? "FAIL" : "PASS"); }
    else { bool caught = wrongExact > 0 || indepFail > 0; rc = !caught; printf("mutant %d: %s\n", mutant, caught ? "caught" : "NOT CAUGHT"); }
    if (ctx) { ctx->release(k); ctx->destroy(); }
    return rc ? 3 : 0;
}
int main(int argc, char** argv) { std::string m = argc > 1 ? argv[1] : "host"; return run(m == "device", argc > 2 ? atoi(argv[2]) : 0); }
