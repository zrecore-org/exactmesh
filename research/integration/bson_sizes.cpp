// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP14-A: how large is a triangle mesh as a BSON document, in three representations, and does it round-trip bit-exactly?
// A minimal BSON writer and reader written from the public BSON specification (bsonspec.org); no MongoDB code or headers are used.
//   bson_sizes [mutant 1..3]      mutant 1 drops the last index, 2 swaps two coordinates of the last vertex, 3 writes a wrong length prefix
// Representations: R1 arrays of arrays (vertices [[x,y,z],...], faces [[a,b,c],...]); R2 flat arrays of doubles and int32; R3 BinData with packed little-endian binary64 / uint32.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
struct Mesh { std::vector<double> v; std::vector<uint32_t> t; size_t nt() const { return t.size() / 3; } };
static const size_t kMax = 16u * 1024u * 1024u;   // BSONObjMaxUserSize, src/mongo/bson/util/builder.h:51 of the fork

struct W {
    std::vector<uint8_t> b; int mutant = 0;
    void raw(const void* p, size_t n) { const uint8_t* c = (const uint8_t*)p; b.insert(b.end(), c, c + n); }
    void i32(int32_t x) { raw(&x, 4); }
    void cstr(const std::string& s) { raw(s.c_str(), s.size() + 1); }
    size_t begin() { size_t at = b.size(); i32(0); return at; }
    void end(size_t at) { b.push_back(0); int32_t n = (int32_t)(b.size() - at); if (mutant == 3 && at == 0) n += 1; memcpy(&b[at], &n, 4); }
    void elemDouble(const std::string& k, double d) { if (!std::isfinite(d)) throw std::runtime_error("refused: non-finite coordinate"); b.push_back(0x01); cstr(k); raw(&d, 8); }
    void elemInt(const std::string& k, int32_t x) { b.push_back(0x10); cstr(k); i32(x); }
    void elemBin(const std::string& k, const void* p, size_t n) { b.push_back(0x05); cstr(k); i32((int32_t)n); b.push_back(0x00); raw(p, n); }
    size_t elemDoc(const std::string& k, uint8_t type) { b.push_back(type); cstr(k); return begin(); }
};
static std::vector<double> verts(const Mesh& m) { std::vector<double> v = m.v; return v; }
static std::vector<uint32_t> idx(const Mesh& m, int mutant) { std::vector<uint32_t> t = m.t; if (mutant == 1 && !t.empty()) t.pop_back(); return t; }
static std::vector<uint8_t> encode(const Mesh& m, int rep, int mutant) {
    W w; w.mutant = mutant; size_t root = w.begin(); auto v = verts(m); auto t = idx(m, mutant);
    if (mutant == 2 && v.size() >= 3) std::swap(v[v.size() - 3], v[v.size() - 2]);
    if (rep == 1) {
        size_t a = w.elemDoc("vertices", 0x04); for (size_t i = 0; i < v.size() / 3; i++) { size_t e = w.elemDoc(std::to_string(i), 0x04); for (int d = 0; d < 3; d++) w.elemDouble(std::to_string(d), v[3 * i + d]); w.end(e); } w.end(a);
        size_t f = w.elemDoc("faces", 0x04); for (size_t i = 0; i < t.size() / 3; i++) { size_t e = w.elemDoc(std::to_string(i), 0x04); for (int d = 0; d < 3; d++) w.elemInt(std::to_string(d), (int32_t)t[3 * i + d]); w.end(e); } w.end(f);
    } else if (rep == 2) {
        size_t a = w.elemDoc("vertices", 0x04); for (size_t i = 0; i < v.size(); i++) w.elemDouble(std::to_string(i), v[i]); w.end(a);
        size_t f = w.elemDoc("faces", 0x04); for (size_t i = 0; i < t.size(); i++) w.elemInt(std::to_string(i), (int32_t)t[i]); w.end(f);
    } else {
        for (double d : v) if (!std::isfinite(d)) throw std::runtime_error("refused: non-finite coordinate");
        w.elemBin("vertices", v.data(), v.size() * 8); w.elemBin("faces", t.data(), t.size() * 4);
    }
    w.end(root); return w.b;
}
// reader: returns false on any structural error
struct R {
    const uint8_t* p; size_t n, at = 0; bool bad = false;
    int32_t i32() { if (at + 4 > n) { bad = true; return 0; } int32_t x; memcpy(&x, p + at, 4); at += 4; return x; }
    std::string cstr() { std::string s; while (at < n && p[at]) s.push_back((char)p[at++]); if (at >= n) bad = true; at++; return s; }
};
static bool decode(const std::vector<uint8_t>& buf, int rep, Mesh& out) {
    R r{buf.data(), buf.size()}; int32_t total = r.i32(); if (total != (int32_t)buf.size() || r.bad) return false;
    out = Mesh();
    auto readArr = [&](bool vertsArr, bool nested) -> bool {
        size_t start = r.at; int32_t len = r.i32(); size_t endAt = start + (size_t)len; if (r.bad || endAt > buf.size()) return false;
        while (r.at < endAt - 1) {
            uint8_t ty = buf[r.at++]; r.cstr();
            if (nested) { if (ty != 0x04) return false; size_t s2 = r.at; int32_t l2 = r.i32(); size_t e2 = s2 + (size_t)l2; while (r.at < e2 - 1) { uint8_t t2 = buf[r.at++]; r.cstr(); if (vertsArr && t2 == 0x01) { double d; memcpy(&d, &buf[r.at], 8); r.at += 8; out.v.push_back(d); } else if (!vertsArr && t2 == 0x10) { out.t.push_back((uint32_t)r.i32()); } else return false; } r.at++; }
            else { if (vertsArr && ty == 0x01) { double d; memcpy(&d, &buf[r.at], 8); r.at += 8; out.v.push_back(d); } else if (!vertsArr && ty == 0x10) out.t.push_back((uint32_t)r.i32()); else return false; }
            if (r.bad) return false;
        }
        r.at++; return true;
    };
    while (r.at < buf.size() - 1) {
        uint8_t ty = buf[r.at++]; std::string k = r.cstr();
        if (rep == 3) { if (ty != 0x05) return false; int32_t len = r.i32(); r.at++; if (r.at + (size_t)len > buf.size()) return false; if (k == "vertices") { out.v.resize(len / 8); memcpy(out.v.data(), &buf[r.at], len); } else { out.t.resize(len / 4); memcpy(out.t.data(), &buf[r.at], len); } r.at += len; }
        else { if (ty != 0x04) return false; if (!readArr(k == "vertices", rep == 1)) return false; }
    }
    return !r.bad;
}
static bool sameBits(const Mesh& a, const Mesh& b) { return a.v.size() == b.v.size() && a.t == b.t && (a.v.empty() || memcmp(a.v.data(), b.v.data(), a.v.size() * 8) == 0); }

