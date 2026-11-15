// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12c: ray-parity point-in-solid through the MP10 LBVH (gpu/pip_bvh.comp). Bars: docs/PLAN.md "MP12c amendment".
//   mp12c_bvh_pip host [stk]       CPU twin walking the tree (built on the host the same way is not possible: the tree is built on the device, so `host` needs a device too)
//   mp12c_bvh_pip device [hmut]    correctness on all classes: device verdicts vs the twin walking the downloaded tree vs the exact CPU
//   mp12c_bvh_pip bench            speed: GPU total vs the fair CPU (BVH ray walk + native exact predicates), cold and amortised
// Environment: shader variants are selected by name (pip_bvh_b1 strict overlap, _b2 stack of 4 with overflow ignored, _b5, _p1.._p5, _s2 = stack of 2, overflow -> UNCERT: not a mutant,
// the bar-4 check). HMUT=3 / 4 (ray box rounded to nearest) are EQUIVALENT mutants (see RESULTS: triangle boxes are rounded outward, so rounding the point either way cannot lose a
// triangle); HMUT=6 rounds the triangle boxes to nearest and must be caught.
#include "vkctx.h"
#include "../cpu/pip_hardened.h"
#include "../cpu/bvh_ref.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#define F64SOFT_C 1
int g_mut = 0;
#include "shaders/f64soft.inc"
#include "orient_filter.inc"
#include "pip_filter.inc"
#include "pip_diag.inc"
extern "C" { void exactinit(void); }
#include "pip_cases.h"

struct FBox { float lo[3], hi[3]; };
static float roundDown(double x) { float f = (float)x; if ((double)f > x) f = nextafterf(f, -INFINITY); return f; }
static float roundUp(double x) { float f = (float)x; if ((double)f < x) f = nextafterf(f, INFINITY); return f; }
static unsigned nThreads() { return std::max(1u, std::min(8u, std::thread::hardware_concurrency())); }
template <class F> static void parFor(size_t n, F fn) { unsigned T = nThreads(); std::vector<std::thread> th; for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] { for (size_t i = n * w / T; i < n * (w + 1) / T; i++) fn(i); }); for (auto& x : th) x.join(); }

// a scene: one mesh, or several meshes concatenated (tmesh[k] = mesh of triangle k; a query only counts triangles of its own mesh)
struct Scene { Mesh m; std::vector<uint32_t> tmesh; bool multi = false; };
static int g_hmut = 0;
static std::vector<FBox> triBoxes(const Mesh& m) {
    std::vector<FBox> out(m.nt());
    for (size_t t = 0; t < m.nt(); t++) for (int d = 0; d < 3; d++) {
        double a = m.v[3 * (size_t)m.t[3 * t] + d], b = m.v[3 * (size_t)m.t[3 * t + 1] + d], c = m.v[3 * (size_t)m.t[3 * t + 2] + d];
        out[t].lo[d] = roundDown(std::min(a, std::min(b, c))); out[t].hi[d] = roundUp(std::max(a, std::max(b, c)));
        if (g_hmut == 6) { out[t].lo[d] = (float)std::min(a, std::min(b, c)); out[t].hi[d] = (float)std::max(a, std::max(b, c)); }   // planted: triangle boxes rounded to nearest
    }
    return out;
}
// the ray box: lo = p rounded down, hi = (max, p) rounded up. FLT_MAX stands for +infinity (every finite box lies below it).
static void rayBox(const double* p, float* b) {
    for (int d = 0; d < 3; d++) {
        b[d] = roundDown(p[d]); b[3 + d] = d == 0 ? FLT_MAX : roundUp(p[d]);
        if (g_hmut == 3) { b[d] = (float)p[d]; if (d) b[3 + d] = (float)p[d]; }
        if (g_hmut == 4 && d == 0) b[0] = (float)p[0];
    }
}

// ---- the tree as downloaded from the device, and the twin walk (same traversal, same stack bound, same overflow rule as the shader) ----
struct HostTree { std::vector<float> nb; std::vector<uint32_t> lc, rc, idx; uint32_t n = 0; };
static bool ovl(const float* a, const float* b, bool strict) {
    for (int d = 0; d < 3; d++) { if (strict ? !(a[d] < b[3 + d] && b[d] < a[3 + d]) : !(a[d] <= b[3 + d] && b[d] <= a[3 + d])) return false; }
    return true;
}
static uint32_t twinWalk(const Scene& s, const HostTree& tr, const double* p, const float* qb, uint32_t mine, int stk, int bmut) {
    uint64_t pb[3] = {bits(p[0]), bits(p[1]), bits(p[2])}; uint32_t crossings = 0;
    std::vector<uint32_t> stack(stk + 2); int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        uint32_t node = stack[--sp];
        if (!ovl(qb, &tr.nb[6 * (size_t)node], bmut == 1)) continue;
        if (node >= tr.n - 1) {
            uint32_t k = tr.idx[node - (tr.n - 1)];
            if (s.multi && s.tmesh[k] != mine) continue;
            uint64_t u[9]; for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) u[3 * c + d] = bits(s.m.v[3 * (size_t)s.m.t[3 * (size_t)k + c] + d]);
            uint32_t r = pipTri(u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], pb[0], pb[1], pb[2]);
            if (r == PIP_UNCERT) return 2;
            crossings += r;
        } else {
            uint32_t a = tr.lc[node], b = tr.rc[node];
            if (sp + 2 > stk) { if (bmut == 2) break; return 2; }
            if (bmut == 5) { stack[sp++] = a; if (ovl(qb, &tr.nb[6 * (size_t)a], false)) stack[sp++] = b; }
            else { stack[sp++] = a; stack[sp++] = b; }
        }
    }
    return crossings & 1u;
}

