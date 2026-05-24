#!/usr/bin/env bash
# Install Homebrew deps for macOS libseekdb CI with pinned versions where drift broke darwin packs.
# CI was bundling thrift 0.23 while many dev machines use thrift@0.22 (see diagnose-packed-artifact.sh).
set -euo pipefail

brew install cmake dylibbundler googletest ccache pybind11 utf8proc re2 brotli bzip2 || true

# Pin thrift: unversioned `brew install thrift` tracks latest (0.23+) on GHA runners.
if brew list thrift@0.22 &>/dev/null; then
  brew link --force --overwrite thrift@0.22
else
  brew install thrift@0.22
  brew link --force --overwrite thrift@0.22
fi

echo "[brew] macOS libseekdb dependency versions:"
brew list --versions cmake dylibbundler thrift thrift@0.22 re2 brotli utf8proc 2>/dev/null || true
command -v thrift && thrift -version 2>/dev/null || true
