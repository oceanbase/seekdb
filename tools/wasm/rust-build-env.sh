#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
# Sourced by Rust builders after setting seekdb_root and seekdb_build_dir.
mkdir -p "${seekdb_build_dir}"
seekdb_build_dir="$(cd "${seekdb_build_dir}" && pwd)"
seekdb_sdk_version="$(cat "${seekdb_root}/tools/wasm/emscripten-version")"
seekdb_rust_version="$(cat "${seekdb_root}/tools/wasm/rust-toolchain-version")"
if ! emcc --version | head -n 1 | grep -Eq " ${seekdb_sdk_version//./\\.}( |$)"; then
  echo "Activate Emscripten ${seekdb_sdk_version} before building Rust." >&2
  exit 1
fi
export RUSTUP_TOOLCHAIN="${seekdb_rust_version}"
export CARGO_TARGET_DIR="${seekdb_build_dir}/target"
export CARGO_HOME="${seekdb_build_dir}/cargo"
SEEKDB_RUSTC="$(rustup which rustc --toolchain "${seekdb_rust_version}")"
export SEEKDB_RUSTC
export SEEKDB_RUST_SYSROOT="${seekdb_build_dir}/sysroot"
python3 "${seekdb_root}/tools/wasm/prepare-rust-sysroot.py" \
  "$("${SEEKDB_RUSTC}" --print sysroot)" "${SEEKDB_RUST_SYSROOT}"
cat > "${seekdb_build_dir}/rustc-wrapper" <<'WRAPPER'
#!/usr/bin/env bash
exec "${SEEKDB_RUSTC}" --sysroot "${SEEKDB_RUST_SYSROOT}" "$@"
WRAPPER
chmod +x "${seekdb_build_dir}/rustc-wrapper"
export RUSTC="${seekdb_build_dir}/rustc-wrapper"
# The prebuilt standard library does not establish the shared-memory ABI.
# Rebuild it with the same features as this crate. Rust never unwinds across
# the C boundary; seekdb's C++ code retains its separate JS exception model.
export RUSTFLAGS="-Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort"
