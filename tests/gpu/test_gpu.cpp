// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// GPU tests (tag [gpu]): the exact-sign orient3d filter (soft binary64 kernel) and the deterministic LBVH with pair emission, run on the Vulkan device that is configured
// (a real GPU, or lavapipe in CI), each against its CPU reference inside the host program. Run from the repository root (the programs read build/shaders/*.spv).
// The planted-mutant test is tagged [gpu-mutant], not [gpu]: a mutant of the tree build can make a kernel never finish, which a CPU Vulkan driver may not survive cleanly, so it is run by hand or on real GPUs.
// The host programs exit 0 on success, 2 or 3 on a mismatch, and print "no GPU: ..." with status 2 when no Vulkan device can be opened (reported here as a skip).
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <string>

namespace {
struct Run { int code = -1; std::string out; };
Run run(const std::string& cmd) {
    Run r;
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
    REQUIRE(p != nullptr);
    std::array<char, 4096> buf;
    while (size_t n = fread(buf.data(), 1, buf.size(), p)) r.out.append(buf.data(), n);
    int st = pclose(p);
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}
std::string bin(const char* name) { return std::string(EXACTMESH_BIN_DIR) + "/" + name; }
void skipIfNoDevice(const Run& r) { if (r.code == 2 && r.out.find("no GPU:") != std::string::npos) SKIP("no Vulkan device available: " << r.out.substr(0, 200)); }
}  // namespace

TEST_CASE("the soft binary64 orient3d filter on the device agrees with the CPU reference", "[gpu][orient]") {
    Run r = run("GPU_POC_CORRECTNESS_ONLY=1 " + bin("mp09_orient_filter") + " device 400000");
    skipIfNoDevice(r);
    INFO(r.out);
    CHECK(r.code == 0);
    CHECK(r.out.find("PASS") != std::string::npos);
}

TEST_CASE("the LBVH with pair emission on the device equals the CPU reference, byte for byte and run after run", "[gpu][lbvh]") {
    Run r = run("GPU_POC_CORRECTNESS_ONLY=1 " + bin("mp10_lbvh") + " device 0.1");
    skipIfNoDevice(r);
    INFO(r.out);
    CHECK(r.code == 0);
    CHECK(r.out.find("determinism: 20 repeats byte-identical") != std::string::npos);
    CHECK(r.out.find("PASS") != std::string::npos);
}

TEST_CASE("planted mutants of the LBVH kernels are caught", "[gpu-mutant]") {
    for (int m = 1; m <= 3; m++) {
        Run r = run("GPU_POC_CORRECTNESS_ONLY=1 MP10_MUT=" + std::to_string(m) + " " + bin("mp10_lbvh") + " device 0.1");
        skipIfNoDevice(r);
        INFO("mutant " << m << ": " << r.out);
        // caught = the program does not report PASS: it prints FAIL and exits 3 (a result that differs from the reference), or it dies on a device error
        // (the mutant of the tree build can make a kernel never finish, which the host reports as a fence timeout)
        CHECK(r.code != 0);
        CHECK(r.out.find("PASS") == std::string::npos);
    }
}
