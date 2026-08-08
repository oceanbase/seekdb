#!/bin/bash
set -e

cd "$(dirname "$0")"

# Set library path (Linux: .so, macOS: .dylib)
SEEKDB_LIB_DIR="$(cd ../../../build_release/src/include && pwd)"
case "$(uname -s)" in
    Darwin*) SEEKDB_LIB_EXT=".dylib" ;;
    *)       SEEKDB_LIB_EXT=".so" ;;
esac
export SEEKDB_LIB_PATH="${SEEKDB_LIB_DIR}/libseekdb${SEEKDB_LIB_EXT}"

echo "=== Testing Rust FFI Binding ==="
echo "SEEKDB_LIB_PATH: $SEEKDB_LIB_PATH"
echo ""

# Check if seekdb library exists
if [ ! -f "${SEEKDB_LIB_PATH}" ]; then
    echo "Error: libseekdb${SEEKDB_LIB_EXT} not found at ${SEEKDB_LIB_PATH}"
    echo "Please build the project first: cd ../../../build_release && make libseekdb"
    exit 1
fi

# Check if cargo is available
if ! command -v cargo >/dev/null 2>&1; then
    echo "Error: cargo command not found"
    echo ""
    echo "Please install Rust first. You can use:"
    echo "  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
    echo "  source \$HOME/.cargo/env"
    exit 1
fi

# Clean up old database directory if it exists to start fresh
DB_DIR="./seekdb.db"
if [ -d "${DB_DIR}" ]; then
    echo "Cleaning up old database directory..."
    rm -rf "${DB_DIR}"
    echo "Old database directory removed."
    echo ""
fi

# Also clean up any test database directories
find . -maxdepth 1 -type d -name "seekdb*.db" -exec rm -rf {} + 2>/dev/null || true

# Set RUSTFLAGS to link against seekdb library and configure rpath
# -L: link-time library search path
# -C link-arg=-Wl,-rpath: runtime library search path (embedded in binary)
# The rpath is embedded in the binary, so LD_LIBRARY_PATH is not needed
export RUSTFLAGS="-L ${SEEKDB_LIB_DIR} -C link-arg=-Wl,-rpath,${SEEKDB_LIB_DIR}"

# Build and run the test as a binary (src/bin/test.rs)
echo "Running Rust tests..."
echo ""
# Note: C ABI layer handles SIGSEGV gracefully during static destructors
# Exit code is 0 and no segfault messages are output
cargo build --bin test

# Run the compiled test binary
TEST_BIN="target/debug/test"
if [ ! -f "$TEST_BIN" ]; then
    echo "Error: Could not find compiled test binary at $TEST_BIN"
    exit 1
fi

"$TEST_BIN" "${DB_DIR}" "test"
RUST_EXIT=$?
if [ $RUST_EXIT -ne 0 ]; then
    echo "First run (relative path) failed with exit $RUST_EXIT"
    exit $RUST_EXIT
fi

# Second run: absolute path (same suite, no close+reopen in process)
DB_DIR_ABS="$(pwd)/seekdb_abs.db"
rm -rf "${DB_DIR_ABS}"
echo ""
echo "Running Rust tests with absolute path: $DB_DIR_ABS"
echo ""
"$TEST_BIN" "${DB_DIR_ABS}" "test"
ABS_EXIT=$?
rm -rf "${DB_DIR_ABS}" 2>/dev/null || true
if [ $ABS_EXIT -ne 0 ]; then
    echo "Second run (absolute path) failed with exit $ABS_EXIT"
    exit $ABS_EXIT
fi

echo ""
echo "Test completed!"

