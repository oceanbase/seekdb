#!/usr/bin/env bash
set -euo pipefail
[ ! -d '/agent/repos/seekdb' ] || cd '/agent/repos/seekdb' || exit $?

export DEBIAN_FRONTEND=noninteractive
if ! dpkg -s libaio1t64 >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends \
    git wget curl rpm rpm2cpio cpio make build-essential binutils m4 file python3 libaio1t64
fi
# Ubuntu 24.04 ships libaio.so.1t64; seekdb links libaio.so.1 from bundled deps at runtime.
LIBAIO_DIR="/usr/lib/x86_64-linux-gnu"
if [[ ! -e "$LIBAIO_DIR/libaio.so.1" && -e "$LIBAIO_DIR/libaio.so.1t64" ]]; then
  sudo ln -sf libaio.so.1t64 "$LIBAIO_DIR/libaio.so.1"
fi

./build.py init
./build.py fetch --jobs=8 --lockfile_mode=error //src/observer:seekdb
./build.py build --nofetch --jobs=8 --lockfile_mode=error //src/observer:seekdb
