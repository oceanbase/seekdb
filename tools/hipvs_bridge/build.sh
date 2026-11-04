#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
HIPVS_ROOT=${HIPVS_ROOT:-/opt/hipvs}
ROCM_ROOT=${ROCM_ROOT:-/opt/rocm}
HIPCC=${HIPCC:-$ROCM_ROOT/bin/hipcc}
OUTPUT=${1:-$SCRIPT_DIR/libseekdb_cuvs_bridge.so}
[[ -x "$HIPCC" ]] || { echo "hipcc is not executable: $HIPCC" >&2; exit 2; }
for header in core/c_api.h neighbors/cagra.h neighbors/hnsw.h; do
  [[ -f "$HIPVS_ROOT/include/cuvs/$header" ]] || { echo "Missing mirror header: $header" >&2; exit 2; }
done
[[ -f "$HIPVS_ROOT/lib/libcuvs_c.so" ]] || { echo "Missing $HIPVS_ROOT/lib/libcuvs_c.so" >&2; exit 2; }
mkdir -p "$(dirname "$OUTPUT")"
"$HIPCC" -x c++ -std=c++17 -shared -fPIC -O2 -pthread -D__HIP_PLATFORM_AMD__ \
  -I"$HIPVS_ROOT/include" -I"$ROCM_ROOT/include" \
  "$SCRIPT_DIR/seekdb_cuvs_bridge.c" -L"$HIPVS_ROOT/lib" \
  -Wl,-soname,libseekdb_cuvs_bridge.so -Wl,-z,defs \
  -Wl,-rpath,"$HIPVS_ROOT/lib:$ROCM_ROOT/lib" -lcuvs_c -o "$OUTPUT"
for symbol in seekdb_cuvs_build seekdb_cuvs_search seekdb_cuvs_free \
              seekdb_cuvs_cagra_knn seekdb_cuvs_serialize seekdb_cuvs_deserialize; do
  nm -D --defined-only "$OUTPUT" | awk '{print $3}' | grep -Fx "$symbol" >/dev/null \
    || { echo "Missing original C ABI symbol: $symbol" >&2; exit 3; }
done
sha256sum "$OUTPUT"
echo "Built $OUTPUT (GPU CAGRA -> full HNSW; host HNSW search)"