// ---- the GPU side: tree build (MP10 kernels) kept on the device, then one traversal dispatch per query batch ----
struct GpuPip {
    gpu::Ctx ctx; gpu::Kernel kMorton, kc, ks, kt, kBuild, kFit, kQ;
    gpu::Buf bV, bT, bB, keyA, keyB, idxA, idxB, bh, lc, rc, par, nb, flag, bTM;
    size_t n = 0; bool multi = false; bool ready = false; double buildMs = 0;
    bool init(const std::string& spv) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        std::string d = gpu::shaderDir() + "/";
        kMorton = ctx.makeKernel(d + "lbvh_morton.spv", 2, 20);
        kc = ctx.makeKernel(d + "sort_count.spv", 2, 12); ks = ctx.makeKernel(d + "sort_scan.spv", 1, 12); kt = ctx.makeKernel(d + "sort_scatter.spv", 5, 12);
        kBuild = ctx.makeKernel(d + "lbvh_build.spv", 4, 4); kFit = ctx.makeKernel(d + "lbvh_fit.spv", 7, 4);
        kQ = ctx.makeKernel(d + spv + ".spv", 11, 12);
        return true;
    }
    void freeScene() { if (!ready) return; for (auto* b : {&bV, &bT, &bB, &keyA, &keyB, &idxA, &idxB, &bh, &lc, &rc, &par, &nb, &flag, &bTM}) ctx.release(*b); ready = false; }
    // cold part: boxes, upload, Morton sort, tree. Returns ms.
    double prepare(const Scene& s) {
        freeScene(); double t0 = gpu::nowMs();
        n = s.m.nt(); multi = s.multi; auto fb = triBoxes(s.m);
        bV = ctx.alloc(s.m.v.size() * 8 + 8); bT = ctx.alloc(s.m.t.size() * 4 + 4); bB = ctx.alloc(n * 24);
        keyA = ctx.alloc(n * 8); keyB = ctx.alloc(n * 8); idxA = ctx.alloc(n * 4); idxB = ctx.alloc(n * 4);
        bh = ctx.alloc(16 * ((n + 4095) / 4096) * 4 + 16); lc = ctx.alloc(n * 4 + 4); rc = ctx.alloc(n * 4 + 4); par = ctx.alloc(2 * n * 4 + 4);
        nb = ctx.alloc(2 * n * 24); flag = ctx.alloc(n * 4 + 4); bTM = ctx.alloc(n * 4 + 4); ready = true;
        memcpy(bV.ptr, s.m.v.data(), s.m.v.size() * 8); memcpy(bT.ptr, s.m.t.data(), s.m.t.size() * 4); memcpy(bB.ptr, fb.data(), n * 24); memset(flag.ptr, 0, n * 4 + 4);
        if (s.multi) memcpy(bTM.ptr, s.tmesh.data(), n * 4);
        float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (auto& b : fb) for (int k = 0; k < 3; k++) { float c = (b.lo[k] + b.hi[k]) * 0.5f; lo[k] = std::min(lo[k], c); hi[k] = std::max(hi[k], c); }
        float ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}); float scale = ext > 0 && std::isfinite(ext) ? 1023.0f / ext : 1.0f;
        struct { uint32_t n; float ox, oy, oz, scale; } pm = {(uint32_t)n, lo[0], lo[1], lo[2], scale};
        uint32_t Bk = (uint32_t)((n + 4095) / 4096); gpu::Buf *ki = &keyA, *ii = &idxA, *ko = &keyB, *io = &idxB;
        ctx.begin();
        for (gpu::Buf* b : {&bV, &bT, &bB, &flag, &bTM}) ctx.upload(*b);
        ctx.dispatch(kMorton, {&bB, &keyA}, &pm, (uint32_t)((n + 63) / 64));
        for (uint32_t p = 0; p < 8; p++) { uint32_t pc[3] = {(uint32_t)n, Bk, p}; ctx.dispatch(kc, {ki, &bh}, pc, Bk); ctx.dispatch(ks, {&bh}, pc, 1); ctx.dispatch(kt, {ki, ii, ko, io, &bh}, pc, Bk); std::swap(ki, ko); std::swap(ii, io); }
        int ni = (int)n; if (n > 1) ctx.dispatch(kBuild, {&keyA, &lc, &rc, &par}, &ni, (uint32_t)((n + 63) / 64));
        uint32_t nn = (uint32_t)n; ctx.dispatch(kFit, {&bB, &idxA, &lc, &rc, &par, &nb, &flag}, &nn, (uint32_t)((n + 63) / 64));
        ctx.submitWait();
        return buildMs = gpu::nowMs() - t0;
    }
    HostTree download() {
        HostTree t; t.n = (uint32_t)n; ctx.begin(); ctx.download(nb, 2 * n * 24); ctx.download(lc, n * 4); ctx.download(rc, n * 4); ctx.download(idxA, n * 4); ctx.submitWait();
        t.nb.assign((float*)nb.ptr, (float*)nb.ptr + 12 * n); t.lc.assign((uint32_t*)lc.ptr, (uint32_t*)lc.ptr + n); t.rc.assign((uint32_t*)rc.ptr, (uint32_t*)rc.ptr + n); t.idx.assign((uint32_t*)idxA.ptr, (uint32_t*)idxA.ptr + n);
        return t;
    }
    // verdicts 0/1/2 for a batch (qmesh may be empty for a single mesh)
    std::vector<uint32_t> query(const std::vector<double>& q, const std::vector<uint32_t>& qmesh, std::vector<float>* boxesOut = nullptr) {
        size_t nq = q.size() / 3; std::vector<float> qb(6 * nq); double tq0 = gpu::nowMs();
        parFor(nq, [&](size_t i) { rayBox(&q[3 * i], &qb[6 * i]); });
        auto bQ = ctx.alloc(nq * 24 + 8), bQB = ctx.alloc(nq * 24 + 8), bMQ = ctx.alloc(nq * 4 + 4), bO = ctx.alloc(nq * 4 + 4);
        memcpy(bQ.ptr, q.data(), nq * 24); memcpy(bQB.ptr, qb.data(), nq * 24); if (multi) memcpy(bMQ.ptr, qmesh.data(), nq * 4);
        uint32_t pc[3] = {(uint32_t)nq, (uint32_t)n, multi ? 1u : 0u};
        ctx.begin(); for (gpu::Buf* b : {&bQ, &bQB, &bMQ}) ctx.upload(*b);
        ctx.dispatch(kQ, {&bV, &bT, &bQ, &bQB, &nb, &lc, &rc, &idxA, &bMQ, &bTM, &bO}, pc, (uint32_t)((nq + 63) / 64)); ctx.download(bO); double tq1 = gpu::nowMs(); double gms = ctx.submitWait();
        if (getenv("PIP_TIMING")) printf("    [query: host prep %.1f ms, GPU submit+wait %.1f ms (reported by ctx %.1f), total %.1f ms]\n", tq1 - tq0, gpu::nowMs() - tq1, gms, gpu::nowMs() - tq0);
        std::vector<uint32_t> v((uint32_t*)bO.ptr, (uint32_t*)bO.ptr + nq);
        for (gpu::Buf* b : {&bQ, &bQB, &bMQ, &bO}) ctx.release(*b);
        if (boxesOut) *boxesOut = std::move(qb);
        return v;
    }
    void shutdown() { freeScene(); for (auto* k : {&kMorton, &kc, &ks, &kt, &kBuild, &kFit, &kQ}) ctx.release(*k); ctx.destroy(); }
};

