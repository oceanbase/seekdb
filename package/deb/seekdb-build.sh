#!/usr/bin/env bash

if [[ -f ~/.bashrc ]]; then
  source ~/.bashrc
fi

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly TOP_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly OUTPUT_DIR="${PWD}"

function fail
{
  echo "[seekdb-deb][ERROR] $*" >&2
  exit 1
}

function usage
{
  echo "Usage: $0 <source-path> seekdb <version> <release>" >&2
  exit 64
}

function cpu_count
{
  if command -v nproc >/dev/null 2>&1; then
    nproc
  else
    getconf _NPROCESSORS_ONLN
  fi
}

function require_file
{
  [[ -f "$1" ]] || fail "required package input is missing: $1"
}

function render_maintainer_scripts
{
  local output_dir=$1
  local version=$2
  local profile_dir="${TOP_DIR}/tools/systemd/profile"
  local script_name
  local -a script_names=(
    pre_install
    post_install
    pre_uninstall
    post_uninstall
  )

  install -d "${output_dir}"
  for script_name in "${script_names[@]}"; do
    require_file "${profile_dir}/${script_name}.sh.template"
    sed -e "s|@OceanBase_VERSION@|${version}|g" \
      "${profile_dir}/${script_name}.sh.template" \
      > "${output_dir}/${script_name}.sh"
    chmod 0755 "${output_dir}/${script_name}.sh"
  done
}

