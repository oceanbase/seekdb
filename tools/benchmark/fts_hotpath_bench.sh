#!/usr/bin/env bash
# 全文索引分词热路径压测脚本
#
# 用途：对比优化前后 seekdb 的分词性能（主要在 TOKENIZE 热路径）
# 建议：分别在优化前/后的二进制上各跑一遍，对比输出的 avg_ms
#
# 用法:
#   ./tools/benchmark/fts_hotpath_bench.sh
#   ROUNDS=5000 ./tools/benchmark/fts_hotpath_bench.sh
#   MYSQL="mysql -h127.0.0.1 -P2881 -uroot" ./tools/benchmark/fts_hotpath_bench.sh

set -euo pipefail

MYSQL="${MYSQL:-mysql -h127.0.0.1 -P2881 -uroot -N -s}"
ROUNDS="${ROUNDS:-2000}"
WARMUP="${WARMUP:-50}"

# 中文长文本：ik 分词会走词典查找 + 停用词检查，最能体现热路径优化
IK_TEXT="OceanBase是一款非常稳定的数据库，全文索引的分词器热路径优化包括解析器实例复用、停用词全局单例、内存池化数据结构等技术，在大量文档索引和查询场景下可以显著降低 CPU 开销。"
# 英文文本：beng 分词
BENG_TEXT="The quick brown fox jumps over the lazy dog. Full-text search indexing requires efficient tokenization on the hot path."

run_tokenize_bench() {
  local parser="$1"
  local text="$2"
  local props="${3:-}"

  # 预热：加载词典、JIT 缓存等
  for ((i = 0; i < WARMUP; i++)); do
    if [[ -n "$props" ]]; then
      $MYSQL -e "SELECT tokenize('${text}', '${parser}', '${props}')" >/dev/null
    else
      $MYSQL -e "SELECT tokenize('${text}', '${parser}')" >/dev/null
    fi
  done

  local start end elapsed_ms avg_ms
  start=$(date +%s%N)
  for ((i = 0; i < ROUNDS; i++)); do
    if [[ -n "$props" ]]; then
      $MYSQL -e "SELECT tokenize('${text}', '${parser}', '${props}')" >/dev/null
    else
      $MYSQL -e "SELECT tokenize('${text}', '${parser}')" >/dev/null
    fi
  done
  end=$(date +%s%N)
  elapsed_ms=$(( (end - start) / 1000000 ))
  avg_ms=$(awk "BEGIN {printf \"%.3f\", ${elapsed_ms} / ${ROUNDS}}")
  echo "  parser=${parser}  rounds=${ROUNDS}  total_ms=${elapsed_ms}  avg_ms=${avg_ms}"
}

echo "=== FTS hot-path benchmark ==="
echo "mysql: ${MYSQL}"
echo "rounds=${ROUNDS}, warmup=${WARMUP}"
echo

# 确保连接可用
if ! $MYSQL -e "SELECT 1" >/dev/null 2>&1; then
  echo "ERROR: cannot connect to seekdb. Set MYSQL env, e.g.:"
  echo "  MYSQL='mysql -h127.0.0.1 -P2881 -uroot -p<password>' $0"
  exit 1
fi

echo "[1/3] ik tokenizer (Chinese, dict lookup hot path)"
run_tokenize_bench "ik" "$IK_TEXT"

echo "[2/3] beng tokenizer (English)"
run_tokenize_bench "beng" "$BENG_TEXT"

echo "[3/3] space tokenizer (English, baseline)"
run_tokenize_bench "space" "$BENG_TEXT"

echo
echo "Done. Run the same script on the baseline build and compare avg_ms."
echo "Functional check: mysql ... < tools/benchmark/fts_hotpath_bench.sql"