// ---- the exact CPU: reference (all triangles) and the fair production path (BVH ray walk on binary64 boxes, native exact predicates on the candidates) ----
static std::vector<uint8_t> exactAll(const Scene& s, const std::vector<double>& q, const std::vector<uint32_t>& qmesh, const std::vector<Mesh>* perMesh = nullptr) {
    size_t n = q.size() / 3; std::vector<uint8_t> out(n);
    parFor(n, [&](size_t i) { const Mesh& m = perMesh ? (*perMesh)[qmesh[i]] : s.m; mesh::Pip p = mesh::classifyPointHardened(m, &q[3 * i]); out[i] = p.boundary ? 2 : (p.inside ? 1 : 0); });
    return out;
}
struct CpuBvh {
    const Scene* s = nullptr; std::vector<bvhref::Box> boxes; bvhref::Bvh bvh; double buildMs = 0;
    void build(const Scene& sc) {
        s = &sc; double t0 = gpu::nowMs(); boxes = bvhref::triangleBoxes(sc.m); bvh.build(boxes); buildMs = gpu::nowMs() - t0;
    }
    // classify queries (all, or the listed subset); out: 0 outside, 1 inside, 2 on the surface
    void classify(const std::vector<double>& q, const std::vector<uint32_t>& qmesh, std::vector<uint8_t>& out, const std::vector<uint32_t>* only) const {
        size_t n = only ? only->size() : q.size() / 3; unsigned T = nThreads(); std::vector<std::thread> th;
        for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] {
            Mesh sub; sub.v = s->m.v; std::vector<uint32_t> cand;
            for (size_t j = n * w / T; j < n * (w + 1) / T; j++) {
                size_t i = only ? (*only)[j] : j; const double* p = &q[3 * i];
                bvhref::Box rb; rb.lo[0] = p[0]; rb.lo[1] = p[1]; rb.lo[2] = p[2]; rb.hi[0] = DBL_MAX; rb.hi[1] = p[1]; rb.hi[2] = p[2];
                cand.clear(); bvh.query(rb, cand); sub.t.clear();
                for (uint32_t k : cand) { if (s->multi && s->tmesh[k] != qmesh[i]) continue; sub.t.push_back(s->m.t[3 * (size_t)k]); sub.t.push_back(s->m.t[3 * (size_t)k + 1]); sub.t.push_back(s->m.t[3 * (size_t)k + 2]); }
                mesh::Pip r = mesh::classifyPointHardened(sub, p); out[i] = r.boundary ? 2 : (r.inside ? 1 : 0);
            }
        });
        for (auto& t : th) t.join();
    }
};

// the CPU path a service would use for many small meshes: per-mesh triangle range (triangles of a mesh are contiguous), projected-box rejection, native exact predicates on the survivors
static void cpuPerMesh(const Scene& s, const std::vector<double>& q, const std::vector<uint32_t>& qmesh, std::vector<uint8_t>& out) {
    size_t n = q.size() / 3; std::vector<uint32_t> start; for (size_t k = 0; k < s.tmesh.size(); k++) if (k == 0 || s.tmesh[k] != s.tmesh[k - 1]) start.push_back((uint32_t)k); start.push_back((uint32_t)s.tmesh.size());
    std::vector<double> lo(2 * s.m.nt()), hi(2 * s.m.nt());
    for (size_t t = 0; t < s.m.nt(); t++) for (int d = 0; d < 2; d++) { double a = s.m.v[3*(size_t)s.m.t[3*t]+1+d], b = s.m.v[3*(size_t)s.m.t[3*t+1]+1+d], c = s.m.v[3*(size_t)s.m.t[3*t+2]+1+d]; lo[2*t+d] = std::min(a, std::min(b, c)); hi[2*t+d] = std::max(a, std::max(b, c)); }
    unsigned T = nThreads(); std::vector<std::thread> th;
    for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] {
        Mesh sub; sub.v = s.m.v;
        for (size_t i = n * w / T; i < n * (w + 1) / T; i++) {
            const double* p = &q[3 * i]; sub.t.clear(); uint32_t m = qmesh[i];
            for (uint32_t t = start[m]; t < start[m + 1]; t++) if (!(p[1] > hi[2*t] || p[1] < lo[2*t] || p[2] > hi[2*t+1] || p[2] < lo[2*t+1])) { sub.t.push_back(s.m.t[3*(size_t)t]); sub.t.push_back(s.m.t[3*(size_t)t+1]); sub.t.push_back(s.m.t[3*(size_t)t+2]); }
            mesh::Pip r = mesh::classifyPointHardened(sub, p); out[i] = r.boundary ? 2 : (r.inside ? 1 : 0);
        }
    });
    for (auto& t : th) t.join();
}

struct Tot { long q = 0, out = 0, in = 0, unc = 0, falseDef = 0, boundary = 0, devTwin = 0, finalBad = 0; };

