#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || "$2" != seekdb ]]; then
  echo "Usage: $0 <source-path> seekdb <version> --distribution debian12|ubuntu22.04 [--binary-package PATH | --seekdb-bin PATH] [package options]" >&2
  exit 64
fi
source_dir="$(cd "$1" && pwd)"
output_dir="$PWD"
# Accept the former positional release without using it as a package counter.
first_option=4
if [[ $# -ge 4 && "${4:-}" != --* ]]; then first_option=5; fi
prebuilt=false
distribution=""
previous=""
for argument in "${@:$first_option}"; do
  if [[ "$previous" == --distribution ]]; then distribution="$argument"; fi
  case "$argument" in
    --binary-package|--binary-package=*|--seekdb-bin|--seekdb-bin=*) prebuilt=true ;;
    --distribution=*) distribution="${argument#*=}" ;;
  esac
  previous="$argument"
done
case "$distribution" in
  debian12|ubuntu22.04) ;;
  *) echo "DEB requires --distribution debian12 or ubuntu22.04" >&2; exit 64 ;;
esac
if [[ "$prebuilt" == false ]]; then
  (
    cd "$source_dir"
    python3 ./build.py init
    python3 ./build.py build --config=package --jobs="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}" \
      //src/observer:seekdb //src/share/inner_table/sys_package:syspack_release
  )
fi
python3 "$source_dir/package/package.py" deb --version "$3" --output "$output_dir" "${@:$first_option}"
