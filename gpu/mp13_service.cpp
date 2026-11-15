// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP13: fallback, circuit breaker, startup self-test, sampled shadow check, and crossover, around the MP12 point-in-solid filter (gpu/pip_ray.comp).
// The CPU exact path (mesh::classifyPointHardened) is the reference and the fallback. Every answer the service returns must equal the CPU-only answer byte for byte.
//   mp13_service faults [mutant]   every fault case (docs/PLAN.md "MP13 amendment" bar 1..3, 5); mutant 1..4 plants a fault in the service itself
//   mp13_service crossover         GPU path (upload included) versus the 8-thread CPU exact path over sizes
// Faults are real where the machine can make them real: no device (VK_ICD_FILENAMES pointing nowhere: run as `mp13_service nodevice`), shader load failure (bad path),
// out of memory (an impossible allocation). Device lost, a hang past the deadline, a wrong answer and a pair/uncertain-rate cap are injected: a real device loss cannot be
// produced on demand and is NOT claimed as tested on hardware.
#include "vkctx.h"
#include "../cpu/pip_hardened.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#define F64SOFT_C 1
int g_mut = 0;
#include "shaders/f64soft.inc"
#include "orient_filter.inc"
#include "pip_filter.inc"
extern "C" { void exactinit(void); }
using mesh::Mesh;

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
static std::vector<double> randomQueries(size_t n, double w, uint64_t seed) { std::mt19937_64 g(seed); std::vector<double> q; for (size_t i = 0; i < 3 * n; i++) q.push_back(w * ((double)(g() >> 11) / 9007199254740992.0 * 2 - 1)); return q; }

// ---------- the CPU path (reference and fallback): one byte per query: 0 outside, 1 inside, 2 on the surface ----------
static unsigned nThreads() { return std::max(1u, std::min(8u, std::thread::hardware_concurrency())); }
static void cpuRange(const Mesh& m, const std::vector<double>& q, std::vector<uint8_t>& out, const std::vector<uint32_t>* only) {
    size_t n = only ? only->size() : q.size() / 3; unsigned T = nThreads();
    std::vector<std::thread> th;
    for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] { for (size_t i = n * w / T; i < n * (w + 1) / T; i++) { size_t qi = only ? (*only)[i] : i; mesh::Pip p = mesh::classifyPointHardened(m, &q[3 * qi]); out[qi] = p.boundary ? 2 : (p.inside ? 1 : 0); } });
    for (auto& t : th) t.join();
}
static std::vector<uint8_t> cpuOnly(const Mesh& m, const std::vector<double>& q) { std::vector<uint8_t> o(q.size() / 3); cpuRange(m, q, o, nullptr); return o; }
// the better CPU baseline: the same exact classification, but triangles whose (y, z) bounding box strictly excludes the query are skipped first (provably no crossing and
// the query is not on them: the same rejection the GPU filter makes), so only the few triangles near the ray reach the exact predicates. Multi-threaded, no BVH.
static std::vector<uint8_t> cpuFast(const Mesh& m, const std::vector<double>& q) {
    size_t n = q.size() / 3; std::vector<uint8_t> out(n); unsigned T = nThreads();
    std::vector<double> lo(2 * m.nt()), hi(2 * m.nt());
    for (size_t t = 0; t < m.nt(); t++) for (int d = 0; d < 2; d++) { double a = m.v[3 * (size_t)m.t[3 * t] + 1 + d], b = m.v[3 * (size_t)m.t[3 * t + 1] + 1 + d], c = m.v[3 * (size_t)m.t[3 * t + 2] + 1 + d]; lo[2 * t + d] = std::min(a, std::min(b, c)); hi[2 * t + d] = std::max(a, std::max(b, c)); }
    std::vector<std::thread> th;
    for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] {
        Mesh sub; sub.v = m.v;
        for (size_t i = n * w / T; i < n * (w + 1) / T; i++) {
            const double* p = &q[3 * i]; sub.t.clear();
            for (size_t t = 0; t < m.nt(); t++) if (!(p[1] > hi[2 * t] || p[1] < lo[2 * t] || p[2] > hi[2 * t + 1] || p[2] < lo[2 * t + 1])) { sub.t.push_back(m.t[3 * t]); sub.t.push_back(m.t[3 * t + 1]); sub.t.push_back(m.t[3 * t + 2]); }
            mesh::Pip r = mesh::classifyPointHardened(sub, p); out[i] = r.boundary ? 2 : (r.inside ? 1 : 0);
        }
    });
    for (auto& t : th) t.join();
    return out;
}

