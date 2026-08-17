#!/usr/bin/env bash

set -uo pipefail

readonly TOPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly DEP_INIT_DIR="${TOPDIR}/deps/init"
readonly DEVTOOLS_DIR="${TOPDIR}/deps/3rd/usr/local/oceanbase/devtools"
readonly -a ALL_ARGS=("$@")

# Get CPU cores; cmake path is resolved in do_build() (Linux may use host cmake before deps devtools exist)
if [[ "$(uname -s)" == "Darwin" ]]; then
  CPU_CORES=$(sysctl -n hw.ncpu)
  KERNEL_RELEASE=""
else
  CPU_CORES=$(grep -c ^processor /proc/cpuinfo)
  KERNEL_RELEASE=$(grep -Po 'release [0-9]{1}' /etc/issue 2>/dev/null)
fi

BUILD_ARGS=()
MAKE_ARGS=(-j $CPU_CORES)
NEED_MAKE=false
NEED_INIT=false
ANDROID_BUILD=false
LLD_OPTION=ON
STATIC_LINK_LGPL_DEPS_OPTION=ON
ENABLE_BOLT_OPTION=ON
WITH_COVERAGE=OFF

echo "$0 ${ALL_ARGS[@]}"

function echo_log() {
  echo -e "[build.sh] $@"
}

function echo_err
{
  echo "[build.sh][ERROR] $*" >&2
}

function fail
{
  echo_err "$*"
  exit 2
}

function usage
{
  cat <<'EOF'
Usage:
  ./build.sh -h
  ./build.sh init [--android]
  ./build.sh clean
  ./build.sh release [--init] [--android] [-DName=Value ...]
  ./build.sh release [--init] [--android] [-DName=Value ...] --make [MakeOptions]
  ./build.sh rpm [--init] [-DName=Value ...]
  ./build.sh rpm [--init] [-DName=Value ...] --make [MakeOptions]

Supported compatibility build:
  Release (RelWithDebInfo, -O2), Unity compilation, seekdb production binary,
  and the Linux RPM packaging profile derived from that Release build.
  Host platforms: Linux and macOS. Android cross-compilation: arm64-v8a.
  Windows x64 uses build.ps1.

Examples:
  source ~/.bashrc && ./build.sh release --init
  cd build_release && make -j80
  source ~/.bashrc && ./build.sh release --make -j80
  source ~/.bashrc && ./build.sh rpm --init
  cd build_rpm && make -j80
  source ~/.bashrc && ./build.sh rpm --make -j80 rpm
  ./build.sh release --android --init --make -j16

Bazel remains the authoritative modular build graph. Use ./bazel.py directly
for Bazel builds, tests, architecture checks, and non-release options.
EOF
}

function print_command
{
  printf '[build.sh] command: %q' "$0"
  if (( ${#ALL_ARGS[@]} > 0 )); then
    printf ' %q' "${ALL_ARGS[@]}"
  fi
  printf '\n'
}

function require_host
{
  case "$(uname -s)" in
    Linux|Darwin)
      ;;
    *)
      fail "build.sh supports Linux and macOS; use build.ps1 on Windows"
      ;;
  esac
}

function cpu_count
{
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif [[ "$(uname -s)" == "Darwin" ]]; then
    sysctl -n hw.ncpu
  else
    getconf _NPROCESSORS_ONLN
  fi
}

function find_cmake
{
  if [[ "$(uname -s)" == "Linux" && -x "${DEVTOOLS_DIR}/bin/cmake" ]]; then
    printf '%s\n' "${DEVTOOLS_DIR}/bin/cmake"
  elif command -v cmake >/dev/null 2>&1; then
    command -v cmake
  else
    return 1
  fi
}

function do_init
{
  local android_build=$1
  local start_time end_time elapsed
  local status=0

  if [[ ! -f "${DEP_INIT_DIR}/dep_create.sh" ]]; then
    echo_err "dependency initializer not found: ${DEP_INIT_DIR}/dep_create.sh"
    return 1
  fi

  start_time="$(date +%s)"
  (
    cd "${DEP_INIT_DIR}" &&
      ANDROID_BUILD="${android_build}" bash dep_create.sh
  ) || status=$?
  if (( status != 0 )); then
    echo_err "dependency initialization failed with status ${status}"
    return "${status}"
  fi

  end_time="$(date +%s)"
  elapsed=$((end_time - start_time))
  echo_log "dependency initialization completed in $((elapsed / 60))m$((elapsed % 60))s"
}

function release_build_dir
{
  local android_build=$1
  if [[ "${android_build}" == true ]]; then
    printf '%s\n' "${TOPDIR}/build_android_release"
  else
    printf '%s\n' "${TOPDIR}/build_release"
  fi
}

function remove_managed_build_dir
{
  local build_dir=$1

  case "${build_dir}" in
    "${TOPDIR}/build_debug"|"${TOPDIR}/build_release"|"${TOPDIR}/build_android_release"|"${TOPDIR}/build_rpm")
      ;;
    *)
      fail "refusing to clean unexpected path: ${build_dir}"
      ;;
  esac
  if [[ -d "${build_dir}" ]]; then
    "$(find_cmake)" -E remove_directory "${build_dir}"
  fi
}

