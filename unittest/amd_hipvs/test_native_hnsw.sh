#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
dep=${VSAG_DEVEL_PREFIX:-"$root/deps/3rd/usr/local/oceanbase/deps/devel"}
out=${1:-"$root/unittest/amd_hipvs/local-results"}
mkdir -p -- "$out"
export LD_LIBRARY_PATH="$dep/lib/vsag_lib:$dep/lib:${LD_LIBRARY_PATH:-}"
g++ -std=c++17 -O1 -g "$root/unittest/amd_hipvs/test_native_hnsw.cpp" \
  -I"$root/src/oblib" -I"$dep/include" -L"$dep/lib/vsag_lib" -L"$dep/lib" \
  -Wl,-rpath-link,"$dep/lib" -lvsag -o "$out/test_native_hnsw"
"$out/test_native_hnsw"
