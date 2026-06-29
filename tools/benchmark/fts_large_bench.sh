#!/usr/bin/env bash
# 大规模全文索引压测：数据加载 + TOKENIZE 热路径 + MATCH 查询
#
# 用于优化前后 seekdb 对比。建议固定 ROWS/ROUNDS/QUERY_ROUNDS，分别打 LABEL。
#
# 用法:
#   ./tools/benchmark/fts_large_bench.sh
#   LABEL=before ROWS=50000 ./tools/benchmark/fts_large_bench.sh
#   LABEL=after  SKIP_LOAD=1 ./tools/benchmark/fts_large_bench.sh   # 仅测查询/分词
#   OUTPUT=./bench_result.txt LABEL=after ./tools/benchmark/fts_large_bench.sh
#
# 环境变量:
#   MYSQL          mysql 客户端命令（默认 -h127.0.0.1 -P2881 -uroot -N -s）
#   LABEL          结果标签，如 before / after（默认 unknown）
#   ROWS           插入文档数（默认 20000）
#   BATCH          每批 INSERT 行数（默认 500）
#   ROUNDS         TOKENIZE 压测轮次（默认 3000）
#   QUERY_ROUNDS   每条 MATCH SQL 重复次数（默认 200）
#   WARMUP         TOKENIZE 预热轮次（默认 30）
#   SKIP_LOAD      设为 1 跳过建库灌数，仅跑分词/查询压测
#   OUTPUT         结果追加写入的文件（可选）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MYSQL="${MYSQL:-mysql -h127.0.0.1 -P2881 -uroot -N -s}"
MYSQL_VERBOSE="${MYSQL_VERBOSE:-mysql -h127.0.0.1 -P2881 -uroot}"
LABEL="${LABEL:-unknown}"
ROWS="${ROWS:-20000}"
BATCH="${BATCH:-500}"
ROUNDS="${ROUNDS:-3000}"
QUERY_ROUNDS="${QUERY_ROUNDS:-200}"
WARMUP="${WARMUP:-30}"
SKIP_LOAD="${SKIP_LOAD:-0}"
OUTPUT="${OUTPUT:-}"

IK_TEXT="OceanBase是一款非常稳定的数据库，全文索引的分词器热路径优化包括解析器实例复用、停用词全局单例、内存池化数据结构等技术，在大量文档索引和查询场景下可以显著降低 CPU 开销。"
BENG_TEXT="The quick brown fox jumps over the lazy dog. Full-text search indexing requires efficient tokenization on the hot path."

declare -A METRICS

log() { echo "$@" >&2; }

now_ms() { date +%s%N | awk '{print int($1/1000000)}'; }

mysql_exec() { $MYSQL -e "$1"; }

run_tokenize_bench() {
  local name="$1" parser="$2" text="$3"
  local i start end elapsed_ms avg_ms

  for ((i = 0; i < WARMUP; i++)); do
    mysql_exec "SELECT tokenize('${text}', '${parser}')" >/dev/null
  done

  start=$(now_ms)
  for ((i = 0; i < ROUNDS; i++)); do
    mysql_exec "SELECT tokenize('${text}', '${parser}')" >/dev/null
  done
  end=$(now_ms)
  elapsed_ms=$((end - start))
  avg_ms=$(awk "BEGIN {printf \"%.4f\", ${elapsed_ms} / ${ROUNDS}}")
  METRICS["${name}_total_ms"]=$elapsed_ms
  METRICS["${name}_avg_ms"]=$avg_ms
  log "  ${name}: rounds=${ROUNDS} total_ms=${elapsed_ms} avg_ms=${avg_ms}"
}

run_query_bench() {
  local name="$1" sql="$2"
  local i start end elapsed_ms avg_ms cnt

  # 预热 1 次
  cnt=$(mysql_exec "$sql" | tail -1)
  : "${cnt:=0}"

  start=$(now_ms)
  for ((i = 0; i < QUERY_ROUNDS; i++)); do
    mysql_exec "$sql" >/dev/null
  done
  end=$(now_ms)
  elapsed_ms=$((end - start))
  avg_ms=$(awk "BEGIN {printf \"%.4f\", ${elapsed_ms} / ${QUERY_ROUNDS}}")
  METRICS["${name}_total_ms"]=$elapsed_ms
  METRICS["${name}_avg_ms"]=$avg_ms
  METRICS["${name}_warmup_hits"]=$cnt
  log "  ${name}: rounds=${QUERY_ROUNDS} warmup_hits=${cnt} total_ms=${elapsed_ms} avg_ms=${avg_ms}"
}

