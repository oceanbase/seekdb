# Completion proof, persistence replay and evidence export

All five scripts live beside sql_benchmark.py and completion_proof.py, so their
imports resolve from any working directory. Run them with the existing Python
and NumPy environment. They do not install packages or manage a cloud instance.
Their exact copied-source hashes are in docs/amd/provenance/operation-scripts108.json.

## Verify and activate a completion contract

generate_contract.py accepts --workspace and --launch. The workspace is the
actual experiment directory, not the repository. It must contain environment.json,
completion_proof.py and hnsw_bridge/{seekdb_cuvs_bridge.c,libseekdb_cuvs_bridge.so}.
For a new workspace, copy the repository's proof script and bridge source into
those locations, build the bridge there, and retain the real server launch and
environment receipts. Never enable a contract using an invented receipt.

```bash
python3 /path/to/seekdb/tools/benchmark/amd_hnsw/generate_contract.py \
  --workspace /workspace/amd107 \
  --launch /workspace/amd107/candidate-quality-launch.json \
  --output /workspace/amd107/quality64-contract.json \
  --activate-after-reviewed-smoke /workspace/amd107/quality64-smoke100k/report.json \
  --recall-threshold 0.96
```

Omit --activate-after-reviewed-smoke to generate an unverified contract. The
chosen 0.96 here is a reproduction setting; the final report must record the
actual run's value. The generator validates existing receipts and executes no
SQL or service operation. The active proof command records absolute paths to
the actual bridge, environment and task-owned observer PID.

## Restart and query persisted indexes

`restart_persisted.py` is the original guarded operator script pinned to
`/workspace/amd107/runtime108/observer`, `candidate-quality64`, port2882 and the
measured bridge hash. It has no `--help` or general CLI: invoking it performs its
guarded restart. A different deployment needs explicit paths/artifact identity.
The original attempt sent owned SIGTERM and refused replacement while the port
remained occupied (operator history; its original refusal diagnostic is not in
the export). Do not describe that attempt as a completed restart.

`recover_persisted.py` is the separate exact script actually used afterward. It
sends no process signals. It requires the old owned PID to have no active
threads, port2882 to have no listener/connection, and the existing populated DB,
observer and bridge to retain their checked identity. It reuses the saved argv
and explicit settings against that existing path, using the current shell
environment overlaid with saved settings. It checks directory inode stability
only during recovery, not historical continuity before shutdown. It does not
create/delete the DB, issue DDL or manage cloud instances. It also has no
`--help`; do not execute it locally as a syntax check.

Actual receipts in `docs/amd/evidence/persistence/` record PID14001 stopped and
PID33736 SQL-ready with unchanged saved argv/settings/runtime hashes. The
complete original process environment and historical directory-inode continuity
are explicitly not proven. Read OPERATION_HISTORY.md there for the exact sequence.

After this actual recovery, the portable read-only CLI was:

```bash
python3 /path/to/seekdb/tools/benchmark/amd_hnsw/replay_existing.py \
  --report /workspace/amd107/quality64-formal100k-warn/report.json \
  --gt-dir /workspace/amd107/gt100k \
  --base /workspace/amd107/scale100k/base.f32 \
  --query /workspace/amd107/scale100k/query.f32 \
  --output /workspace/amd107/persisted-replay100k \
  --driver mysql --host 127.0.0.1 --port 2882 --ef-search 1000 \
  --recall-threshold 0.96
```

The replay selects the original final proven CPU/GPU round2 tables and indexes,
checks row count/range and requested plans, then runs every query against exact
original GT. All205 SQL statements/events/sessions/raw rows were independently
checked; both backends achieved1000/1000. It performs SELECT/EXPLAIN only, with
session ef_search set to1000 by connection initialization. Replay success alone
does not prove restart; its original report retains that limitation and the
separate actual recovery receipt supplies the launch evidence.

## Export raw originals and clipboard evidence

```bash
python3 /path/to/seekdb/tools/benchmark/amd_hnsw/export_evidence.py \
  --root /workspace/amd107 \
  --results quality64-formal100k quality64-persisted100k \
  --output-prefix /workspace/amd107/quality64-export \
  --no-print-payload
```

Add the actual 10k result and replay directory names to --results. Paths must
stay inside --root, outputs must use a new prefix, and result directories must
contain report.json. The exporter reads only the documented result layout and
named source/receipt files. --include accepts additional exact textual receipt
paths when the operator has checked their contents; it does not accept globs.
Private transfer files, signed downloads, credentials, runtime binaries and
database directories are excluded.

The .full.tar.gz retains all selected original SQL/readback bytes and contains
EXPORT_MANIFEST.json with every file's size and SHA256. The .compact.zlib.b64
decodes to UTF-8 JSON, preserving full reports, recall, predictions, plans,
sessions, events and phase logs. Large vector readbacks and SQL INSERT values
are listed in omitted with their original size/hash; their complete originals
remain in the archive. The .summary.json records archive and compact hashes.
Running results or files that change during export are explicitly marked.

Without --no-print-payload, stdout contains only one base64 payload line;
stderr contains the download summary. Decode without executing its contents:

```python
import base64, json, pathlib, zlib
payload = pathlib.Path("quality64-export.compact.zlib.b64").read_text().strip()
evidence = json.loads(zlib.decompress(base64.b64decode(payload)))
```

Validate the manifest against extracted archive bytes before filling the
formal result tables. Keep readback/INSERT omissions visible in any compact
copy, and retain the complete archive for full reproduction.