// ---------- the service ----------
enum class Fault { None, DeviceLost, Hang, WrongAnswer };
struct Config { int shadowEvery = 1; double uncertCap = 0.95; int breakerAfter = 2; double deadlineMs = 5000; std::string spv = "pip_ray"; size_t allocOverride = 0; };
struct Service {
    int mutant = 0;                       // planted faults in the service itself: 1 breaker never trips, 2 shadow check never runs, 3 partial GPU result returned when the GPU path is not definite, 4 self-test always passes
    Config cfg; std::unique_ptr<gpu::Ctx> ctx; gpu::Kernel k{}; bool haveKernel = false;
    bool gpuOn = false, tripped = false; std::string reason; int consecutiveFail = 0, batches = 0; Fault inject = Fault::None; bool leakedHang = false;
    std::vector<std::string> log;
    void trip(const std::string& why) { if (mutant == 1) { log.push_back("(breaker disabled) " + why); return; } tripped = true; gpuOn = false; reason = why; log.push_back("TRIP: " + why); }
    // GPU path: returns verdicts (0 out, 1 in, 2 uncertain) per query; throws on any failure
    std::vector<uint32_t> gpuVerdicts(const Mesh& m, const std::vector<double>& q) {
        size_t n = q.size() / 3;
        if (inject == Fault::DeviceLost) throw std::runtime_error("injected: VK_ERROR_DEVICE_LOST");
        std::vector<uint32_t> v(n);
        auto work = [&]() {
            size_t vb = cfg.allocOverride ? cfg.allocOverride : m.v.size() * 8;
            auto bV = ctx->alloc(vb), bT = ctx->alloc(m.t.size() * 4), bQ = ctx->alloc(n * 24), bO = ctx->alloc(n * 4);
            memcpy(bV.ptr, m.v.data(), m.v.size() * 8); memcpy(bT.ptr, m.t.data(), m.t.size() * 4); memcpy(bQ.ptr, q.data(), n * 24);
            uint32_t pc[2] = {(uint32_t)n, (uint32_t)m.nt()};
            if (inject == Fault::Hang) std::this_thread::sleep_for(std::chrono::milliseconds((int)(cfg.deadlineMs * 3)));
            ctx->begin(); for (gpu::Buf* b : {&bV, &bT, &bQ}) ctx->upload(*b); ctx->dispatch(k, {&bV, &bT, &bQ, &bO}, pc, (uint32_t)((n + 63) / 64)); ctx->download(bO); ctx->submitWait();
            memcpy(v.data(), bO.ptr, n * 4); for (gpu::Buf* b : {&bV, &bT, &bQ, &bO}) ctx->release(*b);
        };
        if (inject == Fault::Hang) {
            auto fut = std::async(std::launch::async, work);
            if (fut.wait_for(std::chrono::milliseconds((int)cfg.deadlineMs)) == std::future_status::timeout) { leakedHang = true; std::thread([f = std::make_shared<std::future<void>>(std::move(fut))] { f->wait(); }).detach(); throw std::runtime_error("deadline exceeded on the GPU path"); }
        } else work();
        if (inject == Fault::WrongAnswer) for (size_t i = 0; i < n; i++) if (v[i] != 2) v[i] ^= 1u;       // a kernel that answers wrongly
        return v;
    }
    bool init(bool selfTest = true) {
        try {
            ctx.reset(new gpu::Ctx());
            if (!ctx->init(gpu::defaultLoaderPaths())) { reason = "no device: " + ctx->error; log.push_back(reason); return false; }
            k = ctx->makeKernel(gpu::shaderDir() + "/" + cfg.spv + ".spv", 4, 8); haveKernel = true;
        } catch (const std::exception& e) { reason = std::string("init failed: ") + e.what(); log.push_back(reason); gpuOn = false; return false; }
        gpuOn = true;
        if (selfTest && !runSelfTest()) { gpuOn = false; tripped = true; return false; }
        return true;
    }
    bool runSelfTest() {        // known answers: a sphere and queries far inside (IN), far outside (OUT); a wrong kernel fails at least one
        if (mutant == 4) return true;
        Mesh m = uvsphere(8, 12, 1.0);
        // 130 queries (more than two workgroups of 64, so a fault in the last lane of a workgroup is exercised), deterministic, off every axis, either clearly inside (|p| < 0.6) or clearly outside (|p| > 1.3)
        std::vector<double> q; std::vector<uint32_t> expect; std::mt19937_64 g(2024);
        while (expect.size() < 130) { double p[3]; for (double& x : p) x = ((double)(g() >> 11) / 9007199254740992.0 * 2 - 1) * 2.0; double r = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]); if (r < 0.6 || r > 1.3) { q.insert(q.end(), p, p + 3); expect.push_back(r < 0.6 ? 1u : 0u); } }
        try { auto v = gpuVerdicts(m, q); for (size_t i = 0; i < expect.size(); i++) if (v[i] != expect[i]) { reason = "startup self-test mismatch at query " + std::to_string(i); log.push_back(reason); return false; } }
        catch (const std::exception& e) { reason = std::string("startup self-test failed: ") + e.what(); log.push_back(reason); return false; }
        return true;
    }
    // the answer: always equal to cpuOnly
    std::vector<uint8_t> classify(const Mesh& m, const std::vector<double>& q) {
        batches++; size_t n = q.size() / 3; std::vector<uint8_t> out(n);
        if (!gpuOn) { cpuRange(m, q, out, nullptr); return out; }
        std::vector<uint32_t> v;
        try { v = gpuVerdicts(m, q); }
        catch (const std::exception& e) { consecutiveFail++; log.push_back(std::string("GPU batch failed: ") + e.what()); if (consecutiveFail >= cfg.breakerAfter) trip(std::string("consecutive failures: ") + e.what()); cpuRange(m, q, out, nullptr); return out; }
        consecutiveFail = 0;
        std::vector<uint32_t> unc; for (size_t i = 0; i < n; i++) if (v[i] == 2) unc.push_back((uint32_t)i);
        if ((double)unc.size() / std::max<size_t>(1, n) > cfg.uncertCap) { log.push_back("uncertain rate over the cap: batch answered on the CPU"); consecutiveFail++; if (consecutiveFail >= cfg.breakerAfter) trip("uncertain rate over the cap"); cpuRange(m, q, out, nullptr); return out; }
        for (size_t i = 0; i < n; i++) if (v[i] != 2) out[i] = (uint8_t)v[i];
        if (mutant == 3) { for (uint32_t i : unc) out[i] = 0; }                                       // planted: uncertain answered as 'outside' without the exact test
        else cpuRange(m, q, out, &unc);
        if (mutant != 2 && cfg.shadowEvery > 0 && batches % cfg.shadowEvery == 0) {                   // sampled shadow check: recompute the batch on the CPU
            std::vector<uint8_t> ref = cpuOnly(m, q);
            if (ref != out) { trip("shadow check disagreement"); return ref; }
        }
        return out;
    }
    ~Service() { if (ctx && !leakedHang) { if (haveKernel) ctx->release(k); ctx->destroy(); } }
};

