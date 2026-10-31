#!/usr/bin/env bash
set -euo pipefail
local_only=false
compile=false
jobs=32
case_name=
while getopts 'lc:n:' option; do
  case "$option" in
    l) local_only=true ;;
    c) local_only=true; compile=true; jobs="$OPTARG" ;;
    n) local_only=true; compile=true; case_name="$OPTARG" ;;
    *) echo "Usage: $0 [-l] [-c jobs] [-n case] [source directory]" >&2; exit 64 ;;
  esac
done
shift $((OPTIND - 1))
source_dir="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
if [[ "$compile" == true ]]; then
  (cd "$source_dir" && python3 ./build.py build --jobs="$jobs" \
    //src/observer:seekdb //src/share/inner_table/sys_package:syspack_release)
fi
artifacts="$source_dir/build/bin"
mkdir -p "bin${VER:-}" lib admin etc agent
install -m 0755 "$artifacts/src/observer/seekdb" "bin${VER:-}/observer"
install -m 0644 "$artifacts"/src/share/inner_table/sys_package/bazel_generated/syspack_release/*.sql admin/
if [[ "$local_only" == false ]]; then
  wget -a dict.wget.log 'http://11.166.86.153:8877/dict.tar.gz' -O "bin${VER:-}/dict.tar.gz"
  tar -zxf "bin${VER:-}/dict.tar.gz" -C "bin${VER:-}/"
fi
if [[ -n "$case_name" ]]; then
  ./mytest "$case_name"
fi
