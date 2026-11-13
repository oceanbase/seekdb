#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || "$2" != seekdb ]]; then
  echo "Usage: $0 <source-path> seekdb <version> --distribution el7|el8 [--binary-package PATH | --seekdb-bin PATH] [package options]" >&2
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
  el7|el8) ;;
  *) echo "RPM requires --distribution el7 or el8" >&2; exit 64 ;;
esac
if [[ "$prebuilt" == false ]]; then
  (
    cd "$source_dir"
    python3 ./build.py init --linux-profile "$distribution"
    python3 ./build.py build --config=rpm --jobs="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}" \
      //src/observer:seekdb //src/share/inner_table/sys_package:syspack_release
  )
fi
python3 "$source_dir/package/package.py" rpm --version "$3" --output "$output_dir" "${@:$first_option}"