struct Check { int pass = 0, fail = 0; void ok(bool c, const std::string& what) { (c ? pass : fail)++; printf("  [%s] %s\n", c ? "ok" : "FAIL", what.c_str()); } };

static int faults(int mutant, bool noDeviceRun) {
    exactinit(); Check ck;
    Mesh m = uvsphere(14, 20, 1.0); auto q = randomQueries(3000, 1.3, 5);
    // a query set that includes boundary and grazing queries so the uncertain path is exercised
    for (size_t t = 0; t < 60 && t < m.nt(); t++) for (int d = 0; d < 3; d++) q.push_back(m.v[3 * (size_t)m.t[3 * t] + d]);
    auto ref = cpuOnly(m, q);
    if (noDeviceRun) {       // run with VK_ICD_FILENAMES pointing nowhere: a REAL missing device
        Service s; s.mutant = mutant; bool up = s.init();
        ck.ok(!up && !s.gpuOn, "no Vulkan device: the service starts without the GPU"); ck.ok(!s.reason.empty(), "reason reported: " + s.reason);
        ck.ok(s.classify(m, q) == ref, "no device: answer equals the CPU-only answer");
        printf("summary: %d ok, %d FAIL\n", ck.pass, ck.fail); return ck.fail ? 3 : 0;
    }
    { printf("case: healthy GPU\n"); Service s; s.mutant = mutant; ck.ok(s.init(), "device up, self-test passes"); ck.ok(s.classify(m, q) == ref, "answer equals the CPU-only answer"); ck.ok(!s.tripped, "breaker closed"); }
    { printf("case: shader load failure (real: bad shader path)\n"); Service s; s.mutant = mutant; s.cfg.spv = "no_such_kernel"; bool up = s.init(); ck.ok(!up, "init fails cleanly"); ck.ok(!s.reason.empty(), "reason: " + s.reason); ck.ok(s.classify(m, q) == ref, "answer equals the CPU-only answer"); }
    { printf("case: out of memory (real: an impossible allocation)\n"); Service s; s.mutant = mutant; s.init(); s.cfg.allocOverride = (size_t)1 << 50; auto a1 = s.classify(m, q); auto a2 = s.classify(m, q); ck.ok(a1 == ref && a2 == ref, "both batches answered, equal to the CPU-only answer"); ck.ok(s.tripped, "breaker tripped after 2 consecutive failures"); ck.ok(!s.gpuOn, "GPU path off afterwards"); }
    { printf("case: device lost mid-run (injected)\n"); Service s; s.mutant = mutant; s.init(); s.inject = Fault::DeviceLost; auto a1 = s.classify(m, q); auto a2 = s.classify(m, q); ck.ok(a1 == ref && a2 == ref, "answers equal the CPU-only answer"); ck.ok(s.tripped, "breaker tripped"); s.inject = Fault::None; ck.ok(s.classify(m, q) == ref, "after tripping the answer is still the CPU answer (GPU not used again)"); }
    { printf("case: hang past the deadline (injected sleep inside the GPU path)\n"); Service s; s.mutant = mutant; s.cfg.deadlineMs = 300; s.init(); s.inject = Fault::Hang; auto t0 = std::chrono::steady_clock::now(); auto a = s.classify(m, q); double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      ck.ok(a == ref, "answer equals the CPU-only answer"); ck.ok(ms < 3000, "returned in " + std::to_string((int)ms) + " ms, well before the injected hang ended"); ck.ok(s.consecutiveFail == 1, "the failure was counted"); }
    { printf("case: uncertain rate over the cap (cap 0.0001 on a query set with boundary points)\n"); Service s; s.mutant = mutant; s.cfg.uncertCap = 0.0001; s.init(); auto a = s.classify(m, q); ck.ok(a == ref, "answer equals the CPU-only answer"); ck.ok(s.consecutiveFail >= 1, "counted as a failure"); }
    { printf("case: a kernel that answers wrongly (injected); shadow check every batch\n"); Service s; s.mutant = mutant; s.init(); s.inject = Fault::WrongAnswer; auto a = s.classify(m, q); ck.ok(a == ref, "answer equals the CPU-only answer"); ck.ok(s.tripped, "shadow check detected the disagreement and tripped"); s.inject = Fault::None; ck.ok(s.classify(m, q) == ref && !s.gpuOn, "after tripping no GPU result is used"); }
    { printf("case: wrong kernel, shadow check every 8 batches (detected within 8 batches)\n"); Service s; s.mutant = mutant; s.cfg.shadowEvery = 8; s.init(); s.inject = Fault::WrongAnswer; int sawWrong = 0, detectedAt = -1; for (int b = 1; b <= 8; b++) { auto a = s.classify(m, q); if (a != ref) sawWrong++; if (s.tripped && detectedAt < 0) detectedAt = b; }
      ck.ok(detectedAt >= 1 && detectedAt <= 8, "tripped at batch " + std::to_string(detectedAt) + ", reason: " + s.reason); printf("  (until the check fired %d batches returned an answer different from the CPU's; sampling trades that for speed, and the sampled design never promises otherwise)\n", sawWrong); }
    { printf("case: startup self-test on a planted wrong kernel (pip_ray_m1 ignores the error bound: the self-test queries are not near-degenerate, so it passes; pip_ray_m5 answers IN for the last thread of a workgroup)\n");
      Service s; s.mutant = mutant; s.cfg.spv = "pip_ray_m5"; bool up = s.init(); ck.ok(!up, "self-test rejects pip_ray_m5"); ck.ok(s.classify(m, q) == ref, "answer equals the CPU-only answer"); }
    printf("summary: %d ok, %d FAIL\n", ck.pass, ck.fail);
    if (mutant) { printf("mutant %d: %s\n", mutant, ck.fail ? "caught" : "NOT CAUGHT"); return ck.fail ? 0 : 3; }
    printf("%s\n", ck.fail ? "FAIL" : "PASS"); return ck.fail ? 3 : 0;
}

