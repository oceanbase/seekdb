#!/usr/bin/env bash
# Collect result and write seekdb_result.json for native execution.
# 参考 farm-jenkins: scripts/opensource/scripts/opensource_publish_result.sh 的结论汇总方式。
set -e

WORKSPACE="${GITHUB_WORKSPACE:-.}"
TASK_DIR="${SEEKDB_TASK_DIR:-$WORKSPACE/seekdb_build/$GITHUB_RUN_ID}"
OUT_JSON="${1:-$WORKSPACE/seekdb_result.json}"

# Success if no fail_cases or empty; otherwise fail
FAILED=""
if [[ -f "$TASK_DIR/fail_cases.output" ]] && [[ -s "$TASK_DIR/fail_cases.output" ]]; then
  FAILED=$(cat "$TASK_DIR/fail_cases.output" | tr '\n' ' ' | sed 's/"/\\"/g')
  SUCCESS="false"
else
  SUCCESS="true"
fi

cat > "$OUT_JSON" << EOF
{
  "success": ${SUCCESS},
  "task_id": "${GITHUB_RUN_ID:-native}",
  "output_url": "",
  "native": true,
  "failed_cases": "${FAILED//\"/\\\"}"
}
EOF
echo "Wrote $OUT_JSON"
cat "$OUT_JSON"
