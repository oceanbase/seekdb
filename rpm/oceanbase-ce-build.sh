#!/usr/bin/env bash

set -euo pipefail

echo "[oceanbase-ce-build][ERROR] OceanBase CE RPM packaging is not supported by the Bazel-only build." >&2
echo "[oceanbase-ce-build][ERROR] The supported Linux package is seekdb; use rpm/seekdb-build.sh." >&2
exit 64
