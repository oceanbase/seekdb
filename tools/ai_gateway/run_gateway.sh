#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
config_path="${AI_GATEWAY_CONFIG:-${script_dir}/config.json}"

exec python3 "${script_dir}/gateway.py" --config "${config_path}" serve
