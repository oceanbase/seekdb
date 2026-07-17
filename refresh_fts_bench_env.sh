#!/usr/bin/env bash
# Rebuild and recreate the local SeekDB instance used by run_fts_large_bench.sh.
#
# Run this script on the host (where Docker is available).  Compilation and
# deployment happen inside seekdb-dev; only the damaged ftsbench instance on
# port 10000 is stopped and recreated.

set -euo pipefail

CONTAINER="${CONTAINER:-seekdb-dev}"
WORKDIR="${WORKDIR:-/workspace/seekdb}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
SKIP_BUILD="${SKIP_BUILD:-0}"
READY_TIMEOUT_SEC="${READY_TIMEOUT_SEC:-300}"
DEPLOY_NAME="ftsbench"
MYSQL_PORT=10000
DATA_DIR=/tmp/obtest/seekdb1

die() {
  echo "ERROR: $*" >&2
  exit 1
}

command -v docker >/dev/null 2>&1 || die "Docker CLI is required; run this script on the host."
docker container inspect "${CONTAINER}" >/dev/null 2>&1 || die "container '${CONTAINER}' does not exist"
[[ "$(docker container inspect --format '{{.State.Running}}' "${CONTAINER}")" == true ]] \
  || die "container '${CONTAINER}' is not running"

echo "Refreshing the FTS benchmark environment in ${CONTAINER}..."

exec docker exec --interactive --user root --workdir "${WORKDIR}" \
  --env "BUILD_JOBS=${BUILD_JOBS}" \
  --env "SKIP_BUILD=${SKIP_BUILD}" \
  --env "READY_TIMEOUT_SEC=${READY_TIMEOUT_SEC}" \
  --env "DEPLOY_NAME=${DEPLOY_NAME}" \
  --env "MYSQL_PORT=${MYSQL_PORT}" \
  --env "DATA_DIR=${DATA_DIR}" \
  "${CONTAINER}" bash -s <<'IN_CONTAINER'
set -euo pipefail

# A deployment touches the shared tools/deploy/bin/seekdb mirror.  Do not let
# two profile/refresh invocations replace it concurrently.
LOCK_FILE=/tmp/seekdb-ftsbench-refresh.lock
exec 9>"${LOCK_FILE}"
if ! flock -n 9; then
  # The lock is advisory and is released when its owning process exits.  Show
  # its holder so a stale OBD wait can be distinguished from a real build.
  lock_holders="$(fuser "${LOCK_FILE}" 2>/dev/null || true)"
  echo "ERROR: another ftsbench refresh/deployment is already running." >&2
  if [[ -n "${lock_holders}" ]]; then
    echo "       Lock holder PID(s) in ${CONTAINER}: ${lock_holders}" >&2
  fi
  echo "       Wait for it to finish; if OBD is stuck after SeekDB is ready, terminate only its" >&2
  echo "       'obd cluster start ftsbench -f' process, then rerun this script." >&2
  exit 1
fi

echo "[1/4] Stop stale ftsbench deployment processes"
# A previous interrupted deployment can leave OBD polling forever.  Limit
# matching to this named benchmark deployment, rather than all OBD processes.
stale_obd_pids="$(pgrep -f 'obd cluster start ftsbench|obd\.sh deploy .* -n ftsbench' || true)"
if [[ -n "${stale_obd_pids}" ]]; then
  kill ${stale_obd_pids} || true
fi
# The lock belongs exclusively to this deployment name and may remain after an
# interrupted OBD invocation.
rm -f tools/deploy/.obd/lock/deploy_ftsbench

# The old server's storage files were deleted while it was running.  Stop only
# that exact instance before removing its configured data directory.
server_pids="$(pgrep -f "seekdb.*--port ${MYSQL_PORT}.*--base-dir ${DATA_DIR}" || true)"
if [[ -n "${server_pids}" ]]; then
  kill ${server_pids}
  for _ in {1..30}; do
    pgrep -f "seekdb.*--port ${MYSQL_PORT}.*--base-dir ${DATA_DIR}" >/dev/null || break
    sleep 1
  done
  if pgrep -f "seekdb.*--port ${MYSQL_PORT}.*--base-dir ${DATA_DIR}" >/dev/null; then
    echo "Server did not stop gracefully; sending SIGKILL."
    kill -9 $(pgrep -f "seekdb.*--port ${MYSQL_PORT}.*--base-dir ${DATA_DIR}")
  fi
fi

echo "[2/4] Remove only the old ftsbench data directory: ${DATA_DIR}"
rm -rf "${DATA_DIR}"

if [[ "${SKIP_BUILD}" == "1" ]]; then
  echo '[3/4] Reuse the existing debug binary (SKIP_BUILD=1)'
else
  echo "[3/4] Build current source (debug, ${BUILD_JOBS} jobs)"
  ./build.sh debug --make -j"${BUILD_JOBS}"
fi

BIN=build_debug/src/observer/seekdb
[[ -x "${BIN}" ]] || { echo "Build completed but ${BIN} is missing" >&2; exit 1; }

echo "[4/4] Deploy a fresh ftsbench instance on port ${MYSQL_PORT}"
# OBD successfully starts SeekDB, but its final readiness probe is for a
# traditional OceanBase cluster: it queries oceanbase.__all_server.  SeekDB
# intentionally does not expose that table, so OBD retries that probe forever
# after the server is already usable.  Put the deploy command in its own
# process group and use the benchmark's real readiness condition below.
setsid tools/deploy/obd.sh deploy -c tools/deploy/single.yaml -n "${DEPLOY_NAME}" \
  --seekdb "${BIN}" --exec-init-sql=0 &
deploy_pid="$!"
cleanup_deploy_waiter() {
  # The server has daemonized by this point; this only stops OBD's incompatible
  # readiness waiter and its wrapper, not the SeekDB server.
  kill -- "-${deploy_pid}" 2>/dev/null || true
  wait "${deploy_pid}" 2>/dev/null || true
}

for ((attempt = 1; attempt <= READY_TIMEOUT_SEC; attempt++)); do
  if mysql -h127.0.0.1 -P"${MYSQL_PORT}" -uroot -Nse 'SELECT 1' >/dev/null 2>&1; then
    cleanup_deploy_waiter
    echo "Ready: SeekDB is accepting connections on port ${MYSQL_PORT}."
    echo "Next: ./run_fts_large_bench.sh"
    exit 0
  fi
  if ! kill -0 "${deploy_pid}" 2>/dev/null; then
    wait "${deploy_pid}"
    exit 1
  fi
  sleep 1
done

cleanup_deploy_waiter
echo "SeekDB did not become ready on port ${MYSQL_PORT} within ${READY_TIMEOUT_SEC}s." >&2
exit 1
IN_CONTAINER
