# API sketch: how mesh functions could surface in MongoDB (MP14-C)

Status: a design note, not an implementation. Every claim is tagged **[measured]** (a result in this repository or in the fork's `poc/gpu/extension/RESULTS.md`), **[read]** (read from source or docs in the fork, this research, not re-verified in a running mongod) or **[unverified]**. Nothing here is a statement about what MongoDB will accept; whether a separately distributed extension built against the SSPL-licensed Extensions API headers may be distributed is a legal question for the owners and is not decided.

## Two surfaces

| | A. Extension aggregation stage | B. Core `$mesh*` expression operators |
|---|---|---|
| Where it runs | A shared library loaded at startup by `--loadExtensions` [read] | Inside the server binary |
| What it can add | Aggregation stages (source, transform, desugar) and rewrite rules; **not** expression operators [read] | Expression operators such as `$meshVolume` [read: `REGISTER_EXPRESSION_CONDITIONALLY` and feature-flag gating exist] |
| Per-document measure | A stage such as `{$meshMeasure: {mesh: "$m", out: "vol"}}`, behaving like `$addFields`; cannot be used inside `$project` / `$match` expressions [read] | `{$project: {v: {$meshVolume: "$m"}}}`, usable anywhere an expression is |
| Batching for a GPU | A stage sees many documents (pull model, `getNext`) and can batch N documents per dispatch [measured in the fake host: MP14a, 1,940 meshes] | One call per document unless the expression engine batches (unverified) |
| Crash/hang isolation | **None** from the API: deadlines and kill are cooperative; a stage that never checks overran a 100 ms deadline by about 500 ms [measured, MP14a]; a crash takes down the host process [read]. A real design needs a separate worker process [recommendation, from that measurement] | Same process, no isolation |
| Language | C++-only public header; production support is Rust only; no Rust bindings in the tree [read]. A thin C++ adapter over the public header builds and passes the fake-host bars [measured] | C++ in the server |
| Where it can load | Load code is compiled only on Linux [read]; not tested here in a real mongod [not run] | Everywhere the server builds |
| Licence consequence | Headers are SSPL-1.0 [read]; this repository keeps the adapter in the fork, outside the Apache-2.0 tree [decision] | SSPL server tree; third-party library licences would apply (Geogram BSD-3, Shewchuk public domain, Boost BSL) |

## Data representation (MP14-A, [measured] by a real BSON encoder and decoder, `research/integration/bson_sizes.cpp`)
Closed triangle meshes (Euler characteristic 2), measured bytes per triangle and the largest mesh that fits the 16 MiB document limit (`BSONObjMaxUserSize`, `src/mongo/bson/util/builder.h:51` in the fork [read]):

| Representation | bytes/triangle (1e3 to 1e5 triangles) | largest mesh in 16 MiB |
|---|---|---|
| R1 arrays of arrays of doubles / int32 | 52.4 to 55.3 | 298,116 triangles |
| R2 flat arrays | 49.0 to 57.8 | 283,024 triangles |
| R3 BinData, packed little-endian binary64 and uint32 | 24.0 | 698,896 triangles |

All three round-trip bit-exactly (including -0.0, subnormals and 2^50 offsets); the encoder refuses NaN and infinity. R3 halves the size and is the one a GPU upload can use without parsing; R1/R2 are queryable with ordinary BSON operators but cost twice the bytes. Mesh size is bounded by the document limit in all three: a mesh above roughly 7e5 triangles does not fit in one document and would need a different storage design (not studied).

## Index (MP14-B, [measured] on an in-memory model, not on MongoDB's B-tree, `research/integration/bbox_index.cpp`)
Candidate keys for 'which meshes may overlap this box': recall was 100% for all three strategies on every set (random, huge boxes, flat boxes, 2^50 and 2^-30, identical, touching). Selectivity differs sharply on 1e5 random boxes (candidates per true overlap): compound range on the X prefix 842, Morton ranges 810, 2D footprint grid plus z range 2.2. Conclusion for design: a single-axis range or a Morton range is a poor filter; a footprint-grid style key (covered cells as multikey entries) is far better here, at the price of one index entry per covered cell. Not studied: real B-tree behaviour, index size, update cost, how a 2dsphere-like key could be reused.

## What the GPU work says about the surface (measured, M1 only)
The GPU filters are exact-safe (0 false definite answers, fallbacks tested) but, against a fair multi-core CPU, **not faster** in the sizes measured (MP11 1.05x; MP13 point-in-solid 0.03x to 0.47x). So the first product surface should not depend on the GPU: the stage can run on the CPU exact path alone, and the GPU stays an optional, self-testing accelerator behind the breaker (MP13) until a NVIDIA-class measurement says otherwise. [recommendation]

## Open questions this sketch does not answer
Whether the owners accept a C++ extension; stage privileges and static properties; how a worker process would receive batches (copy cost); real mongod load on Linux; stage placement relative to `$match` pushdown; collation and replication interplay (results are written as documents, so no change to replication is expected [read in the original plan, not re-verified]).