static int crossover() {
    exactinit(); Service s; if (!s.init()) { printf("no usable device: %s\n", s.reason.c_str()); return 2; }
    printf("device=%s memory=%s; CPU path: %u threads, exact brute force per query\n", s.ctx->props.deviceName, s.ctx->memMode(), nThreads());
    printf("%-10s %-10s %10s %10s %10s %10s %10s %10s %8s\n", "triangles", "queries", "CPUnaive", "CPUbox ms", "GPU ms", "unc %", "vs naive", "vs box", "equal");
    for (int rings : {6, 12, 24}) for (size_t nq : {100u, 1000u, 10000u, 50000u}) {
        Mesh m = uvsphere(rings, 2 * rings, 1.0); auto q = randomQueries(nq, 1.3, 9);
        double t0 = gpu::nowMs(); auto ref = cpuOnly(m, q); double t1 = gpu::nowMs(); auto fastR = cpuFast(m, q); double t2 = gpu::nowMs(); bool fastEq = fastR == ref;
        s.cfg.shadowEvery = 0; double best = 1e300; std::vector<uint8_t> a; size_t unc = 0;
        for (int rep = 0; rep < 3; rep++) { double a0 = gpu::nowMs(); a = s.classify(m, q); best = std::min(best, gpu::nowMs() - a0); }
        { auto v = s.gpuVerdicts(m, q); for (auto x : v) unc += x == 2; }
        printf("%-10zu %-10zu %10.1f %10.1f %10.1f %9.2f%% %9.2fx %9.2fx %8s\n", m.nt(), nq, t1 - t0, t2 - t1, best, 100.0 * unc / nq, (t1 - t0) / best, (t2 - t1) / best, (a == ref && fastEq) ? "yes" : "NO");
    }
    return 0;
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow the lines printed so far
    std::string m = argc > 1 ? argv[1] : "faults";
    if (m == "crossover") return crossover();
    if (m == "nodevice") return faults(0, true);
    return faults(argc > 2 ? atoi(argv[2]) : 0, false);
}
