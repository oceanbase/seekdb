#!/usr/bin/env bash

set -uo pipefail

readonly TOPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly DEP_INIT_DIR="${TOPDIR}/deps/init"
readonly DEVTOOLS_DIR="${TOPDIR}/deps/3rd/usr/local/oceanbase/devtools"
readonly -a ALL_ARGS=("$@")

function echo_log
{
  echo "[build.sh] $*"
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

Supported compatibility build:
  Release (RelWithDebInfo, -O2), Unity compilation, seekdb production binary.
  Host platforms: Linux and macOS. Android cross-compilation: arm64-v8a.
  Windows x64 uses build.ps1.

Examples:
  source ~/.bashrc && ./build.sh release --init
  cd build_release && make -j80
  source ~/.bashrc && ./build.sh release --make -j80
  ./build.sh release --android --init --make -j16

Bazel remains the authoritative modular build graph. Use ./bazel.py directly
for Bazel builds, tests, architecture checks, and non-release options.
EOF
}

function print_command
{
  printf '[build.sh] command:'
  printf ' %q' "$0" "${ALL_ARGS[@]}"
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
    "${TOPDIR}/build_release"|"${TOPDIR}/build_android_release")
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
  configure_release "${android_build}" "${build_dir}" "${cmake_args[@]}" || exit $?

  if [[ "${need_make}" == true ]]; then
    if (( ${#make_args[@]} == 0 )); then
      make_args=(-j"$(cpu_count)")
    fi
    make -C "${build_dir}" "${make_args[@]}" seekdb
  fi
}

function do_clean
{
  local build_dir
  local found=false

  for build_dir in "${TOPDIR}/build_release" "${TOPDIR}/build_android_release"; do
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
    "")
      usage
      exit 2
      ;;
    *)
      fail "unsupported build type or command: $1 (only release is maintained)"
      ;;
  esac
}

main "$@"
