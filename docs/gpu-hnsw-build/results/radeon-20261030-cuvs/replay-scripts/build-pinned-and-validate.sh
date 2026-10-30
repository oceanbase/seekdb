#!/usr/bin/env bash
set -Eeuo pipefail
run=/workspace/hipvs-competition-20261030
scratch=/root/hipvs-competition-build-20261030
export PATH="$scratch/tooling/rust-1.98.1/bin:/opt/venv/bin:/opt/rocm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export RUSTC="$scratch/tooling/rust-1.98.1/bin/rustc"
export CARGO_HOME="$scratch/tooling/cargo-tuna"
export CARGO_NET_OFFLINE=false
export TMPDIR="$scratch/tmp"
stage=configure-pinned
trap 'result=$?; printf "{\"exit_code\":%d,\"stage\":\"%s\",\"finished_utc\":\"%s\"}\n" "$result" "$stage" "$(date -u +%FT%TZ)" > "$run/evidence/pinned-build-status.json"' EXIT
cd "$run/src"
rustc --version
cargo --version
./build.sh release -DOB_BUILD_HIPVS=ON -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm -DCARGO="$scratch/tooling/bin/cargo"
# The initial C++ build is reusable. Rebuild Rust exports using the pinned compiler.
touch rust/Cargo.toml
stage=compile-pinned
make -C build_release -j8 seekdb
stage=validate
bash "$run/incoming/validate-current.sh"
stage=profile
unset HIP_VISIBLE_DEVICES
export LD_LIBRARY_PATH="$run/artifacts/lib:/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=8 OPENBLAS_NUM_THREADS=8
python "$run/incoming/profile-current.py"
stage=collect
python "$run/incoming/collect-evidence.py"
python docs/gpu-hnsw-build/verify_evidence.py --results-dir "$run/evidence"
stage=passed
