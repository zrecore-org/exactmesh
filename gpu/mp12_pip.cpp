// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12 part A: ray-parity point-in-solid. The GPU filter (gpu/pip_ray.comp, exact soft binary64, per-triangle logic in gpu/pip_filter.inc) answers OUT / IN / UNCERTAIN;
// every UNCERTAIN query is decided by the exact CPU test (mesh::classifyPoint, harness/pip.h). Bars: docs/PLAN.md "MP12 amendment".
//   mp12_pip host               CPU twin of the filter (same pip_filter.inc compiled as C++) + exact CPU reference
//   mp12_pip device [1..5]      the shader (or a planted-mutant build) on the Vulkan device
// The classes that must expose each mutant are registered and checked FIRST (coverage: the unmutated twin must reach the UNCERTAIN path on them).
#include "vkctx.h"
#include "../cpu/pip_hardened.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#define F64SOFT_C 1
int g_mut = 0;
#include "shaders/f64soft.inc"
#include "orient_filter.inc"
#include "pip_filter.inc"
extern "C" { void exactinit(void); }
using mesh::Mesh;

#include "pip_cases.h"

static uint32_t twinQuery(const Mesh& m, const double* p) {
    uint64_t pb[3] = {bits(p[0]), bits(p[1]), bits(p[2])}; uint32_t cr = 0;
    for (size_t k = 0; k < m.nt(); k++) {
        uint64_t u[9]; for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) u[3 * c + d] = bits(m.v[3 * (size_t)m.t[3 * k + c] + d]);
        uint32_t r2 = pipTri(u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], pb[0], pb[1], pb[2]);
        if (r2 == PIP_UNCERT) return 2;
        cr += r2;
    }
    return cr & 1u;
}

struct Stats { long q = 0, out = 0, in = 0, unc = 0, falseDef = 0, boundary = 0, devTwin = 0, uncOnBoundary = 0; };

