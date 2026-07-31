#!/usr/bin/env bash

if [[ -f ~/.bashrc ]]; then
  source ~/.bashrc
fi

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly TOP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly OUTPUT_DIR="${PWD}"

function fail
{
  echo "[seekdb-rpm][ERROR] $*" >&2
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

function install_payload
{
  local package_root=$1
  local version=$2
  local release=$3
  local binary="${TOP_DIR}/build_release/src/observer/seekdb"
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
  require_file "${TOP_DIR}/src/sql/fill_help_tables-ob.sql"
  require_file "${profile_dir}/telemetry.sh.template"

  install -d \
    "${package_root}/usr/bin" \
    "${package_root}/usr/lib/systemd/system" \
    "${package_root}/usr/libexec/seekdb/scripts" \
    "${package_root}/usr/share/seekdb/admin" \
    "${package_root}/usr/share/seekdb/help" \
    "${package_root}/usr/share/seekdb/timezone" \
    "${package_root}/usr/share/seekdb/srs" \
    "${package_root}/etc/seekdb"

  install -m 0755 "${binary}" "${package_root}/usr/bin/seekdb"
  install -m 0644 "${profile_dir}/seekdb.service" \
    "${package_root}/usr/lib/systemd/system/seekdb.service"
  install -m 0755 \
    "${TOP_DIR}/tools/import_time_zone_info.py" \
    "${TOP_DIR}/tools/import_srs_data.py" \
    "${package_root}/usr/libexec/seekdb/"
  install -m 0755 \
    "${profile_dir}/seekdb_systemd_start" \
    "${profile_dir}/seekdb_systemd_stop" \
    "${profile_dir}/pre_install.sh" \
    "${profile_dir}/post_install.sh" \
    "${profile_dir}/pre_uninstall.sh" \
    "${profile_dir}/post_uninstall.sh" \
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
  install -m 0644 "${TOP_DIR}/src/sql/fill_help_tables-ob.sql" \
    "${package_root}/usr/share/seekdb/help/"
  install -m 0644 \
    "${TOP_DIR}/tools/timezone_V1.log" \
    "${TOP_DIR}/tools/timezone.data" \
    "${TOP_DIR}/tools/timezone_name.data" \
    "${TOP_DIR}/tools/timezone_trans.data" \
    "${TOP_DIR}/tools/timezone_trans_type.data" \
    "${package_root}/usr/share/seekdb/timezone/"
  install -m 0644 \
    "${TOP_DIR}/tools/spatial_reference_systems.data" \
    "${TOP_DIR}/tools/default_srs_data_mysql.sql" \
    "${package_root}/usr/share/seekdb/srs/"
}

(( $# == 4 )) || usage
readonly PROJECT_NAME=$2
readonly VERSION=$3
readonly RELEASE=$4

[[ "${PROJECT_NAME}" == "seekdb" ]] ||
  fail "unsupported project '${PROJECT_NAME}'; only seekdb is packaged"
[[ "$(uname -s)" == "Linux" ]] ||
  fail "seekdb RPM packaging is supported only on Linux"
[[ "${VERSION}" =~ ^[0-9A-Za-z.+~_]+$ ]] ||
  fail "invalid RPM version: ${VERSION}"
[[ "${RELEASE}" =~ ^[0-9A-Za-z.+~_]+$ ]] ||
  fail "invalid RPM release: ${RELEASE}"
command -v rpmbuild >/dev/null 2>&1 ||
  fail "rpmbuild is required to build the seekdb RPM"

readonly WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/seekdb-rpm.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT
readonly RPM_TOPDIR="${WORK_DIR}/rpmbuild"
readonly PACKAGE_ROOT="${RPM_TOPDIR}/SOURCES/payload"
readonly SPEC_PATH="${RPM_TOPDIR}/SPECS/seekdb.spec"

read -r -a MAKE_ARGUMENTS <<< "${MAKE_ARGS:--j$(cpu_count)}"

cd "${TOP_DIR}"
./build.sh release --init
(cd build_release && make "${MAKE_ARGUMENTS[@]}")

[[ -x "${TOP_DIR}/build_release/src/observer/seekdb" ]] ||
  fail "Bazel seekdb binary is missing or not executable"

install -d \
  "${RPM_TOPDIR}/BUILD" \
  "${RPM_TOPDIR}/BUILDROOT" \
  "${RPM_TOPDIR}/RPMS" \
  "${RPM_TOPDIR}/SOURCES" \
  "${RPM_TOPDIR}/SPECS" \
  "${RPM_TOPDIR}/SRPMS"
install_payload "${PACKAGE_ROOT}" "${VERSION}" "${RELEASE}"

cat > "${SPEC_PATH}" <<EOF
Name: seekdb
Version: ${VERSION}
Release: ${RELEASE}%{?dist}
Summary: seekdb single-node database
License: Apache-2.0
URL: https://www.oceanbase.ai/
Requires: libaio
Requires: systemd
%global debug_package %{nil}

%description
Bazel release Unity build of the seekdb server.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a %{_sourcedir}/payload/. %{buildroot}/

%post
/usr/libexec/seekdb/scripts/post_install.sh "\$1" || :

%preun
/usr/libexec/seekdb/scripts/pre_uninstall.sh "\$1" || :

%files
%defattr(-,root,root,-)
/usr/bin/seekdb
/usr/lib/systemd/system/seekdb.service
/usr/libexec/seekdb
/usr/share/seekdb
%dir /etc/seekdb
%config(noreplace) /etc/seekdb/seekdb.cnf
/etc/seekdb/default_parameter.json
/etc/seekdb/default_system_variable.json
/etc/seekdb/oceanbase-pre.json
/etc/seekdb/telemetry-pre.json
EOF

rpmbuild --define "_topdir ${RPM_TOPDIR}" -bb "${SPEC_PATH}"

mapfile -t BUILT_PACKAGES < <(
  find "${RPM_TOPDIR}/RPMS" -type f -name 'seekdb-*.rpm' -print
)
(( ${#BUILT_PACKAGES[@]} == 1 )) ||
  fail "expected one seekdb RPM, found ${#BUILT_PACKAGES[@]}"
install -m 0644 "${BUILT_PACKAGES[0]}" "${OUTPUT_DIR}/"
echo "[seekdb-rpm] package: ${OUTPUT_DIR}/$(basename "${BUILT_PACKAGES[0]}")"
