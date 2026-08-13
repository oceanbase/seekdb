#!/usr/bin/env bash

# One-command verification for Bazel module boundaries and native targets.

set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BAZEL="${ROOT}/bazel.py"
readonly TMP_DIR="$(mktemp -d)"

trap 'rm -rf "${TMP_DIR}"' EXIT

python3 "${ROOT}/tools/module_check/module_layer_check.py" \
  "${ROOT}" --strict-boundaries --strict-bridges
printf '[OK] source dependency ratchets passed\n'

python3 "${ROOT}/tools/module_check/share_terminal_check.py" "${ROOT}"
printf '[OK] Share terminal architecture constraints passed\n'

python3 "${ROOT}/tools/module_check/oblib_terminal_check.py" "${ROOT}"
printf '[OK] OBLib terminal architecture constraints passed\n'

python3 "${ROOT}/tools/module_check/share_header_ownership_check.py" "${ROOT}"
printf '[OK] Share public/private header ownership passed\n'

if [[ -d "${ROOT}/bazel/migration" ||
      -e "${ROOT}/bazel/seekdb_unity_target.bzl" ]]; then
  printf 'repository-wide Bazel migration compatibility rules still exist\n' >&2
  exit 1
fi
migration_refs="$(
  grep -R -n -F '//bazel/migration:' \
    --include='BUILD' --include='BUILD.bazel' --include='*.bzl' \
    "${ROOT}/bazel" "${ROOT}/src" 2>/dev/null || true
)"
if [[ -n "${migration_refs}" ]]; then
  printf 'native Bazel declarations still reference the migration layer:\n%s\n' \
    "${migration_refs}" >&2
  exit 1
fi
printf '[OK] repository-wide Bazel migration compatibility layer is absent\n'

python3 "${ROOT}/tools/module_check/storage_header_ownership_check.py" \
  "${ROOT}"
printf '[OK] Storage header ownership and SQL include policy passed\n'

python3 "${ROOT}/tools/module_check/sql_source_ownership_check.py" "${ROOT}"
python3 "${ROOT}/tools/module_check/sql_header_ownership_check.py" "${ROOT}"
printf '[OK] SQL source and public/private header ownership passed\n'

python3 "${ROOT}/tools/module_check/logservice_source_ownership_check.py" \
  "${ROOT}"
printf '[OK] Logservice source ownership passed\n'

python3 "${ROOT}/tools/module_check/syspack_manifest_check.py" "${ROOT}"
printf '[OK] syspack BUILD manifest matches syspack_config\n'

python3 "${ROOT}/tools/module_check/runtime_seam_definition_check.py" \
  "${ROOT}"
printf '[OK] cross-module runtime seams retain concrete definitions\n'

"${ROOT}/tools/bazel_migration/toolchain_contract_check.sh"

"${BAZEL}" cquery \
  'kind("source file", deps(//src/observer:seekdb))' \
  --output=label 2>/dev/null |
  sed 's/ (null)$//' |
  python3 "${ROOT}/tools/bazel_migration/source_input_check.py" "${ROOT}"
printf '[OK] every source file in the production Bazel graph exists\n'

"${BAZEL}" build //src/observer:seekdb_source_ownership
printf '[OK] every production source has exactly one Unity owner\n'

oblib_upward="$(${BAZEL} query \
  'deps((//src/oblib:* + //src/oblib/easy:*) except //src/oblib/easy:bazel_pilot_illegal_src_dependency) intersect (//src/... except //src/oblib/...)' \
  --output=label 2>/dev/null)"
if [[ -n "${oblib_upward}" ]]; then
  printf 'OBLib has upward Bazel dependencies:\n%s\n' \
    "${oblib_upward}" >&2
  exit 1
fi
printf '[OK] OBLib has no Bazel dependency on peer source modules\n'

printf '[OK] removed deps roots have no generated dependency ownership\n'

expect_failure()
{
  local target="$1"
  local expected="$2"
  local log_name
  local log

  log_name="$(printf '%s' "${target}" | tr '/:' '__')"
  log="${TMP_DIR}/${log_name}.log"
  if "${BAZEL}" build "${target}" >"${log}" 2>&1; then
    printf 'expected failure but build succeeded: %s\n' "${target}" >&2
    return 1
  fi
  if ! grep -Fq "${expected}" "${log}"; then
    printf 'failure did not contain expected text: %s\n' "${target}" >&2
    cat "${log}" >&2
    return 1
  fi
  printf '[OK] rejected %s\n' "${target}"
}

expect_failure \
  //src/sql/bazel_pilot:bazel_pilot_illegal_sql_to_pl \
  "module dependency violation"
printf '[OK] central module policy rejects SQL -> PL independently of visibility\n'

expect_failure \
  //src/pl:bazel_pilot_illegal_observer_dependency \
  "module dependency violation"