function configure_release
{
  local android_build=$1
  local build_dir=$2
  shift 2
  local cmake_command
  local lld_option=ON
  local -a cmake_args=(
    -S "${TOPDIR}"
    -B "${build_dir}"
    -G "Unix Makefiles"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DOB_ENABLE_UNITY=ON
  )

  cmake_command="$(find_cmake)" || fail "cmake not found; initialize dependencies or install CMake 3.20+"

  # The bundled lld is unavailable on historical EL6 environments.
  if [[ "$(uname -s)" == "Linux" ]] && grep -qE 'release 6([^0-9]|$)' /etc/issue 2>/dev/null; then
    lld_option=OFF
    echo_log "lld disabled on release 6 compatibility host"
  fi
  cmake_args+=("-DOB_USE_LLD=${lld_option}")

  if [[ "${android_build}" == true ]]; then
    local ndk_home="${ANDROID_NDK_HOME:-${HOME}/Library/Android/sdk/ndk/27.3.13750724}"
    if [[ ! -f "${ndk_home}/build/cmake/android.toolchain.cmake" ]]; then
      fail "Android NDK not found: ${ndk_home}; set ANDROID_NDK_HOME"
    fi
    cmake_args+=(
      "-DCMAKE_TOOLCHAIN_FILE=${ndk_home}/build/cmake/android.toolchain.cmake"
      -DANDROID_ABI=arm64-v8a
      -DANDROID_PLATFORM=android-28
    )
    # cmake/Rust.cmake cross-compiles sql-nio with `cargo build --target
    # aarch64-linux-android`; make sure that target exists. Run from rust/ so
    # rust-toolchain.toml pins the toolchain (the default may be unset).
    if command -v rustup >/dev/null 2>&1; then
      (cd "${TOPDIR}/rust" && rustup target add aarch64-linux-android)
    else
      echo_err "rustup not found in PATH; cannot install the aarch64-linux-android Rust target (needed by sql-nio)"
      exit 1
    fi
  fi

  # Replace the former Bazel-backed build_release entry point in place.  A
  # normal CMake directory is incrementally reconfigured and kept intact.
  if [[ -f "${build_dir}/.seekdb_bazel_release" ]]; then
    echo_log "replacing legacy Bazel compatibility directory: ${build_dir}"
    remove_managed_build_dir "${build_dir}"
  fi

  echo_log "configuring CMake release build: ${build_dir}"
  "${cmake_command}" "${cmake_args[@]}" "$@"
}

