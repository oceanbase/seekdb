#!/bin/bash
set -e

cd "$(dirname "$0")"
# shellcheck source=../binding-exit-probe.sh
source "$(cd "$(dirname "$0")/.." && pwd)/binding-exit-probe.sh"

SCRIPT_DIR="$(pwd)"
PROJECT_ROOT="$(cd ../../.. && pwd)"

# Set library path (Linux: .so, macOS: .dylib)
SEEKDB_LIB_DIR="${PROJECT_ROOT}/build_release/src/include"
case "$(uname -s)" in
    Darwin*) SEEKDB_LIB_EXT=".dylib" ;;
    *)       SEEKDB_LIB_EXT=".so" ;;
esac
SEEKDB_LIB_PATH="${SEEKDB_LIB_DIR}/libseekdb${SEEKDB_LIB_EXT}"

echo "=== Testing Java JNI Binding ==="
echo "SEEKDB_LIB_PATH: ${SEEKDB_LIB_PATH}"
echo ""

# Check if libseekdb exists
if [ ! -f "${SEEKDB_LIB_PATH}" ]; then
    echo "Error: libseekdb${SEEKDB_LIB_EXT} not found at ${SEEKDB_LIB_PATH}"
    echo "Please build the project first: cd ${PROJECT_ROOT}/build_release && make libseekdb"
    exit 1
fi

# Check if Java is available
if ! command -v java >/dev/null 2>&1; then
    echo "Error: java command not found"
    echo "Please install JDK 11 or 17"
    exit 1
fi

# Set JAVA_HOME for CMake FindJNI (macOS: /usr/libexec/java_home; Linux: often JAVA_HOME or java -XshowSettings:properties)
if [ -z "${JAVA_HOME}" ]; then
    if [ "$(uname -s)" = "Darwin" ]; then
        JAVA_HOME=$(/usr/libexec/java_home 2>/dev/null || true)
    fi
    if [ -z "${JAVA_HOME}" ] && command -v java >/dev/null 2>&1; then
        _java_bin=$(command -v java)
        _java_real=$("${_java_bin}" -XshowSettings:properties -version 2>&1 | sed -n 's/^[[:space:]]*java.home[[:space:]]*=[[:space:]]*//p' | head -1)
        [ -n "${_java_real}" ] && JAVA_HOME="${_java_real}"
    fi
    [ -n "${JAVA_HOME}" ] && export JAVA_HOME && echo "JAVA_HOME: ${JAVA_HOME}"
fi

# Build JNI library
echo "Building JNI library..."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cd ..

JNI_BUILD_DIR="${SCRIPT_DIR}/build"
case "$(uname -s)" in
    Darwin*) JNI_LIB_NAME="libseekdb_jni.dylib" ;;
    *)       JNI_LIB_NAME="libseekdb_jni.so" ;;
esac

if [ ! -f "${JNI_BUILD_DIR}/${JNI_LIB_NAME}" ]; then
    echo "Error: JNI library not found at ${JNI_BUILD_DIR}/${JNI_LIB_NAME}"
    exit 1
fi

# Build Java
echo "Building Java..."
mvn -q compile test-compile

# Clean up old database directory
DB_DIR="./seekdb.db"
if [ -d "${DB_DIR}" ]; then
    echo "Cleaning up old database directory..."
    rm -rf "${DB_DIR}"
    echo "Old database directory removed."
    echo ""
fi

# Run the test
echo "Running Java tests..."
echo ""
JAVA_LIB_PATH="${JNI_BUILD_DIR}:${SEEKDB_LIB_DIR}"
run_with_binding_exit_probe "$BINDING_TEST_TIMEOUT_MS" "$BINDING_EXIT_PROBE_GRACE_MS" -- \
  java -Djava.library.path="${JAVA_LIB_PATH}" \
    -cp "target/classes:target/test-classes" \
    seekdb.SeekdbTest "${DB_DIR}"

JAVA_EXIT=$?
if [ $JAVA_EXIT -ne 0 ]; then
    echo "First run (relative path) failed with exit $JAVA_EXIT"
    exit $JAVA_EXIT
fi

# Second run: absolute path
DB_DIR_ABS="${SCRIPT_DIR}/seekdb_abs.db"
rm -rf "${DB_DIR_ABS}"
echo ""
echo "Running Java tests with absolute path: $DB_DIR_ABS"
echo ""
run_with_binding_exit_probe "$BINDING_TEST_TIMEOUT_MS" "$BINDING_EXIT_PROBE_GRACE_MS" -- \
  java -Djava.library.path="${JAVA_LIB_PATH}" \
    -cp "target/classes:target/test-classes" \
    seekdb.SeekdbTest "${DB_DIR_ABS}"

ABS_EXIT=$?
rm -rf "${DB_DIR_ABS}" 2>/dev/null || true
if [ $ABS_EXIT -ne 0 ]; then
    echo "Second run (absolute path) failed with exit $ABS_EXIT"
    exit $ABS_EXIT
fi

echo ""
echo "Test completed!"
