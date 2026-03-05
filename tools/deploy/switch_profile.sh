#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-}"
RESTART_CLUSTER="${2:-yes}"

if [[ -z "${MODE}" ]]; then
  echo "Usage: $0 <perf|comp> [yes|no]"
  echo "  perf: high-throughput local experiment profile"
  echo "  comp: competition-compliant profile (<=11G, 8C)"
  echo "  second arg: restart cluster after apply (default: yes)"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEEKDB_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OBCLIENT="${SEEKDB_ROOT}/deps/3rd/u01/obclient/bin/obclient"
OBD_SH="${SEEKDB_ROOT}/tools/deploy/obd.sh"

if [[ ! -x "${OBCLIENT}" ]]; then
  echo "obclient not found: ${OBCLIENT}"
  exit 1
fi

case "${MODE}" in
  perf)
    MEMORY_LIMIT="128G"
    SYSTEM_MEMORY="8G"
    CPU_COUNT="64"
    WORKERS_PER_CPU_QUOTA="20"
    NET_THREAD_COUNT="16"
    ;;
  comp)
    MEMORY_LIMIT="10G"
    SYSTEM_MEMORY="1G"
    CPU_COUNT="8"
    WORKERS_PER_CPU_QUOTA="10"
    NET_THREAD_COUNT="4"
    ;;
  *)
    echo "Unknown mode: ${MODE}"
    echo "Usage: $0 <perf|comp> [yes|no]"
    exit 1
    ;;
esac

echo "[switch_profile] applying mode=${MODE}"
"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot@sys -A -D oceanbase -e "
alter system set memory_limit='${MEMORY_LIMIT}';
alter system set system_memory='${SYSTEM_MEMORY}';
alter system set cpu_count=${CPU_COUNT};
alter system set workers_per_cpu_quota=${WORKERS_PER_CPU_QUOTA};
alter system set net_thread_count=${NET_THREAD_COUNT};
"

if [[ "${RESTART_CLUSTER}" == "yes" ]]; then
  echo "[switch_profile] restarting cluster to apply static params"
  (cd "${SEEKDB_ROOT}" && bash "${OBD_SH}" restart -n seekdb)
fi

echo "[switch_profile] effective parameters:"
"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot@sys -A -N -D oceanbase -e "
show parameters where name in (
  'memory_limit',
  'system_memory',
  'cpu_count',
  'workers_per_cpu_quota',
  'net_thread_count'
);
"

echo "[switch_profile] observer rss:"
ps -C observer -o pid,pcpu,pmem,rss,vsz,etime,cmd --sort=-pcpu | head -n 5
