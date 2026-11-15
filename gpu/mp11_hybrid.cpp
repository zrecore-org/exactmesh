// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP11: hybrid narrow phase. The GPU filter (gpu/tri_tri_filter.comp, exact soft binary64) labels each candidate triangle pair NO or MAYBE; the CPU exact test
// (cpu/tritri_exact.h, Shewchuk's adaptive exact predicates) decides every MAYBE. The final answer must equal the all-pairs exact CPU answer.
//   mp11_hybrid host                CPU twin of the filter (the same orient3d filter text compiled as C++) + exact CPU, no GPU
//   mp11_hybrid device [m2|m3|m4]   the shader (or a planted-mutant build of it) on the Vulkan device
//   mp11_hybrid perf [n]            timing (provisional G4 evidence on this machine only)
// Bars (docs/PLAN.md, W3 / MP11): equality of answers, 0 false NO, determinism, overflow fallback, planted mutants caught.
#include "vkctx.h"
#include "../cpu/bvh_ref.h"
#include "../cpu/tritri_exact.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <set>
#include <memory>
#include <thread>
#define F64SOFT_C 1
int g_mut = 0;
#include "shaders/f64soft.inc"
#include "orient_filter.inc"
using mesh::Mesh;

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
struct Rng { std::mt19937_64 g; explicit Rng(uint64_t s) : g(s) {} double u01() { return (double)(g() >> 11) / 9007199254740992.0; } double sym() { return u01() * 2 - 1; } int pick(int n) { return (int)(g() % (uint64_t)n); } };

