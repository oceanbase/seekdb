#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 4 || "$2" != seekdb ]]; then
  echo "Usage: $0 <source-path> seekdb <version> <release> [--binary-package PATH | --seekdb-bin PATH] [package options]" >&2
  exit 64
fi
source_dir="$(cd "$1" && pwd)"
output_dir="$PWD"
prebuilt=false
for argument in "${@:5}"; do
  case "$argument" in
    --binary-package|--binary-package=*|--seekdb-bin|--seekdb-bin=*) prebuilt=true ;;
  esac
done
if [[ "$prebuilt" == false ]]; then
  (
    cd "$source_dir"
    python3 ./build.py init
    python3 ./build.py build --config=rpm --jobs="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}" \
      //src/observer:seekdb //src/share/inner_table/sys_package:syspack_release
  )
fi
python3 "$source_dir/package/package.py" rpm --version "$3" --release "$4" --output "$output_dir" "${@:5}"
