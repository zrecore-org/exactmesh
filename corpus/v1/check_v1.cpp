// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// V1 checker (docs/PLAN.md, "V1: validator nesting and orientation").
//   check_v1 set <setdir> [--list]      validates every case of a gen_v1 set in default and strict mode and compares with manifest.tsv. Exit 0 iff 0 false refusals, 0 wrong accepts,
//                                       0 wrong reasons. --list prints one line per case (id, default verdict, strict verdict, cavities, components) for hashing.
//   check_v1 compare <dir>...           runs the frozen mp21b::validate and v1::validate on every .msh below the directories (recursively); prints per directory the counts of
//                                       accepted / refused for both, the files whose verdict changed, and the validation time of both. Exit 0 iff nothing changed.
// Built with -DMUT_V1_NOSIGN / -DMUT_V1_NONEST / -DMUT_V1_DOUBLEVOL it is a planted-mutant run: the `set` mode must then exit non-zero.
#include "../../lib/geogram/v1/validator_v1.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
using namespace mesh; namespace fs = std::filesystem;

static std::string code(const std::string& r) { if (r.empty()) return "ACCEPT"; size_t i = r.find(':'); return r.rfind("V1-", 0) == 0 && i != std::string::npos ? r.substr(0, i) : "OLD:" + r; }
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static int setMode(const std::string& dir, bool list) {
    std::ifstream f(dir + "/manifest.tsv"); std::string line; std::getline(f, line);
    size_t legit = 0, invalid = 0, falseRefusal = 0, wrongAccept = 0, wrongReason = 0, strictBad = 0; int cavSum = 0;
    std::map<std::string, std::pair<size_t, size_t>> perFamily;   // family -> (cases, mismatches)
    while (std::getline(f, line)) {
        std::stringstream ss(line); std::string id, cls, fam, ed, es, cav, comps; std::getline(ss, id, '\t'); std::getline(ss, cls, '\t'); std::getline(ss, fam, '\t'); std::getline(ss, ed, '\t'); std::getline(ss, es, '\t'); std::getline(ss, cav, '\t'); std::getline(ss, comps, '\t');
        Mesh m; if (!readMesh(dir + "/cases/" + id + ".msh", m)) { fprintf(stderr, "cannot read %s\n", id.c_str()); return 2; }
        v1::Report rep; std::string d = code(v1::validate(m, v1::Options{true}, &rep)), s = code(v1::validate(m, v1::Options{false}));
        bool isLegit = cls == "legit"; (isLegit ? legit : invalid)++;
        bool bad = false;
        if (d != ed) { bad = true; if (isLegit) falseRefusal++; else if (d == "ACCEPT") wrongAccept++; else wrongReason++; }
        if (s != es) { bad = true; strictBad++; }
        if (isLegit && d == "ACCEPT" && (rep.cavities != atoi(cav.c_str()) || rep.components != atoi(comps.c_str()))) { bad = true; wrongReason++; }
        cavSum += rep.cavities;
        auto& pf = perFamily[cls + "/" + fam]; pf.first++; pf.second += bad;
        if (bad) fprintf(stderr, "MISMATCH %s: default %s (expected %s), strict %s (expected %s), cavities %d (expected %s), components %d (expected %s)\n", id.c_str(), d.c_str(), ed.c_str(), s.c_str(), es.c_str(), rep.cavities, cav.c_str(), rep.components, comps.c_str());
        if (list) printf("%s\t%s\t%s\t%d\t%d\n", id.c_str(), d.c_str(), s.c_str(), rep.cavities, rep.components);
    }
    for (auto& p : perFamily) fprintf(stderr, "  %-38s %3zu cases, %zu mismatches\n", p.first.c_str(), p.second.first, p.second.second);
    fprintf(stderr, "check_v1 set: legit %zu, invalid %zu | false refusals %zu, wrong accepts %zu, wrong reason codes (or cavity/component counts) %zu, strict-mode mismatches %zu\n", legit, invalid, falseRefusal, wrongAccept, wrongReason, strictBad);
    return (falseRefusal || wrongAccept || wrongReason || strictBad) ? 1 : 0;
}

