#!/usr/bin/env bash
# Run binding tests with exit-probe + SIGKILL grace (dylib/DLL unload hang workaround).
# Mirrors unittest/include/run-libseekdb-binding-tests.ps1 on Unix CI.
#
# BINDING_TEST_TIMEOUT_MS: wall-clock cap if the child never writes an exit probe
#   (e.g. stuck in seekdb_open). Normal runs finish in seconds; 5 min is generous.
# BINDING_EXIT_PROBE_GRACE_MS: wait after probe before SIGKILL (dylib unload hang).

BINDING_TEST_TIMEOUT_MS="${SEEKDB_BINDING_TEST_TIMEOUT_MS:-300000}"
BINDING_EXIT_PROBE_GRACE_MS="${SEEKDB_BINDING_EXIT_PROBE_GRACE_MS:-15000}"

# A timed-out child leaves no console output when the embedded engine hangs inside
# seekdb_open, so collect the state the process cannot print itself.
sample_hung_process() {
  local pid="${1:?pid required}"
  [[ "$(uname -s)" == "Darwin" ]] || return 0
  command -v sample >/dev/null 2>&1 || return 0
  local out="${TMPDIR:-/tmp}/seekdb_binding_sample_${pid}.txt"
  sample "$pid" 1 -file "$out" >/dev/null 2>&1 || true
  if [[ -f "$out" ]]; then
    # The "Call graph" section summarizes every thread; the per-thread details
    # that follow repeat the same frames and bury the blocked thread's stack.
    echo "::group::[seekdb-bind] stack sample of timed-out pid=${pid}"
    sed -n '/^Call graph:/,/^Total number in stack/p' "$out" | head -n 300 || true
    echo "::endgroup::"
  fi
  rm -f "$out" 2>/dev/null || true
}

dump_hang_probe_log() {
  local pid="${1:?pid required}"
  local probe_file="${TMPDIR:-/tmp}/seekdb_binding_hang_probe_${pid}.log"
  [[ -f "$probe_file" ]] || return 0
  echo "::group::[seekdb-bind] hang probe of timed-out pid=${pid}"
  cat "$probe_file" || true
  echo "::endgroup::"
}

dump_seekdb_startup_logs() {
  local log
  while IFS= read -r log; do
    [[ -n "$log" && -f "$log" ]] || continue
    echo "::group::[seekdb-bind] tail of ${log}"
    tail -n 150 "$log" || true
    echo "::endgroup::"
  done < <(find . -maxdepth 5 -name 'seekdb.log' -path '*/log/*' 2>/dev/null | head -10)
}