static Mesh sphere(int rings, int segs, double scale = 1.0, double off = 0.0) {   // closed, Euler characteristic 2
    Mesh m; const double PI = 3.14159265358979323846;
    auto addV = [&](double x, double y, double z) { m.v.push_back(x * scale + off); m.v.push_back(y * scale + off); m.v.push_back(z * scale + off); return (uint32_t)(m.v.size() / 3 - 1); };
    uint32_t top = addV(0, 0, 1);
    for (int i = 1; i < rings; i++) for (int j = 0; j < segs; j++) { double th = PI * i / rings, ph = 2 * PI * j / segs; addV(std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th)); }
    uint32_t bot = addV(0, 0, -1); auto V = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segs + (j % segs)); };
    auto T = [&](uint32_t a, uint32_t b, uint32_t c) { m.t.push_back(a); m.t.push_back(b); m.t.push_back(c); };
    for (int j = 0; j < segs; j++) T(top, V(1, j), V(1, j + 1));
    for (int i = 1; i + 1 < rings; i++) for (int j = 0; j < segs; j++) { T(V(i, j), V(i + 1, j), V(i + 1, j + 1)); T(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    for (int j = 0; j < segs; j++) T(bot, V(rings - 1, j + 1), V(rings - 1, j));
    return m;
}
static Mesh sphereWithTriangles(size_t target) { int segs = std::max(3, (int)std::lround(std::sqrt((double)target))); int rings = std::max(2, (int)std::lround((double)target / (2.0 * segs) + 1)); return sphere(rings, segs); }

int main(int argc, char** argv) {
    int mutant = argc > 1 ? atoi(argv[1]) : 0; int failures = 0, refusedOk = 0, refusedBad = 0;
    const char* names[4] = {"", "R1 arrays of arrays", "R2 flat arrays", "R3 BinData packed"};
    // (a) round trip on special meshes
    std::vector<std::pair<std::string, Mesh>> specials;
    specials.push_back({"sphere 12x16", sphere(12, 16)}); specials.push_back({"sphere at +2^50", sphere(12, 16, 1.0, std::ldexp(1.0, 50))}); specials.push_back({"sphere scaled 2^-30", sphere(12, 16, std::ldexp(1.0, -30))});
    { Mesh m = sphere(6, 8); m.v[0] = -0.0; m.v[1] = 5e-324; m.v[2] = std::ldexp(1.0, -1022); m.v[3] = 1.7976931348623157e308; specials.push_back({"-0.0, subnormal, smallest normal, DBL_MAX", m}); }
    { Mesh m = sphere(6, 8); for (size_t i = 0; i < m.v.size(); i++) m.v[i] += 1e-3 * (double)((i * 2654435761u) % 1000) / 1000.0; specials.push_back({"sphere with distinct full-mantissa coordinates (last vertex has x, y, z all different)", m}); }   // added after mutant 2 escaped the first set: the last vertex of a sphere is (0, 0, -1), so swapping two of its coordinates changed nothing
    for (int rep = 1; rep <= 3; rep++) for (auto& s : specials) {
        try { auto buf = encode(s.second, rep, mutant); Mesh back; bool ok = decode(buf, rep, back) && sameBits(s.second, back); if (!ok) { failures++; printf("  round trip FAILS: %s, %s\n", names[rep], s.first.c_str()); } }
        catch (const std::exception& e) { failures++; printf("  unexpected refusal: %s, %s: %s\n", names[rep], s.first.c_str(), e.what()); }
    }
    for (double bad : {NAN, INFINITY, -INFINITY}) for (int rep = 1; rep <= 3; rep++) { Mesh m = sphere(6, 8); m.v[7] = bad; try { encode(m, rep, mutant); refusedBad++; } catch (const std::exception&) { refusedOk++; } }
    printf("round-trip failures %d; non-finite coordinates refused %d of 9 (accepted: %d)\n", failures, refusedOk, refusedBad); if (refusedBad) failures++;
    // (b) measured sizes
    if (!mutant) {
        printf("\n%-22s %10s %12s %14s %12s\n", "representation", "triangles", "bytes", "bytes/triangle", "");
        for (int rep = 1; rep <= 3; rep++) for (size_t tgt : {1000u, 10000u, 100000u}) { Mesh m = sphereWithTriangles(tgt); auto buf = encode(m, rep, 0); printf("%-22s %10zu %12zu %14.2f\n", names[rep], m.nt(), buf.size(), (double)buf.size() / m.nt()); }
        printf("\nlargest closed sphere mesh (Euler characteristic 2) whose encoding fits 16 MiB = %zu bytes, by bisection on the real encoder:\n", kMax);
        for (int rep = 1; rep <= 3; rep++) { size_t lo = 100, hi = 4000000; while (hi - lo > 200) { size_t mid = (lo + hi) / 2; if (encode(sphereWithTriangles(mid), rep, 0).size() <= kMax) lo = mid; else hi = mid; } Mesh m = sphereWithTriangles(lo); printf("  %-22s %zu triangles (%zu bytes)\n", names[rep], m.nt(), encode(m, rep, 0).size()); }
    }
    if (mutant) { printf("mutant %d: %s\n", mutant, failures ? "caught" : "NOT CAUGHT"); return failures ? 0 : 3; }
    printf("%s\n", failures ? "FAIL" : "PASS"); return failures ? 3 : 0;
}