static int correctness(const std::string& spv, int bmutTwin, int stk, bool checkCoverage) {
    exactinit();
    GpuPip g; if (!g.init(spv)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s shader=%s hmut=%d stack=%d\n", g.ctx.props.deviceName, g.ctx.memMode(), spv.c_str(), g_hmut, stk);
    printf("%-46s %-62s %7s %7s %7s %7s %8s %6s %6s %8s\n", "mesh", "query class", "n", "OUT", "IN", "UNCERT", "falseDef", "bndry", "dev/tw", "finalBad");
    Tot tot; bool coverageOk = true; long cpuBvhBad = 0;
    auto cases = makeCases();
    // extra scenes: a lattice block with equal Morton codes is in makeCases; add a larger block (deep tree) and the multi-mesh scene below
    { Case c; Rng r(77); c.mesh = "block 8x8x8"; c.m = block(8); addClasses(c, 8, r, 0, 0, 0, 0); cases.push_back(std::move(c)); }
    { Case c; Rng r(78); c.mesh = "sphere 60x80"; c.m = uvsphere(60, 80, 1, 0, 0, 0); addClasses(c, 0, r, 0, 0, 0, 0); cases.push_back(std::move(c)); }
    auto account = [&](const std::string& mname, const std::string& cname, const std::vector<uint32_t>& dev, const std::vector<uint32_t>& twin, const std::vector<uint8_t>& exact, bool must) {
        size_t n = dev.size(); Tot s; s.q = (long)n;
        for (size_t i = 0; i < n; i++) {
            uint32_t v = dev[i]; bool bnd = exact[i] == 2; s.boundary += bnd; s.devTwin += v != twin[i];
            if (v == 2) s.unc++; else { (v ? s.in : s.out)++; if (bnd || (v != 0) != (exact[i] == 1)) s.falseDef++; }
            if (v != 2 && v != exact[i]) s.finalBad++;      // the final answer (verdict, or the exact answer for UNCERT) differs from the exact CPU
        }
        printf("%-46s %-62s %7ld %7ld %7ld %7ld %8ld %6ld %6ld %8ld\n", mname.c_str(), cname.c_str(), s.q, s.out, s.in, s.unc, s.falseDef, s.boundary, s.devTwin, s.finalBad);
        if (must && checkCoverage) { long tu = 0; for (auto x : twin) tu += x == 2; if (!tu) { printf("  COVERAGE FAIL: this class never reaches UNCERT in the unmutated twin\n"); coverageOk = false; } }
        tot.q += s.q; tot.out += s.out; tot.in += s.in; tot.unc += s.unc; tot.falseDef += s.falseDef; tot.boundary += s.boundary; tot.devTwin += s.devTwin; tot.finalBad += s.finalBad;
    };
    for (auto& cs : cases) {
        Scene sc; sc.m = cs.m; g.prepare(sc); HostTree tr = g.download(); CpuBvh cpuTree; cpuTree.build(sc);
        for (auto& cl : cs.classes) {
            size_t n = cl.q.size() / 3; std::vector<uint32_t> none;
            std::vector<float> qb; auto dev = g.query(cl.q, none, &qb);
            std::vector<uint32_t> twin(n); parFor(n, [&](size_t i) { twin[i] = twinWalk(sc, tr, &cl.q[3 * i], &qb[6 * i], 0, stk, bmutTwin); });
            auto exact = exactAll(sc, cl.q, none);
            { std::vector<uint8_t> cb(n); cpuTree.classify(cl.q, none, cb, nullptr); for (size_t i = 0; i < n; i++) cpuBvhBad += cb[i] != exact[i]; }
            account(cs.mesh, cl.name, dev, twin, exact, cl.mustReachUncertain);
        }
    }
    // many meshes in one scene, spatially overlapping (so the mesh id test is exercised): 40 blocks of 2x2x2 at random shifts within a 3-unit box, 200 queries each
    {
        Scene sc; sc.multi = true; std::vector<Mesh> per; Rng r(79); std::vector<double> q; std::vector<uint32_t> qm;
        for (uint32_t k = 0; k < 40; k++) {
            Mesh b = mapped(block(2), 0, (double)r.pick(3) * 0.5, (double)r.pick(3) * 0.5, (double)r.pick(3) * 0.5); uint32_t base = (uint32_t)sc.m.nv();
            for (size_t i = 0; i < b.nv(); i++) sc.m.addV(b.v[3*i], b.v[3*i+1], b.v[3*i+2]);
            for (size_t t = 0; t < b.nt(); t++) { sc.m.addT(base + b.t[3*t], base + b.t[3*t+1], base + b.t[3*t+2]); sc.tmesh.push_back(k); }
            per.push_back(b);
            for (int i = 0; i < 200; i++) { for (int d = 0; d < 3; d++) q.push_back(-0.5 + 4.0 * r.u01()); if (i % 4 == 0) for (int d = 0; d < 3; d++) q[q.size() - 3 + d] = std::round(q[q.size() - 3 + d] * 2) / 2; qm.push_back(k); }
        }
        g.prepare(sc); HostTree tr = g.download(); size_t n = qm.size();
        std::vector<float> qb; auto dev = g.query(q, qm, &qb);
        std::vector<uint32_t> twin(n); parFor(n, [&](size_t i) { twin[i] = twinWalk(sc, tr, &q[3 * i], &qb[6 * i], qm[i], stk, bmutTwin); });
        auto exact = exactAll(sc, q, qm, &per);
        { CpuBvh cpuTree; cpuTree.build(sc); std::vector<uint8_t> cb(n); cpuTree.classify(q, qm, cb, nullptr); for (size_t i = 0; i < n; i++) cpuBvhBad += cb[i] != exact[i]; }
        account("40 overlapping 2x2x2 blocks in one scene", "queries (half on the half-integer lattice)", dev, twin, exact, false);
    }
    printf("total: queries %ld, OUT %ld, IN %ld, UNCERTAIN %ld (%.2f%%); false definite %ld; exact-boundary %ld; device vs twin differences %ld\n", tot.q, tot.out, tot.in, tot.unc, 100.0 * tot.unc / std::max(1L, tot.q), tot.falseDef, tot.boundary, tot.devTwin);
    printf("fair CPU baseline (BVH ray walk + native exact predicates) versus the all-triangles exact test: %ld disagreements\n", cpuBvhBad);
    if (cpuBvhBad) { printf("FAIL: the CPU baseline is wrong\n"); g.shutdown(); return 3; }
    bool mutantRun = (spv != "pip_bvh" && spv != "pip_bvh_s2") || g_hmut;
    int rc;
    if (!mutantRun) { rc = !(tot.falseDef == 0 && tot.devTwin == 0 && coverageOk); printf("%s\n", rc ? "FAIL" : "PASS"); }
    else { bool caught = tot.falseDef > 0 || tot.devTwin > 0; rc = !caught; printf("mutant %s hmut=%d: %s\n", spv.c_str(), g_hmut, caught ? "caught" : "NOT CAUGHT"); }
    g.shutdown(); return rc ? 3 : 0;
}

// ---- speed (reported, never a correctness bar) ----
static Scene sphereScene(int rings) { Scene s; s.m = uvsphere(rings, 2 * rings, 1.0, 0, 0, 0); return s; }
// K small spheres on a 3D grid (spacing 3), one mesh id each; queries near their own sphere
static Scene manyScene(int K, std::vector<double>& centers) {
    Scene s; s.multi = true; Mesh base = uvsphere(6, 12, 1.0, 0, 0, 0); int g = (int)std::ceil(std::cbrt((double)K));
    for (int k = 0; k < K; k++) {
        double cx = 3.0 * (k % g), cy = 3.0 * ((k / g) % g), cz = 3.0 * (k / (g * g)); centers.insert(centers.end(), {cx, cy, cz}); uint32_t b0 = (uint32_t)s.m.nv();
        for (size_t i = 0; i < base.nv(); i++) s.m.addV(base.v[3*i] + cx, base.v[3*i+1] + cy, base.v[3*i+2] + cz);
        for (size_t t = 0; t < base.nt(); t++) { s.m.addT(b0 + base.t[3*t], b0 + base.t[3*t+1], b0 + base.t[3*t+2]); s.tmesh.push_back((uint32_t)k); }
    }
    return s;
}
static int bench(const std::string& spv) {
    exactinit(); GpuPip g; if (!g.init(spv)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s; CPU baseline: %u threads, BVH ray walk (cpu/bvh_ref.h, binary64 boxes) + native exact predicates on the candidates only\n", g.ctx.props.deviceName, g.ctx.memMode(), nThreads());
    printf("times in ms. GPU cold = tree build + upload + traverse + readback + exact CPU on UNCERT; amortised = the same without the tree build/upload of the mesh. CPU cold = BVH build + queries.\n");
    printf("%-26s %9s %9s | %9s %9s | %9s %9s %7s | %9s %9s %6s\n", "scene", "triangles", "queries", "CPU cold", "CPU amort", "GPU cold", "GPU amort", "unc %", "cold x", "amort x", "equal");
    struct Run { std::string name; Scene s; std::vector<double> q; std::vector<uint32_t> qm; };
    auto runOne = [&](Run& r) {
        size_t nq = r.q.size() / 3;
        CpuBvh cpu; cpu.build(r.s); std::vector<uint8_t> ref(nq);
        double c0 = gpu::nowMs(); cpu.classify(r.q, r.qm, ref, nullptr); double cpuQ = gpu::nowMs() - c0; for (int i = 0; i < 2; i++) { c0 = gpu::nowMs(); cpu.classify(r.q, r.qm, ref, nullptr); cpuQ = std::min(cpuQ, gpu::nowMs() - c0); }
        double cpuCold = cpu.buildMs + cpuQ; double pmMs = -1;
        if (r.s.multi) {   // the per-mesh CPU path is the fair baseline for many small meshes: use the better of the two CPU paths
            std::vector<uint8_t> pm(nq); pmMs = 1e300; for (int i = 0; i < 3; i++) { double t0 = gpu::nowMs(); cpuPerMesh(r.s, r.q, r.qm, pm); pmMs = std::min(pmMs, gpu::nowMs() - t0); }
            if (pm != ref) printf("   WARNING: the per-mesh CPU path disagrees with the BVH CPU path\n");
            printf("   (many small meshes: combined-tree CPU %.1f ms, per-mesh CPU %.1f ms; the ratios below use the better one)\n", cpuQ, pmMs);
            cpuQ = std::min(cpuQ, pmMs); cpuCold = std::min(cpuCold, pmMs);
        }
        // GPU cold (first run, includes tree build and upload; the CPU tree for UNCERT fallback is built only if needed, and counted)
        auto gpuRun = [&](bool coldRun, size_t* uncOut, std::vector<uint8_t>& out) {
            double t0 = gpu::nowMs(); if (coldRun) g.prepare(r.s);
            auto v = g.query(r.q, r.qm); std::vector<uint32_t> unc; out.assign(nq, 0);
            for (size_t i = 0; i < nq; i++) { if (v[i] == 2) unc.push_back((uint32_t)i); else out[i] = (uint8_t)v[i]; }
            if (!unc.empty()) { CpuBvh c2; const CpuBvh* use = &cpu; if (coldRun) { c2.build(r.s); use = &c2; } use->classify(r.q, r.qm, out, &unc); }
            *uncOut = unc.size(); return gpu::nowMs() - t0;
        };
        std::vector<uint8_t> out; size_t unc = 0; double gCold = gpuRun(true, &unc, out); bool eq = out == ref;
        double gAm = 1e300; for (int i = 0; i < 3; i++) gAm = std::min(gAm, gpuRun(false, &unc, out)); eq = eq && out == ref;
        printf("%-26s %9zu %9zu | %9.1f %9.1f | %9.1f %9.1f %6.2f%% | %8.2fx %8.2fx %6s\n", r.name.c_str(), r.s.m.nt(), nq, cpuCold, cpuQ, gCold, gAm, 100.0 * unc / nq, cpuCold / gCold, cpuQ / gAm, eq ? "yes" : "NO");
    };
    for (int rings : {50, 100, 200, 350, 500}) for (size_t nq : {10000u, 100000u, 1000000u}) {
        Run r; r.name = "one sphere"; r.s = sphereScene(rings); std::mt19937_64 gen(rings * 7 + nq); r.q.resize(3 * nq);
        for (auto& x : r.q) x = 1.3 * ((double)(gen() >> 11) / 9007199254740992.0 * 2 - 1);
        runOne(r);
    }
    for (int K : {1000, 10000}) {
        Run r; std::vector<double> ctr; r.s = manyScene(K, ctr); r.name = "many small (" + std::to_string(K) + " meshes)"; std::mt19937_64 gen(K);
        for (int k = 0; k < K; k++) for (int i = 0; i < 100; i++) { for (int d = 0; d < 3; d++) r.q.push_back(ctr[3 * k + d] + 1.3 * ((double)(gen() >> 11) / 9007199254740992.0 * 2 - 1)); r.qm.push_back((uint32_t)k); }
        runOne(r);
    }
    g.shutdown(); return 0;
}

// ---- bulk: 1e6 mixed queries on larger meshes, the production CPU path as the exact reference, 20 repeated device runs byte-identical ----
static int bulk(const std::string& spv) {
    exactinit(); GpuPip g; if (!g.init(spv)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s shader=%s\n", g.ctx.props.deviceName, spv.c_str());
    int fails = 0; size_t totalQ = 0; long totalFalse = 0;
    for (int which = 0; which < 2; which++) {
        Scene sc; Mesh m = which == 0 ? uvsphere(100, 200, 1.0, 0, 0, 0) : mapped(block(20), 0, std::ldexp(1.0, 30), 0, 0); sc.m = m; const char* name = which == 0 ? "sphere 100x200" : "block 20^3 at +2^30";
        size_t nq = 500000; Rng r(900 + which); std::vector<double> q; std::vector<uint32_t> none;
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (size_t i = 0; i < m.nv(); i++) for (int d = 0; d < 3; d++) { lo[d] = std::min(lo[d], m.v[3*i+d]); hi[d] = std::max(hi[d], m.v[3*i+d]); }
        for (size_t i = 0; i < nq; i++) {
            int kind = r.pick(10);
            if (kind < 6) for (int d = 0; d < 3; d++) { double w = hi[d] - lo[d]; q.push_back(lo[d] - 0.25 * w + 1.5 * w * r.u01()); }
            else if (kind < 8) { size_t v = (size_t)r.pick((int)m.nv()); q.push_back(lo[0] + (hi[0] - lo[0]) * 1.5 * r.u01() - 0.25 * (hi[0] - lo[0])); q.push_back(m.v[3*v+1]); q.push_back(m.v[3*v+2]); }
            else { size_t t = (size_t)r.pick((int)m.nt()); double w0 = r.u01(), w1 = r.u01(), w2 = r.u01(), sw = w0 + w1 + w2; w0 /= sw; w1 /= sw; w2 /= sw; double p[3];
                   for (int d = 0; d < 3; d++) p[d] = w0 * m.v[3*m.t[3*t]+d] + w1 * m.v[3*m.t[3*t+1]+d] + w2 * m.v[3*m.t[3*t+2]+d];
                   int ax = r.pick(3), k2 = r.pick(7) - 3; p[ax] = ulps(p[ax], k2); q.insert(q.end(), p, p + 3); }
        }
        CpuBvh cpu; cpu.build(sc); std::vector<uint8_t> ref(nq); cpu.classify(q, none, ref, nullptr);
        g.prepare(sc); std::vector<uint32_t> first; bool identical = true;
        for (int rep = 0; rep < 20; rep++) { auto v = g.query(q, none); if (rep == 0) first = v; else identical = identical && v == first; }
        long falseDef = 0, unc = 0, bnd = 0; for (size_t i = 0; i < nq; i++) { bnd += ref[i] == 2; if (first[i] == 2) unc++; else if (ref[i] == 2 || first[i] != ref[i]) falseDef++; }
        printf("%-22s queries %7zu  UNCERT %7ld (%.1f%%)  exact-boundary %7ld  false definite %ld  20 repeats byte-identical: %s\n", name, nq, unc, 100.0 * unc / nq, bnd, falseDef, identical ? "yes" : "NO");
        totalQ += nq; totalFalse += falseDef; if (!identical || falseDef) fails++;
    }
    printf("bulk total queries %zu, false definite %ld -> %s\n", totalQ, totalFalse, fails ? "FAIL" : "PASS");
    g.shutdown(); return fails ? 3 : 0;
}

// ---- diagnostic for device-versus-twin differences: find the queries, then compare device and twin per (query, triangle) pair and field by field ----
static void twinCollect(const Scene& s, const HostTree& tr, const float* qb, std::vector<uint32_t>& visited) {
    visited.clear(); std::vector<uint32_t> stack(80); int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        uint32_t node = stack[--sp];
        if (!ovl(qb, &tr.nb[6 * (size_t)node], false)) continue;
        if (node >= tr.n - 1) { visited.push_back(tr.idx[node - (tr.n - 1)]); continue; }
        if (sp + 2 > 64) continue;
        stack[sp++] = tr.lc[node]; stack[sp++] = tr.rc[node];
    }
}
static int diag(const std::string& spv) {
    exactinit(); GpuPip g; if (!g.init(spv)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    gpu::Kernel kd = g.ctx.makeKernel(gpu::shaderDir() + "/pip_diag.spv", 5, 4);
    printf("device=%s shader=%s\n", g.ctx.props.deviceName, spv.c_str());
    auto cases = makeCases(); std::vector<Case> use; use.push_back(std::move(cases[0]));
    { Case c; Rng r(78); c.mesh = "sphere 60x80"; c.m = uvsphere(60, 80, 1, 0, 0, 0); addClasses(c, 0, r, 0, 0, 0, 0); use.push_back(std::move(c)); }
    long diffQ = 0, diffPairs = 0, pairsChecked = 0, fieldDiffs[DIAG_FIELDS] = {0}, shown = 0, qDevMatchesPerTri = 0, qTwinMatchesPerTri = 0, travOnly = 0;
    for (auto& cs : use) {
        Scene sc; sc.m = cs.m; g.prepare(sc); HostTree tr = g.download();
        auto tri = [&](uint32_t k, uint64_t* u) { for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) u[3 * c + d] = bits(sc.m.v[3 * (size_t)sc.m.t[3 * (size_t)k + c] + d]); };
        for (auto& cl : cs.classes) {
            size_t n = cl.q.size() / 3; std::vector<uint32_t> none; std::vector<float> qb; auto dev = g.query(cl.q, none, &qb);
            std::vector<size_t> bad; std::vector<uint32_t> twinV(n);
            for (size_t i = 0; i < n; i++) { twinV[i] = twinWalk(sc, tr, &cl.q[3 * i], &qb[6 * i], 0, 64, 0); if (twinV[i] != dev[i]) bad.push_back(i); }
            if (bad.empty()) continue;
            printf("%s | %s: %zu device-vs-twin differences\n", cs.mesh.c_str(), cl.name.c_str(), bad.size()); diffQ += (long)bad.size();
            // every (differing query, triangle of the mesh) pair on the device
            std::vector<uint32_t> prs; for (size_t bi = 0; bi < bad.size(); bi++) for (uint32_t k = 0; k < sc.m.nt(); k++) { prs.push_back((uint32_t)bad[bi]); prs.push_back(k); }
            size_t np = prs.size() / 2;
            auto bQ = g.ctx.alloc(n * 24 + 8), bP = g.ctx.alloc(np * 8 + 8), bO = g.ctx.alloc(np * DIAG_FIELDS * 4 + 8);
            memcpy(bQ.ptr, cl.q.data(), n * 24); memcpy(bP.ptr, prs.data(), np * 8); uint32_t pc = (uint32_t)np;
            g.ctx.begin(); g.ctx.upload(bQ); g.ctx.upload(bP); g.ctx.dispatch(kd, {&g.bV, &g.bT, &bQ, &bP, &bO}, &pc, (uint32_t)((np + 63) / 64)); g.ctx.download(bO); g.ctx.submitWait();
            const uint32_t* o = (const uint32_t*)bO.ptr;
            for (size_t bi = 0; bi < bad.size(); bi++) {
                size_t qi = bad[bi]; const double* p = &cl.q[3 * qi]; uint64_t pb[3] = {bits(p[0]), bits(p[1]), bits(p[2])};
                std::vector<uint32_t> vis; twinCollect(sc, tr, &qb[6 * qi], vis); std::vector<char> inVis(sc.m.nt(), 0); for (auto k : vis) inVis[k] = 1;
                uint32_t crD = 0, crT = 0; bool uD = false, uT = false; int nd = 0;
                for (uint32_t k = 0; k < sc.m.nt(); k++) {
                    uint64_t u[9]; tri(k, u); const uint32_t* od = o + (size_t)(bi * sc.m.nt() + k) * DIAG_FIELDS; pairsChecked++;
                    uint32_t tw[DIAG_FIELDS]; for (uint32_t w = 0; w < DIAG_FIELDS; w++) tw[w] = diagField(w, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], pb[0], pb[1], pb[2]);
                    bool differ = false; for (int w = 0; w < DIAG_FIELDS; w++) if (od[w] != tw[w]) { fieldDiffs[w]++; differ = true; }
                    if (inVis[k]) { if (od[0] == PIP_UNCERT) uD = true; else crD += od[0]; if (tw[0] == PIP_UNCERT) uT = true; else crT += tw[0]; }
                    if (differ) {
                        diffPairs++; nd++;
                        if (shown < 12) {
                            shown++; printf("  query %zu p=(%.17g, %.17g, %.17g) triangle %u (in twin-visited set: %d)\n    device-fields:", qi, p[0], p[1], p[2], k, (int)inVis[k]);
                            for (int w = 0; w < DIAG_FIELDS; w++) printf(" %u", od[w]); printf("\n    twin-fields:  ");
                            for (int w = 0; w < DIAG_FIELDS; w++) printf(" %u", tw[w]); printf("\n    tri bits:"); for (int w = 0; w < 9; w++) printf(" %016llx", (unsigned long long)u[w]); printf("\n    p bits:  %016llx %016llx %016llx\n", (unsigned long long)pb[0], (unsigned long long)pb[1], (unsigned long long)pb[2]);
                        }
                    }
                }
                uint32_t perTriDev = uD ? 2u : (crD & 1u), perTriTwin = uT ? 2u : (crT & 1u);
                qDevMatchesPerTri += perTriDev == dev[qi]; qTwinMatchesPerTri += perTriTwin == twinV[qi]; travOnly += nd == 0;
                printf("  query %zu: device verdict %u, twin verdict %u; per-triangle device results over the twin-visited set give %u, per-triangle twin results give %u; %d triangle pairs differ field-wise\n", qi, dev[qi], twinV[qi], perTriDev, perTriTwin, nd);
            }
            for (gpu::Buf* b : {&bQ, &bP, &bO}) g.ctx.release(*b);
        }
    }
    printf("summary: %ld differing queries; %ld pairs checked on the device (every triangle of the mesh for each such query); %ld pairs differ in at least one field\n", diffQ, pairsChecked, diffPairs);
    printf("field differences (0 pipTri, 1-3 edge signs, 4 normal sign, 5 orient3d side, 6-13 det words):"); for (int w = 0; w < DIAG_FIELDS; w++) printf(" %ld", fieldDiffs[w]); printf("\n");
    printf("queries whose per-triangle device sum equals the device verdict: %ld of %ld; per-triangle twin sum equals the twin verdict: %ld of %ld; queries with NO field-level difference at all (so the difference is in the traversal): %ld\n", qDevMatchesPerTri, diffQ, qTwinMatchesPerTri, diffQ, travOnly);
    g.ctx.release(kd); g.shutdown(); return 0;
}

// ---- f64soft check on the device: random operand pairs through f64_add / f64_mul / f64_sub against the hardware double, and the multiplication stages against the host ----
static int f64check(int nPairs) {
    exactinit(); gpu::Ctx ctx; if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    gpu::Kernel k = ctx.makeKernel(gpu::shaderDir() + "/f64_check.spv", 2, 4);
    printf("device=%s\n", ctx.props.deviceName);
    std::mt19937_64 g(4242); std::vector<uint64_t> ab(2 * (size_t)nPairs);
    for (int i = 0; i < nPairs; i++) for (int j = 0; j < 2; j++) {   // normal doubles, exponents within +-20 of 1 so that results stay normal; some pairs are exactly representable cancellations
        uint64_t m = g() & 0xFFFFFFFFFFFFFull; uint64_t e = 1023 - 20 + g() % 41; uint64_t sgn = g() & 1; ab[2 * (size_t)i + j] = (sgn << 63) | (e << 52) | m; }
    auto bA = ctx.alloc(ab.size() * 8), bO = ctx.alloc((size_t)nPairs * 24 * 4);
    memcpy(bA.ptr, ab.data(), ab.size() * 8); uint32_t pc = (uint32_t)nPairs;
    ctx.begin(); ctx.upload(bA); ctx.dispatch(k, {&bA, &bO}, &pc, (uint32_t)((nPairs + 63) / 64)); ctx.download(bO); ctx.submitWait();
    const uint32_t* o = (const uint32_t*)bO.ptr; long devHw[3] = {0, 0, 0}, bad[3] = {0, 0, 0}, stageBad[8] = {0}, shBad = 0, upBad = 0, shown = 0, hwBad[3] = {0, 0, 0};
    auto w64 = [&](size_t i, int w) { return (uint64_t)o[24 * i + w] | ((uint64_t)o[24 * i + w + 1] << 32); };
    for (int ii = 0; ii < nPairs; ii++) {
        size_t i = (size_t)ii; uint64_t a = ab[2 * i], b = ab[2 * i + 1]; double da, db; memcpy(&da, &a, 8); memcpy(&db, &b, 8);
        double hw[3] = {da + db, da * db, da - db}; uint64_t hwb[3]; for (int j = 0; j < 3; j++) memcpy(&hwb[j], &hw[j], 8);
        uint64_t host[3] = {f64_add(a, b), f64_mul(a, b), f64_sub(a, b)};
        for (int j = 0; j < 3; j++) { if (w64(i, 2 * j) != host[j]) bad[j]++; if (w64(i, 2 * j) != hwb[j]) devHw[j]++; if (host[j] != hwb[j]) hwBad[j]++; }
        const uint64_t kMant = 0x000FFFFFFFFFFFFFull, kHidden = 0x0010000000000000ull;
        uint64_t ma = (a & kMant) | kHidden, mb = (b & kMant) | kHidden, a0 = ma & 0xFFFFFFFFull, a1 = ma >> 32, b0 = mb & 0xFFFFFFFFull, b1 = mb >> 32;
        uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1, mid = p01 + p10, lo = p00 + ((mid & 0xFFFFFFFFull) << 32), hi = p11 + (mid >> 32) + (lo < p00 ? 1ull : 0ull);
        int sh = (hi >= 0x20000000000ull) ? 53 : 52; uint64_t rem = lo & ((1ull << sh) - 1ull), halfway = 1ull << (sh - 1), m = (hi << (64 - sh)) | (lo >> sh); bool up = rem > halfway || (rem == halfway && (m & 1ull));
        uint64_t st[8] = {p00, p01, p10, p11, mid, lo, hi, rem}; bool any = false;
        for (int j = 0; j < 8; j++) if (w64(i, 6 + 2 * j) != st[j]) { stageBad[j]++; any = true; }
        if ((int)o[24 * i + 22] != sh) shBad++; if ((o[24 * i + 23] != 0) != up) upBad++;
        if ((any || w64(i, 2) != host[1]) && shown < 6) { shown++; printf("  pair %zu a=%016llx b=%016llx\n    device mul=%016llx host mul=%016llx hardware=%016llx\n    stages p00 p01 p10 p11 mid lo hi rem (device / host):\n", i, (unsigned long long)a, (unsigned long long)b, (unsigned long long)w64(i, 2), (unsigned long long)host[1], (unsigned long long)hwb[1]);
            for (int j = 0; j < 8; j++) printf("      %016llx / %016llx %s\n", (unsigned long long)w64(i, 6 + 2 * j), (unsigned long long)st[j], w64(i, 6 + 2 * j) == st[j] ? "" : "<-- differs"); }
    }
    printf("%d pairs. device vs host f64soft: add %ld, mul %ld, sub %ld mismatches. host f64soft vs hardware double: add %ld, mul %ld, sub %ld.\n", nPairs, bad[0], bad[1], bad[2], hwBad[0], hwBad[1], hwBad[2]);
    printf("device f64soft vs hardware double: add %ld, mul %ld, sub %ld mismatches.\n", devHw[0], devHw[1], devHw[2]);
    printf("mul stage mismatches device vs host (p00 p01 p10 p11 mid lo hi rem): %ld %ld %ld %ld %ld %ld %ld %ld; sh %ld; round-up flag %ld\n", stageBad[0], stageBad[1], stageBad[2], stageBad[3], stageBad[4], stageBad[5], stageBad[6], stageBad[7], shBad, upBad);
    ctx.release(bA); ctx.release(bO); ctx.release(k); ctx.destroy(); return (bad[0] || bad[1] || bad[2] || devHw[0] || devHw[1] || devHw[2]) ? 3 : 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string mode = argc > 1 ? argv[1] : "device";
    if (getenv("HMUT")) g_hmut = atoi(getenv("HMUT"));
    std::string spv = argc > 2 ? argv[2] : "pip_bvh";
    int stk = 64; if (spv == "pip_bvh_s2") stk = 2; if (spv == "pip_bvh_b2") stk = 4;
    int bm = spv == "pip_bvh_b1" ? 1 : spv == "pip_bvh_b2" ? 2 : spv == "pip_bvh_b5" ? 5 : 0;
    if (mode == "device") return correctness(spv, bm, stk, spv == "pip_bvh" && !g_hmut);
    if (mode == "bench") return bench(spv);
    if (mode == "diag") return diag(spv);
    if (mode == "f64check") return f64check(argc > 2 ? atoi(argv[2]) : 1000000);
    if (mode == "bulk") return bulk(spv);
    printf("unknown mode\n"); return 1;
}
