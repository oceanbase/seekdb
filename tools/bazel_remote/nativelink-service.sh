#!/usr/bin/env bash

set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BINARY="${ROOT}/bin/nativelink"
readonly CONFIG="${ROOT}/config/nativelink.json5"
readonly LOG="${ROOT}/logs/nativelink.log"
readonly PID_FILE="${ROOT}/run/nativelink.pid"

is_running() {
  [[ -f "${PID_FILE}" ]] || return 1
  local pid
  pid="$(cat "${PID_FILE}")"
  [[ "${pid}" =~ ^[0-9]+$ ]] || return 1
  kill -0 "${pid}" 2>/dev/null || return 1
  [[ "$(readlink -f "/proc/${pid}/exe" 2>/dev/null)" == "$(readlink -f "${BINARY}")" ]]
}

start() {
  if is_running; then
    echo "NativeLink is already running (pid $(cat "${PID_FILE}"))"
    return
  fi
  [[ -x "${BINARY}" ]] || {
    echo "NativeLink binary is missing: ${BINARY}" >&2
    exit 1
  }
  [[ -f "${CONFIG}" ]] || {
    echo "NativeLink config is missing: ${CONFIG}" >&2
    exit 1
  }

  mkdir -p "${ROOT}/logs" "${ROOT}/run"
  cd "${ROOT}"
  nohup "${BINARY}" "${CONFIG}" >>"${LOG}" 2>&1 &
  local pid=$!
  printf '%s\n' "${pid}" >"${PID_FILE}"
  sleep 1
  if ! is_running; then
    echo "NativeLink failed to start; inspect ${LOG}" >&2
    exit 1
  fi
  echo "NativeLink started (pid ${pid})"
}

stop() {
  if ! is_running; then
    rm -f "${PID_FILE}"
    echo "NativeLink is not running"
    return
  fi

  local pid
  pid="$(cat "${PID_FILE}")"
  kill "${pid}"
  for ((attempt = 0; attempt < 50; ++attempt)); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      rm -f "${PID_FILE}"
      echo "NativeLink stopped"
      return
    fi
    sleep 0.1
  done
  echo "NativeLink did not stop cleanly (pid ${pid})" >&2
  exit 1
}

status() {
  if is_running; then
    echo "NativeLink is running (pid $(cat "${PID_FILE}"))"
  else
    echo "NativeLink is not running"
    return 1
  fi
}

case "${1:-}" in
  start)
    start
    ;;
  stop)
    stop
    ;;
  restart)
    stop
    start
    ;;
  status)
    status
    ;;
  *)
    echo "usage: $0 {start|stop|restart|status}" >&2
    exit 2
    ;;
esac
