#!/usr/bin/env bash
set -Eeuo pipefail
run=/workspace/hipvs-recheck-20261031
test "$(python3 -c 'import json; print(json.load(open("/workspace/hipvs-recheck-20261031/evidence/run-status.json"))["stage"])')" = passed
cp "$run/evidence/final-environment.json" "$run/artifacts/environment.json"
cp "$run/evidence/source-sha256.json" "$run/artifacts/source-sha256.json"
cp "$run/evidence/tested-binary-sha256.txt" "$run/artifacts/tested-binary-sha256.txt"
mkdir -p "$run/artifacts/scripts"
cp "$run/src/.github/script/seekdb/sdb.py" "$run/artifacts/scripts/"
cp "$run/src/docs/gpu-hnsw-build/benchmark.py" "$run/artifacts/scripts/"
cp "$run/src/unittest/sql/vector_index/check_hipvs_sql.py" "$run/artifacts/scripts/"
cat > "$run/artifacts/README.txt" <<'EOF'
Original tested Linux x86-64 binaries from 0e86b8c38afaf8fe3359041645c9b53c5a8524fe.
This is not a Windows executable or a self-contained ROCm distribution.
Requires the matching seekdb-hipvs image libraries: /opt/hipvs and /opt/rocm.
Set LD_LIBRARY_PATH to the extracted artifacts/lib plus /opt/hipvs/lib:/opt/rocm/lib.
Keep bin/seekdb and bin/seekdb_hipvs_hnsw_worker together.
See docs/gpu-hnsw-build/SUBMISSION.md in the repository for startup and SQL validation.
The measured database was stopped before packaging; it is not included.

After extracting on the matching Linux image, from the extraction directory:
export LD_LIBRARY_PATH="$PWD/artifacts/lib:/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=8 OPENBLAS_NUM_THREADS=8
python3 artifacts/scripts/sdb.py start --binary "$PWD/artifacts/bin/seekdb" --base-dir /var/tmp/hipvs-replay --port 2881 --parameter cpu_count=16
python3 artifacts/scripts/sdb.py wait-ready --client mysql --base-dir /var/tmp/hipvs-replay --port 2881
python3 artifacts/scripts/check_hipvs_sql.py --database hipvs_replay_contract --output /workspace/hipvs-replay-contract.json
python3 artifacts/scripts/sdb.py stop --base-dir /var/tmp/hipvs-replay
EOF
tar -C "$run" -cf - artifacts | gzip -1 > "$run/seekdb-hipvs-tested-linux-20261031.tar.gz"
sha256sum "$run/seekdb-hipvs-tested-linux-20261031.tar.gz" > "$run/runtime-archive.sha256"
ls -lh "$run/seekdb-hipvs-tested-linux-20261031.tar.gz"
