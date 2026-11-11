#!/usr/bin/env bash
set -euo pipefail
# Keep the historical CI entrypoint; the implementation lives with its spec.
exec bash "$(dirname "$0")/../package/rpm/seekdb-build.sh" "$@"
