#!/bin/bash
# Test script for N-API binding with proper library paths

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../binding-exit-probe.sh
source "$(cd "${SCRIPT_DIR}/.." && pwd)/binding-exit-probe.sh"

# From unittest/include/nodejs_napi/ to project root: ../../../ (3 levels up)
# unittest/include/nodejs_napi/ -> unittest/include/ -> unittest/ -> project root
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../../" && pwd)"

# Set library path (Linux: .so, macOS: .dylib)
SEEKDB_LIB_DIR="${PROJECT_ROOT}/build_release/src/include"
case "$(uname -s)" in
    Darwin*) SEEKDB_LIB_EXT=".dylib" ;;
    *)       SEEKDB_LIB_EXT=".so" ;;
esac
export SEEKDB_LIB_PATH="${SEEKDB_LIB_DIR}/libseekdb${SEEKDB_LIB_EXT}"

echo "=== Testing Node.js N-API Binding ==="
echo "SEEKDB_LIB_PATH: ${SEEKDB_LIB_PATH}"
echo ""

# Check if seekdb library exists
if [ ! -f "${SEEKDB_LIB_PATH}" ]; then
    echo "Error: libseekdb${SEEKDB_LIB_EXT} not found at ${SEEKDB_LIB_PATH}"
    echo "Please build the project first: cd ${PROJECT_ROOT}/build_release && make libseekdb"
    exit 1
fi

# Check if node is available
if ! command -v node >/dev/null 2>&1; then
    echo "Error: node command not found"
    echo "Please install Node.js first"
    exit 1
fi

# Clean up old database directory if it exists to start fresh
cd "${SCRIPT_DIR}"
DB_DIR="./seekdb.db"
if [ -d "${DB_DIR}" ]; then
    echo "Cleaning up old database directory..."
    rm -rf "${DB_DIR}"
    echo "Old database directory removed."
    echo ""
fi

# Run the test
echo "Running Node.js N-API tests..."
echo ""
# seekdb_close() + process.exit() can hang in dylib/DLL unload; probe + SIGKILL grace (see binding-exit-probe.sh).
run_node_with_binding_exit_probe "$BINDING_TEST_TIMEOUT_MS" "$BINDING_EXIT_PROBE_GRACE_MS" -- test.js "${DB_DIR}" "test"
NODE_EXIT=$?
if [ $NODE_EXIT -ne 0 ]; then
    echo "Node.js N-API binding tests failed with exit $NODE_EXIT"
    exit $NODE_EXIT
fi

echo ""
echo "Test completed!"
