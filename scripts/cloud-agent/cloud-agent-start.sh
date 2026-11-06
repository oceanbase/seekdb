#!/usr/bin/env bash
set -euo pipefail
[ ! -d '/agent/repos/seekdb' ] || cd '/agent/repos/seekdb' || exit $?

BASE_DIR="$PWD/build/dev-instance"
PORT=2881
BINARY="build/bin/src/observer/seekdb"
CLIENT="deps/3rd/u01/obclient/bin/obclient"
DEPS_LIB="$PWD/deps/3rd/usr/local/oceanbase/deps/devel/lib"
export LD_LIBRARY_PATH="${DEPS_LIB}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [[ ! -x "$BINARY" ]]; then
  echo "seekdb binary missing at $BINARY; run install first" >&2
  exit 1
fi

if python3 tools/deploy/sdb.py wait-ready \
  --client "$CLIENT" --base-dir "$BASE_DIR" --port "$PORT" --timeout 5; then
  exit 0
fi

python3 tools/deploy/sdb.py start \
  --binary "$BINARY" --base-dir "$BASE_DIR" --port "$PORT"
exec python3 tools/deploy/sdb.py wait-ready \
  --client "$CLIENT" --base-dir "$BASE_DIR" --port "$PORT"