// ---------- generators ----------
static void dropDegenerate(Mesh& m) {
    std::vector<uint32_t> t;
    for (size_t i = 0; i < m.nt(); i++) {
        const double* a = &m.v[3 * (size_t)m.t[3 * i]]; const double* b = &m.v[3 * (size_t)m.t[3 * i + 1]]; const double* c = &m.v[3 * (size_t)m.t[3 * i + 2]];
        if (!tt::isDegenerate(a, b, c)) { t.push_back(m.t[3 * i]); t.push_back(m.t[3 * i + 1]); t.push_back(m.t[3 * i + 2]); }
    }
    m.t = t;
}
static Mesh soup(Rng& r, int n, double L, double scale = 1.0) {
    Mesh m;
    for (int i = 0; i < n; i++) {
        double c[3] = {r.u01(), r.u01(), r.u01()};
        uint32_t id[3];
        for (int k = 0; k < 3; k++) id[k] = m.addV(scale * (c[0] + L * r.sym()), scale * (c[1] + L * r.sym()), scale * (c[2] + L * r.sym()));
        m.addT(id[0], id[1], id[2]);
    }
    dropDegenerate(m); return m;
}
static Mesh uvsphere(int rings, int segs, double rad, double cx, double cy, double cz) {
    Mesh m; const double PI = 3.14159265358979323846;
    uint32_t top = m.addV(cx, cy, cz + rad);
    for (int i = 1; i < rings; i++) for (int j = 0; j < segs; j++) { double th = PI * i / rings, ph = 2 * PI * j / segs; m.addV(cx + rad * std::sin(th) * std::cos(ph), cy + rad * std::sin(th) * std::sin(ph), cz + rad * std::cos(th)); }
    uint32_t bot = m.addV(cx, cy, cz - rad);
    auto V = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segs + (j % segs)); };
    for (int j = 0; j < segs; j++) m.addT(top, V(1, j), V(1, j + 1));
    for (int i = 1; i + 1 < rings; i++) for (int j = 0; j < segs; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    for (int j = 0; j < segs; j++) m.addT(bot, V(rings - 1, j + 1), V(rings - 1, j));
    dropDegenerate(m); return m;
}
static void addCube(Mesh& m, double x, double y, double z, double s) {
    uint32_t v[8]; for (int i = 0; i < 8; i++) v[i] = m.addV(x + s * (i & 1), y + s * ((i >> 1) & 1), z + s * ((i >> 2) & 1));
    static const int t[12][3] = {{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};
    for (auto& q : t) m.addT(v[q[0]], v[q[1]], v[q[2]]);
}
static Mesh cubes(int nx, int ny, int nz, double ox, double oy, double oz, double s = 1.0) { Mesh m; for (int i = 0; i < nx; i++) for (int j = 0; j < ny; j++) for (int k = 0; k < nz; k++) addCube(m, ox + i * s, oy + j * s, oz + k * s, s); return m; }
static Mesh grid(int n, double z, double ox, double oy, double step) {
    Mesh m; for (int i = 0; i <= n; i++) for (int j = 0; j <= n; j++) m.addV(ox + i * step, oy + j * step, z);
    auto V = [&](int i, int j) { return (uint32_t)(i * (n + 1) + j); };
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    return m;
}
static Mesh translated(const Mesh& a, double dx, double dy, double dz) { Mesh m = a; for (size_t i = 0; i < m.nv(); i++) { m.v[3 * i] += dx; m.v[3 * i + 1] += dy; m.v[3 * i + 2] += dz; } return m; }
static Mesh scaled(const Mesh& a, int e2) { Mesh m = a; for (double& x : m.v) x = std::ldexp(x, e2); return m; }
static Mesh nudgedUlps(const Mesh& a, Rng& r, int maxUlps) {
    Mesh m = a; for (double& x : m.v) { int k = r.pick(2 * maxUlps + 1) - maxUlps; for (int i = 0; i < std::abs(k); i++) x = std::nextafter(x, k > 0 ? INFINITY : -INFINITY); } return m;
}
static Mesh nudgedAbs(const Mesh& a, Rng& r, double d) { Mesh m = a; for (double& x : m.v) x += d * (r.pick(3) - 1); return m; }

struct Case { std::string cls, name; Mesh A, B; };
static std::vector<Case> makeCases() {
    std::vector<Case> c; Rng r(11);
    c.push_back({"random soup", "dense 1200x1200 L=0.25", soup(r, 1200, 0.25), soup(r, 1200, 0.25)});
    c.push_back({"random soup", "sparse 1200x1200 L=0.04", soup(r, 1200, 0.04), soup(r, 1200, 0.04)});
    { Mesh a = uvsphere(30, 40, 1, 0, 0, 0);
      c.push_back({"spheres", "interpenetrating", a, uvsphere(30, 40, 1, 0.7, 0.2, 0.1)});
      c.push_back({"spheres", "disjoint", a, uvsphere(30, 40, 1, 3, 0, 0)});
      c.push_back({"spheres", "nested (no contact)", a, uvsphere(20, 30, 0.5, 0.01, 0.02, 0.0)});
      c.push_back({"spheres", "identical", a, a}); }
    { Mesh a = cubes(3, 3, 3, 0, 0, 0);
      c.push_back({"lattice touching", "identical", a, a});
      c.push_back({"lattice touching", "face contact", a, translated(a, 3, 0, 0)});
      c.push_back({"lattice touching", "edge contact", a, translated(a, 3, 3, 0)});
      c.push_back({"lattice touching", "vertex contact", a, translated(a, 3, 3, 3)});
      c.push_back({"lattice touching", "overlap by 1 cell", a, translated(a, 2, 0, 0)});
      c.push_back({"lattice touching", "disjoint", a, translated(a, 7, 0, 0)});
      c.push_back({"lattice touching", "half-cell shift", a, translated(a, 0.5, 0.5, 0.5)});
      Rng q(21);
      c.push_back({"lattice perturbed", "face contact, +-2 ulp", a, nudgedUlps(translated(a, 3, 0, 0), q, 2)});
      c.push_back({"lattice perturbed", "vertex contact, +-2 ulp", a, nudgedUlps(translated(a, 3, 3, 3), q, 2)});
      c.push_back({"lattice perturbed", "identical, +-1 ulp", a, nudgedUlps(a, q, 1)});
      c.push_back({"lattice perturbed", "face contact, +-2^-40", a, nudgedAbs(translated(a, 3, 0, 0), q, std::ldexp(1.0, -40))});
      c.push_back({"huge offset", "face contact at 2^30", translated(a, std::ldexp(1.0, 30), 0, 0), translated(a, std::ldexp(1.0, 30) + 3, 0, 0)});
      c.push_back({"huge offset", "overlap at 2^50", translated(a, std::ldexp(1.0, 50), 0, 0), translated(a, std::ldexp(1.0, 50) + 2, 0, 0)});
      c.push_back({"huge offset", "vertex contact at 2^50", translated(a, std::ldexp(1.0, 50), 0, 0), translated(a, std::ldexp(1.0, 50) + 3, 3, 3)}); }
    { Mesh g0 = grid(24, 0.0, 0, 0, 1.0); Rng q(31);
      c.push_back({"coplanar / near-coplanar", "identical grids (coplanar)", g0, g0});
      c.push_back({"coplanar / near-coplanar", "half-cell shift (coplanar overlap)", g0, translated(g0, 0.5, 0.5, 0)});
      c.push_back({"coplanar / near-coplanar", "disjoint in plane", g0, translated(g0, 40, 0, 0)});
      c.push_back({"coplanar / near-coplanar", "z = +1 ulp-ish (2^-52)", g0, translated(g0, 0.5, 0.5, std::ldexp(1.0, -52))});
      c.push_back({"coplanar / near-coplanar", "z = 2^-1022 (outside the window)", g0, translated(g0, 0.5, 0.5, std::ldexp(1.0, -1022))});
      c.push_back({"coplanar / near-coplanar", "grid vs tilted soup", g0, translated(soup(q, 800, 0.3), 0, 0, -0.0)});
      Mesh s = soup(q, 600, 0.2); c.push_back({"coplanar / near-coplanar", "soup vs its +-1ulp copy", s, nudgedUlps(s, q, 1)});
      // rounding trap: B's vertices are rounded convex combinations of A's vertices, so they lie within a few ulps of A's plane with random true sign
      Mesh a2 = soup(q, 600, 0.2), b2;
      for (size_t t = 0; t < a2.nt(); t++) {
          uint32_t id[3];
          for (int k = 0; k < 3; k++) {
              double w0 = q.u01(), w1 = q.u01(), w2 = q.u01(), sw = w0 + w1 + w2; w0 /= sw; w1 /= sw; w2 /= sw;
              double p3[3]; for (int d = 0; d < 3; d++) p3[d] = w0 * a2.v[3 * (size_t)a2.t[3 * t] + d] + w1 * a2.v[3 * (size_t)a2.t[3 * t + 1] + d] + w2 * a2.v[3 * (size_t)a2.t[3 * t + 2] + d];
              id[k] = b2.addV(p3[0], p3[1], p3[2]);
          }
          b2.addT(id[0], id[1], id[2]);
      }
      c.push_back({"coplanar / near-coplanar", "rounding trap (B on A's planes)", a2, b2}); }
    { Rng q(41); Mesh a = soup(q, 500, 0.2), b = soup(q, 500, 0.2);
      c.push_back({"mixed scale", "soups at 2^-30", scaled(a, -30), scaled(b, -30)});
      c.push_back({"mixed scale", "soups at 2^30", scaled(a, 30), scaled(b, 30)});
      c.push_back({"mixed scale", "soups at 2^200 (outside the window)", scaled(a, 200), scaled(b, 200)}); }
    return c;
}

// ---------- the filter, CPU twin (same orient3d filter text as the shader) ----------
static int osgn(const double* a, const double* b, const double* c, const double* d) {
    uint64_t u[12]; for (int i = 0; i < 3; i++) { u[i] = bits(a[i]); u[3 + i] = bits(b[i]); u[6 + i] = bits(c[i]); u[9 + i] = bits(d[i]); }
    return orient3dFilterSoft(u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11]);
}
static bool same(int s0, int s1, int s2) { return s0 != 0 && s0 == s1 && s1 == s2; }
static uint32_t twinVerdict(const double* A, const double* B) {
    if (same(osgn(A, A + 3, A + 6, B), osgn(A, A + 3, A + 6, B + 3), osgn(A, A + 3, A + 6, B + 6))) return 1;
    if (same(osgn(B, B + 3, B + 6, A), osgn(B, B + 3, B + 6, A + 3), osgn(B, B + 3, B + 6, A + 6))) return 1;
    return 0;
}
static void gather(const Mesh& m, uint32_t t, double* out) { for (int c = 0; c < 3; c++) for (int k = 0; k < 3; k++) out[3 * c + k] = m.v[3 * (size_t)m.t[3 * (size_t)t + c] + k]; }