expect_failure \
  //src/pl:bazel_pilot_illegal_rootserver_dependency \
  "module dependency violation"
printf '[OK] central module policy rejects PL -> Observer/Rootserver independently of visibility\n'

expect_failure \
  //src/rootserver:module_policy_illegal_observer_dependency \
  "module dependency violation"
expect_failure \
  //src/storage:module_policy_illegal_observer_dependency \
  "module dependency violation"
printf '[OK] central module policy rejects Rootserver/Storage -> Observer independently of visibility\n'

expect_no_dependency_path()
{
  local from="$1"
  local to="$2"
  local path

  path="$(
    "${BAZEL}" query "somepath(${from}, ${to})" --output=label 2>/dev/null
  )"
  if [[ -n "${path}" ]]; then
    printf 'unexpected dependency path from %s to %s:\n%s\n' \
      "${from}" "${to}" "${path}" >&2
    return 1
  fi
  printf '[OK] no dependency path from %s to %s\n' "${from}" "${to}"
}

# The generated parser C sources use short includes from bazel-out. Compiling
# this target proves its package-private source-header carrier remains intact.
"${BAZEL}" build \
  --output_groups=compilation_outputs \
  //bazel/probes:oblib_common_interface_probe \
  //bazel/probes:oblib_compression_interface_probe \
  //bazel/probes:oblib_foundation_interface_probe \
  //bazel/probes:oblib_restore_advanced_interface_probe \
  //bazel/probes:oblib_rpc_interface_probe \
  //bazel/probes:oblib_vector_interface_probe \
  //bazel/probes:logservice_public_interface_probe \
  //src/sql/parser:_server_parser_c \
  //src/sql/bazel_pilot:prepare_pipeline \
  //src/sql/bazel_pilot:prepare_public_interface_probe \
  //src/sql/bazel_pilot:optimizer_public_interface_probe

