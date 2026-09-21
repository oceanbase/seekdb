#!/usr/bin/env python3
# Copyright (c) 2025 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Emit a CMake fragment from seekdb's data-only Bazel source inventories.

The Bazel inventory files are deliberately restricted to literal assignments
and ``struct(...)`` records.  This reader accepts only that subset; it never
executes Starlark or repository code.  CMake can therefore share Bazel's
production source membership and exact Unity boundaries without requiring a
Bazel installation or maintaining a second hand-written source list.
"""

import argparse
import ast
import re
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Sequence, Tuple


# Python 3.14 removed these deprecated AST compatibility node classes.  Keep
# accepting them when running on older Python versions, where literal nodes may
# still be represented by the legacy classes.
_AST_STR = getattr(ast, "Str", None)
_AST_NUM = getattr(ast, "Num", None)
_AST_NAME_CONSTANT = getattr(ast, "NameConstant", None)


class InventoryError(RuntimeError):
    """Raised when an inventory no longer matches the supported data subset."""


def _read_value(node: ast.AST) -> Any:
    if isinstance(node, ast.Constant) and isinstance(node.value, (str, type(None))):
        return node.value
    if _AST_STR is not None and isinstance(node, _AST_STR):
        return node.s
    if (
        _AST_NAME_CONSTANT is not None
        and isinstance(node, _AST_NAME_CONSTANT)
        and node.value is None
    ):
        return None
    if (
        isinstance(node, ast.Constant)
        and isinstance(node.value, int)
        and not isinstance(node.value, bool)
    ):
        return node.value
    if (
        _AST_NUM is not None
        and isinstance(node, _AST_NUM)
        and isinstance(node.n, int)
        and not isinstance(node.n, bool)
    ):
        return node.n
    if isinstance(node, ast.List):
        return [_read_value(item) for item in node.elts]
    if isinstance(node, ast.Tuple):
        return tuple(_read_value(item) for item in node.elts)
    if isinstance(node, ast.Dict):
        return {
            _read_value(key): _read_value(value)
            for key, value in zip(node.keys, node.values)
        }
    if (
        isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "struct"
        and not node.args
    ):
        if any(keyword.arg is None for keyword in node.keywords):
            raise InventoryError("struct ** expansion is not supported")
        return {keyword.arg: _read_value(keyword.value) for keyword in node.keywords}
    raise InventoryError(
        "unsupported inventory expression at line %s: %s"
        % (getattr(node, "lineno", "?"), ast.dump(node, include_attributes=False))
    )


def _read_assignments(path: Path) -> Dict[str, Any]:
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    except (OSError, SyntaxError) as exc:
        raise InventoryError("cannot parse %s: %s" % (path, exc)) from exc

    result: Dict[str, Any] = {}
    for statement in tree.body:
        if (
            isinstance(statement, ast.Expr)
            and (
                (
                    isinstance(statement.value, ast.Constant)
                    and isinstance(statement.value.value, str)
                )
                or (
                    _AST_STR is not None
                    and isinstance(statement.value, _AST_STR)
                )
            )
        ):
            continue
        if (
            isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
        ):
            name = statement.targets[0].id
            result[name] = _read_value(statement.value)
            continue
        raise InventoryError(
            "%s:%s is executable Starlark; source inventories must stay data-only"
            % (path, getattr(statement, "lineno", "?"))
        )
    return result


GROUP_SPECS: Sequence[Tuple[str, str, str]] = (
    ("SEEKDB_SHARE_UNITY", "src/share/share_source_inventory.bzl", "SHARE_UNITY_GROUPS"),
    ("SEEKDB_SQL_UNITY", "src/sql/sql_source_inventory.bzl", "SQL_UNITY_GROUPS"),
    ("SEEKDB_SQL_SIMD_UNITY", "src/sql/sql_source_inventory.bzl", "SQL_SIMD_UNITY_GROUPS"),
    ("SEEKDB_STORAGE_UNITY", "src/storage/storage_source_inventory.bzl", "STORAGE_UNITY_GROUPS"),
    ("SEEKDB_STORAGE_SIMD_UNITY", "src/storage/storage_source_inventory.bzl", "STORAGE_SIMD_UNITY_GROUPS"),
    ("SEEKDB_PL_UNITY", "src/pl/pl_source_inventory.bzl", "PL_UNITY_GROUPS"),
    ("SEEKDB_ROOTSERVER_UNITY", "src/rootserver/rootserver_source_inventory.bzl", "ROOTSERVER_UNITY_GROUPS"),
    ("SEEKDB_LOGSERVICE_UNITY", "src/logservice/logservice_build_defs.bzl", "LOGSERVICE_UNITY_GROUPS"),
    ("SEEKDB_OBSERVER_UNITY", "src/observer/observer_source_inventory.bzl", "OBSERVER_UNITY_GROUPS"),
)

GROUP_DICT_SPECS: Sequence[Tuple[str, str, str, str]] = (
    ("SEEKDB_OBLIB_COMMON_UNITY", "src/oblib/oblib_source_inventory.bzl", "OBLIB_UNITY_GROUPS", "src/oblib/common:oblib_common"),
    ("SEEKDB_OBLIB_MALLOC_UNITY", "src/oblib/oblib_source_inventory.bzl", "OBLIB_UNITY_GROUPS", "src/oblib/lib:ob_malloc_object"),
    ("SEEKDB_OBLIB_LIB_UNITY", "src/oblib/oblib_source_inventory.bzl", "OBLIB_UNITY_GROUPS", "src/oblib/lib:oblib_lib"),
    ("SEEKDB_OBLIB_BITMAP_UNITY", "src/oblib/oblib_source_inventory.bzl", "OBLIB_UNITY_GROUPS", "src/oblib/lib:oblib_lib_bitmap"),
    ("SEEKDB_OBLIB_SIMD_UNITY", "src/oblib/oblib_source_inventory.bzl", "OBLIB_UNITY_GROUPS", "src/oblib/lib:oblib_lib_simd"),
    ("SEEKDB_OBLIB_RPC_UNITY", "src/oblib/oblib_source_inventory.bzl", "OBLIB_UNITY_GROUPS", "src/oblib/rpc:oblib_rpc"),
)

SOURCE_SPECS: Sequence[Tuple[str, str, str]] = (
    ("SEEKDB_SHARE_STANDALONE", "src/share/share_source_inventory.bzl", "SHARE_STANDALONE_SOURCES"),
    ("SEEKDB_SHARE_DATUM_STANDALONE", "src/share/share_source_inventory.bzl", "SHARE_DATUM_STANDALONE_SOURCES"),
    ("SEEKDB_SQL_STANDALONE", "src/sql/sql_source_inventory.bzl", "SQL_STANDALONE_SOURCES"),
    ("SEEKDB_SQL_EXTRA", "src/sql/sql_source_inventory.bzl", "SQL_EXTRA_SOURCES"),
    ("SEEKDB_STORAGE_STANDALONE", "src/storage/storage_source_inventory.bzl", "STORAGE_STANDALONE_SOURCES"),
    ("SEEKDB_STORAGE_EXTRA", "src/storage/storage_source_inventory.bzl", "STORAGE_EXTRA_SOURCES"),
    ("SEEKDB_PL_STANDALONE", "src/pl/pl_source_inventory.bzl", "PL_STANDALONE_SOURCES"),
    ("SEEKDB_ROOTSERVER_STANDALONE", "src/rootserver/rootserver_source_inventory.bzl", "ROOTSERVER_STANDALONE_SOURCES"),
    ("SEEKDB_OBSERVER_STANDALONE", "src/observer/observer_source_inventory.bzl", "OBSERVER_STANDALONE_SOURCES"),
    ("SEEKDB_OBSERVER_MAIN", "src/observer/observer_source_inventory.bzl", "OBSERVER_MAIN_SOURCES"),
    ("SEEKDB_OBSERVER_RETRIEVAL_COMPOSITION", "src/observer/observer_source_inventory.bzl", "OBSERVER_RETRIEVAL_COMPOSITION_SOURCES"),
)

SOURCE_DICT_SPECS: Sequence[Tuple[str, str, str, str]] = (
    ("SEEKDB_OBLIB_COMMON_STANDALONE", "src/oblib/oblib_source_inventory.bzl", "OBLIB_STANDALONE_SOURCES", "src/oblib/common:oblib_common"),
    ("SEEKDB_OBLIB_ZSTD_STANDALONE", "src/oblib/oblib_source_inventory.bzl", "OBLIB_STANDALONE_SOURCES", "src/oblib/lib/compress/zstd_1_3_8:zstd_1_3_8_objs"),
    ("SEEKDB_OBLIB_COMPRESS_STANDALONE", "src/oblib/oblib_source_inventory.bzl", "OBLIB_STANDALONE_SOURCES", "src/oblib/lib/compress:compress"),
    ("SEEKDB_OBLIB_RESTORE_STANDALONE", "src/oblib/oblib_source_inventory.bzl", "OBLIB_STANDALONE_SOURCES", "src/oblib/lib/restore:restore"),
    ("SEEKDB_OBLIB_MALLOC_HOOK_STANDALONE", "src/oblib/oblib_source_inventory.bzl", "OBLIB_STANDALONE_SOURCES", "src/oblib/lib:malloc_hook"),
)

RELATIVE_SOURCE_SPECS: Sequence[Tuple[str, str, str, str]] = (
    ("SEEKDB_DATA_PLANE_TABLET_SCAN", "src/data_plane/data_plane_build_defs.bzl", "DATA_PLANE_TABLET_SCAN_SOURCES", "src/data_plane"),
    ("SEEKDB_DATA_PLANE_PARALLEL_RANGE", "src/data_plane/data_plane_build_defs.bzl", "DATA_PLANE_PARALLEL_RANGE_TASK_PLANNER_SOURCES", "src/data_plane"),
    ("SEEKDB_QUERY_MYSQL_PROTOCOL", "src/query/query_build_defs.bzl", "QUERY_MYSQL_PROTOCOL_UTIL_SOURCES", "src/query"),
    ("SEEKDB_QUERY_SCHEDULER", "src/query/query_build_defs.bzl", "QUERY_SCHEDULER_SOURCES", "src/query"),
    ("SEEKDB_QUERY_VECTOR_EMBEDDING", "src/query/query_build_defs.bzl", "QUERY_VECTOR_EMBEDDING_SOURCES", "src/query"),
    ("SEEKDB_STORAGE_TABLET_AUTOINCREMENT_STATE", "src/storage/storage_source_inventory.bzl", "STORAGE_TABLET_AUTOINCREMENT_STATE_SOURCES", "src/storage"),
)

PRETEST_MODULES: Sequence[str] = (
    "oblib",
    "share",
    "storage",
    "sql",
    "rootserver",
    "observer",
    "logservice",
    "pl",
    "query",
    "data_plane",
)


def _pretest_spec(path: Path, module: str) -> Mapping[str, Any]:
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    except (OSError, SyntaxError) as exc:
        raise InventoryError("cannot parse %s: %s" % (path, exc)) from exc

    matches: List[ast.Call] = []
    for statement in tree.body:
        if not isinstance(statement, ast.Expr) or not isinstance(statement.value, ast.Call):
            continue
        call = statement.value
        if isinstance(call.func, ast.Name) and call.func.id == "seekdb_module_cc_test":
            matches.append(call)
    if len(matches) != 1:
        raise InventoryError(
            "%s must contain exactly one seekdb_module_cc_test call" % path
        )

    call = matches[0]
    if call.args or any(keyword.arg is None for keyword in call.keywords):
        raise InventoryError("%s uses unsupported test macro arguments" % path)
    keywords = {keyword.arg: keyword.value for keyword in call.keywords}
    required = ("name", "shard_count")
    missing = [name for name in required if name not in keywords]
    if missing:
        raise InventoryError("%s lacks test fields: %s" % (path, missing))

    name = _read_value(keywords["name"])
    unity_size = _read_value(keywords.get("unity_size", ast.Constant(value=8)))
    shard_count = _read_value(keywords["shard_count"])
    exceptions = _read_value(
        keywords.get("unity_exceptions", ast.Dict(keys=[], values=[]))
    )
    if name != module + "_tests":
        raise InventoryError("%s has unexpected test target %r" % (path, name))
    if not isinstance(unity_size, int) or unity_size <= 0:
        raise InventoryError("%s has invalid unity_size %r" % (path, unity_size))
    if not isinstance(shard_count, int) or shard_count <= 0:
        raise InventoryError("%s has invalid shard_count %r" % (path, shard_count))
    if not isinstance(exceptions, dict) or not all(
        isinstance(source, str)
        and source
        and isinstance(reason, str)
        and reason.strip()
        for source, reason in exceptions.items()
    ):
        raise InventoryError("%s has invalid unity_exceptions" % path)
    return {
        "name": name,
        "unity_size": unity_size,
        "shard_count": shard_count,
        "unity_exceptions": sorted(exceptions),
    }


def _cmake_name(value: str) -> str:
    name = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not name or name[0].isdigit():
        name = "_" + name
    return name


def _source_path(record: Any, context: str) -> str:
    if isinstance(record, str):
        path = record
    elif isinstance(record, dict) and isinstance(record.get("path"), str):
        path = record["path"]
        if record.get("kind") not in ("source", "generated"):
            raise InventoryError("%s has unsupported kind: %r" % (context, record.get("kind")))
    else:
        raise InventoryError("%s is not a source record: %r" % (context, record))
    if not path.startswith("src/") or ".." in Path(path).parts:
        raise InventoryError("%s is outside src/: %s" % (context, path))
    return path


def _cmake_path(path: str) -> str:
    if path.startswith("src/share/inner_table/ob_inner_table_schema."):
        return "${CMAKE_BINARY_DIR}/generated/" + path[len("src/"):]
    if path == "src/observer/virtual_table/ob_all_virtual_sqlite_tables.cpp":
        return "${CMAKE_BINARY_DIR}/generated/" + path[len("src/"):]
    return "${CMAKE_SOURCE_DIR}/" + path


def _emit_set(lines: List[str], name: str, values: Iterable[str]) -> None:
    lines.append("set(%s" % name)
    for value in values:
        lines.append('  "%s"' % _cmake_path(value))
    lines.append(")")
    lines.append("")


def emit(repo: Path, output: Path) -> None:
    cache: Dict[Path, Dict[str, Any]] = {}

    def assignments(relative: str) -> Dict[str, Any]:
        path = repo / relative
        if path not in cache:
            cache[path] = _read_assignments(path)
        return cache[path]

    lines = [
        "# Generated by tools/cmake/emit_bazel_source_inventory.py; do not edit.",
        "# Bazel source inventories are the single source of truth.",
        "",
    ]
    owned: Dict[str, str] = {}

    group_inputs: List[Tuple[str, str, str, Any]] = []
    for prefix, relative, variable in GROUP_SPECS:
        data = assignments(relative)
        groups = data.get(variable)
        group_inputs.append((prefix, relative, variable, groups))
    for prefix, relative, variable, key in GROUP_DICT_SPECS:
        data = assignments(relative)
        groups_by_owner = data.get(variable)
        if not isinstance(groups_by_owner, dict):
            raise InventoryError("%s:%s is not a dict" % (relative, variable))
        group_inputs.append((prefix, relative, "%s[%r]" % (variable, key), groups_by_owner.get(key)))

    for prefix, relative, variable, groups in group_inputs:
        if not isinstance(groups, list):
            raise InventoryError("%s:%s is not a list" % (relative, variable))
        group_names: List[str] = []
        for index, group in enumerate(groups):
            context = "%s:%s[%d]" % (relative, variable, index)
            if not isinstance(group, dict):
                raise InventoryError("%s is not a struct" % context)
            name = group.get("name")
            sources = group.get("srcs")
            if not isinstance(name, str) or not isinstance(sources, list):
                raise InventoryError("%s lacks name/srcs" % context)
            if group.get("generated_srcs") not in (None, []):
                raise InventoryError("%s has generated_srcs not supported by CMake" % context)
            if group.get("external_srcs") not in (None, []):
                raise InventoryError("%s has external_srcs not supported by CMake" % context)
            language = group.get("language")
            if language not in ("c", "c++"):
                raise InventoryError("%s has unsupported language %r" % (context, language))
            cmake_group = _cmake_name(name + "_" + language)
            if cmake_group in group_names:
                raise InventoryError("duplicate CMake Unity group %s in %s" % (cmake_group, prefix))
            group_names.append(cmake_group)
            paths = [_source_path(source, context) for source in sources]
            for path in paths:
                if path in owned:
                    raise InventoryError("%s is owned by both %s and %s" % (path, owned[path], context))
                owned[path] = context
            _emit_set(lines, "%s_GROUP_%s" % (prefix, cmake_group), paths)
        lines.append("set(%s_GROUPS %s)" % (prefix, " ".join(group_names)))
        lines.append("")

    source_inputs: List[Tuple[str, str, str, Any]] = []
    for name, relative, variable in SOURCE_SPECS:
        data = assignments(relative)
        records = data.get(variable)
        source_inputs.append((name, relative, variable, records))
    for name, relative, variable, key in SOURCE_DICT_SPECS:
        data = assignments(relative)
        records_by_owner = data.get(variable)
        if not isinstance(records_by_owner, dict):
            raise InventoryError("%s:%s is not a dict" % (relative, variable))
        source_inputs.append((name, relative, "%s[%r]" % (variable, key), records_by_owner.get(key)))

    for name, relative, variable, records in source_inputs:
        if not isinstance(records, list):
            raise InventoryError("%s:%s is not a list" % (relative, variable))
        paths = [_source_path(record, "%s:%s" % (relative, variable)) for record in records]
        for path in paths:
            if path in owned:
                raise InventoryError("%s is owned by both %s and %s:%s" % (path, owned[path], relative, variable))
            owned[path] = "%s:%s" % (relative, variable)
        _emit_set(lines, name, paths)

    for name, relative, variable, package in RELATIVE_SOURCE_SPECS:
        data = assignments(relative)
        records = data.get(variable)
        if not isinstance(records, list) or not all(isinstance(path, str) for path in records):
            raise InventoryError("%s:%s is not a string list" % (relative, variable))
        paths = []
        for record in records:
            if not record or record.startswith("/") or ".." in Path(record).parts:
                raise InventoryError("%s:%s escapes its package: %s" % (relative, variable, record))
            path = package + "/" + record
            if path in owned:
                raise InventoryError("%s is owned by both %s and %s:%s" % (path, owned[path], relative, variable))
            owned[path] = "%s:%s" % (relative, variable)
            paths.append(path)
        _emit_set(lines, name, paths)

    pretest_modules = [
        module
        for module in PRETEST_MODULES
        if (repo / "unittest" / module / "BUILD.bazel").is_file()
    ]
    lines.append("set(SEEKDB_PRETEST_MODULES %s)" % " ".join(pretest_modules))
    lines.append("")
    for module in pretest_modules:
        relative = "unittest/%s/BUILD.bazel" % module
        spec = _pretest_spec(repo / relative, module)
        prefix = "SEEKDB_PRETEST_%s" % _cmake_name(module).upper()
        exception_paths = [
            "unittest/%s/%s" % (module, source)
            for source in spec["unity_exceptions"]
        ]
        for path in exception_paths:
            if not (repo / path).is_file():
                raise InventoryError(
                    "%s names missing Unity exception %s" % (relative, path)
                )
        lines.append('set(%s_TARGET "%s")' % (prefix, spec["name"]))
        lines.append("set(%s_UNITY_SIZE %d)" % (prefix, spec["unity_size"]))
        lines.append("set(%s_SHARD_COUNT %d)" % (prefix, spec["shard_count"]))
        _emit_set(lines, "%s_UNITY_EXCEPTIONS" % prefix, exception_paths)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text("\n".join(lines), encoding="utf-8")
    temporary.replace(output)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        emit(args.repo.resolve(), args.output.resolve())
    except InventoryError as exc:
        print("source inventory error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