function do_release
{
  local need_init=false
  local need_make=false
  local collecting_make_args=false
  local android_build=false
  local build_dir
  local -a cmake_args=()
  local -a make_args=()

  while (( $# > 0 )); do
    case "$1" in
      --init)
        need_init=true
        ;;
      --android)
        android_build=true
        ;;
      --make)
        if [[ "${need_make}" == true ]]; then
          fail "--make may only be specified once"
        fi
        need_make=true
        collecting_make_args=true
        ;;
      --coverage|--ob-make)
        fail "$1 is outside the CMake compatibility boundary"
        ;;
      -D*)
        if [[ "${collecting_make_args}" == true ]]; then
          fail "CMake options must appear before --make: $1"
        fi
        cmake_args+=("$1")
        ;;
      release)
        if [[ "${collecting_make_args}" == true ]]; then
          make_args+=("$1")
        fi
        ;;
      *)
        if [[ "${collecting_make_args}" == true ]]; then
          make_args+=("$1")
        else
          fail "unexpected release argument: $1"
        fi
        ;;
    esac
    shift
  done

  require_host
  if [[ "${need_init}" == true ]]; then
    do_init "${android_build}" || exit $?
  fi

  build_dir="$(release_build_dir "${android_build}")"
  if (( ${#cmake_args[@]} > 0 )); then
    configure_release "${android_build}" "${build_dir}" "${cmake_args[@]}" || exit $?
  else
    configure_release "${android_build}" "${build_dir}" || exit $?
  fi

  if [[ "${need_make}" == true ]]; then
    if (( ${#make_args[@]} == 0 )); then
      make_args=(-j"$(cpu_count)")
    fi
    make -C "${build_dir}" "${make_args[@]}" seekdb
  fi
}

function do_rpm
{
  local need_init=false
  local need_make=false
  local collecting_make_args=false
  local build_dir="${TOPDIR}/build_rpm"
  local -a cmake_args=()
  local -a make_args=()
  local -a rpm_cmake_args=()

  while (( $# > 0 )); do
    case "$1" in
      --init)
        need_init=true
        ;;
      --make)
        if [[ "${need_make}" == true ]]; then
          fail "--make may only be specified once"
        fi
        need_make=true
        collecting_make_args=true
        ;;
      --android|--coverage|--ob-make)
        fail "$1 is outside the CMake RPM compatibility boundary"
        ;;
      -D*)
        if [[ "${collecting_make_args}" == true ]]; then
          fail "CMake options must appear before --make: $1"
        fi
        cmake_args+=("$1")
        ;;
      *)
        if [[ "${collecting_make_args}" == true ]]; then
          make_args+=("$1")
        else
          fail "unexpected rpm argument: $1"
        fi
        ;;
    esac
    shift
  done

  require_host
  [[ "$(uname -s)" == "Linux" ]] || fail "RPM packaging is supported only on Linux"
  if [[ "${need_init}" == true ]]; then
    do_init false || exit $?
  fi

  rpm_cmake_args=(
    -DOB_BUILD_PACKAGE=ON
    -DOB_BUILD_RPM=ON
    -DENABLE_AUTO_FDO=ON
    -DENABLE_THIN_LTO=ON
    -DENABLE_HOTFUNC=ON
    -DOB_STATIC_LINK_LGPL_DEPS=ON
    -DDEFAULT_LOG_LEVEL=OB_LOG_LEVEL_DBA_WARN
    -DDEFAULT_LOG_FILE_SIZE_MB=16
  )
  if (( ${#cmake_args[@]} > 0 )); then
    rpm_cmake_args=("${cmake_args[@]}" "${rpm_cmake_args[@]}")
  fi
  configure_release false "${build_dir}" "${rpm_cmake_args[@]}" || exit $?

  if [[ "${need_make}" == true ]]; then
    if (( ${#make_args[@]} > 0 )); then
      make -C "${build_dir}" -j"$(cpu_count)" "${make_args[@]}"
    else
      make -C "${build_dir}" -j"$(cpu_count)"
    fi
  fi
}

function do_clean
{
  local build_dir
  local found=false

  for build_dir in \
      "${TOPDIR}/build_debug" \
      "${TOPDIR}/build_release" \
      "${TOPDIR}/build_android_release" \
      "${TOPDIR}/build_rpm"; do
    if [[ -d "${build_dir}" ]]; then
      remove_managed_build_dir "${build_dir}"
      echo_log "removed ${build_dir}"
      found=true
    fi
  done
  if [[ "${found}" == false ]]; then
    echo_log "nothing to clean"
  fi
}

function do_init_command
{
  local android_build=false
  if (( $# > 1 )); then
    fail "init accepts only --android"
  fi
  if (( $# == 1 )); then
    [[ "$1" == "--android" ]] || fail "unexpected init argument: $1"
    android_build=true
  fi
  require_host
  do_init "${android_build}"
}

function main
{
  print_command

  case "${1:-}" in
    -h|--help)
      (( $# == 1 )) || fail "$1 does not accept arguments"
      usage
      ;;
    init)
      do_init_command "${@:2}"
      ;;
    clean)
      (( $# == 1 )) || fail "clean does not accept arguments"
      require_host
      do_clean
      ;;
    release)
      do_release "${@:2}"
      ;;
    rpm)
      do_rpm "${@:2}"
      ;;
    "")
      usage
      exit 2
      ;;
    *)
      fail "unsupported build type or command: $1 (maintained modes: release, rpm)"
      ;;
  esac
}

main "$@"