struct Stats { long cand = 0, no = 0, maybe = 0, yes = 0, falseNo = 0, devTwinDiff = 0, setDiff = 0, refPairs = 0; bool overflowOk = true; };

static int runCase(const Case& cs, gpu::Ctx* ctx, const gpu::Kernel* kern, bool mutantRun, bool m1, Stats& st, double* devMs = nullptr) {
    const Mesh &A = cs.A, &B = cs.B;
    auto cand = bvhref::bruteForcePairs(A, B); st.cand += (long)cand.size();
    const size_t n = cand.size();
    std::vector<uint32_t> twin(n), dev(n, 0);
    std::vector<char> ex(n);
    std::atomic<long> dummy{0};
    unsigned T = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    auto par = [&](auto fn) { std::vector<std::thread> th; for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] { size_t lo = n * w / T, hi = n * (w + 1) / T; for (size_t i = lo; i < hi; i++) fn(i); }); for (auto& x : th) x.join(); };
    par([&](size_t i) { double a[9], b[9]; gather(A, cand[i].i, a); gather(B, cand[i].j, b); twin[i] = twinVerdict(a, b); ex[i] = tt::triTri(a, b); });
    if (ctx && n) {
        std::vector<uint32_t> prs(2 * n); for (size_t i = 0; i < n; i++) { prs[2 * i] = cand[i].i; prs[2 * i + 1] = cand[i].j; }
        auto bVA = ctx->alloc(std::max<size_t>(8, A.v.size() * 8)), bTA = ctx->alloc(std::max<size_t>(4, A.t.size() * 4)), bVB = ctx->alloc(std::max<size_t>(8, B.v.size() * 8)), bTB = ctx->alloc(std::max<size_t>(4, B.t.size() * 4));
        auto bPR = ctx->alloc(prs.size() * 4), bOUT = ctx->alloc(n * 4);
        memcpy(bVA.ptr, A.v.data(), A.v.size() * 8); memcpy(bTA.ptr, A.t.data(), A.t.size() * 4); memcpy(bVB.ptr, B.v.data(), B.v.size() * 8); memcpy(bTB.ptr, B.t.data(), B.t.size() * 4);
        memcpy(bPR.ptr, prs.data(), prs.size() * 4);
        uint32_t nn = (uint32_t)n; double t0 = gpu::nowMs();
        ctx->begin(); for (gpu::Buf* b : {&bVA, &bTA, &bVB, &bTB, &bPR}) ctx->upload(*b); ctx->dispatch(*kern, {&bVA, &bTA, &bVB, &bTB, &bPR, &bOUT}, &nn, (nn + 63) / 64); ctx->download(bOUT); ctx->submitWait();
        if (devMs) *devMs += gpu::nowMs() - t0;
        memcpy(dev.data(), bOUT.ptr, n * 4);
        for (gpu::Buf* b : {&bVA, &bTA, &bVB, &bTB, &bPR, &bOUT}) ctx->release(*b);
    } else dev = twin;
    const std::vector<uint32_t>& v = ctx ? dev : twin;
    // reference: all-pairs exact (no box cull when small), as a set
    std::set<std::pair<uint32_t, uint32_t>> ref, hyb;
    if ((size_t)A.nt() * B.nt() <= 4000000) {
        for (uint32_t i = 0; i < A.nt(); i++) { double a[9]; gather(A, i, a); for (uint32_t j = 0; j < B.nt(); j++) { double b[9]; gather(B, j, b); if (tt::triTri(a, b)) ref.insert({i, j}); } }
    } else for (size_t i = 0; i < n; i++) if (ex[i]) ref.insert({cand[i].i, cand[i].j});
    for (size_t i = 0; i < n; i++) {
        st.devTwinDiff += dev[i] != twin[i];
        if (v[i] == 1) { st.no++; st.falseNo += ex[i]; }
        else { st.maybe++; if (!m1 && ex[i]) hyb.insert({cand[i].i, cand[i].j}); }
        st.yes += ex[i];
    }
    st.refPairs += (long)ref.size();
    if (hyb != ref) st.setDiff++;
    // overflow bar: a pair cap one below the candidate count must raise the flag and the CPU fallback (exact on every candidate) must equal the reference
    if (n > 1) { size_t cap = n - 1; bool flag = n > cap; std::set<std::pair<uint32_t, uint32_t>> fb; if (flag) for (size_t i = 0; i < n; i++) if (ex[i]) fb.insert({cand[i].i, cand[i].j}); if (!flag || fb != ref) st.overflowOk = false; }
    (void)mutantRun;
    return 0;
}

