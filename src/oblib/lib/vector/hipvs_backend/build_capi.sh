#!/bin/bash
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
#
# Build the AMD hipVS backend shared object that seekdb dlopen()s
# (SEEKDB_HIPVS_LIBRARY=/path/libseekdb_hipvs.so).
#
# This variant targets the *runtime* hipVS package shipped with the AMD
# competition image: self-contained C API headers plus libcuvs_c.so/libcuvs.so.
# That package has no raft/CCL C++ headers and no cuvs CMake config, so the C++
# backend (hipvs_build.cpp, needs cuvs::cuvs + hipcc) cannot be built there while
# hipvs_build_capi.cpp can. It needs no hipcc: only the hip runtime headers are
# used for the device probe.
#
# Usage:  ./build_capi.sh [output_dir]
#   CUVS_HOME  hipVS runtime package (default /opt/hipvs)
#   ROCM_HOME  ROCm installation     (default /opt/rocm)
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build}
cuvs_home=${CUVS_HOME:-/opt/hipvs}
rocm_home=${ROCM_HOME:-/opt/rocm}

mkdir -p "$out"
g++ -O3 -std=c++17 -fPIC -shared -fvisibility=hidden -D__HIP_PLATFORM_AMD__ \
    -I"$cuvs_home/include" -I"$rocm_home/include" -I"$here/.." \
    "$here/hipvs_build_capi.cpp" -o "$out/libseekdb_hipvs.so" \
    -L"$cuvs_home/lib" -lcuvs_c -lcuvs -L"$rocm_home/lib" -lamdhip64 \
    -Wl,--no-undefined

echo "built $out/libseekdb_hipvs.so"
echo "verify:  SEEKDB_HIPVS_LIBRARY=$out/libseekdb_hipvs.so <observer>"