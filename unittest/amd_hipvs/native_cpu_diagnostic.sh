#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
dep=${VSAG_DEVEL_PREFIX:-"$root/deps/3rd/usr/local/oceanbase/deps/devel"}
output=${1:-"$root/unittest/amd_hipvs/local-results/native-cpu-$(date -u +%Y%m%dT%H%M%SZ)"}
python=${PYTHON:-python3}
# Require a new directory so original diagnostic records remain intact.
mkdir -p -- "$(dirname -- "$output")"
mkdir -- "$output"
output=$(cd -- "$output" && pwd)
export LD_LIBRARY_PATH="$dep/lib/vsag_lib:$dep/lib:${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=1
"$python" "$root/unittest/amd_hipvs/native_cpu_diagnostic.py" prepare --output "$output"
g++ -std=c++17 -O2 "$root/unittest/amd_hipvs/native_cpu_diagnostic.cpp"   -I"$dep/include" -L"$dep/lib/vsag_lib" -L"$dep/lib"   -Wl,-rpath-link,"$dep/lib" -lvsag -o "$output/native_cpu_diagnostic"
sha256sum "$dep/lib/vsag_lib/libvsag.so" > "$output/library.sha256"
"$output/native_cpu_diagnostic" "$output" pk 16 200 > "$output/pk-m16-efc200.log" 2>&1
"$python" "$root/unittest/amd_hipvs/native_cpu_diagnostic.py" analyze --output "$output"
cat "$output/pk-m16-efc200.log"
printf 'Native CPU diagnostic reports: %s\n' "$output"
