# AMD submission materials

Official rules: https://oceanbase.yuque.com/org-wiki-obtech-vh7w9r/hc3pio/seebnbaoxot1b0tb
Question 600072/subquestion 800509, maximum 20. The saved official text requires
an upstream seekdb PR, implementation, reproducible method and CPU/GPU HNSW
construction report; it does not publish a fixed numerical speedup/quality gate.

| Material | Location | Status |
|---|---|---|
| Optional dense-L2 HNSW integration | src/oblib/lib/vector and src/observer/vector_index | Measured revision 108 retained; public compatibility separate |
| GPU CAGRA → full HNSW bridge | tools/hipvs_bridge | Source, build and six-symbol ABI retained |
| Data/GT/SQL and completion proof | tools/benchmark/amd_hnsw | Reproduction scripts and exact source hashes retained |
| Three fresh CPU/GPU rounds at each of two scales | REPORT.md/results.json | Independently reviewed: 10k 1.4543×; 100k 13.1287× |
| Complete-query Recall@10 | evidence/formal | All 12 rounds 1000/1000; exact predictions/GT retained |
| Full-N GPU phases and no fallback | Per-round proof and phase originals |12 strict proofs reviewed |
| Selected review raw and all original hashes | evidence/formal/originals, small-raw-index.json |449 selected originals;21665-entry original manifest |
| Complete bulk archive local download | External archive + full-archive-review108.json | Archive SHA and all 21,665 original sizes/SHA verified; 2,676 compact omissions restored |
| Existing-database observer recovery/persistence replay | evidence/persistence | PID14001→33736; both100k indexes1000/1000,205 read-only SQL records reviewed |
| Upstream seekdb PR | To be added | Pending final commit/push and PR creation |

The backend's measured deployment synchronizes GPU process M/ef settings with
SQL. It does not claim transparent arbitrary per-index/per-query GPU settings.
Ordinary CPU VSAG remains the existing parameter/search path. Only eligible
ordinary dense float32 L2 HNSW indexes without extra information opt into the
GPU route. Explicit CPU fallback is excluded from GPU construction results.