function install_payload
{
  local package_root=$1
  local version=$2
  local release=$3
  local maintainer_script_dir=$4
  local binary="${TOP_DIR}/build_release/src/observer/seekdb"
  local obshell_binary="${TOP_DIR}/deps/3rd/home/admin/oceanbase/bin/obshell"
  local syspack_dir="${TOP_DIR}/build_release/syspack_release"
  local profile_dir="${TOP_DIR}/tools/systemd/profile"

  local -a syspack_files=()
  shopt -s nullglob
  syspack_files=("${syspack_dir}"/*)
  shopt -u nullglob
  (( ${#syspack_files[@]} > 0 )) ||
    fail "Bazel syspack output is empty: ${syspack_dir}"

  require_file "${TOP_DIR}/src/share/parameter/default_parameter.json"
  require_file "${TOP_DIR}/src/share/system_variable/default_system_variable.json"
  require_file "${profile_dir}/telemetry.sh.template"
  require_file "${obshell_binary}"

  install -d \
    "${package_root}/usr/bin" \
    "${package_root}/usr/lib/systemd/system" \
    "${package_root}/usr/libexec/seekdb/scripts" \
    "${package_root}/usr/share/seekdb/admin" \
    "${package_root}/usr/share/seekdb/timezone" \
    "${package_root}/usr/share/seekdb/srs" \
    "${package_root}/etc/seekdb"

  install -m 0755 "${binary}" "${package_root}/usr/bin/seekdb"
  install -m 0755 "${obshell_binary}" "${package_root}/usr/bin/obshell"
  install -m 0644 "${profile_dir}/seekdb.service" \
    "${package_root}/usr/lib/systemd/system/seekdb.service"
  install -m 0755 \
    "${TOP_DIR}/tools/import_time_zone_info.py" \
    "${TOP_DIR}/tools/import_srs_data.py" \
    "${TOP_DIR}/tools/seekdb_cli.py" \
    "${TOP_DIR}/tools/seekdb-cli" \
    "${package_root}/usr/libexec/seekdb/"
  ln -s /usr/libexec/seekdb/seekdb-cli "${package_root}/usr/bin/seekdb-cli"
  install -m 0755 \
    "${profile_dir}/seekdb_systemd_start" \
    "${profile_dir}/seekdb_systemd_stop" \
    "${maintainer_script_dir}/pre_install.sh" \
    "${maintainer_script_dir}/post_install.sh" \
    "${maintainer_script_dir}/pre_uninstall.sh" \
    "${maintainer_script_dir}/post_uninstall.sh" \
    "${package_root}/usr/libexec/seekdb/scripts/"

  sed \
    -e 's|@SEEKDB_PACKAGE_NAME@|seekdb|g' \
    -e "s|@SEEKDB_PACKAGE_VERSION@|${version}|g" \
    -e "s|@SEEKDB_PACKAGE_RELEASE@|${release}|g" \
    "${profile_dir}/telemetry.sh.template" \
    > "${package_root}/usr/libexec/seekdb/scripts/telemetry.sh"
  chmod 0755 "${package_root}/usr/libexec/seekdb/scripts/telemetry.sh"

  install -m 0644 \
    "${TOP_DIR}/src/share/parameter/default_parameter.json" \
    "${TOP_DIR}/src/share/system_variable/default_system_variable.json" \
    "${profile_dir}/seekdb.cnf" \
    "${profile_dir}/oceanbase-pre.json" \
    "${profile_dir}/telemetry-pre.json" \
    "${package_root}/etc/seekdb/"
  install -m 0644 "${syspack_files[@]}" \
    "${package_root}/usr/share/seekdb/admin/"
  install -m 0644 "${TOP_DIR}/tools/timezone_V1.log" \
    "${package_root}/usr/share/seekdb/timezone/"
  install -m 0644 "${TOP_DIR}/tools/default_srs_data_mysql.sql" \
    "${package_root}/usr/share/seekdb/srs/"
}

(( $# == 4 )) || usage
readonly PROJECT_NAME=$2
readonly VERSION=$3
readonly RELEASE=$4

[[ "${PROJECT_NAME}" == "seekdb" ]] ||
  fail "unsupported project '${PROJECT_NAME}'; only seekdb is packaged"
[[ "${VERSION}" =~ ^[0-9A-Za-z.+:~_-]+$ ]] ||
  fail "invalid Debian version: ${VERSION}"
[[ "${RELEASE}" =~ ^[0-9A-Za-z.+~_-]+$ ]] ||
  fail "invalid Debian release: ${RELEASE}"
command -v dpkg-deb >/dev/null 2>&1 ||
  fail "dpkg-deb is required to build the seekdb Debian package"

readonly WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/seekdb-deb.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT
readonly PACKAGE_ROOT="${WORK_DIR}/root"
readonly MAINTAINER_SCRIPT_DIR="${WORK_DIR}/maintainer"
readonly PACKAGE_VERSION="${VERSION}-${RELEASE}"
readonly ARCHITECTURE="$(dpkg --print-architecture)"
readonly OUTPUT_PATH="${OUTPUT_DIR}/seekdb_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"

read -r -a MAKE_ARGUMENTS <<< "${MAKE_ARGS:--j$(cpu_count)}"

render_maintainer_scripts "${MAINTAINER_SCRIPT_DIR}" "${VERSION}"

cd "${TOP_DIR}"
./build.sh release --init
(cd build_release && make "${MAKE_ARGUMENTS[@]}")

[[ -x "${TOP_DIR}/build_release/src/observer/seekdb" ]] ||
  fail "Bazel seekdb binary is missing or not executable"

install_payload \
  "${PACKAGE_ROOT}" \
  "${VERSION}" \
  "${RELEASE}" \
  "${MAINTAINER_SCRIPT_DIR}"
install -d "${PACKAGE_ROOT}/DEBIAN"
cat > "${PACKAGE_ROOT}/DEBIAN/control" <<EOF
Package: seekdb
Version: ${PACKAGE_VERSION}
Section: database
Priority: optional
Architecture: ${ARCHITECTURE}
Maintainer: OceanBase
Depends: libaio1 | libaio1t64, systemd
Description: seekdb single-node database
 Bazel release Unity build of the seekdb server.
EOF
printf '%s\n' '/etc/seekdb/seekdb.cnf' > "${PACKAGE_ROOT}/DEBIAN/conffiles"
install -m 0755 \
  "${MAINTAINER_SCRIPT_DIR}/pre_install.sh" \
  "${PACKAGE_ROOT}/DEBIAN/preinst"
install -m 0755 \
  "${MAINTAINER_SCRIPT_DIR}/post_install.sh" \
  "${PACKAGE_ROOT}/DEBIAN/postinst"
install -m 0755 \
  "${MAINTAINER_SCRIPT_DIR}/pre_uninstall.sh" \
  "${PACKAGE_ROOT}/DEBIAN/prerm"
install -m 0755 \
  "${MAINTAINER_SCRIPT_DIR}/post_uninstall.sh" \
  "${PACKAGE_ROOT}/DEBIAN/postrm"

rm -f "${OUTPUT_PATH}"
dpkg-deb --root-owner-group --build "${PACKAGE_ROOT}" "${OUTPUT_PATH}"
echo "[seekdb-deb] package: ${OUTPUT_PATH}"