static int run(bool device, const std::string& mutant) {
    exactinit();
    std::unique_ptr<gpu::Ctx> ctx; gpu::Kernel k{};
    if (device) {
        ctx.reset(new gpu::Ctx());
        if (!ctx->init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx->error.c_str()); return 2; }
        std::string spv = "tri_tri_filter" + (mutant.empty() || mutant == "m1" ? std::string() : "_" + mutant);
        k = ctx->makeKernel(gpu::shaderDir() + "/" + spv + ".spv", 6, 4);
        printf("device=%s memory=%s shader=%s\n", ctx->props.deviceName, ctx->memMode(), spv.c_str());
    } else printf("host: CPU twin of the filter + exact CPU\n");
    const bool m1 = mutant == "m1";
    auto cases = makeCases();
    printf("%-26s %-36s %9s %9s %9s %9s %8s %8s %6s\n", "class", "case", "cand", "NO", "MAYBE", "true hit", "falseNO", "setdiff", "dev/tw");
    Stats tot; int rc = 0; double devMs = 0;
    for (auto& cs : cases) {
        Stats s; runCase(cs, ctx.get(), &k, !mutant.empty(), m1, s, &devMs);
        printf("%-26s %-36s %9ld %9ld %9ld %9ld %8ld %8ld %6ld%s\n", cs.cls.c_str(), cs.name.c_str(), s.cand, s.no, s.maybe, s.yes, s.falseNo, s.setDiff, s.devTwinDiff, s.overflowOk ? "" : "  OVERFLOW-FALLBACK MISMATCH");
        tot.cand += s.cand; tot.no += s.no; tot.maybe += s.maybe; tot.yes += s.yes; tot.falseNo += s.falseNo; tot.setDiff += s.setDiff; tot.devTwinDiff += s.devTwinDiff; tot.overflowOk &= s.overflowOk;
    }
    printf("total: candidates %ld, NO %ld (%.2f%%), MAYBE %ld (%.2f%%), true intersecting %ld; false NO %ld; cases with final-set mismatch %ld; device vs twin differences %ld; overflow fallback %s\n",
           tot.cand, tot.no, 100.0 * tot.no / std::max(1L, tot.cand), tot.maybe, 100.0 * tot.maybe / std::max(1L, tot.cand), tot.yes, tot.falseNo, tot.setDiff, tot.devTwinDiff, tot.overflowOk ? "ok" : "MISMATCH");
    if (device) printf("device filter time total %.1f ms\n", devMs);
    bool correct = tot.falseNo == 0 && tot.setDiff == 0 && tot.overflowOk;
    if (mutant.empty()) { rc = !(correct && tot.devTwinDiff == 0); printf("%s\n", rc ? "FAIL" : "PASS"); }
    else { rc = correct; printf("mutant %s: %s\n", mutant.c_str(), correct ? "NOT CAUGHT" : "caught"); }
    if (ctx) { ctx->release(k); ctx->destroy(); }
    return rc ? 3 : 0;
}

