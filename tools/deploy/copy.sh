#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
artifacts="${1:-$source_dir/build/bin}"
if (( $# > 1 )); then
  echo "Usage: $0 [Bazel artifact directory]" >&2
  exit 64
fi
mkdir -p "bin${VER:-}" lib tools etc debug admin
install -m 0755 "$artifacts/src/observer/seekdb" "bin${VER:-}/seekdb"
install -m 0644 "$artifacts"/src/share/inner_table/sys_package/bazel_generated/syspack_release/*.sql admin/
install -m 0644 "${SEEKDB_TIMEZONE_FILE:-$source_dir/tools/timezone_V1.log}" etc/timezone_V1.log
install -m 0644 "$source_dir/tools/default_srs_data_mysql.sql" etc/
install -m 0755 "$source_dir/deps/3rd/usr/local/oceanbase/devtools/bin/llvm-symbolizer" tools/
obshell="$source_dir/deps/3rd/home/admin/oceanbase/bin/obshell"
if [[ -f "$obshell" ]]; then
  install -m 0755 "$obshell" "bin${VER:-}/obshell"
fi
