#!/usr/bin/env bash
# Install the Rust toolchain required by the seekdb build (see rust/rust-toolchain.toml).
# Used by .github/actions/setup-rust and CI jobs that run inside Docker containers.
set -euo pipefail

repo_root="${SEEKDB_REPO_ROOT:-${GITHUB_WORKSPACE:-}}"
if [[ -z "$repo_root" ]]; then
  repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fi

install_root="${SEEKDB_RUST_INSTALL_ROOT:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/seekdb-rust}"
export CARGO_HOME="${CARGO_HOME:-$install_root/cargo}"
export RUSTUP_HOME="${RUSTUP_HOME:-$install_root/rustup}"

if ! command -v rustup >/dev/null 2>&1 || ! command -v cargo >/dev/null 2>&1; then
  mkdir -p "$install_root" "$CARGO_HOME" "$RUSTUP_HOME"
  export PATH="$CARGO_HOME/bin:$PATH"

  rustup_init="$install_root/rustup-init.sh"
  rustup_init_url="${RUSTUP_INIT_URL:-https://sh.rustup.rs}"
  if command -v curl >/dev/null 2>&1; then
    curl --proto '=https' --tlsv1.2 --retry 3 --fail --silent \
      --show-error --location "$rustup_init_url" --output "$rustup_init"
  elif command -v wget >/dev/null 2>&1; then
    wget --tries=3 --quiet -O "$rustup_init" "$rustup_init_url"
  else
    echo "install-rust: curl or wget is required" >&2
    exit 1
  fi
  sh "$rustup_init" -y --profile minimal --default-toolchain none --no-modify-path
fi

export CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}"
export RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}"
rustup_bin_dir="$(dirname "$(command -v rustup)")"
export PATH="$rustup_bin_dir:$PATH"

if [[ "${SEEKDB_CARGO_RSProxy_MIRROR:-}" == "true" ]]; then
  mkdir -p "$CARGO_HOME"
  cat >> "$CARGO_HOME/config.toml" <<'EOF'
[source.crates-io]
replace-with = "rsproxy-sparse"
[source.rsproxy-sparse]
registry = "sparse+https://rsproxy.cn/index/"
EOF
fi

# The version check triggers rustup's auto-install of the pinned toolchain
# (rust-toolchain.toml). Run it in a subshell: this script is sourced by CI
# steps that call `bash build.sh ...` right afterwards, and the cd must not
# leak into the caller's working directory.
(
  cd "$repo_root/rust"
  RUSTUP_AUTO_INSTALL=1 cargo --version
  rustc --version
)

# Binding-test crates (e.g. unittest/include/rust) have no rust-toolchain.toml
# in their ancestor chain, so with --default-toolchain none (above) a bare
# `cargo` there fails: "no default is configured". Pin the default to the
# workspace's pinned channel, read from rust-toolchain.toml so the version
# stays in one place.
pinned_channel="$(sed -n 's/^channel = "\(.*\)"/\1/p' "$repo_root/rust/rust-toolchain.toml" | head -n 1)"
if [[ -z "$pinned_channel" ]]; then
  echo "install-rust: failed to read channel from $repo_root/rust/rust-toolchain.toml" >&2
  exit 1
fi
rustup default "$pinned_channel"