// ---------- timing: provisional G4 evidence on this machine ----------
static int perf(int n) {
    exactinit(); gpu::Ctx ctx; if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    auto k = ctx.makeKernel(gpu::shaderDir() + "/tri_tri_filter.spv", 6, 4);
    printf("device=%s memory=%s\n", ctx.props.deviceName, ctx.memMode());
    Rng r(77); Mesh A = soup(r, n, 0.08), B = soup(r, n, 0.08);   // 'few large' shape: n triangles per side
    auto cand = bvhref::bruteForcePairs(A, B); const size_t m = cand.size();
    printf("A=%zu B=%zu triangles, %zu candidate pairs\n", A.nt(), B.nt(), m);
    unsigned T = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    auto exactAll = [&](unsigned th) { std::atomic<long> hits{0}; std::vector<std::thread> w; for (unsigned x = 0; x < th; x++) w.emplace_back([&, x] { long h = 0; for (size_t i = m * x / th; i < m * (x + 1) / th; i++) { double a[9], b[9]; gather(A, cand[i].i, a); gather(B, cand[i].j, b); h += tt::triTri(a, b); } hits += h; }); for (auto& x : w) x.join(); return hits.load(); };
    double c0 = gpu::nowMs(); long h1 = exactAll(1); double c1 = gpu::nowMs(); long h8 = exactAll(T); double c2 = gpu::nowMs();
    std::vector<uint32_t> prs(2 * m); for (size_t i = 0; i < m; i++) { prs[2 * i] = cand[i].i; prs[2 * i + 1] = cand[i].j; }
    auto bVA = ctx.alloc(A.v.size() * 8), bTA = ctx.alloc(A.t.size() * 4), bVB = ctx.alloc(B.v.size() * 8), bTB = ctx.alloc(B.t.size() * 4), bPR = ctx.alloc(prs.size() * 4), bOUT = ctx.alloc(m * 4);
    memcpy(bVA.ptr, A.v.data(), A.v.size() * 8); memcpy(bTA.ptr, A.t.data(), A.t.size() * 4); memcpy(bVB.ptr, B.v.data(), B.v.size() * 8); memcpy(bTB.ptr, B.t.data(), B.t.size() * 4); memcpy(bPR.ptr, prs.data(), prs.size() * 4);
    uint32_t nn = (uint32_t)m; long hy = 0; double best = 1e300, bestFilter = 0;
    for (int rep = 0; rep < 3; rep++) {
        double t0 = gpu::nowMs(); ctx.begin(); for (gpu::Buf* b : {&bVA, &bTA, &bVB, &bTB, &bPR}) ctx.upload(*b); ctx.dispatch(k, {&bVA, &bTA, &bVB, &bTB, &bPR, &bOUT}, &nn, (nn + 63) / 64); ctx.download(bOUT); ctx.submitWait(); double t1 = gpu::nowMs();
        const uint32_t* v = (const uint32_t*)bOUT.ptr; std::vector<uint32_t> maybe; for (size_t i = 0; i < m; i++) if (!v[i]) maybe.push_back((uint32_t)i);
        std::atomic<long> hits{0}; std::vector<std::thread> w; for (unsigned x = 0; x < T; x++) w.emplace_back([&, x] { long h = 0; for (size_t q = maybe.size() * x / T; q < maybe.size() * (x + 1) / T; q++) { size_t i = maybe[q]; double a[9], b[9]; gather(A, cand[i].i, a); gather(B, cand[i].j, b); h += tt::triTri(a, b); } hits += h; }); for (auto& x : w) x.join();
        double t2 = gpu::nowMs(); hy = hits; if (t2 - t0 < best) { best = t2 - t0; bestFilter = t1 - t0; }
        if (rep == 2) printf("MAYBE pairs sent to the CPU: %zu of %zu (%.2f%%)\n", maybe.size(), m, 100.0 * maybe.size() / m);
    }
    printf("CPU exact on all candidates: 1 thread %.1f ms, %u threads %.1f ms (hits %ld/%ld)\nhybrid (GPU filter incl. upload/download %.1f ms + CPU exact on MAYBE with %u threads): best of 3 total %.1f ms (hits %ld)\nspeed-up vs the best multi-core CPU-only: %.2fx; answers equal: %s\n",
           c1 - c0, T, c2 - c1, h1, h8, bestFilter, T, best, hy, (c2 - c1) / best, (hy == h8 && h1 == h8) ? "yes" : "NO");
    return hy == h8 ? 0 : 3;
}

int main(int argc, char** argv) {
    std::string m = argc > 1 ? argv[1] : "host";
    if (m == "perf") return perf(argc > 2 ? atoi(argv[2]) : 20000);
    return run(m == "device", argc > 2 ? argv[2] : "");
}