compile_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("CppCompile", set(//src/sql/engine/prepare:engine_prepare //src/sql/resolver/prepare:resolver_prepare))' \
    --output=summary 2>&1
)"
if ! grep -Fq "CppCompile: 2" <<<"${compile_summary}"; then
  printf '%s\n' "${compile_summary}" >&2
  printf 'expected exactly two Unity compile actions\n' >&2
  exit 1
fi
printf '[OK] prepare pipeline uses exactly 2 CppCompile actions\n'

optimizer_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", set(//src/sql/optimizer:_optimizer_ob_sql_optimizer_0_objects //src/sql/optimizer:_optimizer_ob_sql_optimizer_1_objects //src/sql/optimizer:_optimizer_ob_sql_optimizer_2_objects //src/sql/optimizer:_optimizer_ob_sql_optimizer_stat_0_objects //src/sql/optimizer:_optimizer_ob_sql_optimizer_stat_1_objects))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 5" <<<"${optimizer_action_summary}"
grep -Fq "CppArchive: 5" <<<"${optimizer_action_summary}"
printf '[OK] Optimizer owns 5 exact-closure Unity compile/archive groups\n'

logservice_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", //src/logservice:logservice)' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 5" <<<"${logservice_action_summary}"
grep -Fq "CppArchive: 1" <<<"${logservice_action_summary}"
printf '[OK] Logservice is one archive with 5 Unity compile actions\n'

oblib_foundation_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("CppCompile", deps(//src/oblib:_oblib_foundation_impl))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 12" <<<"${oblib_foundation_action_summary}"

oblib_common_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("CppCompile", deps(//src/oblib:_oblib_common_impl))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 14" <<<"${oblib_common_action_summary}"

oblib_rpc_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("CppCompile", deps(//src/oblib:_oblib_rpc_impl))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 4" <<<"${oblib_rpc_action_summary}"
printf '[OK] OBLib preserves its 12/14/4 semantic Unity groups\n'

module_targets="$(
  "${BAZEL}" query \
    '//src/...:* + //src/oblib/easy:* + //src/oblib:*' \
    --output=label 2>/dev/null
)"
legacy_escape_targets="$(
  grep -E ':(.*legacy.*headers|.*legacy_public_dependency_closure)' \
    <<<"${module_targets}" || true
)"
if [[ -n "${legacy_escape_targets}" ]]; then
  printf 'legacy header escape targets still exist:\n%s\n' \
    "${legacy_escape_targets}" >&2
  exit 1
fi
printf '[OK] module graph has no legacy header escape target\n'

for native_module in data_plane query objit logservice; do
  native_module_targets="$(
    "${BAZEL}" query "//src/${native_module}:all" --output=label 2>/dev/null
  )"
  if grep -Eq ':_headers|_migration|compatibility.*closure' \
    <<<"${native_module_targets}"; then
    printf '%s still exposes mechanical or migration targets:\n%s\n' \
      "${native_module}" "${native_module_targets}" >&2
    exit 1
  fi
done
printf '[OK] Data Plane, Query, Objit, and Logservice expose native targets only\n'

share_targets="$("${BAZEL}" query '//src/share:all' --output=label 2>/dev/null)"
share_legacy_targets="$(
  grep -E ':_headers_|ob_share_.*migration' <<<"${share_targets}" || true
)"
if [[ -n "${share_legacy_targets}" ]]; then
  printf 'Share still exposes mechanical or migration targets:\n%s\n' \
    "${share_legacy_targets}" >&2
  exit 1
fi
printf '[OK] Share exposes only native semantic targets\n'

share_runtime_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", filter("_share_runtime_.*_objects$", deps(//src/share:share_runtime, 1)))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 54" <<<"${share_runtime_action_summary}"
grep -Fq "CppArchive: 54" <<<"${share_runtime_action_summary}"

share_runtime_c_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", //src/share:share_runtime_c)' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 1" <<<"${share_runtime_c_action_summary}"
grep -Fq "CppArchive: 1" <<<"${share_runtime_c_action_summary}"
printf '[OK] Share owns 55 exact-closure C/C++ Unity compile/archive groups\n'

storage_targets="$("${BAZEL}" query '//src/storage:all' --output=label 2>/dev/null)"
storage_legacy_targets="$(
  grep -E ':_headers_|ob_storage_.*migration' <<<"${storage_targets}" || true
)"
if [[ -n "${storage_legacy_targets}" ]]; then
  printf 'Storage still exposes mechanical or migration targets:\n%s\n' \
    "${storage_legacy_targets}" >&2
  exit 1
fi
printf '[OK] Storage exposes only native semantic targets\n'

storage_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", filter("_storage_runtime_.*_objects$", deps(//src/storage:storage_runtime, 1)) + filter("_storage_runtime_simd_.*_objects$", deps(//src/storage:storage_runtime_simd, 1)) + //src/storage:tablet_autoincrement_state)' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 51" <<<"${storage_action_summary}"
grep -Fq "CppArchive: 51" <<<"${storage_action_summary}"
printf '[OK] Storage owns 51 exact-closure Unity compile/archive groups\n'

pl_targets="$("${BAZEL}" query '//src/pl:all' --output=label 2>/dev/null)"
pl_legacy_targets="$(
  grep -E ':_headers_|ob_pl_.*migration|syspack_source_migration' \
    <<<"${pl_targets}" || true
)"
if [[ -n "${pl_legacy_targets}" ]]; then
  printf 'PL still exposes mechanical or migration targets:\n%s\n' \
    "${pl_legacy_targets}" >&2
  exit 1
fi
printf '[OK] PL exposes only native semantic targets\n'

pl_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", filter("_pl_runtime_.*_objects$", deps(//src/pl:pl_runtime, 1)) + filter("_pl_parser_c_.*_objects$", deps(//src/pl:pl_parser_c, 1)) + //src/pl:syspack_source)' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 10" <<<"${pl_action_summary}"
grep -Fq "CppArchive: 10" <<<"${pl_action_summary}"
printf '[OK] PL owns 10 exact-closure Unity compile/archive groups\n'

rootserver_targets="$("${BAZEL}" query '//src/rootserver:all' --output=label 2>/dev/null)"
rootserver_legacy_targets="$(
  grep -E ':_headers|ob_rootserver_.*migration' \
    <<<"${rootserver_targets}" || true
)"
if [[ -n "${rootserver_legacy_targets}" ]]; then
  printf 'Rootserver still exposes mechanical or migration targets:\n%s\n' \
    "${rootserver_legacy_targets}" >&2
  exit 1
fi
printf '[OK] Rootserver exposes only native semantic targets\n'

rootserver_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", filter("_rootserver_runtime_.*_objects$", deps(//src/rootserver:rootserver_runtime, 1)))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 17" <<<"${rootserver_action_summary}"
grep -Fq "CppArchive: 17" <<<"${rootserver_action_summary}"
printf '[OK] Rootserver owns 17 exact-closure Unity compile/archive groups\n'

sql_targets="$(${BAZEL} query '//src/sql/...' --output=label 2>/dev/null)"
sql_legacy_targets="$(
  grep -E ':_headers|ob_sql_.*migration' <<<"${sql_targets}" || true
)"
if [[ -n "${sql_legacy_targets}" ]]; then
  printf 'SQL still exposes mechanical or migration targets:\n%s\n' \
    "${sql_legacy_targets}" >&2
  exit 1
fi
printf '[OK] SQL exposes only native semantic targets\n'

