# Reproduce the HNSW build comparison

Use the same built seekdb binary and existing AMD ROCm/hipVS environment for both
backends. No GPU library installation or cloud-instance management is performed
by these benchmark scripts. The formal result table is pending in
docs/amd/REPORT.md; substitute its recorded configuration once available.

From this checkout, the normal full source build is:

```bash
./build.sh release --init --make -j8
bash tools/hipvs_bridge/build.sh /workspace/results/libseekdb_cuvs_bridge.so
```

These commands are the repository's standard build plus the supplied bridge
build, not a claim that a new full build has run in the cloud. Revision 108 was
compiled and relinked from an audited complete local object cache; the exact
compiler/link argv and checks are recorded in docs/amd/evidence/local/receipt108.json.

Start a separate task-owned server using the local binary path and the required
bridge environment. Correct e6a parameter names are memory_budget, cpu_count,
datafile_size, datafile_maxsize and log_disk_size, without a leading underscore.
Record the actual OS affinity/cgroup limits as well as these logical settings.
Use identical resources, M, ef_construction and session ef_search for both groups.

Generate a separate deterministic corpus, or use the exact immutable image raw
files identified by their recorded hashes. A generated reproduction example:

```bash
python3 tools/benchmark/amd_hnsw/generate_dataset.py \
  --output /workspace/results/corpus --base-count 10000 --query-count 100 \
  --dimension 128 --seed 20261028
python3 tools/benchmark/amd_hnsw/prepare_ground_truth.py \
  --base /workspace/results/corpus/base.f32 --query /workspace/results/corpus/query.f32 \
  --base-count 10000 --query-count 100 --dimension 128 --k 10 --metric l2 \
  --output /workspace/results/truth
```

GT is exact float64 L2 over every actual float32 row. Decimal SQL inputs are
round-trip checked bitwise. The SQL harness reads back arrays using a DOUBLE
promotion expression, then compares float32 bits; it does not relax precision
to compensate for the default vector formatter.

First run a diagnostic SQL smoke against the already running candidate:

```bash
python3 tools/benchmark/amd_hnsw/sql_benchmark.py \
  --gt-dir /workspace/results/truth --output /workspace/results/sql-smoke \
  --mode smoke --driver mysql --host 127.0.0.1 --port 2882 --ef-search 512
```

Every new mysql CLI/PyMySQL connection sets and reads @@session.ob_hnsw_ef_search.
DDL ef_search alone is not the effective query setting. Passwords, if needed,
are read from SEEKDB_PASSWORD; never place credentials in command arguments.
All output paths must be new. The harness creates a unique schema and never
drops an existing one.

Formal mode requires an integration-verified completion contract. Copy
contract.example.json to an external run directory, replace the public/local
script and source/library/environment paths with the actual files, and keep
image_adapter_verified=false until real SQL, full readback, exact-GT recall and
the GPU phase proof are accepted. completion_proof.py uses real log offsets,
source hashes, full-N synchronized phase events and caller-FD snapshot evidence;
missing, interleaved or fallback work is rejected.

```bash
python3 tools/benchmark/amd_hnsw/sql_benchmark.py \
  --gt-dir /workspace/results/truth --output /workspace/results/formal \
  --mode formal --driver mysql --host 127.0.0.1 --port 2882 \
  --ef-search 512 --m 16 --ef-construction 200 \
  --recall-threshold 0.95 --verified-contract /workspace/results/contract.json \
  --server-log /workspace/owned-server/log/seekdb.log
```

0.95 here is an explicit reproduction acceptance setting, not an unpublished
official threshold. The harness alternates CPU/GPU order across three fresh
rounds each. Both groups start with identical raw data loaded and index absent,
and end only when the complete index and first query are proven ready. CPU/GPU
medians form the build speedup; all queries and K targets remain in recall's
denominator. Data generation/import/startup and search durations are separate.

Save the complete output directory and immutable configuration, run the local
controls listed in docs/amd/README.md, then populate REPORT.md/results.json from
those actual receipts. No formal result is filled by this README.

OPERATIONS.md documents the final contract-generation, original guarded restart, separate stopped-process recovery,
read-only persisted-index replay, and full/compact evidence export scripts.
Their source bytes are retained separately from their run receipts.
