#!/usr/bin/env bash
set -euo pipefail
bridge_dir=$(cd -- "$(dirname -- "$0")" && pwd)
hipvs_prefix=${HIPVS_PREFIX:-/opt/hipvs}
rocm_prefix=${ROCM_PATH:-/opt/rocm}
hipvs_libdir=${HIPVS_LIBDIR:-"$hipvs_prefix/lib"}
out_dir=${1:-"$bridge_dir/build"}
mkdir -p -- "$out_dir"
"$rocm_prefix/bin/hipcc" -std=c++17 -O3 -fPIC -shared -pthread \
  -I"$hipvs_prefix/include" "$bridge_dir/seekdb_hipvs_build.cpp" \
  -L"$hipvs_libdir" -lcuvs_c -Wl,-z,defs -Wl,-rpath,"$hipvs_libdir" \
  -Wl,-rpath,"$rocm_prefix/lib" -o "$out_dir/libseekdb_hipvs_build.so"
printf 'Built %s/libseekdb_hipvs_build.so\n' "$out_dir"
