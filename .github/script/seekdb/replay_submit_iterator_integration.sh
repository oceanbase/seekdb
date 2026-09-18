#!/usr/bin/env bash

# Run the replay submit iterator promotion test with the Release-compatible
# replaytest binary.  The test-only hook is confined to src/logservice and is
# never enabled by the normal release/package profiles.

set -euo pipefail

WORKSPACE="${GITHUB_WORKSPACE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
BINARY="${1:-$WORKSPACE/build_replaytest/src/observer/seekdb}"
OBTEST_SOURCE="$WORKSPACE/tools/obtest"
CASE_NAME="stanby/replay_submit_iterator_release_deadline"
BASE_ROOT="${RUNNER_TEMP:-$WORKSPACE/.validation}"

for command_name in java rsync sha256sum; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "required command is unavailable: $command_name" >&2
    exit 1
  fi
done
if [[ ! -x "$BINARY" ]]; then
  echo "replaytest binary is missing or not executable: $BINARY" >&2
  exit 1
fi
BINARY="$(readlink -f "$BINARY")"
if [[ ! -f "$WORKSPACE/build_replaytest/CMakeCache.txt" ]] \
    || ! grep -qx 'ENABLE_REPLAY_SUBMIT_ITERATOR_TEST_HOOK:BOOL=ON' \
      "$WORKSPACE/build_replaytest/CMakeCache.txt"; then
  echo "build_replaytest was not configured with the integration-test hook" >&2
  exit 1
fi
if grep -qx 'ENABLE_SANITY:BOOL=ON' "$WORKSPACE/build_replaytest/CMakeCache.txt"; then
  echo "build_replaytest must remain Release-compatible, not Sanity-instrumented" >&2
  exit 1
fi

mkdir -p "$BASE_ROOT"
RUN_ROOT="$(mktemp -d "$BASE_ROOT/replay-submit-iterator.XXXXXX")"
TEST_ROOT="$RUN_ROOT/obtest"
RUNTIME_ROOT="$RUN_ROOT/runtime"
PORT_BASE="${REPLAYTEST_PORT_BASE:-57000}"
KEEP_RUNTIME="${REPLAYTEST_KEEP_RUNTIME:-0}"

cleanup()
{
  local pattern="^$RUNTIME_ROOT/workdir/.*/bin/(observer|seekdb)"
  pkill -9 -u "$(id -un)" -f "$pattern" 2>/dev/null || true
  if [[ "$KEEP_RUNTIME" != "1" ]]; then
    case "$RUN_ROOT" in
      "$BASE_ROOT"/replay-submit-iterator.*)
        rm -rf -- "$RUN_ROOT"
        ;;
      *)
        echo "refusing to remove unexpected runtime path: $RUN_ROOT" >&2
        ;;
    esac
  else
    echo "replaytest runtime retained at $RUN_ROOT"
  fi
}
trap cleanup EXIT INT TERM

mkdir -p \
  "$TEST_ROOT/bin" "$TEST_ROOT/etc" "$TEST_ROOT/log" \
  "$TEST_ROOT/var" "$TEST_ROOT/collected_log" \
  "$TEST_ROOT/t/stanby" "$TEST_ROOT/r/stanby" \
  "$RUNTIME_ROOT/data" "$RUNTIME_ROOT/clog" "$RUNTIME_ROOT/workdir" \
  "$RUN_ROOT/bin"
ln -s "$OBTEST_SOURCE/jar" "$TEST_ROOT/jar"
ln -s "$OBTEST_SOURCE/lib" "$TEST_ROOT/lib"
ln -s "$OBTEST_SOURCE/conf" "$TEST_ROOT/conf"
ln -s "$BINARY" "$TEST_ROOT/bin/observer"
cp "$OBTEST_SOURCE/t/$CASE_NAME.test" "$TEST_ROOT/t/$CASE_NAME.test"
cp "$OBTEST_SOURCE/r/$CASE_NAME.result" "$TEST_ROOT/r/$CASE_NAME.result"

# MyTest uses ssh/rsync even when every node is localhost.  Route localhost
# commands directly and redirect its legacy /data/1/core* cleanup into this
# test's private runtime.  No host-global process or filesystem cleanup is
# permitted by this wrapper.
cat > "$RUN_ROOT/bin/ssh" <<'SSH_WRAPPER'
#!/usr/bin/env bash
set -euo pipefail

