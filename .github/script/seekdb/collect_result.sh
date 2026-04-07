#!/usr/bin/env bash
# Collect result: read fail_cases.output and write seekdb_result.json.
# Usage: collect_result.sh <output_json_path>
# Required env: GITHUB_WORKSPACE, GITHUB_RUN_ID, SEEKDB_TASK_DIR
set -e

OUT_PATH="${1:-$GITHUB_WORKSPACE/seekdb_result.json}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"
WORKSPACE="${GITHUB_WORKSPACE:-.}"

FAIL_FILE="$TASK_DIR/fail_cases.output"

failed_cases=()
if [[ -f "$FAIL_FILE" ]] && [[ -s "$FAIL_FILE" ]]; then
  while IFS= read -r line; do
    [[ -n "$line" ]] && failed_cases+=("$line")
  done < "$FAIL_FILE"
fi

success="true"
[[ ${#failed_cases[@]} -gt 0 ]] && success="false"

# Build JSON (escape quotes in case names)
cs=""
for c in "${failed_cases[@]}"; do
  esc="${c//\"/\\\"}"
  [[ -n "$cs" ]] && cs="$cs,"
  cs="$cs\"$esc\""
done
echo "{\"success\":$success,\"run_id\":\"${GITHUB_RUN_ID:-0}\",\"failed_cases\":[$cs]}" > "$OUT_PATH"

echo "[collect_result.sh] Result written to $OUT_PATH (success=$success, failed=${#failed_cases[@]})."c