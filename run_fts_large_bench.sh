#!/usr/bin/env bash
# Run the FTS large benchmark inside the development container.
#
# Usage:
#   ./run_fts_large_bench.sh
#   LABEL=before ROWS=50000 ./run_fts_large_bench.sh
#   LABEL=after SKIP_LOAD=1 ./run_fts_large_bench.sh
#   MYSQL_PORT=10001 ./run_fts_large_bench.sh
#
# The report is printed to stdout and, unless OUTPUT is supplied, is saved under
# tools/benchmark/results/ in the shared repository mount.

set -euo pipefail

CONTAINER="${CONTAINER:-seekdb-dev}"
WORKDIR="${WORKDIR:-/workspace/seekdb}"
MYSQL_PORT="${MYSQL_PORT:-10000}"

if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: docker CLI is required to run this benchmark." >&2
  exit 1
fi

if ! docker container inspect "${CONTAINER}" >/dev/null 2>&1; then
  echo "ERROR: container '${CONTAINER}' does not exist." >&2
  exit 1
fi

if [[ "$(docker container inspect --format '{{.State.Running}}' "${CONTAINER}")" != "true" ]]; then
  echo "ERROR: container '${CONTAINER}' is not running. Start it, then retry." >&2
  exit 1
fi

# Match the caller's host UID/GID so saved reports remain writable outside the
# bind-mounted container workspace as well.
docker_args=(exec --interactive --user "$(id -u):$(id -g)" --workdir "${WORKDIR}")
for name in LABEL ROWS BATCH ROUNDS QUERY_ROUNDS SAMPLES WARMUP SKIP_LOAD OUTPUT MYSQL MYSQL_VERBOSE; do
  if [[ -v "${name}" ]]; then
    docker_args+=(--env "${name}")
  fi
done

# tools/deploy/single.yaml uses port 10000, whereas the benchmark's standalone
# default is 2881.  Preserve explicit client commands supplied by the caller.
if [[ ! -v MYSQL ]]; then
  docker_args+=(--env "MYSQL=mysql -h127.0.0.1 -P${MYSQL_PORT} -uroot -N -s --default-character-set=utf8mb4")
fi
if [[ ! -v MYSQL_VERBOSE ]]; then
  docker_args+=(--env "MYSQL_VERBOSE=mysql -h127.0.0.1 -P${MYSQL_PORT} -uroot --default-character-set=utf8mb4")
fi
# The development image exports a root-only BASH_ENV. Clear it because this
# wrapper deliberately runs as the caller UID to keep generated reports owned
# by that caller.
docker_args+=(--env BASH_ENV=)

docker_args+=("${CONTAINER}" bash -c '
  set -euo pipefail
  if [[ ! -x "tools/benchmark/fts_large_bench.sh" ]]; then
    echo "ERROR: tools/benchmark/fts_large_bench.sh was not found in $(pwd)." >&2
    exit 1
  fi

  if [[ -z "${OUTPUT:-}" ]]; then
    OUTPUT="tools/benchmark/results/fts_large_bench_$(date +%Y%m%d_%H%M%S).txt"
  fi
  mkdir -p "$(dirname "${OUTPUT}")"
  export OUTPUT

  echo "Running tools/benchmark/fts_large_bench.sh"
  echo "Report: ${OUTPUT}"
  exec ./tools/benchmark/fts_large_bench.sh
')

exec docker "${docker_args[@]}"
