#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 4 || "$2" != seekdb ]]; then
  echo "Usage: $0 <source-path> seekdb <version> <release>" >&2
  exit 64
fi
source_dir="$(cd "$1" && pwd)"
output_dir="$PWD"
cd "$source_dir"
python3 ./build.py init
python3 ./build.py build --config=rpm --jobs="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}" \
  //src/observer:seekdb //src/share/inner_table/sys_package:syspack_release
python3 package/package.py rpm --version "$3" --release "$4" --output "$output_dir"
