#!/usr/bin/env bash
# Install Homebrew deps for macOS libseekdb CI with pinned versions where drift broke darwin packs.
# CI was bundling thrift 0.23 while many dev machines use thrift@0.22 (see diagnose-packed-artifact.sh).
set -euo pipefail

export HOMEBREW_NO_ENV_HINTS=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THRIFT_FORMULA="$SCRIPT_DIR/homebrew-local/Formula/thrift@0.22.rb"

echo "[brew] installing build tools (thrift 0.22 pinned separately)"
brew update
brew install cmake dylibbundler googletest ccache pybind11 utf8proc re2 brotli bzip2

install_thrift_0_22() {
  if brew list thrift@0.22 &>/dev/null; then
    brew link --force --overwrite thrift@0.22
    return 0
  fi

  if brew list thrift &>/dev/null; then
    echo "[brew] unlinking unversioned thrift (0.23+) before pin"
    brew unlink thrift || true
  fi

  set +e
  brew install thrift@0.22
  core_rc=$?
  set -e
  if [[ "$core_rc" -eq 0 ]]; then
    brew link --force --overwrite thrift@0.22
    return 0
  fi

  echo "[brew] core thrift@0.22 unavailable; installing via seekdb/local tap (tap-new)"
  if [[ ! -f "$THRIFT_FORMULA" ]]; then
    echo "[brew] error: missing $THRIFT_FORMULA" >&2
    exit 1
  fi

  brew install bison boost openssl@3
  export PATH="/opt/homebrew/opt/bison/bin:${PATH:-}"

  brew untap seekdb/local 2>/dev/null || true
  brew tap-new seekdb/local --no-git
  local tap_formula
  tap_formula="$(brew --repository seekdb/local)/Formula/thrift@0.22.rb"
  mkdir -p "$(dirname "$tap_formula")"
  cp "$THRIFT_FORMULA" "$tap_formula"

  brew install seekdb/local/thrift@0.22
  brew link --force --overwrite thrift@0.22
}

install_thrift_0_22

echo "[brew] macOS libseekdb dependency versions:"
brew list --versions cmake dylibbundler re2 brotli utf8proc thrift@0.22 2>/dev/null || true

if command -v thrift &>/dev/null; then
  echo "[brew] active thrift: $(thrift -version 2>&1 || true)"
else
  echo "[brew] error: thrift not on PATH after install" >&2
  exit 1
fi
