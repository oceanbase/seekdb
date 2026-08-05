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

(( $# == 4 )) || usage
readonly PROJECT_DIR=$1
readonly PROJECT_NAME=$2
readonly VERSION=$3
readonly RELEASE=$4

[[ "${PROJECT_NAME}" == "seekdb" ]] ||
  fail "unsupported project '${PROJECT_NAME}'; only seekdb is packaged"
[[ "$(uname -s)" == "Linux" ]] ||
  fail "seekdb RPM packaging is supported only on Linux"
[[ "${VERSION}" =~ ^[0-9A-Za-z.+~_]+$ ]] || fail "invalid RPM version: ${VERSION}"
[[ "${RELEASE}" =~ ^[0-9A-Za-z.+~_]+$ ]] || fail "invalid RPM release: ${RELEASE}"
command -v rpmbuild >/dev/null 2>&1 || fail "rpmbuild is required"

declare -a cmake_args=(
  "-DSEEKDB_PACKAGE_VERSION=${VERSION}"
  "-DOB_RELEASEID=${RELEASE}"
  "-DBUILD_NUMBER=${RELEASE}"
  -DUSE_LTO_CACHE=ON
)
if [[ "${OB_DISABLE_LSE:-0}" == "1" ]]; then
  cmake_args+=(-DOB_DISABLE_LSE=ON)
fi

echo "[seekdb-rpm] source=${PROJECT_DIR} version=${VERSION} release=${RELEASE}"
cd "${TOP_DIR}"
./build.sh clean
./build.sh rpm "${cmake_args[@]}" --init --make rpm

shopt -s nullglob
packages=("${TOP_DIR}/build_rpm"/*.rpm)
shopt -u nullglob
(( ${#packages[@]} > 0 )) || fail "build_rpm did not produce an RPM"
for package in "${packages[@]}"; do
  install -m 0644 "${package}" "${OUTPUT_DIR}/"
  echo "[seekdb-rpm] package: ${OUTPUT_DIR}/$(basename "${package}")"
done
