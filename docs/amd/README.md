# seekdb × AMD hipVS HNSW construction

Three fresh CPU/GPU rounds at each scale produced **1.45× build acceleration
on the official10k corpus** and **13.13× on a separate100k corpus**. Every one
of the12 formal rounds achieved complete Recall@10=1.0000 (100queries/1000targets).
The measured backend uses GPU CAGRA → full HNSW construction and CPU HNSW search.

Read REPORT.md for the full timing boundary, resources, both result tables and
the retained failed/low-recall diagnostics. results.json preserves exact values.
evidence/formal/formal-review108.json records the independent prediction/GT,
strict-proof and original-file SHA review. Selected originals and the full hash
manifest are retained under evidence/formal/; the remaining compact raw import
is external to Git. Existing100k CPU/GPU indexes also each retain1000/1000
after same-path observer recovery (PID14001→33736); separate actual recovery,
read-only replay originals and independent review are in evidence/persistence/.
The saved settings are reused over the current shell environment; full original
environment reuse and historical inode continuity are not claimed. The complete
formal archive and all 21,665 original file sizes/SHA256 are locally verified;
this byte audit does not claim a new raw-vector semantic recalculation.

Implementation base: `76f871eb5a84b664d487b03b3412ce50194135b5`. Measured source108 is preserved at
111c02a7b510199e1d1a69c46a9c2c6ef9b0312c; the separate public master-compatibility
commit is8a4507da2b784fe3e39b5b615fd3d716daf01bfd. Product/source and dependency
identities are recorded in provenance/. GPU parameter use is an explicitly
synchronized process profile, documented in PARAMETER_CONTRACT108.md.

1. SUBMISSION.md — official requirements and current delivery status.
2. tools/hipvs_bridge/README.md — ABI, bridge build and routing scope.
3. tools/benchmark/amd_hnsw/README.md and OPERATIONS.md — complete reproduction.
4. REPORT.md and results.json — measured results and evidence scope.
5. PARAMETER_CONTRACT108.md — SQL/environment alignment and dynamic-ef limits.
6. THIRD_PARTY_NOTICES.md — source references and licenses.

Portable local controls, from the repository root:

```bash
python3 unittest/amd_hipvs/test_prepare_ground_truth.py
python3 unittest/amd_hipvs/test_completion_proof.py
python3 unittest/amd_hipvs/test_dispatch.py
python3 unittest/amd_hipvs/snapshot_io_contract.py
```

The packaged run passed7 GT tests,4 completion-contract tests, both extracted
production-dispatch cases and32 snapshot I/O checks. These local controls do
not start services or claim GPU execution. Original outputs are under
evidence/local/portable108/. The runtime GPU evidence is separate under formal/.
