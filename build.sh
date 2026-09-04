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
  ./build.sh sanity [--init] [-DName=Value ...]
  ./build.sh sanity [--init] [-DName=Value ...] --make [MakeOptions]
  ./build.sh {rpm|deb|tgz} [--init] [-DName=Value ...]
  ./build.sh {rpm|deb|tgz} [--init] [-DName=Value ...] --make [MakeOptions]

Supported compatibility build:
  Release (RelWithDebInfo, -O2), Unity compilation, seekdb production binary,
  and RPM, DEB, or TGZ packaging profiles derived from that Release build.
  RPM and DEB packaging require Linux; TGZ supports Linux and macOS.
  Sanity is a Linux-only CMake RelWithDebInfo Unity build with memory
  instrumentation enabled.
  Host platforms: Linux and macOS. Android cross-compilation: arm64-v8a.
  Windows x64 uses build.ps1.

Examples:
  ./build.sh release --init
  cd build_release && make -j80
  ./build.sh release --make -j80
  ./build.sh sanity --init
  cd build_sanity && make -j32 seekdb
  ./build.sh sanity --make -j32
  ./build.sh rpm --init
  cd build_rpm && make -j80
  ./build.sh rpm --make -j80 rpm
  ./build.sh deb --init --make -j80 deb
  ./build.sh tgz --init --make -j16 tgz
  ./build.sh release --android --init --make -j16

On Linux, CMake also provides module unit-test targets and the pretest aggregate.
See docs/developer-guide/en/unittest.md for build and test commands.
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
    "${TOPDIR}/build_debug"|"${TOPDIR}/build_release"|"${TOPDIR}/build_sanity"|"${TOPDIR}/build_android_release"|"${TOPDIR}/build_rpm"|"${TOPDIR}/build_deb"|"${TOPDIR}/build_tgz")
      ;;
    *)
      fail "refusing to clean unexpected path: ${build_dir}"
      ;;
  esac
  if [[ -d "${build_dir}" ]]; then
    "$(find_cmake)" -E remove_directory "${build_dir}"
  fi
}

function configure_cmake
{
  local build_label=$1
  local android_build=$2
  local build_dir=$3
  shift 3
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

  echo_log "configuring CMake ${build_label} build: ${build_dir}"
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
    configure_cmake release "${android_build}" "${build_dir}" "${cmake_args[@]}" || exit $?
  else
    configure_cmake release "${android_build}" "${build_dir}" || exit $?
  fi

  if [[ "${need_make}" == true ]]; then
    if (( ${#make_args[@]} == 0 )); then
      make_args=(-j"$(cpu_count)")
    fi
    make -C "${build_dir}" "${make_args[@]}" seekdb
  fi
}

function do_sanity
{
  local need_init=false
  local need_make=false
  local collecting_make_args=false
  local build_dir="${TOPDIR}/build_sanity"
  local -a cmake_args=()
  local -a make_args=()

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
        fail "$1 is outside the CMake Sanity build boundary"
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
          fail "unexpected sanity argument: $1"
        fi
        ;;
    esac
    shift
  done

  require_host
  [[ "$(uname -s)" == "Linux" ]] || fail "Sanity builds are supported only on Linux"
  if [[ "${need_init}" == true ]]; then
    do_init false || exit $?
  fi

  configure_cmake sanity false "${build_dir}" \
    "${cmake_args[@]}" -DENABLE_SANITY=ON || exit $?

  if [[ "${need_make}" == true ]]; then
    if (( ${#make_args[@]} == 0 )); then
      make_args=(-j"$(cpu_count)")
    fi
    make -C "${build_dir}" "${make_args[@]}" seekdb
  fi
}

function do_package
{
  local package_type=$1
  shift
  local package_label
  local package_option
  local need_init=false
  local need_make=false
  local collecting_make_args=false
  local build_dir="${TOPDIR}/build_${package_type}"
  local -a cmake_args=()
  local -a make_args=()
  local -a package_cmake_args=()

  case "${package_type}" in
    rpm)
      package_label=RPM
      package_option=OB_BUILD_RPM
      ;;
    deb)
      package_label=DEB
      package_option=OB_BUILD_DEB
      ;;
    tgz)
      package_label=TGZ
      package_option=OB_BUILD_TGZ
      ;;
    *)
      fail "unsupported package type: ${package_type}"
      ;;
  esac

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
        fail "$1 is outside the CMake ${package_label} compatibility boundary"
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
          fail "unexpected ${package_type} argument: $1"
        fi
        ;;
    esac
    shift
  done

  require_host
  if [[ "${package_type}" == "rpm" || "${package_type}" == "deb" ]]; then
    [[ "$(uname -s)" == "Linux" ]] ||
      fail "${package_label} packaging is supported only on Linux"
  fi
  if [[ "${package_type}" == "deb" && "${need_make}" == true ]]; then
    command -v dpkg-deb >/dev/null 2>&1 ||
      fail "dpkg-deb is required to build a DEB package"
  fi
  if [[ "${need_init}" == true ]]; then
    do_init false || exit $?
  fi

  package_cmake_args=(
    -DOB_BUILD_PACKAGE=ON
    "-D${package_option}=ON"
    -DOB_STATIC_LINK_LGPL_DEPS=ON
    -DDEFAULT_LOG_LEVEL=OB_LOG_LEVEL_DBA_WARN
    -DDEFAULT_LOG_FILE_SIZE_MB=16
  )
  if [[ "${package_type}" == "tgz" ]]; then
    package_cmake_args+=(
      -DENABLE_AUTO_FDO=OFF
      -DENABLE_THIN_LTO=ON
      -DENABLE_HOTFUNC=OFF
    )
  else
    package_cmake_args+=(
      -DENABLE_AUTO_FDO=ON
      -DENABLE_THIN_LTO=ON
      -DENABLE_HOTFUNC=ON
    )
  fi
  if (( ${#cmake_args[@]} > 0 )); then
    package_cmake_args=("${cmake_args[@]}" "${package_cmake_args[@]}")
  fi
  configure_cmake "${package_type}" false "${build_dir}" "${package_cmake_args[@]}" || exit $?

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
      "${TOPDIR}/build_sanity" \
      "${TOPDIR}/build_android_release" \
      "${TOPDIR}/build_rpm" \
      "${TOPDIR}/build_deb" \
      "${TOPDIR}/build_tgz"; do
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
    sanity)
      do_sanity "${@:2}"
      ;;
    rpm|deb|tgz)
      do_package "$1" "${@:2}"
      ;;
    "")
      usage
      exit 2
      ;;
    *)
      fail "unsupported build type or command: $1 (maintained modes: release, sanity, rpm, deb, tgz)"
      ;;
  esac
}

main "$@"
