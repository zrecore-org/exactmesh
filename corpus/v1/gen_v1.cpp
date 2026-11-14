// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V1 corpus (docs/PLAN.md, "V1: validator nesting and orientation"): meshes with several components, generated deterministically.
//   class legit   : valid hollow / multi-solid inputs; the validator must ACCEPT them (default mode); in strict mode (no cavities) only those with a cavity are refused.
//   class invalid : nesting / orientation / volume defects; the validator must REFUSE them with the expected V1 reason code (both modes).
// usage: gen_v1 <outdir>   writes <outdir>/cases/<id>.msh and <outdir>/manifest.tsv (id class family expect_default expect_strict cavities components).
// Only IEEE sqrt, +, -, *, / and exact scalings are used, so the files are identical on every platform (-ffp-contract=off).
#include "../../harness/exact.h"
#include "../../tests/common/testmeshes.h"
#include <cstdio>
#include <map>
#include <array>
#include <string>
#include <sys/stat.h>
using namespace mesh;
using tmesh::Vec;

static std::string g_out; static FILE* g_man; static size_t g_n[2];
static tmesh::Rng rng(0x56315F4E45535431ull);   // "V1_NEST1"

static Mesh append(Mesh a, const Mesh& b) {
    uint32_t o = (uint32_t)a.nv();
    for (size_t i = 0; i < b.nv(); i++) a.addV(b.v[3*i], b.v[3*i+1], b.v[3*i+2]);
    for (size_t k = 0; k < b.nt(); k++) a.addT(b.t[3*k] + o, b.t[3*k+1] + o, b.t[3*k+2] + o);
    return a;
}
static Mesh inverted(Mesh m) { for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3*k+1], m.t[3*k+2]); return m; }
static Mesh outward(Mesh m) { return signExact(exactVolumeTimes6(m)) < 0 ? inverted(m) : m; }

static Mesh icosphere(Vec c, double r, int level) {
    Mesh m; const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    double P[12][3] = {{-1,phi,0},{1,phi,0},{-1,-phi,0},{1,-phi,0},{0,-1,phi},{0,1,phi},{0,-1,-phi},{0,1,-phi},{phi,0,-1},{phi,0,1},{-phi,0,-1},{-phi,0,1}};
    static const uint32_t F[20][3] = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    std::vector<std::array<double,3>> V; for (auto& p : P) V.push_back({p[0], p[1], p[2]});
    std::vector<std::array<uint32_t,3>> T; for (auto& f : F) T.push_back({f[0], f[1], f[2]});
    for (int l = 0; l < level; l++) {
        std::map<std::pair<uint32_t,uint32_t>, uint32_t> mid; std::vector<std::array<uint32_t,3>> N;
        auto get = [&](uint32_t a, uint32_t b) { auto k = std::make_pair(std::min(a,b), std::max(a,b)); auto it = mid.find(k); if (it != mid.end()) return it->second;
            V.push_back({(V[a][0]+V[b][0])/2, (V[a][1]+V[b][1])/2, (V[a][2]+V[b][2])/2}); return mid[k] = (uint32_t)V.size() - 1; };
        for (auto& t : T) { uint32_t a = get(t[0],t[1]), b = get(t[1],t[2]), c2 = get(t[2],t[0]); N.push_back({t[0],a,c2}); N.push_back({t[1],b,a}); N.push_back({t[2],c2,b}); N.push_back({a,b,c2}); }
        T = N;
    }
    for (auto& p : V) { double n = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]); m.addV(c[0] + r * (p[0]/n), c[1] + r * (p[1]/n), c[2] + r * (p[2]/n)); }
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return outward(m);
}
// coplanar "tetrahedron": four points in the plane z = z0, p3 strictly inside the triangle p0 p1 p2; closed, manifold, non-degenerate triangles, exactly zero volume
static Mesh flatTetra(Vec o, double s) {
    Mesh m; m.addV(o[0], o[1], o[2]); m.addV(o[0] + 4*s, o[1], o[2]); m.addV(o[0], o[1] + 4*s, o[2]); m.addV(o[0] + s, o[1] + s, o[2]);
    m.addT(0, 2, 1); m.addT(0, 1, 3); m.addT(1, 2, 3); m.addT(0, 3, 2);
    return m;
}
struct Frame { double k; double ox; };   // power-of-two scale k and x offset ox: coordinates are (integer grid) * k + offset (exact for the sizes used)
static Frame pickFrame(bool extreme) {
    static const double ks[] = {1, 8, 1.0/16, 1024, 1.0/1024}; static const double os[] = {0, 1048576.0, 1099511627776.0};
    if (!extreme) return {ks[rng.pick(2)], 0};
    return {ks[rng.pick(5)], os[rng.pick(3)]};
}
static Frame sphFrame(bool extreme) { Frame f = pickFrame(extreme); if (f.k < 1) f.k = 1; return f; }   // spheres are rounded when translated: keep them well above the ulp of the offset
static Mesh boxF(const Frame& f, double x0, double y0, double z0, double x1, double y1, double z1) { return tmesh::box({x0*f.k + f.ox, y0*f.k, z0*f.k}, {x1*f.k + f.ox, y1*f.k, z1*f.k}); }