static int compareMode(const std::vector<std::string>& dirs) {
    int changed = 0;
    for (auto& d : dirs) {
        size_t files = 0, oldAcc = 0, oldRef = 0, newAcc = 0, newRef = 0, multi = 0; double tOld = 0, tNew = 0; std::map<std::string, size_t> newReasons;
        std::vector<fs::path> ps; for (auto& e : fs::recursive_directory_iterator(d)) if (e.is_regular_file() && e.path().extension() == ".msh") ps.push_back(e.path());
        std::sort(ps.begin(), ps.end());
        for (auto& p : ps) {
            Mesh m; if (!readMesh(p.string(), m)) { fprintf(stderr, "cannot read %s\n", p.c_str()); return 2; }
            double t0 = now(); std::string o = mp21b::validate(m); double t1 = now(); v1::Report rep; std::string nw = v1::validate(m, v1::Options{true}, &rep); double t2 = now();
            tOld += t1 - t0; tNew += t2 - t1; files++; if (rep.components > 1) multi++;
            (o.empty() ? oldAcc : oldRef)++; (nw.empty() ? newAcc : newRef)++;
            if (!nw.empty()) newReasons[code(nw)]++;
            if (o != nw) { changed++; printf("CHANGED %s: old '%s' new '%s'\n", p.c_str(), o.c_str(), nw.c_str()); }
        }
        printf("%s: %zu files (%zu with several components) | frozen validator accepted %zu refused %zu | V1 accepted %zu refused %zu | time frozen %.3fs, frozen+V1 %.3fs\n", d.c_str(), files, multi, oldAcc, oldRef, newAcc, newRef, tOld, tNew);
        for (auto& r : newReasons) printf("    V1 refusal reasons: %s x%zu\n", r.first.c_str(), r.second);
    }
    printf("compare: %d files whose verdict or reason changed\n", changed);
    return changed ? 1 : 0;
}

// synthetic cost check (labelled as such in the docs; the real-mesh set was not available to this run)
static Mesh cat(Mesh a, const Mesh& b) { uint32_t o = (uint32_t)a.nv(); for (size_t i = 0; i < b.nv(); i++) a.addV(b.v[3*i], b.v[3*i+1], b.v[3*i+2]); for (size_t k = 0; k < b.nt(); k++) a.addT(b.t[3*k] + o, b.t[3*k+1] + o, b.t[3*k+2] + o); return a; }
static Mesh box(double x0, double y0, double z0, double x1, double y1, double z1, bool inv) {
    Mesh m; for (int i = 0; i < 8; i++) m.addV((i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0);
    static const uint32_t T[12][3] = {{0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},{2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}};
    for (auto& t : T) m.addT(t[0], inv ? t[2] : t[1], inv ? t[1] : t[2]); return m; }
static int benchMode() {
    struct Case { const char* name; Mesh m; } cases[3];
    cases[0].name = "4800 disjoint boxes (57,600 triangles, 4800 components)"; for (int i = 0; i < 40; i++) for (int j = 0; j < 40; j++) for (int k = 0; k < 3; k++) cases[0].m = cat(cases[0].m, box(3*i, 3*j, 3*k, 3*i+2, 3*j+2, 3*k+2, false));
    cases[1].name = "alternating nested chain of 40 boxes (480 triangles)"; for (int d = 0; d < 40; d++) cases[1].m = cat(cases[1].m, box(d, d, d, 100 - d, 100 - d, 100 - d, d & 1));
    cases[2].name = "one box (12 triangles, 1 component)"; cases[2].m = box(0,0,0,1,1,1,false);
    for (auto& c : cases) { double t0 = now(); std::string o = mp21b::validate(c.m); double t1 = now(); v1::Report r; std::string n = v1::validate(c.m, v1::Options{true}, &r); double t2 = now();
        printf("bench %-62s frozen %.4fs (%s) | V1 total %.4fs (%s), %d components, depth %d\n", c.name, t1 - t0, o.empty() ? "accept" : "refuse", t2 - t1, n.empty() ? "accept" : n.substr(0, 40).c_str(), r.components, r.maxDepth); }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "bench") return benchMode();
    if (argc >= 3 && std::string(argv[1]) == "set") return setMode(argv[2], argc >= 4 && std::string(argv[3]) == "--list");
    if (argc >= 3 && std::string(argv[1]) == "compare") return compareMode(std::vector<std::string>(argv + 2, argv + argc));
    fprintf(stderr, "usage: check_v1 set <setdir> [--list] | compare <dir>...\n"); return 2;
}