run_with_binding_exit_probe() {
  local _restore_e=0 _restore_m=0
  case $- in *e*) _restore_e=1 ;; esac
  case $- in *m*) _restore_m=1 ;; esac
  set +e
  set +m

  local timeout_ms="${1:?timeout_ms required}"
  local grace_ms="${2:?grace_ms required}"
  shift 2
  [[ "${1:-}" == "--" ]] && shift

  # Preload libseekdb into the launched process only. Exporting it here would
  # also reach the probe's own children (sleep, tail, find); a library that fails
  # to resolve its symbols would then kill them and hide the real failure.
  local -a preload_prefix=()
  if [[ "$(uname -s)" == "Linux" && -n "${SEEKDB_LIB_PATH:-}" && -f "${SEEKDB_LIB_PATH}" ]]; then
    preload_prefix=(env "LD_PRELOAD=${SEEKDB_LIB_PATH}${LD_PRELOAD:+:$LD_PRELOAD}")
  fi

  export SEEKDB_BINDING_EXIT_PROBE=1
  # Python records ordered checkpoints (SEEKDB_PROBE_LOG, named after the child
  # pid) so a timeout points at the exact ctypes call that never returned.
  export SEEKDB_BINDING_HANG_PROBE=1

  local probe_dir="${TMPDIR:-/tmp}"
  local probe=""
  local pid=""
  local exit_code=1
  local probe_seen=0
  local grace_polls=$(( grace_ms / 500 ))
  (( grace_polls < 1 )) && grace_polls=1
  local polls_after_probe=0
  local max_polls=$(( timeout_ms / 500 ))
  (( max_polls < 1 )) && max_polls=1
  local poll=0
  local wait_rc=0
  local rc=1

  # Bash 3.2 (macOS /bin/bash) and 4.2 raise "unbound variable" for an empty
  # array expansion under `set -u`, so expand only when a preload is needed.
  if (( ${#preload_prefix[@]} > 0 )); then
    "${preload_prefix[@]}" "$@" &
  else
    "$@" &
  fi
  pid=$!
  # Do not disown: a disowned job is dropped from the job table, so `wait` below
  # returns 0 instead of the child's status. A test binary that dies while the
  # loader resolves symbols (for example a missing symbol in libseekdb) would then
  # be reported as a pass.
  probe="${probe_dir}/seekdb_binding_exit_probe_${pid}.log"
  rm -f "$probe" 2>/dev/null || true

  while kill -0 "$pid" 2>/dev/null; do
    poll=$((poll + 1))
    if (( poll > max_polls )); then
      echo "::error::binding test exceeded ${timeout_ms}ms; killing pid=${pid} ($*)" >&2
      dump_hang_probe_log "$pid"
      sample_hung_process "$pid"
      dump_seekdb_startup_logs
      kill -9 "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      rc=124
      ((_restore_m)) && set -m || true
      ((_restore_e)) && set -e
      return "$rc"
    fi

    if [[ -f "$probe" ]]; then
      local line
      line="$(grep -E 'before_process_exit code=' "$probe" 2>/dev/null | tail -1 || true)"
      if [[ -n "$line" ]]; then
        exit_code="${line##*code=}"
        if (( probe_seen == 0 )); then
          probe_seen=1
          polls_after_probe=0
          echo "::notice::[seekdb-bind] binding exit probe seen pid=${pid} code=${exit_code} (${grace_ms}ms grace before SIGKILL if stuck in native unload)"
        else
          polls_after_probe=$((polls_after_probe + 1))
          if (( polls_after_probe >= grace_polls )); then
            echo "::notice::[seekdb-bind] forcing SIGKILL pid=${pid} (native teardown hang workaround)"
            kill -9 "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            rm -f "$probe" 2>/dev/null || true
            rc="$exit_code"
            ((_restore_m)) && set -m || true
            ((_restore_e)) && set -e
            return "$rc"
          fi
        fi
      fi
    fi
    sleep 0.5
  done

  wait_rc=0
  wait "$pid" 2>/dev/null || wait_rc=$?
  if [[ -f "$probe" ]]; then
    local line
    line="$(grep -E 'before_process_exit code=' "$probe" 2>/dev/null | tail -1 || true)"
    if [[ -n "$line" ]]; then
      exit_code="${line##*code=}"
      rm -f "$probe" 2>/dev/null || true
      rc="$exit_code"
      ((_restore_m)) && set -m || true
      ((_restore_e)) && set -e
      return "$rc"
    fi
  fi
  rm -f "$probe" 2>/dev/null || true
  rc="$wait_rc"
  # A child killed by a signal (>=128) never wrote an exit probe, so report the
  # ordered checkpoints and the engine's own log; a bare "Segmentation fault"
  # line in CI says nothing about how far startup got.
  if (( rc >= 128 )); then
    echo "::error::[seekdb-bind] binding test pid=${pid} died from a signal (rc=${rc}): $*" >&2
    dump_hang_probe_log "$pid"
    dump_seekdb_startup_logs
  fi
  ((_restore_m)) && set -m || true
  ((_restore_e)) && set -e
  return "$rc"
}

run_node_with_binding_exit_probe() {
  local timeout_ms="${1:?timeout_ms required}"
  local grace_ms="${2:?grace_ms required}"
  shift 2
  [[ "${1:-}" == "--" ]] && shift
  export SEEKDB_NODE_BINDING_PROBE=1
  run_with_binding_exit_probe "$timeout_ms" "$grace_ms" -- node "$@"
}
