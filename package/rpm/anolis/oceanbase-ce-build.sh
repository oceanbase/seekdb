#!/usr/bin/env bash

set -euo pipefail

echo "[anolis-oceanbase-ce-build][ERROR] The OceanBase CE Anolis package entry is retired." >&2
echo "[anolis-oceanbase-ce-build][ERROR] The Bazel-only build currently supports only the seekdb Linux package." >&2
exit 64
