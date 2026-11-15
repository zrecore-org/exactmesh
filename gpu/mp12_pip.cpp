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

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
struct Rng { std::mt19937_64 g; explicit Rng(uint64_t s) : g(s) {} double u01() { return (double)(g() >> 11) / 9007199254740992.0; } int pick(int n) { return (int)(g() % (uint64_t)n); } };

static Mesh uvsphere(int rings, int segs, double rad, double cx, double cy, double cz) {
    Mesh m; const double PI = 3.14159265358979323846;
    uint32_t top = m.addV(cx, cy, cz + rad);
    for (int i = 1; i < rings; i++) for (int j = 0; j < segs; j++) { double th = PI * i / rings, ph = 2 * PI * j / segs; m.addV(cx + rad * std::sin(th) * std::cos(ph), cy + rad * std::sin(th) * std::sin(ph), cz + rad * std::cos(th)); }
    uint32_t bot = m.addV(cx, cy, cz - rad);
    auto V = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segs + (j % segs)); };
    for (int j = 0; j < segs; j++) m.addT(top, V(1, j), V(1, j + 1));
    for (int i = 1; i + 1 < rings; i++) for (int j = 0; j < segs; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    for (int j = 0; j < segs; j++) m.addT(bot, V(rings - 1, j + 1), V(rings - 1, j));
    return m;
}
// a solid block of n^3 unit cubes as ONE closed surface (outer faces only, each unit square split along one diagonal), integer coordinates
static Mesh block(int n) {
    Mesh m; auto vid = [&](int x, int y, int z) { return (uint32_t)((x * (n + 1) + y) * (n + 1) + z); };
    for (int x = 0; x <= n; x++) for (int y = 0; y <= n; y++) for (int z = 0; z <= n; z++) m.addV(x, y, z);
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, bool flip) { if (flip) { m.addT(a, c, b); m.addT(a, d, c); } else { m.addT(a, b, c); m.addT(a, c, d); } };
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
        quad(vid(0, i, j), vid(0, i, j + 1), vid(0, i + 1, j + 1), vid(0, i + 1, j), true);   quad(vid(n, i, j), vid(n, i + 1, j), vid(n, i + 1, j + 1), vid(n, i, j + 1), true);
        quad(vid(i, 0, j), vid(i + 1, 0, j), vid(i + 1, 0, j + 1), vid(i, 0, j + 1), true);   quad(vid(i, n, j), vid(i, n, j + 1), vid(i + 1, n, j + 1), vid(i + 1, n, j), true);
        quad(vid(i, j, 0), vid(i + 1, j, 0), vid(i + 1, j + 1, 0), vid(i, j + 1, 0), true);   quad(vid(i, j, n), vid(i, j + 1, n), vid(i + 1, j + 1, n), vid(i + 1, j, n), true);
    }
    return m;
}
static Mesh mapped(const Mesh& a, double scaleExp2, double ox, double oy, double oz) { Mesh m = a; for (size_t i = 0; i < m.nv(); i++) { m.v[3*i] = std::ldexp(m.v[3*i], (int)scaleExp2) + ox; m.v[3*i+1] = std::ldexp(m.v[3*i+1], (int)scaleExp2) + oy; m.v[3*i+2] = std::ldexp(m.v[3*i+2], (int)scaleExp2) + oz; } return m; }
static double ulps(double x, int k) { for (int i = 0; i < std::abs(k); i++) x = std::nextafter(x, k > 0 ? INFINITY : -INFINITY); return x; }

struct Cls { std::string name; std::vector<double> q; bool mustReachUncertain; };    // q: xyz per query
struct Case { std::string mesh; Mesh m; std::vector<Cls> classes; };

