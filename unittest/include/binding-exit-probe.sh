#!/usr/bin/env bash
# Run binding tests with exit-probe + SIGKILL grace (dylib/DLL unload hang workaround).
# Mirrors unittest/include/run-libseekdb-binding-tests.ps1 on Unix CI.
#
# BINDING_TEST_TIMEOUT_MS: wall-clock cap if the child never writes an exit probe
#   (e.g. stuck in seekdb_open). Normal runs finish in seconds; 5 min is generous.
# BINDING_EXIT_PROBE_GRACE_MS: wait after probe before SIGKILL (dylib unload hang).

BINDING_TEST_TIMEOUT_MS="${SEEKDB_BINDING_TEST_TIMEOUT_MS:-300000}"
BINDING_EXIT_PROBE_GRACE_MS="${SEEKDB_BINDING_EXIT_PROBE_GRACE_MS:-15000}"

# libseekdb is whole-archive linked; loading it via dlopen/ctypes/JNI after the
# interpreter starts can exceed glibc's static TLS block on Linux.
apply_linux_libseekdb_preload() {
  local lib_path="${1:?lib path required}"
  if [[ "$(uname -s)" == "Linux" && -f "$lib_path" ]]; then
    export LD_PRELOAD="${lib_path}${LD_PRELOAD:+:$LD_PRELOAD}"
  fi
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

  if [[ -n "${SEEKDB_LIB_PATH:-}" ]]; then
    apply_linux_libseekdb_preload "${SEEKDB_LIB_PATH}"
  fi

  export SEEKDB_BINDING_EXIT_PROBE=1

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

  "$@" &
  pid=$!
  disown "$pid" 2>/dev/null || true
  probe="${probe_dir}/seekdb_binding_exit_probe_${pid}.log"
  rm -f "$probe" 2>/dev/null || true

  while kill -0 "$pid" 2>/dev/null; do
    poll=$((poll + 1))
    if (( poll > max_polls )); then
      echo "::error::binding test exceeded ${timeout_ms}ms; killing pid=${pid} ($*)" >&2
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