static int run(bool device, int mutant) {
    exactinit();
    std::unique_ptr<gpu::Ctx> ctx; gpu::Kernel k{};
    if (device) {
        ctx.reset(new gpu::Ctx());
        if (!ctx->init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx->error.c_str()); return 2; }
        std::string spv = mutant ? "pip_ray_m" + std::to_string(mutant) : "pip_ray";
        k = ctx->makeKernel(gpu::shaderDir() + "/" + spv + ".spv", 4, 8);
        printf("device=%s memory=%s shader=%s\n", ctx->props.deviceName, ctx->memMode(), spv.c_str());
    } else printf("host: CPU twin of the filter + exact CPU\n");
    // the error-bound constants must equal the doubles they stand for
    { const double eps = std::ldexp(1.0, -53); double b2 = (3.0 + 16.0 * eps) * eps; uint64_t bb; memcpy(&bb, &b2, 8); if (bb != ORIENT2D_ERRBOUND_A_BITS) { printf("FAIL: ORIENT2D_ERRBOUND_A_BITS is 0x%016llx, the double is 0x%016llx\n", (unsigned long long)ORIENT2D_ERRBOUND_A_BITS, (unsigned long long)bb); return 3; } }
    auto cases = makeCases();
    unsigned T = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    printf("%-46s %-62s %7s %7s %7s %7s %8s %6s %6s\n", "mesh", "query class", "n", "OUT", "IN", "UNCERT", "falseDef", "bndry", "dev/tw");
    Stats tot; bool coverageOk = true;
    for (auto& cs : cases) {
        const Mesh& m = cs.m;
        std::unique_ptr<gpu::Buf> bV, bT;
        for (auto& cl : cs.classes) {
            const size_t n = cl.q.size() / 3; Stats s; s.q = (long)n;
            std::vector<uint32_t> twin(n), dev(n), exact(n); std::vector<char> bnd(n);
            auto par = [&](auto fn) { std::vector<std::thread> th; for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] { for (size_t i = n * w / T; i < n * (w + 1) / T; i++) fn(i); }); for (auto& x : th) x.join(); };
            par([&](size_t i) { twin[i] = twinQuery(m, &cl.q[3 * i]); mesh::Pip p = mesh::classifyPointHardened(m, &cl.q[3 * i]); bnd[i] = p.boundary; exact[i] = p.inside; });
            if (ctx) {
                auto bV2 = ctx->alloc(m.v.size() * 8), bT2 = ctx->alloc(m.t.size() * 4), bQ = ctx->alloc(n * 24), bO = ctx->alloc(n * 4);
                memcpy(bV2.ptr, m.v.data(), m.v.size() * 8); memcpy(bT2.ptr, m.t.data(), m.t.size() * 4); memcpy(bQ.ptr, cl.q.data(), n * 24);
                uint32_t pc[2] = {(uint32_t)n, (uint32_t)m.nt()};
                ctx->begin(); for (gpu::Buf* b : {&bV2, &bT2, &bQ}) ctx->upload(*b); ctx->dispatch(k, {&bV2, &bT2, &bQ, &bO}, pc, (uint32_t)((n + 63) / 64)); ctx->download(bO); ctx->submitWait();
                memcpy(dev.data(), bO.ptr, n * 4);
                for (gpu::Buf* b : {&bV2, &bT2, &bQ, &bO}) ctx->release(*b);
            } else dev = twin;
            for (size_t i = 0; i < n; i++) {
                uint32_t v = dev[i]; s.devTwin += v != twin[i]; s.boundary += bnd[i];
                if (v == 2) { s.unc++; s.uncOnBoundary += bnd[i]; }
                else { (v ? s.in : s.out)++; if (bnd[i] || (v != 0) != (exact[i] != 0)) { s.falseDef++; if (getenv("PIPDBG") && s.falseDef <= 3) printf("   falseDef: p=(%.17g, %.17g, %.17g) filter=%u exactInside=%d boundary=%d\n", cl.q[3*i], cl.q[3*i+1], cl.q[3*i+2], v, (int)exact[i], (int)bnd[i]); } }
            }
            printf("%-46s %-62s %7ld %7ld %7ld %7ld %8ld %6ld %6ld\n", cs.mesh.c_str(), cl.name.c_str(), s.q, s.out, s.in, s.unc, s.falseDef, s.boundary, s.devTwin);
            if (cl.mustReachUncertain && !mutant) {
                long twinUnc = 0; for (size_t i = 0; i < n; i++) twinUnc += twin[i] == 2;
                if (twinUnc == 0) { printf("  COVERAGE FAIL: this class never reaches the UNCERTAIN path in the unmutated filter\n"); coverageOk = false; }
            }
            tot.q += s.q; tot.out += s.out; tot.in += s.in; tot.unc += s.unc; tot.falseDef += s.falseDef; tot.boundary += s.boundary; tot.devTwin += s.devTwin;
        }
    }
    printf("total: queries %ld, OUT %ld, IN %ld, UNCERTAIN %ld (%.2f%%); false definite answers %ld; exact-boundary queries %ld; device vs twin differences %ld\n",
           tot.q, tot.out, tot.in, tot.unc, 100.0 * tot.unc / std::max(1L, tot.q), tot.falseDef, tot.boundary, tot.devTwin);
    int rc;
    if (!mutant) { rc = !(tot.falseDef == 0 && tot.devTwin == 0 && coverageOk); printf("%s\n", rc ? "FAIL" : "PASS"); }
    else { bool caught = tot.falseDef > 0 || tot.devTwin > 0; rc = !caught; printf("mutant %d: %s\n", mutant, caught ? "caught" : "NOT CAUGHT"); }
    if (ctx) { ctx->release(k); ctx->destroy(); }
    return rc ? 3 : 0;
}

int main(int argc, char** argv) {
    std::string m = argc > 1 ? argv[1] : "host";
    return run(m == "device", argc > 2 ? atoi(argv[2]) : 0);
}
