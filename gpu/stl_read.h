// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// STL reader for MP12e: binary or ASCII STL, float32 widened exactly to binary64, vertices welded by exactly equal coordinates (-0.0 folded to +0.0).
// Same behaviour as research/realmesh/stl_to_msh.cpp (the MP26 tool); kept as a header so the GPU programs can load models directly.
#pragma once
#include "../harness/mesh_io.h"
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
namespace stlr {
struct Key { double x, y, z; bool operator==(const Key& o) const { return memcmp(this, &o, sizeof *this) == 0; } };
struct KH { size_t operator()(const Key& k) const { uint64_t b[3]; memcpy(b, &k, 24); uint64_t h = 1469598103934665603ull; for (auto v : b) { h ^= v; h *= 1099511628211ull; } return h; } };
// Returns false if unreadable or empty. degen = triangles with a repeated vertex index after welding (kept in the mesh).
inline bool readStl(const std::string& path, mesh::Mesh& m, size_t* degen) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::array<double, 3>> tri;
    bool bin = false;
    if (data.size() >= 84) { uint32_t n; memcpy(&n, data.data() + 80, 4); if (84 + (size_t)n * 50 == data.size()) { bin = true;
        for (uint32_t i = 0; i < n; i++) for (int v = 0; v < 3; v++) { float c[3]; memcpy(c, data.data() + 84 + (size_t)i * 50 + 12 + v * 12, 12); tri.push_back({c[0], c[1], c[2]}); } } }
    if (!bin) {
        std::istringstream ss(data); std::string line;
        while (std::getline(ss, line)) { std::istringstream ls(line); std::string w; ls >> w; if (w == "vertex") { float x, y, z; ls >> x >> y >> z; tri.push_back({x, y, z}); } }
    }
    std::unordered_map<Key, uint32_t, KH> w;
    auto add = [&](const std::array<double, 3>& p) { Key k{p[0] + 0.0, p[1] + 0.0, p[2] + 0.0}; auto it = w.find(k); if (it == w.end()) it = w.emplace(k, m.addV(k.x, k.y, k.z)).first; return it->second; };
    for (size_t i = 0; i + 2 < tri.size(); i += 3) { uint32_t a = add(tri[i]), b = add(tri[i + 1]), c = add(tri[i + 2]); m.addT(a, b, c); }
    size_t d = 0; for (size_t k = 0; k < m.nt(); k++) if (m.t[3*k] == m.t[3*k+1] || m.t[3*k+1] == m.t[3*k+2] || m.t[3*k] == m.t[3*k+2]) d++;
    if (degen) *degen = d;
    return m.nt() > 0;
}
}