sql_action_summary="$(
  "${BAZEL}" aquery \
    'mnemonic("Cpp(Compile|Archive)", filter("_sql_runtime_.*_objects$", deps(//src/sql:sql_runtime, 1)) + filter("_sql_runtime_simd_.*_objects$", deps(//src/sql:sql_runtime_simd, 1)) + filter("_optimizer_.*_objects$", deps(//src/sql/optimizer:optimizer, 1)) + set(//src/sql/engine/prepare:engine_prepare //src/sql/resolver/prepare:resolver_prepare //src/sql/parser:_server_parser_c //src/sql/parser:_server_parser_cxx))' \
    --output=summary 2>&1
)"
grep -Fq "CppCompile: 126" <<<"${sql_action_summary}"
grep -Fq "CppArchive: 108" <<<"${sql_action_summary}"
printf '[OK] SQL owns 126 Unity compile actions in exact-closure archives\n'

observer_targets="$(${BAZEL} query '//src/observer:all' --output=label 2>/dev/null)"
observer_legacy_targets="$(
  grep -E ':_headers|_migration' <<<"${observer_targets}" || true
)"
if [[ -n "${observer_legacy_targets}" ]]; then
  printf 'Observer still exposes mechanical or migration targets:\n%s\n' \
    "${observer_legacy_targets}" >&2
  exit 1
fi
printf '[OK] Observer exposes only native composition targets\n'

expect_no_dependency_path \
  //src/storage:storage_static \
  //src/sql/optimizer:optimizer
expect_no_dependency_path \
  //src/storage:storage_static \
  //src/sql:sql_static
expect_no_dependency_path \
  //src/logservice:logservice \
  //src/sql/optimizer:optimizer
expect_no_dependency_path \
  //src/share:share_static \
  //src/logservice:logservice
expect_no_dependency_path \
  //src/pl:pl_static \
  //src/observer:observer_runtime
expect_no_dependency_path \
  //src/pl:pl_static \
  //src/rootserver:rootserver_static
expect_no_dependency_path \
  //src/rootserver:rootserver_static \
  //src/observer:observer_runtime
expect_no_dependency_path \
  //src/storage:storage_static \
  //src/observer:observer_runtime

rootserver_composition_path="$(
  "${BAZEL}" cquery \
    'somepath(//src/observer:seekdb_link, //src/rootserver:rootserver_static)' \
    --output=label 2>&1
)"
grep -Fq "//src/observer:seekdb_link" <<<"${rootserver_composition_path}"
grep -Fq "//src/rootserver:rootserver_static" <<<"${rootserver_composition_path}"
printf '[OK] dependency path is Observer -> Rootserver with no reverse path\n'

dependency_path="$(
  "${BAZEL}" query \
    'somepath(//src/sql/engine/prepare:engine_prepare, //src/sql/resolver/prepare:resolver_prepare)' \
    --output=label 2>&1
)"
grep -Fq "//src/sql/engine/prepare:engine_prepare" <<<"${dependency_path}"
grep -Fq "//src/sql/resolver/prepare:resolver_prepare" <<<"${dependency_path}"
printf '[OK] dependency path is engine/prepare -> resolver/prepare\n'

expect_failure \
  //src/sql/resolver/prepare:bazel_pilot_illegal_reverse_dependency \
  "Visibility error"
expect_failure \
  //src/sql/bazel_pilot:bazel_pilot_illegal_resolver_implementation \
  "Visibility error"
expect_failure \
  //src/storage:bazel_pilot_illegal_sql_dependency \
  "Visibility error"
expect_failure \
  //src/storage:bazel_pilot_illegal_sql_engine_dependency \
  "Visibility error"
expect_failure \
  //src/storage:bazel_pilot_illegal_sql_optimizer_dependency \
  "Visibility error"
expect_failure \
  //src/logservice:bazel_pilot_illegal_sql_optimizer_dependency \
  "Visibility error"
expect_failure \
  //src/oblib/easy:bazel_pilot_illegal_src_dependency \
  "module dependency violation"
expect_failure \
  //bazel/probes:oblib_private_header_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:observer_private_header_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:pl_private_header_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:rootserver_private_header_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:sql_private_header_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:storage_private_header_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:oblib_private_implementation_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:observer_private_implementation_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:pl_private_implementation_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:rootserver_private_implementation_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:share_private_implementation_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:sql_private_implementation_probe \
  "Visibility error"
expect_failure \
  //bazel/probes:storage_private_implementation_probe \
  "Visibility error"
expect_failure \
  //src/sql/bazel_pilot:optimizer_private_header_probe \
  "file not found"
expect_failure \
  //src/sql/bazel_pilot:bazel_pilot_undeclared_resolver_header \
  "file not found"
expect_failure \
  //src/sql/bazel_pilot:bazel_pilot_transitive_storage_header \
  "file not found"

printf '[OK] Bazel first-level module boundary verification passed\n'
