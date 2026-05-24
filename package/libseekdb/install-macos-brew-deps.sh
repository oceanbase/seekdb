#!/usr/bin/env bash
# Install Homebrew deps for macOS libseekdb CI with pinned versions where drift broke darwin packs.
# CI was bundling thrift 0.23 while many dev machines use thrift@0.22 (see diagnose-packed-artifact.sh).
set -euo pipefail

export HOMEBREW_NO_ENV_HINTS=1

echo "[brew] installing build tools (thrift pinned separately)"
brew install cmake dylibbundler googletest ccache pybind11 utf8proc re2 brotli bzip2

install_thrift_0_22() {
  # macOS dev laptops often have thrift@0.22; GHA Homebrew core may only ship unversioned thrift (0.23+).
  if brew list thrift &>/dev/null; then
    echo "[brew] unlinking unversioned thrift before pin"
    brew unlink thrift || true
  fi

  if brew list thrift@0.22 &>/dev/null; then
    brew link --force --overwrite thrift@0.22
    return 0
  fi

  if brew install thrift@0.22 2>/dev/null; then
    brew link --force --overwrite thrift@0.22
    return 0
  fi

  echo "[brew] thrift@0.22 not in core tap; extracting apache thrift 0.22.0"
  brew tap-new seekdb/local --no-git 2>/dev/null || true
  if ! brew list "seekdb/local/thrift@0.22" &>/dev/null 2>&1; then
    brew extract --version=0.22.0 thrift seekdb/local
    brew install "seekdb/local/thrift@0.22"
  fi
  brew link --force --overwrite "seekdb/local/thrift@0.22" 2>/dev/null \
    || brew link --force --overwrite thrift@0.22
}

install_thrift_0_22

echo "[brew] macOS libseekdb dependency versions:"
brew list --versions cmake dylibbundler re2 brotli utf8proc 2>/dev/null || true
brew list --versions thrift thrift@0.22 2>/dev/null || true
brew list --versions seekdb/local/thrift@0.22 2>/dev/null || true

if command -v thrift &>/dev/null; then
  echo "[brew] active thrift: $(thrift -version 2>&1 || true)"
else
  echo "[brew] error: thrift not on PATH after install" >&2
  exit 1
fi
