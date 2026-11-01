#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
dep=${VSAG_DEVEL_PREFIX:-"$root/deps/3rd/usr/local/oceanbase/deps/devel"}
out=${1:-"$root/unittest/amd_hipvs/local-results/gpu"}
mkdir -p -- "$out"
export LD_LIBRARY_PATH="$dep/lib/vsag_lib:$dep/lib:${LD_LIBRARY_PATH:-}"
bash "$root/tools/hipvs_bridge_build/build.sh" "$out"
g++ -std=c++17 -O1 -g "$root/unittest/amd_hipvs/test_gpu_export.cpp" \
  -I"$root/src/oblib" -I"$dep/include" -L"$dep/lib/vsag_lib" -L"$dep/lib" \
  -Wl,-rpath-link,"$dep/lib" -lvsag -o "$out/test_gpu_export"
python3 "$root/unittest/amd_hipvs/probe_gpu.py" --output-dir "$out"
"$out/test_gpu_export" "$out/probe_128.hnsw" "$out/probe_128.f32" 128 8
"$out/test_gpu_export" "$out/probe_1000.hnsw" "$out/probe_1000.f32" 1000 32
