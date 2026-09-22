#!/usr/bin/env bash

set -euo pipefail

usage()
{
  cat <<'EOF'
Usage: replay_submit_iterator_integration.sh --observer <path> [options] <case> [case ...]

Run the replay submit-iterator integration cases in an isolated Docker
container. Supported cases are:
  stanby/replay_submit_iterator_release_deadline
  stanby/standby_sstable_replay
  stanby/switchover_roundtrip

Options:
  --observer <path>    seekdb/observer binary to test (required)
  --image <name>       Docker image with the obtest runtime dependencies
  --port-base <port>   Base port for the first cluster (default: 58000)
  --runtime-root <dir> Preserve test data and logs in this directory
  -h, --help           Show this help

Environment:
  OBTEST_CONTAINER_IMAGE  Default image when --image is omitted
  OBTEST_HOST_IP          Host address visible from a host-network container
  OBTEST_HOST_DEV         Network device that owns OBTEST_HOST_IP
EOF
}

fail()
{
  echo "ERROR: $*" >&2
  exit 1
}

current_container=""
cleanup_container()
{
  if [[ -n "$current_container" ]]; then
    docker rm -f "$current_container" >/dev/null 2>&1 || true
  fi
}
trap cleanup_container EXIT
trap 'cleanup_container; exit 130' INT TERM

