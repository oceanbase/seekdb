#!/usr/bin/env bash

set -euo pipefail

echo "[oceanbase-build][ERROR] OceanBase Debian packaging is not supported by the Bazel-only build." >&2
echo "[oceanbase-build][ERROR] The supported Linux package is seekdb; use package/deb/seekdb-build.sh." >&2
exit 64
