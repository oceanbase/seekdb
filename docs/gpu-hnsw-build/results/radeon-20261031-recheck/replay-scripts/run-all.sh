#!/usr/bin/env bash
set -Eeuo pipefail
run=/workspace/hipvs-recheck-20261031
scratch="$run/build-area"
export PATH="$scratch/tooling/rust-1.98.1/bin:/opt/venv/bin:/opt/rocm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export TMPDIR="$scratch/tmp"
stage=prepare
trap 'result=$?; printf "{\"exit_code\":%d,\"stage\":\"%s\",\"finished_utc\":\"%s\"}\n" "$result" "$stage" "$(date -u +%FT%TZ)" > "$run/evidence/run-status.json"' EXIT
mkdir -p "$scratch/tmp" "$scratch/tooling/bin" "$scratch/tooling/cargo-tuna" "$run/src"
cd "$run/src"
sha256sum "$run/incoming/source-shallow.tar" > "$run/evidence/source-archive-sha256.txt"
tar -xf "$run/incoming/source-shallow.tar"
git config core.autocrlf false
git config core.filemode true
git config core.symlinks true
git remote set-url origin https://github.com/sevenmiles666/psfseekdb.git
test "$(git rev-parse HEAD)" = 0e86b8c38afaf8fe3359041645c9b53c5a8524fe
git checkout HEAD -- .
git status --porcelain > "$run/evidence/source-git-status-before-build.txt"
ln -s /opt/seekdb-deps-cache/ac032efc6b707fc0273e6b553db53011/3rd deps/3rd
mkdir -p "$scratch/build_release"
ln -s "$scratch/build_release" build_release
cp "$run/incoming/rust-workspace-Cargo.lock" rust/Cargo.lock
cp "$run/incoming/cargo-wrapper.sh" "$scratch/tooling/bin/cargo"
chmod +x "$scratch/tooling/bin/cargo"
cat > "$scratch/tooling/cargo-tuna/config.toml" <<'EOF'
[source.crates-io]
replace-with = 'mirror'
[source.mirror]
registry = "sparse+https://mirrors.tuna.tsinghua.edu.cn/crates.io-index/"
[net]
retry = 2
EOF
stage=toolchain
python "$run/incoming/install-pinned-rust.py"
export RUSTC="$scratch/tooling/rust-1.98.1/bin/rustc"
export CARGO_HOME="$scratch/tooling/cargo-tuna"
stage=configure
./build.sh release -DOB_BUILD_HIPVS=ON -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm -DCARGO="$scratch/tooling/bin/cargo"
stage=compile
make -C build_release -j8 seekdb
stage=validate
bash "$run/incoming/validate-current.sh"
stage=profile
export LD_LIBRARY_PATH="$run/artifacts/lib:/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=8 OPENBLAS_NUM_THREADS=8
python "$run/incoming/profile-current.py"
python "$run/incoming/check-no-gpu.py"
stage=collect
python "$run/incoming/collect-evidence.py"
cp "$run/incoming/source-snapshot.json" "$run/evidence/"
python -m unittest discover -s unittest/sql/vector_index -p 'test_hipvs_*.py' -v > "$run/evidence/python-regressions.log" 2>&1
python docs/gpu-hnsw-build/verify_evidence.py --results-dir "$run/evidence" > "$run/evidence/evidence-verification.json"
grep -h -F '[HIPVS_BUILD]' "$scratch/db/log/seekdb.log"* > "$run/evidence/hipvs-build-events.log"
stage=passed
