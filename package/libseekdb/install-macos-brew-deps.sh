#!/usr/bin/env bash
# Install Homebrew deps for macOS libseekdb CI with pinned versions where drift broke darwin packs.
# CI was bundling thrift 0.23 while many dev machines use thrift@0.22 (see diagnose-packed-artifact.sh).
set -euo pipefail

export HOMEBREW_NO_ENV_HINTS=1

echo "[brew] installing build tools (thrift installed separately as thrift@0.22)"
brew install cmake dylibbundler googletest ccache pybind11 utf8proc re2 brotli bzip2

# GHA macos-14 images may ship unversioned thrift (0.23+). Unlink so thrift@0.22 is on PATH.
if brew list thrift &>/dev/null; then
  echo "[brew] unlinking unversioned thrift before pinning thrift@0.22"
  brew unlink thrift || true
fi

if ! brew list thrift@0.22 &>/dev/null; then
  brew install thrift@0.22
fi

brew link --force --overwrite thrift@0.22

echo "[brew] macOS libseekdb dependency versions:"
brew list --versions cmake dylibbundler re2 brotli utf8proc thrift@0.22 2>/dev/null || true
if command -v thrift &>/dev/null; then
  echo "[brew] active thrift: $(thrift -version 2>&1 || true)"
else
  echo "[brew] warning: thrift not on PATH after link" >&2
  exit 1
fi