write_report() {
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  {
    echo "========================================"
    echo "FTS Large Benchmark Report"
    echo "========================================"
    echo "timestamp:     ${ts}"
    echo "label:         ${LABEL}"
    echo "rows:          ${ROWS}"
    echo "batch:         ${BATCH}"
    echo "rounds:        ${ROUNDS}"
    echo "query_rounds:  ${QUERY_ROUNDS}"
    echo "skip_load:     ${SKIP_LOAD}"
    echo "----------------------------------------"
    echo "load_total_sec:        ${METRICS[load_total_sec]:-N/A}"
    echo "load_rows_per_sec:     ${METRICS[load_rows_per_sec]:-N/A}"
    echo "tokenize_ik_avg_ms:    ${METRICS[tokenize_ik_avg_ms]:-N/A}"
    echo "tokenize_beng_avg_ms:  ${METRICS[tokenize_beng_avg_ms]:-N/A}"
    echo "query_cn_avg_ms:       ${METRICS[query_cn_avg_ms]:-N/A}"
    echo "query_en_avg_ms:       ${METRICS[query_en_avg_ms]:-N/A}"
    echo "query_mixed_avg_ms:    ${METRICS[query_mixed_avg_ms]:-N/A}"
    echo "query_limit_avg_ms:    ${METRICS[query_limit_avg_ms]:-N/A}"
    echo "========================================"
  }
}

if ! $MYSQL -e "SELECT 1" >/dev/null 2>&1; then
  log "ERROR: cannot connect seekdb. Example:"
  log "  MYSQL='mysql -h127.0.0.1 -P2881 -uroot' $0"
  exit 1
fi

log "=== FTS Large Benchmark (label=${LABEL}) ==="
log "rows=${ROWS} batch=${BATCH} rounds=${ROUNDS} query_rounds=${QUERY_ROUNDS}"

if [[ "${SKIP_LOAD}" != "1" ]]; then
  log "[1/4] setup schema ..."
  $MYSQL_VERBOSE < "${SCRIPT_DIR}/fts_large_bench_setup.sql" >/dev/null

  log "[2/4] bulk load ${ROWS} docs (batch=${BATCH}) ..."
  load_start=$(date +%s.%N)
  python3 "${SCRIPT_DIR}/fts_large_bench_gen.py" --rows "${ROWS}" --batch "${BATCH}" | $MYSQL_VERBOSE >/dev/null
  load_end=$(date +%s.%N)
  load_sec=$(awk "BEGIN {printf \"%.3f\", ${load_end} - ${load_start}}")
  rps=$(awk "BEGIN {printf \"%.1f\", ${ROWS} / ${load_sec}}")
  METRICS[load_total_sec]=$load_sec
  METRICS[load_rows_per_sec]=$rps
  log "  load done: ${load_sec}s (${rps} rows/s)"

  loaded=$(mysql_exec "USE fts_large_bench; SELECT COUNT(*) FROM docs;" | tail -1)
  log "  table rows: ${loaded}"
else
  log "[1-2/4] SKIP_LOAD=1, reuse existing fts_large_bench.docs"
  loaded=$(mysql_exec "USE fts_large_bench; SELECT COUNT(*) FROM docs;" 2>/dev/null | tail -1 || echo "0")
  if [[ "${loaded}" == "0" ]]; then
    log "ERROR: fts_large_bench.docs is empty. Run without SKIP_LOAD first."
    exit 1
  fi
  log "  table rows: ${loaded}"
  ROWS=$loaded
fi

log "[3/4] tokenize hot-path ..."
run_tokenize_bench "tokenize_ik" "ik" "${IK_TEXT}"
run_tokenize_bench "tokenize_beng" "beng" "${BENG_TEXT}"

log "[4/4] MATCH query ..."
DB_PREFIX="USE fts_large_bench;"
run_query_bench "query_cn" \
  "${DB_PREFIX} SELECT COUNT(*) FROM docs WHERE MATCH(title, content) AGAINST('全文索引 分词器');"
run_query_bench "query_en" \
  "${DB_PREFIX} SELECT COUNT(*) FROM docs WHERE MATCH(title, content) AGAINST('database performance audit');"
run_query_bench "query_mixed" \
  "${DB_PREFIX} SELECT COUNT(*) FROM docs WHERE MATCH(title, content) AGAINST('OceanBase 稳定 database');"
run_query_bench "query_limit" \
  "${DB_PREFIX} SELECT COUNT(*) FROM (SELECT id FROM docs WHERE MATCH(content) AGAINST('倒排索引 tokenizer') LIMIT 20) t;"

report=$(write_report)
echo "${report}"
if [[ -n "${OUTPUT}" ]]; then
  echo "${report}" >> "${OUTPUT}"
  log "Report appended to ${OUTPUT}"
fi

log ""
log "Compare before/after on these fields (lower is better for *_avg_ms and load_total_sec):"
log "  tokenize_ik_avg_ms, tokenize_beng_avg_ms"
log "  load_total_sec / load_rows_per_sec"
log "  query_cn_avg_ms, query_en_avg_ms, query_mixed_avg_ms, query_limit_avg_ms"
