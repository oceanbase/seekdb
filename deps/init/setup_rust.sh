#!/usr/bin/env bash
# Source this file so the selected toolchain remains on the caller's PATH.

seekdb_rust_matches() {
  local expected=$1 cargo_version rustc_version
  cargo_version=$(cargo --version 2>/dev/null) || return 1
  rustc_version=$(rustc --version 2>/dev/null) || return 1
  [[ "$cargo_version" == "cargo $expected "* && "$rustc_version" == "rustc $expected "* ]]
}

seekdb_prepare_rust() {
  local source_root=$1 install_root=$2 pinned rustup_bin installer installer_url host
  pinned=$(sed -n 's/^[[:space:]]*channel[[:space:]]*=[[:space:]]*"\([0-9][0-9.]*\)".*/\1/p' \
    "$source_root/rust/rust-toolchain.toml") || return $?
  if [[ ! "$pinned" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "[setup-rust] Cannot read exact Rust version from rust/rust-toolchain.toml" >&2
    return 1
  fi

  # A standalone official toolchain is sufficient for a native C++ build.
  # Setting the exact override also avoids installing unrelated mobile targets
  # from rust-toolchain.toml when the executables are rustup shims.
  export RUSTUP_TOOLCHAIN="$pinned"
  if seekdb_rust_matches "$pinned"; then
    echo "[setup-rust] Reusing Rust/Cargo $pinned"
    return 0
  fi

  if ! command -v rustup >/dev/null 2>&1; then
    export CARGO_HOME="${CARGO_HOME:-$install_root/cargo}"
    export RUSTUP_HOME="${RUSTUP_HOME:-$install_root/rustup}"
    export PATH="$CARGO_HOME/bin:$PATH"
    if ! command -v rustup >/dev/null 2>&1; then
      mkdir -p "$install_root" "$CARGO_HOME" "$RUSTUP_HOME" || return $?
      installer="$install_root/rustup-init"
      if [[ -n "${RUSTUP_INIT_URL:-}" ]]; then
        installer_url="$RUSTUP_INIT_URL"
      else
        case "$(uname -s)/$(uname -m)" in
          Linux/x86_64) host=x86_64-unknown-linux-gnu ;;
          Linux/aarch64) host=aarch64-unknown-linux-gnu ;;
          Darwin/x86_64) host=x86_64-apple-darwin ;;
          Darwin/arm64) host=aarch64-apple-darwin ;;
          *) echo "[setup-rust] Set RUSTUP_INIT_URL for this host" >&2; return 1 ;;
        esac
        installer_url="${RUSTUP_UPDATE_ROOT:-https://static.rust-lang.org/rustup}/dist/$host/rustup-init"
      fi
      echo "[setup-rust] Installing private rustup in $install_root"
      if command -v curl >/dev/null 2>&1; then
        curl --proto '=https' --tlsv1.2 --retry 3 --fail --silent --show-error \
          --location "$installer_url" --output "$installer" || return $?
      elif command -v wget >/dev/null 2>&1; then
        wget --tries=3 --quiet -O "$installer" "$installer_url" || return $?
      else
        echo "[setup-rust] curl or wget is required to install Rust $pinned" >&2
        return 1
      fi
      if [[ -n "${RUSTUP_INIT_URL:-}" ]]; then
        RUSTUP_INIT_SKIP_PATH_CHECK=yes sh "$installer" -y --profile minimal \
          --default-toolchain none --no-modify-path || return $?
      else
        chmod u+x "$installer" || return $?
        RUSTUP_INIT_SKIP_PATH_CHECK=yes "$installer" -y --profile minimal \
          --default-toolchain none --no-modify-path || return $?
      fi
    fi
  fi

  rustup_bin=$(command -v rustup) || return $?
  export PATH="$(dirname "$rustup_bin"):$PATH"
  rustup toolchain install "$pinned" --profile minimal --no-self-update || return $?
  if ! seekdb_rust_matches "$pinned"; then
    echo "[setup-rust] Rust and Cargo must both match pinned version $pinned" >&2
    return 1
  fi
  echo "[setup-rust] Selected Rust/Cargo $pinned"
}
