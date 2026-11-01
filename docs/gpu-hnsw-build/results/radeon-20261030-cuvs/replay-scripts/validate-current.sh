#!/usr/bin/env bash
set -Eeuo pipefail
run=/workspace/hipvs-competition-20261030
scratch=/root/hipvs-competition-build-20261030
src="$run/src"
db="$scratch/db"
stage=artifacts
trap 'result=$?; printf "{\"exit_code\":%d,\"stage\":\"%s\",\"finished_utc\":\"%s\"}\n" "$result" "$stage" "$(date -u +%FT%TZ)" > "$run/evidence/validation-status.json"' EXIT
mkdir -p "$run/artifacts/bin" "$run/artifacts/lib"
cp "$src/build_release/src/observer/seekdb" "$src/build_release/src/observer/seekdb_hipvs_hnsw_worker" "$run/artifacts/bin/"
cp "$src/build_release/lib/libseekdb_hipvs_hnsw.so" "$run/artifacts/lib/"
export PATH=/opt/venv/bin:/opt/rocm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export LD_LIBRARY_PATH="$run/artifacts/lib:/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=8 OPENBLAS_NUM_THREADS=8
export TMPDIR="$scratch/tmp"
sha256sum "$run/artifacts/bin/seekdb" "$run/artifacts/bin/seekdb_hipvs_hnsw_worker" "$run/artifacts/lib/libseekdb_hipvs_hnsw.so" > "$run/evidence/tested-binary-sha256.txt"
cp "$src/rust/Cargo.lock" "$run/evidence/rust-workspace-Cargo.lock"
cp "$src/build_release/CMakeCache.txt" "$run/evidence/CMakeCache.txt"
stage=server-start
python "$src/.github/script/seekdb/sdb.py" start --binary "$run/artifacts/bin/seekdb" --base-dir "$db" --port 2881 --parameter cpu_count=16
python "$src/.github/script/seekdb/sdb.py" wait-ready --client mysql --base-dir "$db" --port 2881 --timeout 240
stage=sql-contract
python "$src/unittest/sql/vector_index/check_hipvs_sql.py" --database hipvs_contract_20261030 --output "$run/evidence/sql-contract.json"
stage=sql-build-benchmark
python "$src/docs/gpu-hnsw-build/benchmark.py" --gpu-lib cuvs --rows 50000 --dim 128 --queries 100 --rounds 3 --m 32 --ef-construction 200 --ef-search 1000 --min-recall 0.99 --database hipvs_build_benchmark_20261030 --server-log "$db/log/seekdb.log" --output "$run/evidence/sql-50000x128.json"
stage=restart
python "$src/.github/script/seekdb/sdb.py" stop --base-dir "$db"
export HIP_VISIBLE_DEVICES=-1
python "$src/.github/script/seekdb/sdb.py" start --binary "$run/artifacts/bin/seekdb" --base-dir "$db" --port 2881 --parameter cpu_count=16
python "$src/.github/script/seekdb/sdb.py" wait-ready --client mysql --base-dir "$db" --port 2881 --timeout 240
python "$src/docs/gpu-hnsw-build/benchmark.py" --verify-existing --gpu-lib cuvs --rows 50000 --dim 128 --queries 100 --rounds 3 --m 32 --ef-construction 200 --ef-search 1000 --min-recall 0.99 --database hipvs_build_benchmark_20261030 --server-log "$db/log/seekdb.log" --output "$run/evidence/sql-50000x128.json"
python "$src/.github/script/seekdb/sdb.py" stop --base-dir "$db"
stage=passed
