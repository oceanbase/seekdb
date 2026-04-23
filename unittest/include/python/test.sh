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

echo "=== Testing Python FFI Binding ==="
echo "SEEKDB_LIB_PATH: $SEEKDB_LIB_PATH"
echo ""

# Check if seekdb library exists
if [ ! -f "${SEEKDB_LIB_PATH}" ]; then
    echo "Error: libseekdb${SEEKDB_LIB_EXT} not found at ${SEEKDB_LIB_PATH}"
    echo "Please build the project first: cd ../../../build_release && make libseekdb"
    exit 1
fi

# Check Python version
PYTHON_CMD=""
if command -v python3 &> /dev/null; then
    PYTHON_CMD="python3"
elif command -v python &> /dev/null; then
    PYTHON_CMD="python"
else
    echo "Error: Python not found"
    exit 1
fi

echo "Using Python: $PYTHON_CMD"
$PYTHON_CMD --version
echo ""

# Clean up old database directory if it exists
DB_DIR="./seekdb.db"
if [ -d "${DB_DIR}" ]; then
    echo "Cleaning up old database directory..."
    rm -rf "${DB_DIR}"
    echo "Old database directory removed."
    echo ""
fi

# Run the test (-u for unbuffered output)
echo "Running Python tests..."
echo ""
# Note: C ABI layer handles SIGSEGV gracefully during static destructors
# Python also uses os._exit() to avoid cleanup issues
# Exit code is 0 and no segfault messages are output
$PYTHON_CMD -u test.py "${DB_DIR}" "test"
PY_EXIT=$?
if [ $PY_EXIT -ne 0 ]; then
    echo "Python binding tests failed with exit $PY_EXIT"
    exit $PY_EXIT
fi

rm -rf "$(pwd)/seekdb_abs.db" 2>/dev/null || true

echo ""
echo "Test completed!"