// query classes for any mesh; `lat` true for the integer block (adds lattice / grazing / face classes)
static void addClasses(Case& c, int blockN, Rng& r, double xform2, double ox, double oy, double oz) {
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (size_t i = 0; i < c.m.nv(); i++) for (int d = 0; d < 3; d++) { lo[d] = std::min(lo[d], c.m.v[3*i+d]); hi[d] = std::max(hi[d], c.m.v[3*i+d]); }
    auto mp = [&](double x, double y, double z, double* o) { o[0] = std::ldexp(x, (int)xform2) + ox; o[1] = std::ldexp(y, (int)xform2) + oy; o[2] = std::ldexp(z, (int)xform2) + oz; };
    { Cls k{"random in and around the bbox", {}, false};
      for (int i = 0; i < 3000; i++) for (int d = 0; d < 3; d++) { double w = hi[d] - lo[d]; k.q.push_back(lo[d] - 0.25 * w + 1.5 * w * r.u01()); }
      c.classes.push_back(k); }
    { Cls k{"ray through a vertex (py,pz of a vertex, px random)", {}, false};
      for (int i = 0; i < 1500; i++) { size_t v = (size_t)r.pick((int)c.m.nv()); double w = hi[0] - lo[0]; k.q.push_back(lo[0] - 0.25 * w + 1.5 * w * r.u01()); k.q.push_back(c.m.v[3*v+1]); k.q.push_back(c.m.v[3*v+2]); }
      c.classes.push_back(k); }
    { Cls k{"on the surface (rounded barycentric points)", {}, false};
      for (int i = 0; i < 1500; i++) { size_t t = (size_t)r.pick((int)c.m.nt()); double w0 = r.u01(), w1 = r.u01(), w2 = r.u01(), sw = w0 + w1 + w2; w0 /= sw; w1 /= sw; w2 /= sw;
          for (int d = 0; d < 3; d++) k.q.push_back(w0 * c.m.v[3*c.m.t[3*t]+d] + w1 * c.m.v[3*c.m.t[3*t+1]+d] + w2 * c.m.v[3*c.m.t[3*t+2]+d]); }
      c.classes.push_back(k); }
    { Cls k{"within 1..3 ulp of a face point (each axis)", {}, true};
      for (int i = 0; i < 3000; i++) { size_t t = (size_t)r.pick((int)c.m.nt()); double w0 = r.u01(), w1 = r.u01(), w2 = r.u01(), sw = w0 + w1 + w2; w0 /= sw; w1 /= sw; w2 /= sw; double p[3];
          for (int d = 0; d < 3; d++) p[d] = w0 * c.m.v[3*c.m.t[3*t]+d] + w1 * c.m.v[3*c.m.t[3*t+1]+d] + w2 * c.m.v[3*c.m.t[3*t+2]+d];
          int ax = r.pick(3), k2 = r.pick(7) - 3; p[ax] = ulps(p[ax], k2); k.q.insert(k.q.end(), p, p + 3); }
      c.classes.push_back(k); }
    if (blockN > 0) {
        { Cls k{"lattice points step 1/2 (faces, edges, vertices, grazing rays)", {}, true};
          for (int x = -2; x <= 2 * blockN + 2; x++) for (int y = -2; y <= 2 * blockN + 2; y++) for (int z = -2; z <= 2 * blockN + 2; z++) { double o[3]; mp(x / 2.0, y / 2.0, z / 2.0, o); k.q.insert(k.q.end(), o, o + 3); }
          c.classes.push_back(k); }
        { Cls k{"on a block face, 1..3 ulp off along the normal", {}, true};
          for (int i = 0; i < 3000; i++) { double a = blockN * r.u01(), b = blockN * r.u01(); int face = r.pick(6), dk = r.pick(7) - 3; double p[3];
              int ax = face / 2; double fv = (face & 1) ? blockN : 0; int a1 = (ax + 1) % 3, a2 = (ax + 2) % 3; p[ax] = ulps(fv, dk == 0 ? 1 : dk); p[a1] = a; p[a2] = b;
              double o[3]; mp(p[0], p[1], p[2], o); k.q.insert(k.q.end(), o, o + 3); }
          c.classes.push_back(k); }
    }
}

static std::vector<Case> makeCases() {
    std::vector<Case> cs; Rng r(12);
    { Case c; c.mesh = "sphere 30x40"; c.m = uvsphere(30, 40, 1, 0, 0, 0); addClasses(c, 0, r, 0, 0, 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3"; c.m = block(3); addClasses(c, 3, r, 0, 0, 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 at +2^30"; c.m = mapped(block(3), 0, std::ldexp(1.0, 30), 0, 0); addClasses(c, 3, r, 0, std::ldexp(1.0, 30), 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 at +2^50 (x,y,z)"; double o = std::ldexp(1.0, 50); c.m = mapped(block(3), 0, o, o, o); addClasses(c, 3, r, 0, o, o, o); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 scaled 2^-30"; c.m = mapped(block(3), -30, 0, 0, 0); addClasses(c, 3, r, -30, 0, 0, 0); cs.push_back(std::move(c)); }
    { Case c; c.mesh = "block 3x3x3 scaled 2^200 (outside the window)"; c.m = mapped(block(3), 200, 0, 0, 0); addClasses(c, 3, r, 200, 0, 0, 0); cs.push_back(std::move(c)); }
    return cs;
}

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