static void emit(const std::string& cls, const char* fam, const Mesh& m, const std::string& ed, const std::string& es, int cav, int comps) {
    char id[64]; snprintf(id, sizeof id, "%s_%s_%03zu", cls.c_str(), fam, g_n[cls == "invalid"]++);
    writeMesh(g_out + "/cases/" + id + ".msh", m);
    fprintf(g_man, "%s\t%s\t%s\t%s\t%s\t%d\t%d\n", id, cls.c_str(), fam, ed.c_str(), es.c_str(), cav, comps);
}
static void legit(const char* fam, const Mesh& m, int cav, int comps) { emit("legit", fam, m, "ACCEPT", cav ? "V1-CAVITY-NOT-ALLOWED" : "ACCEPT", cav, comps); }
static void bad(const char* fam, const Mesh& m, const char* code, int comps) { emit("invalid", fam, m, code, code, 0, comps); }
static int ri(int lo, int hi) { return lo + rng.pick(hi - lo + 1); }   // inclusive

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: gen_v1 <outdir>\n"); return 2; }
    g_out = argv[1]; mkdir(g_out.c_str(), 0777); mkdir((g_out + "/cases").c_str(), 0777);
    g_man = fopen((g_out + "/manifest.tsv").c_str(), "w");
    fprintf(g_man, "id\tclass\tfamily\texpect_default\texpect_strict\tcavities\tcomponents\n");
    const double TH = std::ldexp(1.0, -20);
    // ---------------- legit ----------------
    for (int i = 0; i < 12; i++) { Frame f = pickFrame(i >= 4); int L = ri(8, 16), a = ri(1, 3), b = ri(a + 1, L - 1);
        legit("box_cavity", append(boxF(f, 0,0,0, L,L,L), inverted(boxF(f, a,a,a, b,b,b))), 1, 2); }
    for (int i = 0; i < 8; i++) { Frame f = sphFrame(i >= 4); Vec c{f.ox, 0, 0}; double R = 8 * f.k; int lv = 1 + (i & 1);
        legit("sphere_cavity", append(icosphere(c, R, lv), inverted(icosphere(c, R * 0.5, lv))), 1, 2); }
    for (int i = 0; i < 10; i++) { Frame f = pickFrame(i >= 4); legit("cavity_island", append(append(boxF(f, 0,0,0, 30,30,30), inverted(boxF(f, 4,4,4, 26,26,26))), boxF(f, 10,10,10, 14,14,14 + i % 3)), 1, 3); }
    for (int i = 0; i < 4; i++) { Frame f = sphFrame(true); Vec c{f.ox, 0, 0}; double R = 16 * f.k;
        legit("sphere_cavity_island", append(append(icosphere(c, R, 1), inverted(icosphere(c, R * 0.7, 1))), icosphere(c, R * 0.3, 1 + (i & 1))), 1, 3); }
    for (int i = 0; i < 4; i++) { Frame f = pickFrame(i >= 2);   // + - + - : a cavity inside the island inside the cavity
        legit("four_levels", append(append(append(boxF(f, 0,0,0, 40,40,40), inverted(boxF(f, 2,2,2, 38,38,38))), boxF(f, 6,6,6, 34,34,34)), inverted(boxF(f, 10,10,10, 20 + i,20,20))), 2, 4); }
    for (int i = 0; i < 6; i++) { Frame f = pickFrame(i >= 3);
        legit("two_cavities", append(append(boxF(f, 0,0,0, 30,10,10), inverted(boxF(f, 2,2,2, 8,8,8))), inverted(boxF(f, 12,2,2, 18+i,8,8))), 2, 3); }
    for (int i = 0; i < 6; i++) { Frame f = pickFrame(i >= 3);
        legit("disjoint_solids", append(boxF(f, 0,0,0, 4,4,4), boxF(f, 6+i,0,0, 10+i,4,4)), 0, 2); }
    for (int i = 0; i < 4; i++) { double L = 4, u = std::nextafter(L, 0.0), l = std::nextafter(0.0, 1.0);   // one-ulp gaps
        Mesh outer = tmesh::box({0,0,0}, {L,L,L});
        Mesh in = i % 2 ? tmesh::box({1, 1, 1}, {u, u, u}) : tmesh::box({l, 1, 1}, {2, 2, 2});
        legit("one_ulp_cavity", append(outer, inverted(in)), 1, 2); }
    for (int i = 0; i < 6; i++) { const double o = std::ldexp(1.0, 40);   // thin cavity far from the origin: the sign of its volume is below the double rounding error
        Mesh outer = tmesh::box({o, 0, 0}, {o + 8, 8, 8}), in = tmesh::box({o + 1, 1.0 + i, 1}, {o + 3, 3.0 + i, 1 + TH});
        legit("thin_far_cavity", append(outer, inverted(in)), 1, 2); }
    for (int i = 0; i < 4; i++) { Vec c{0,0,0}; double R = 8;   // a positive box in the bounding-box corner of a sphere but outside it
        Mesh s = icosphere(c, R, 2); legit("box_in_sphere_corner", append(s, tmesh::box({6, 6, 6 + i*0.25}, {7.5, 7.5, 7.5 + i*0.25})), 0, 2); }
    // ---------------- invalid ----------------
    for (int i = 0; i < 30; i++) { Frame f = pickFrame(i >= 8); int L = ri(8, 16), a = ri(1, 3), b = ri(a + 1, L - 1);
        bad("same_box_in_box", append(boxF(f, 0,0,0, L,L,L), boxF(f, a,a,a, b,b,b)), "V1-NESTED-SAME-ORIENTATION", 2); }
    for (int i = 0; i < 15; i++) { Frame f = sphFrame(i >= 5); Vec c{f.ox, 0, 0}; double R = 8 * f.k; int lv = 1 + (i & 1);
        bad("same_sphere_in_sphere", append(icosphere(c, R, lv), icosphere(c, R * 0.5, lv)), "V1-NESTED-SAME-ORIENTATION", 2); }
    for (int i = 0; i < 30; i++) { Frame f = pickFrame(i >= 8); int L = ri(8, 16);
        bad("negative_beside_box", append(boxF(f, 0,0,0, L,L,L), inverted(boxF(f, L + 2 + i % 5, 0,0, L + 5 + i % 5, 2,2))), "V1-NEGATIVE-OUTSIDE", 2); }
    for (int i = 0; i < 10; i++) { Frame f = sphFrame(i >= 3); Vec c{f.ox, 0, 0}; double R = 8 * f.k;
        bad("negative_beside_sphere", append(icosphere(c, R, 1 + (i & 1)), inverted(icosphere({f.ox + 20 * f.k, 0, 0}, R * 0.25, 1))), "V1-NEGATIVE-OUTSIDE", 2); }
    for (int i = 0; i < 20; i++) { Frame f = pickFrame(i >= 6);   // + - - : a cavity inside a cavity
        bad("cavity_in_cavity", append(append(boxF(f, 0,0,0, 30,30,30), inverted(boxF(f, 4,4,4, 26,26,26))), inverted(boxF(f, 8,8,8+i%3, 14,14,14+i%3))), "V1-NESTED-SAME-ORIENTATION", 3); }
    for (int i = 0; i < 12; i++) { Frame f = pickFrame(i >= 4);   // + - + + : a solid inside the island inside the cavity
        bad("solid_in_island", append(append(append(boxF(f, 0,0,0, 40,40,40), inverted(boxF(f, 2,2,2, 38,38,38))), boxF(f, 6,6,6, 34,34,34)), boxF(f, 10,10,10, 20+i%4,20,20)), "V1-NESTED-SAME-ORIENTATION", 4); }
    for (int i = 0; i < 10; i++) { Frame f = pickFrame(i >= 3);   // two positive boxes and two negative boxes, all outside each other
        bad("two_negatives_outside", append(append(append(boxF(f, 0,0,0, 20,20,20), boxF(f, 30,0,0, 50,20,20)), inverted(boxF(f, 60,0,0, 62,2,2))), inverted(boxF(f, 70,0,0, 73+i%3,2,2))), "V1-NEGATIVE-OUTSIDE", 4); }
    for (int i = 0; i < 15; i++) { Frame f = pickFrame(i >= 5); Vec o{f.ox + 40 * f.k, 0, 0};   // zero volume component beside a box
        bad("zero_volume_beside", append(boxF(f, 0,0,0, 20,20,20), flatTetra(o, f.k * (1 + i % 3))), "V1-ZERO-VOLUME-COMPONENT", 2); }
    for (int i = 0; i < 10; i++) { Frame f = pickFrame(i >= 3); Vec o{f.ox + 4 * f.k, 4 * f.k, 4 * f.k};   // zero volume component inside a box
        bad("zero_volume_inside", append(boxF(f, 0,0,0, 20,20,20), flatTetra(o, f.k * (1 + i % 3))), "V1-ZERO-VOLUME-COMPONENT", 2); }
    for (int i = 0; i < 20; i++) { const double o = std::ldexp(1.0, 40);   // thin negative component far from the origin, next to a big box: double volume gets its sign wrong
        Mesh big = tmesh::box({o, 0, 0}, {o + 8, 8, 8});
        bad("thin_far_negative_beside", append(big, inverted(tmesh::box({o + 10, 1.0 + i % 4, 1}, {o + 12, 3.0 + i % 4, 1 + TH}))), "V1-NEGATIVE-OUTSIDE", 2); }
    for (int i = 0; i < 20; i++) { const double o = std::ldexp(1.0, 40);   // thin positive component far from the origin, inside a big box (same orientation)
        Mesh big = tmesh::box({o, 0, 0}, {o + 8, 8, 8});
        bad("thin_far_same_inside", append(big, tmesh::box({o + 1, 1.0 + i % 4, 1}, {o + 3, 3.0 + i % 4, 1 + TH})), "V1-NESTED-SAME-ORIENTATION", 2); }
    for (int i = 0; i < 15; i++) { Vec c{0, 0, 0}; double R = 8;   // negative box in the bounding-box corner of a sphere, outside the sphere (bbox prefilter passes, point-in-solid must reject)
        Mesh s = icosphere(c, R, 2); bad("negative_in_sphere_corner", append(s, inverted(tmesh::box({6, 6, 6 + i*0.25}, {7.5, 7.5, 7.5 + i*0.25}))), "V1-NEGATIVE-OUTSIDE", 2); }
    fclose(g_man);
    printf("gen_v1: %zu legit, %zu invalid cases written to %s\n", g_n[0], g_n[1], g_out.c_str());
    return 0;
}
