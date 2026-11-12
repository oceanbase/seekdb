#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || "$2" != seekdb ]]; then
  echo "Usage: $0 <source-path> seekdb <version> [--binary-package PATH | --seekdb-bin PATH] [package options]" >&2
  exit 64
fi
source_dir="$(cd "$1" && pwd)"
output_dir="$PWD"
# Accept the former positional release without using it as a package counter.
first_option=4
if [[ $# -ge 4 && "${4:-}" != --* ]]; then first_option=5; fi
prebuilt=false
for argument in "${@:$first_option}"; do
  case "$argument" in
    --binary-package|--binary-package=*|--seekdb-bin|--seekdb-bin=*) prebuilt=true ;;
  esac
done
if [[ "$prebuilt" == false ]]; then
  (
    cd "$source_dir"
    python3 ./build.py init
    python3 ./build.py build --config=package --jobs="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}" \
      //src/observer:seekdb //src/share/inner_table/sys_package:syspack_release
  )
fi
python3 "$source_dir/package/package.py" deb --version "$3" --output "$output_dir" "${@:$first_option}"