while (( $# > 0 )); do
  case "$1" in
    --)
      shift
      break
      ;;
    -[1246AaCfGgKkMNnqsTtVvXxYy])
      shift
      ;;
    -[BbCcEeFfIiJLlmOopQRSWw])
      shift 2
      ;;
    -o*)
      shift
      ;;
    *)
      shift
      break
      ;;
  esac
done

if (( $# == 0 )); then
  exit 0
fi
remote_command="$*"
remote_command="${remote_command//\/data\/1\/core\*/$REPLAYTEST_RUNTIME_ROOT\/core\*}"
exec bash -c "$remote_command"
SSH_WRAPPER
chmod +x "$RUN_ROOT/bin/ssh"

cat > "$RUN_ROOT/obtest.ini" <<EOF
1.obs_hosts=127.0.0.1,127.0.0.1
2:1.obs_hosts=127.0.0.1,127.0.0.1
dev=lo
data_path=$RUNTIME_ROOT/data
clog_path=$RUNTIME_ROOT/clog
workdir=$RUNTIME_ROOT/workdir
user=$(id -un)
ob_version=1.0
memory_size_limit=4G
log_disk_size=2G
node_cpu_count=4
node_cpu_reserved=0
save_core=0
save_log=1
gen_pstack=0
cleanup=0
cleanup_after=0
delete_core_on_reboot=0
with_coverage=0
disk_avail_space=2G
server_log_level=info
port_offset=0
port_base=$PORT_BASE
use_ob_connector=1
ps=0
EOF

BINARY_SHA256="$(sha256sum "$BINARY" | awk '{print $1}')"
echo "replaytest binary: $BINARY"
echo "replaytest sha256: $BINARY_SHA256"
echo "replaytest port base: $PORT_BASE"

export PATH="$RUN_ROOT/bin:$PATH"
export REPLAYTEST_RUNTIME_ROOT="$RUNTIME_ROOT"
export LD_LIBRARY_PATH="$OBTEST_SOURCE/lib:${LD_LIBRARY_PATH:-}"

pushd "$TEST_ROOT" >/dev/null
set +e
java -Xms512m -Xmx2048m \
  -Duserid="$(id -u)" \
  -Dlog4j.log.app="$CASE_NAME" \
  -cp "$OBTEST_SOURCE/jar/mytest.jar" \
  com.etao.mytest.main.MyTest \
  -use_ob_connector true \
  -result_file "$TEST_ROOT/r/$CASE_NAME.result" \
  -test_file "$TEST_ROOT/t/$CASE_NAME.test" \
  "--conf=$RUN_ROOT/obtest.ini"
test_ret=$?
set -e
popd >/dev/null
if (( test_ret != 0 )); then
  echo "replay submit iterator integration test failed: $test_ret" >&2
  exit "$test_ret"
fi

CASE_LOG="$TEST_ROOT/log/$CASE_NAME"
CASE_RESULT="$TEST_ROOT/r/$CASE_NAME.result"
STANDBY_LOG="$RUNTIME_ROOT/workdir/db_s.z1.obs0/log/seekdb.log"
if [[ ! -f "$CASE_LOG" || ! -f "$CASE_RESULT" || ! -f "$STANDBY_LOG" ]]; then
  echo "required test evidence is missing" >&2
  exit 1
fi

grep -q 'test success' "$CASE_LOG"
grep -q 'err_msg => Timeout' "$CASE_LOG"
grep -q 'err_msg => standby tenant is read only' "$CASE_LOG"
if [[ "$(grep -c '^0$' "$CASE_RESULT")" -lt 2 ]]; then
  echo "iterator memory did not remain zero after promotion and checkpoint" >&2
  exit 1
fi
grep -q 'replay submit iterator release deadline test passed' "$CASE_RESULT"
grep -q 'replay submit iterator release blocked.*last_state.*RWLOCK_BUSY' "$STANDBY_LOG"
grep -q 'replay submit iterator release finished.*last_state.*RELEASED' "$STANDBY_LOG"

echo "verified: RWLOCK_BUSY caused promotion timeout and kept the write gate closed"
echo "verified: retry reached RELEASED and IteratorStorage stayed at zero"
