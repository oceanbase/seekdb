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

echo "=== Testing Go FFI Binding ==="
echo "SEEKDB_LIB_PATH: $SEEKDB_LIB_PATH"
echo ""

# Check if seekdb library exists
if [ ! -f "${SEEKDB_LIB_PATH}" ]; then
    echo "Error: libseekdb${SEEKDB_LIB_EXT} not found at ${SEEKDB_LIB_PATH}"
    echo "Please build the project first: cd ../../../build_release && make libseekdb"
    exit 1
fi

# Check if go is available
if ! command -v go >/dev/null 2>&1; then
    echo "Error: go command not found"
    echo "Please install Go first"
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

# Run the test
echo "Running Go tests..."
echo ""
# Note: C ABI layer handles SIGSEGV gracefully during static destructors
# Exit code is 0 and no segfault messages are output
go run test.go "${DB_DIR}"
GO_EXIT=$?
if [ $GO_EXIT -ne 0 ]; then
    echo "First run (relative path) failed with exit $GO_EXIT"
    exit $GO_EXIT
fi

# Second run: absolute path (same suite, no close+reopen in process)
DB_DIR_ABS="$(pwd)/seekdb_abs.db"
rm -rf "${DB_DIR_ABS}"
echo ""
echo "Running Go tests with absolute path: $DB_DIR_ABS"
echo ""
go run test.go "${DB_DIR_ABS}"
ABS_EXIT=$?
rm -rf "${DB_DIR_ABS}" 2>/dev/null || true
if [ $ABS_EXIT -ne 0 ]; then
    echo "Second run (absolute path) failed with exit $ABS_EXIT"
    exit $ABS_EXIT
fi

echo ""
echo "Test completed!"