observer=""
image="${OBTEST_CONTAINER_IMAGE:-wa-seekdb-obtest:session1789547536290}"
port_base=58000
runtime_root=""
cases=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --observer)
      [[ $# -ge 2 ]] || fail "--observer requires a path"
      observer=$2
      shift 2
      ;;
    --image)
      [[ $# -ge 2 ]] || fail "--image requires a name"
      image=$2
      shift 2
      ;;
    --port-base)
      [[ $# -ge 2 ]] || fail "--port-base requires a port"
      port_base=$2
      shift 2
      ;;
    --runtime-root)
      [[ $# -ge 2 ]] || fail "--runtime-root requires a directory"
      runtime_root=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      cases+=("$@")
      break
      ;;
    -*)
      fail "unknown option: $1"
      ;;
    *)
      cases+=("$1")
      shift
      ;;
  esac
done

[[ -n "$observer" ]] || fail "--observer is required"
[[ -x "$observer" ]] || fail "observer is not executable: $observer"
[[ ${#cases[@]} -gt 0 ]] || fail "at least one case is required"
[[ "$port_base" =~ ^[0-9]+$ ]] || fail "invalid port base: $port_base"
(( port_base >= 1024 && port_base <= 62000 )) || fail "port base must be in [1024, 62000]"

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../../.." && pwd)
observer=$(realpath "$observer")
build_root=$(cd "$(dirname "$observer")/../.." && pwd)

[[ -f "$build_root/CMakeCache.txt" ]] || fail "cannot locate CMakeCache.txt under $build_root"
command -v docker >/dev/null || fail "docker is required"
command -v rsync >/dev/null || fail "rsync is required"
docker image inspect "$image" >/dev/null 2>&1 || fail "Docker image is unavailable: $image"
container_user=$(docker run --rm --user "$(id -u):$(id -g)" "$image" id -un)
[[ -n "$container_user" ]] || fail "could not determine the container user name"

for test_case in "${cases[@]}"; do
  case "$test_case" in
    stanby/replay_submit_iterator_release_deadline)
      grep -q '^ENABLE_SANITY:BOOL=ON$' "$build_root/CMakeCache.txt" \
        || fail "$test_case requires an ENABLE_SANITY build"
      ;;
    stanby/standby_sstable_replay|stanby/switchover_roundtrip)
      ;;
    *)
      fail "unsupported case: $test_case"
      ;;
  esac
  [[ -f "$repo_root/tools/obtest/t/$test_case.test" ]] \
    || fail "missing test file: tools/obtest/t/$test_case.test"
  [[ -f "$repo_root/tools/obtest/r/$test_case.result" ]] \
    || fail "missing result file: tools/obtest/r/$test_case.result"
done

if [[ -z "$runtime_root" ]]; then
  runtime_parent="$build_root/wa_replay_submit_iterator_obtest"
  mkdir -p "$runtime_parent"
  runtime_root=$(mktemp -d "$runtime_parent/run.XXXXXX")
else
  mkdir -p "$runtime_root"
  runtime_root=$(cd "$runtime_root" && pwd)
fi

test_root="$runtime_root/oceanbase/tools/obtest"
mkdir -p "$test_root"
rsync -a \
  --exclude '/bin/' \
  --exclude '/collected_log/' \
  --exclude '/log/' \
  --exclude '/var/' \
  "$repo_root/tools/obtest/" "$test_root/"
mkdir -p \
  "$test_root/bin" \
  "$test_root/collected_log" \
  "$test_root/log" \
  "$test_root/var" \
  "$runtime_root/backup" \
  "$runtime_root/backup_backup" \
  "$runtime_root/clog" \
  "$runtime_root/container-data1" \
  "$runtime_root/data" \
  "$runtime_root/tmp"
cp -p "$observer" "$test_root/bin/observer"

host_ip="${OBTEST_HOST_IP:-}"
host_dev="${OBTEST_HOST_DEV:-}"
if [[ -z "$host_ip" || -z "$host_dev" ]]; then
  route_line=$(ip -4 route get 1.1.1.1 | head -1)
  [[ -n "$host_ip" ]] || host_ip=$(awk '{for (i=1; i<=NF; ++i) if ($i == "src") {print $(i+1); exit}}' <<<"$route_line")
  [[ -n "$host_dev" ]] || host_dev=$(awk '{for (i=1; i<=NF; ++i) if ($i == "dev") {print $(i+1); exit}}' <<<"$route_line")
fi
[[ -n "$host_ip" ]] || fail "could not determine host IP; set OBTEST_HOST_IP"
[[ -n "$host_dev" ]] || fail "could not determine host device; set OBTEST_HOST_DEV"

cat >"$test_root/conf/configure.ini" <<EOF
1.obs_hosts = $host_ip
2.obs_hosts = $host_ip
1:1.obs_hosts = $host_ip
2:1.obs_hosts = $host_ip
data_path = $runtime_root/data
clog_path = $runtime_root/clog
workdir = $runtime_root
user = $container_user
port_base = $port_base
cleanup = 1
cleanup_after = 1
save_log = 1
save_core = 0
gen_pstack = 0
enable_obesi = 0
memory_size_limit = 6G
init_unit_mem = 2G
node_cpu_count = 4
node_cpu_reserved = 0
log_disk_size = 4G
disk_avail_space = 10G
server_log_level = info
dev = $host_dev
use_ob_connector = 1
ps = 0
backup_restore_mode = 1
file_dir = $runtime_root/backup
file_dir_1 = $runtime_root/backup_backup
extra_tenant_params = ob_compaction_schedule_interval = '3s',major_freeze_duty_time = 'disable'
EOF

java_home=/usr/lib/jvm/java-1.8.0-openjdk-1.8.0.482.b08-1.0.1.1.al8.x86_64/jre
[[ -x "$java_home/bin/java" ]] || fail "required Java runtime is unavailable: $java_home/bin/java"
[[ -f /usr/share/javazi-1.8/tzdb.dat ]] || fail "required Java timezone database is unavailable"

echo "observer=$observer"
echo "observer_sha256=$(sha256sum "$observer" | awk '{print $1}')"
echo "build_root=$build_root"
echo "runtime_root=$runtime_root"
echo "container_image=$image"
echo "host=$host_ip dev=$host_dev port_base=$port_base"

docker_allocator_args=()
if grep -q '^ENABLE_SANITY:BOOL=ON$' "$build_root/CMakeCache.txt"; then
  # The deadline case needs ERRSIM scheduling, not jemalloc shadow checking.
  # Keep the Sanity binary while selecting the other supported allocator so
  # unrelated allocator-shadow findings cannot prevent the server from booting.
  docker_allocator_args+=(--env MALLOC_BACKEND=obmalloc)
  echo "allocator_backend=obmalloc (Sanity/ERRSIM observer)"
fi

for test_case in "${cases[@]}"; do
  log_name=${test_case//\//.}
  run_log="$runtime_root/$log_name.log"
  : >"$run_log"
  container_name="wa-rsi-$(basename "$runtime_root")-${test_case//\//-}"
  current_container=$container_name
  echo "Running $test_case"
  set +e
  docker run --rm --name "$container_name" \
    --network host \
    --user "$(id -u):$(id -g)" \
    --env HOME=/home/wauser \
    --env TMPDIR="$runtime_root/tmp" \
    --env PATH="$java_home/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    "${docker_allocator_args[@]}" \
    --volume "$repo_root:$repo_root" \
    --volume /usr/lib/jvm:/usr/lib/jvm:ro \
    --volume /usr/share/javazi-1.8:/usr/share/javazi-1.8:ro \
    --volume "$runtime_root/container-data1:/data/1" \
    --workdir "$test_root" \
    "$image" \
    bash -lc "./mytest '$test_case' --conf=conf/configure.ini" \
    2>&1 | tee "$run_log" &
  pipeline_pid=$!
  set -e

  # Some MyTest versions retain non-daemon cleanup threads after ResultHandler
  # has compared the complete result and emitted TestCase "test success".  At
  # that point all assertions are final, so stop the isolated container rather
  # than leaving the runner and its observer processes alive indefinitely.
  while kill -0 "$pipeline_pid" 2>/dev/null; do
    if grep -q 'TestCase.*test success' "$run_log"; then
      docker stop --time 1 "$container_name" >/dev/null 2>&1 || true
      break
    elif grep -qE 'MyTest.*test failed|the result is not expected' "$run_log"; then
      docker stop --time 1 "$container_name" >/dev/null 2>&1 || true
      break
    fi
    sleep 1
  done

  set +e
  wait "$pipeline_pid"
  pipeline_rc=$?
  set -e
  current_container=""
  grep -q 'TestCase.*test success' "$run_log" \
    || fail "$test_case did not report test success (runner exit $pipeline_rc)"
  echo "PASS $test_case log=$run_log"
done

echo "All requested replay submit-iterator integration cases passed"
