#!/usr/bin/env python3
"""Verify that Share has reached its terminal Bazel architecture.

The check intentionally has no migration baseline or allowlist.  It parses the
parts of BUILD syntax relevant to target declarations and dependencies, and
follows file-local list variables so an upward dependency cannot be hidden
behind an aggregate such as ``_SHARE_RUNTIME_DEPS``.
"""

import ast
import io
import os
import re
import sys
import tokenize
from dataclasses import dataclass
from pathlib import Path


UPPER_MODULES = {
    "data_plane",
    "logservice",
    "objit",
    "observer",
    "pl",
    "query",
    "rootserver",
    "sql",
    "storage",
}
DEPENDENCY_ATTRIBUTES = {
    "actual",
    "deps",
    "exports",
    "implementation_deps",
    "interface_deps",
    "runtime_deps",
    "whole_archive_deps",
}
HEADER_ATTRIBUTES = {
    "hdrs",
    "private_hdrs",
    "public_hdrs",
    "textual_hdrs",
}
FULL_HEADER_INVENTORIES = {
    "SHARE_INTERFACE_CLOSURE_HEADERS",
    "SHARE_PRIVATE_HEADERS",
    "SHARE_PUBLIC_HEADER_ROOTS",
}
FORBIDDEN_EXACT_TARGETS = {
    "share_runtime_interface",
    "share_runtime_internal_headers",
}
FORBIDDEN_TARGET_PATTERNS = (
    re.compile(r".*_legacy_headers$"),
    re.compile(r".*_headers_root$"),
    re.compile(r".*_migration$"),
    re.compile(r"share_(?:all|full|public|runtime)_headers$"),
    re.compile(r"share_.*headers_(?:aggregate|closure)$"),
)
INTERNAL_TARGET_PATTERNS = (
    re.compile(r".*_(?:internal|private)$"),
    re.compile(r".*_(?:internal|private)_headers$"),
    re.compile(r".*_(?:internal|private)_(?:aggregate|closure)$"),
)
SOURCE_LABEL_PATTERN = re.compile(
    r"(?:@[^/\s]+)?//src/([A-Za-z0-9_]+)"
    r"(?:/[A-Za-z0-9_./+-]+)?(?::[A-Za-z0-9_./+-]+)?"
)
SHARE_LABEL_PATTERN = re.compile(
    r"(?:@[^/\s]+)?//src/share:([A-Za-z0-9_./+-]+)"
)
LOCAL_LABEL_PATTERN = re.compile(r"^:([A-Za-z0-9_./+-]+)$")
SKIPPED_WALK_DIRS = {
    ".git",
    ".idea",
    ".vscode",
    "__pycache__",
    "deps",
}
CPP_SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".ipp",
    ".tcc",
}
CPP_IMPLEMENTATION_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
}
UPPER_INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s*[<"]('
    + "|".join(sorted(UPPER_MODULES))
    + r')(?:/|[>"])'
)
GLOBAL_IMPLEMENTATION_ACCESS_PATTERN = re.compile(
    r"\bg_mp\s*(?:->|\.)"
)
DIRECT_TENANT_LOCATOR_PATTERN = re.compile(
    r"\bMTL(?:_[A-Z0-9_]+)?\s*\("
)
GLOBAL_CONTEXT_LOCATOR_PATTERN = re.compile(r"\bGCTX\b")
GLOBAL_SCHEMA_LOCATOR_PATTERN = re.compile(r"\bGSCHEMASERVICE\b")
SCHEMA_SINGLETON_ACCESS_PATTERN = re.compile(
    r"\bObMultiVersionSchemaService::get_instance\s*\("
)
FORBIDDEN_RUNTIME_ESCAPE_PATTERNS = {
    "ObIModuleProvider": re.compile(r"\bObIModuleProvider\b"),
    "g_mp": re.compile(r"\bg_mp\b"),
    "ob_module_provider.h": re.compile(r"\bob_module_provider\.h\b"),
    "MTL_MEMBERS": re.compile(r"\bMTL_MEMBERS\b"),
    "MTL_WITH_CHECK": re.compile(r"\bMTL_WITH_CHECK\b"),
    "mtl_sop": re.compile(r"\bmtl_sop_[A-Za-z0-9_]*\b"),
}
FORBIDDEN_GCTX_UPPER_FIELDS = {
    "conn_res_mgr_",
    "disk_reporter_",
    "executor_rpc_",
    "in_zone_master_",
    "log_block_mgr_",
    "net_frame_",
    "ob_service_",
    "omt_",
    "pl_engine_",
    "res_inner_conn_pool_",
    "root_service_",
    "session_mgr_",
    "sql_engine_",
    "startup_accel_handler_",
    "vt_iter_creator_",
    "vt_par_ser_",
}
GCTX_UPPER_FIELD_PATTERN = re.compile(
    r"\b(?:GCTX|gctx_|gctx\.)\s*(?:\.|->)?\s*("
    + "|".join(sorted(FORBIDDEN_GCTX_UPPER_FIELDS))
    + r")\b"
)
GCTX_UPPER_TYPE_PATTERN = re.compile(
    r"\b(?:data_plane|logservice|observer|omt|pl|rootserver|sql|storage)::"
)
MOVED_DEFINITION_DEBT_PATTERN = re.compile(
    r"(?:moved\s+definition\s+to|definition\s+moved\s+to).*"
    r"(?:upper-layer|(?:src/)?(?:sql|storage|rootserver|observer|"
    r"logservice|data_plane|query|pl)/)",
    re.IGNORECASE,
)
UPPER_OWNS_SHARE_DEFINITION_PATTERN = re.compile(
    r"(?:definition\s+moved\s+from|moved\s+definition\s+from)"
    r"\s+(?:src/)?share(?:/|\b)",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Expression:
    tokens: tuple
    line: int


@dataclass(frozen=True)
class Rule:
    kind: str
    line: int
    attrs: dict


@dataclass(frozen=True)
class ParsedBuild:
    path: Path
    assignments: dict
    rules: tuple
    tokens: tuple


def _significant(tokens):
    ignored = {
        tokenize.COMMENT,
        tokenize.INDENT,
        tokenize.DEDENT,
        tokenize.NL,
    }
    return [token for token in tokens if token.type not in ignored]


def _matching_paren(tokens, open_index):
    depth = 0
    for index in range(open_index, len(tokens)):
        token = tokens[index]
        if token.type != tokenize.OP:
            continue
        if token.string in "([{":
            depth += 1
        elif token.string in ")]}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(
        "unclosed %s at line %d"
        % (tokens[open_index].string, tokens[open_index].start[0])
    )


def _split_call_attrs(tokens):
    attrs = {}
    start = 0
    depth = 0
    parts = []
    for index, token in enumerate(tokens):
        if token.type == tokenize.OP:
            if token.string in "([{":
                depth += 1
            elif token.string in ")]}":
                depth -= 1
            elif token.string == "," and depth == 0:
                parts.append(tokens[start:index])
                start = index + 1
    parts.append(tokens[start:])

    for part in parts:
        if not part:
            continue
        depth = 0
        equal_index = None
        for index, token in enumerate(part):
            if token.type == tokenize.OP:
                if token.string in "([{":
                    depth += 1
                elif token.string in ")]}":
                    depth -= 1
                elif token.string == "=" and depth == 0:
                    equal_index = index
                    break
        if equal_index is None:
            continue
        left = part[:equal_index]
        if len(left) != 1 or left[0].type != tokenize.NAME:
            continue
        value = tuple(part[equal_index + 1 :])
        line = value[0].start[0] if value else left[0].start[0]
        attrs[left[0].string] = Expression(value, line)
    return attrs


def _parse_build(path):
    text = path.read_text(encoding="utf-8")
    try:
        raw_tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (IndentationError, tokenize.TokenError) as error:
        raise ValueError("cannot tokenize BUILD syntax: %s" % error)
    tokens = _significant(raw_tokens)
    assignments = {}
    rules = []
    index = 0

    while index < len(tokens):
        token = tokens[index]
        if token.type in {tokenize.NEWLINE, tokenize.ENDMARKER}:
            index += 1
            continue
        if token.type != tokenize.NAME or index + 1 >= len(tokens):
            index += 1
            continue
        operator = tokens[index + 1]
        if operator.type != tokenize.OP:
            index += 1
            continue

        if operator.string in {"=", "+="}:
            end = index + 2
            depth = 0
            while end < len(tokens):
                current = tokens[end]
                if current.type == tokenize.OP:
                    if current.string in "([{":
                        depth += 1
                    elif current.string in ")]}":
                        depth -= 1
                if current.type == tokenize.NEWLINE and depth == 0:
                    break
                end += 1
            value = tuple(tokens[index + 2 : end])
            line = value[0].start[0] if value else token.start[0]
            if operator.string == "+=" and token.string in assignments:
                value = assignments[token.string].tokens + value
                line = assignments[token.string].line
            assignments[token.string] = Expression(value, line)
            index = end + 1
            continue

        if operator.string == "(":
            close = _matching_paren(tokens, index + 1)
            body = [
                item
                for item in tokens[index + 2 : close]
                if item.type != tokenize.NEWLINE
            ]
            rules.append(
                Rule(
                    kind=token.string,
                    line=token.start[0],
                    attrs=_split_call_attrs(body),
                )
            )
            index = close + 1
            continue

        index += 1

    return ParsedBuild(
        path=path,
        assignments=assignments,
        rules=tuple(rules),
        tokens=tuple(raw_tokens),
    )


def _literal_strings(expression):
    values = set()
    for token in expression.tokens:
        if token.type != tokenize.STRING:
            continue
        value = _literal_string(token)
        if isinstance(value, str):
            values.add(value)
    return values


def _literal_string(token):
    try:
        return ast.literal_eval(token.string)
    except (SyntaxError, ValueError):
        return None


def _names(expression):
    return {
        token.string
        for token in expression.tokens
        if token.type == tokenize.NAME
    }


def _resolve_expression(expression, assignments, resolving=None):
    resolving = set() if resolving is None else resolving
    values = set(_literal_strings(expression))
    for name in _names(expression):
        if name not in assignments or name in resolving:
            continue
        values.update(
            _resolve_expression(
                assignments[name],
                assignments,
                resolving | {name},
            )
        )
    return values


def _resolve_names(expression, assignments, resolving=None):
    resolving = set() if resolving is None else resolving
    values = set(_names(expression))
    for name in tuple(values):
        if name not in assignments or name in resolving:
            continue
        values.update(
            _resolve_names(
                assignments[name],
                assignments,
                resolving | {name},
            )
        )
    return values


def _rule_name(rule, assignments):
    expression = rule.attrs.get("name")
    if expression is None:
        return None
    values = _resolve_expression(expression, assignments)
    if len(values) == 1:
        return next(iter(values))
    return None


def _dependency_attrs(rule):
    return {
        name: expression
        for name, expression in rule.attrs.items()
        if name in DEPENDENCY_ATTRIBUTES or name.endswith("_deps")
    }


def _forbidden_target_reason(name):
    if name in FORBIDDEN_EXACT_TARGETS:
        return "forbidden runtime-wide compatibility target"
    for pattern in FORBIDDEN_TARGET_PATTERNS:
        if pattern.fullmatch(name):
            return "forbidden legacy/migration/header-root target"
    return None


def _is_internal_target(name):
    return any(pattern.fullmatch(name) for pattern in INTERNAL_TARGET_PATTERNS)


def _is_external_visibility(label):
    if label == "//visibility:private":
        return False
    if label.startswith("//src/share:") or label.startswith("//src/share/"):
        return False
    return True


def _target_from_share_label(value, local):
    if local:
        match = LOCAL_LABEL_PATTERN.fullmatch(value)
        if match:
            return match.group(1)
    match = SHARE_LABEL_PATTERN.fullmatch(value)
    if match:
        return match.group(1)
    return None


def _share_rule_catalog(parsed, errors):
    broad_targets = set()
    internal_targets = set()

    for rule in parsed.rules:
        name = _rule_name(rule, parsed.assignments)
        if name is None:
            continue
        reason = _forbidden_target_reason(name)
        if reason:
            errors.add(
                "%s:%d: %s declared: %s"
                % (parsed.path, rule.line, reason, name)
            )

        header_names = set()
        header_strings = set()
        for attr_name in HEADER_ATTRIBUTES:
            expression = rule.attrs.get(attr_name)
            if expression is None:
                continue
            header_names.update(
                _resolve_names(expression, parsed.assignments)
            )
            header_strings.update(
                _resolve_expression(expression, parsed.assignments)
            )
        inventory_refs = sorted(header_names & FULL_HEADER_INVENTORIES)
        recursive_glob = any("**" in value for value in header_strings)
        if inventory_refs or recursive_glob:
            broad_targets.add(name)
            detail = (
                ", ".join(inventory_refs)
                if inventory_refs
                else "recursive header glob"
            )
            errors.add(
                "%s:%d: broad Share header aggregate declared: %s (%s)"
                % (parsed.path, rule.line, name, detail)
            )

        if _is_internal_target(name) or "SHARE_PRIVATE_HEADERS" in header_names:
            internal_targets.add(name)
            visibility = rule.attrs.get("visibility")
            if visibility is not None:
                labels = _resolve_expression(
                    visibility,
                    parsed.assignments,
                )
                external = sorted(
                    label for label in labels if _is_external_visibility(label)
                )
                if external:
                    errors.add(
                        "%s:%d: Share internal target %s is externally "
                        "visible to %s"
                        % (parsed.path, rule.line, name, external)
                    )

    return broad_targets, internal_targets


def _check_share_build(parsed, forbidden_targets, errors):
    relative = parsed.path
    migration_lines = sorted(
        {
            token.start[0]
            for token in parsed.tokens
            if (
                token.type == tokenize.NAME
                and token.string == "SEEKDB_C_MIGRATION_COPTS"
            )
            or (
                token.type == tokenize.STRING
                and _literal_string(token) == "SEEKDB_C_MIGRATION_COPTS"
            )
        }
    )
    for line in migration_lines:
        errors.add(
            "%s:%d: SEEKDB_C_MIGRATION_COPTS is forbidden in Share"
            % (relative, line)
        )

    for rule in parsed.rules:
        name = _rule_name(rule, parsed.assignments)
        display_name = name or "<unnamed %s>" % rule.kind
        for attr_name, expression in _dependency_attrs(rule).items():
            values = _resolve_expression(expression, parsed.assignments)
            for value in sorted(values):
                match = SOURCE_LABEL_PATTERN.fullmatch(value)
                if match and match.group(1) in UPPER_MODULES:
                    errors.add(
                        "%s:%d: Share target %s attribute %s depends on "
                        "upper module %s"
                        % (
                            relative,
                            rule.line,
                            display_name,
                            attr_name,
                            value,
                        )
                    )

        for attr_name, expression in rule.attrs.items():
            if attr_name == "name":
                continue
            for value in _resolve_expression(
                expression,
                parsed.assignments,
            ):
                target = _target_from_share_label(value, local=True)
                if target is not None and (
                    target in forbidden_targets
                    or _forbidden_target_reason(target) is not None
                ):
                    errors.add(
                        "%s:%d: Share target %s references forbidden target "
                        "%s via %s"
                        % (
                            relative,
                            rule.line,
                            display_name,
                            target,
                            attr_name,
                        )
                    )

    for variable, expression in parsed.assignments.items():
        for value in _literal_strings(expression):
            target = _target_from_share_label(value, local=True)
            if target is not None and (
                target in forbidden_targets
                or _forbidden_target_reason(target) is not None
            ):
                errors.add(
                    "%s:%d: variable %s references forbidden Share target %s"
                    % (relative, expression.line, variable, target)
                )


def _check_external_build(parsed, forbidden_targets, errors):
    for rule in parsed.rules:
        name = _rule_name(rule, parsed.assignments)
        display_name = name or "<unnamed %s>" % rule.kind
        for attr_name, expression in rule.attrs.items():
            if attr_name in {"name", "visibility"}:
                continue
            for value in _resolve_expression(
                expression,
                parsed.assignments,
            ):
                target = _target_from_share_label(value, local=False)
                if target is not None and (
                    target in forbidden_targets
                    or _forbidden_target_reason(target) is not None
                ):
                    errors.add(
                        "%s:%d: external target %s references forbidden "
                        "Share target %s via %s"
                        % (
                            parsed.path,
                            rule.line,
                            display_name,
                            target,
                            attr_name,
                        )
                    )


def _check_layer_debt(path, errors):
    debt_group = None
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(),
        1,
    ):
        stripped = raw_line.strip()
        marker = re.match(
            r"#\s*---\s*(purified|quarantine)\s*:",
            stripped,
            re.IGNORECASE,
        )
        if marker:
            debt_group = marker.group(1).lower()
            continue
        section = re.match(r"\[([A-Za-z0-9_]+)\]", stripped)
        if section:
            name = section.group(1).lower()
            debt_group = name if name in {"purified", "quarantine"} else None
            continue
        if debt_group is None or not stripped or stripped.startswith("#"):
            continue
        path_token = stripped.split("#", 1)[0].split()[0]
        if path_token == "src/share" or path_token.startswith("src/share/"):
            errors.add(
                "%s:%d: Share path remains in %s debt: %s"
                % (path, line_number, debt_group, path_token)
            )


def _iter_share_cpp_files(repo):
    root = repo / "src/share"
    for path in sorted(root.rglob("*")):
        if (
            path.is_file()
            and not path.is_symlink()
            and path.suffix.lower() in CPP_SOURCE_SUFFIXES
        ):
            yield path


def _iter_upper_cpp_files(repo):
    for module in sorted(UPPER_MODULES):
        root = repo / "src" / module
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if (
                path.is_file()
                and not path.is_symlink()
                and path.suffix.lower() in CPP_SOURCE_SUFFIXES
            ):
                yield path


def _iter_all_cpp_files(repo):
    for root_name in ("src", "unittest"):
        root = repo / root_name
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if (
                path.is_file()
                and not path.is_symlink()
                and path.suffix.lower() in CPP_SOURCE_SUFFIXES
            ):
                yield path


def _check_share_sources(repo, errors):
    for path in _iter_share_cpp_files(repo):
        relative = path.relative_to(repo)
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as error:
            errors.add("%s: cannot inspect Share source: %s" % (relative, error))
            continue

        for line_number, line in enumerate(lines, 1):
            code = line.split("//", 1)[0]
            include = UPPER_INCLUDE_PATTERN.search(line)
            if include:
                errors.add(
                    "%s:%d: Share source includes upper module %s"
                    % (relative, line_number, include.group(1))
                )
            if (
                path.suffix.lower() in CPP_IMPLEMENTATION_SUFFIXES
                and GLOBAL_IMPLEMENTATION_ACCESS_PATTERN.search(line)
            ):
                errors.add(
                    "%s:%d: Share implementation accesses an upper "
                    "implementation through a global provider"
                    % (relative, line_number)
                )
            if MOVED_DEFINITION_DEBT_PATTERN.search(line):
                errors.add(
                    "%s:%d: moved-definition transition debt remains in Share"
                    % (relative, line_number)
                )
            if (
                not code.lstrip().startswith("#")
                and DIRECT_TENANT_LOCATOR_PATTERN.search(code)
            ):
                errors.add(
                    "%s:%d: Share implementation uses a hidden tenant locator; "
                    "inject the required value, service, or run wrapper"
                    % (relative, line_number)
                )
            if (
                not code.lstrip().startswith("#")
                and GLOBAL_CONTEXT_LOCATOR_PATTERN.search(code)
            ):
                errors.add(
                    "%s:%d: Share implementation uses the hidden global "
                    "context locator; inject the required runtime dependency"
                    % (relative, line_number)
                )
            if (
                not code.lstrip().startswith("#")
                and GLOBAL_SCHEMA_LOCATOR_PATTERN.search(code)
            ):
                errors.add(
                    "%s:%d: Share implementation uses the hidden global "
                    "schema locator; inject ObMultiVersionSchemaService"
                    % (relative, line_number)
                )
            if (
                not code.lstrip().startswith("#")
                and not (
                    relative
                    == Path(
                        "src/share/schema/"
                        "ob_multi_version_schema_service.cpp"
                    )
                    and code.lstrip().startswith(
                        "ObMultiVersionSchemaService "
                        "&ObMultiVersionSchemaService::get_instance("
                    )
                )
                and SCHEMA_SINGLETON_ACCESS_PATTERN.search(code)
            ):
                errors.add(
                    "%s:%d: Share implementation locates the schema service "
                    "singleton directly; inject ObMultiVersionSchemaService"
                    % (relative, line_number)
                )
            if relative == Path("src/share/ob_server_struct.h"):
                upper_field = re.search(
                    r"\b(" + "|".join(sorted(FORBIDDEN_GCTX_UPPER_FIELDS))
                    + r")\b",
                    line,
                )
                if upper_field:
                    errors.add(
                        "%s:%d: Share-owned GCTX declares upper field %s"
                        % (relative, line_number, upper_field.group(1))
                    )
                if GCTX_UPPER_TYPE_PATTERN.search(line):
                    errors.add(
                        "%s:%d: Share-owned GCTX mentions an upper "
                        "implementation type"
                        % (relative, line_number)
                    )

    for path in _iter_upper_cpp_files(repo):
        relative = path.relative_to(repo)
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as error:
            errors.add(
                "%s: cannot inspect upper-module source: %s"
                % (relative, error)
            )
            continue
        for line_number, line in enumerate(lines, 1):
            if UPPER_OWNS_SHARE_DEFINITION_PATTERN.search(line):
                errors.add(
                    "%s:%d: upper module still owns a definition moved "
                    "out of Share"
                    % (relative, line_number)
                )

    for path in _iter_all_cpp_files(repo):
        relative = path.relative_to(repo)
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as error:
            errors.add(
                "%s: cannot inspect runtime escape symbols: %s"
                % (relative, error)
            )
            continue
        for line_number, line in enumerate(lines, 1):
            for name, pattern in FORBIDDEN_RUNTIME_ESCAPE_PATTERNS.items():
                if pattern.search(line):
                    errors.add(
                        "%s:%d: terminal Share architecture forbids runtime "
                        "escape symbol %s"
                        % (relative, line_number, name)
                    )
            field = GCTX_UPPER_FIELD_PATTERN.search(line)
            if field:
                errors.add(
                    "%s:%d: Share-owned GCTX still exposes upper field %s"
                    % (relative, line_number, field.group(1))
                )


def _iter_build_files(repo):
    for root, dirnames, filenames in os.walk(str(repo), followlinks=False):
        dirnames[:] = [
            name
            for name in dirnames
            if name not in SKIPPED_WALK_DIRS
            and not name.startswith("bazel-")
            and not Path(root, name).is_symlink()
        ]
        for filename in filenames:
            if filename in {"BUILD", "BUILD.bazel"}:
                yield Path(root, filename)


def _is_share_build(path, repo):
    relative = path.relative_to(repo).parts
    return len(relative) >= 3 and relative[:2] == ("src", "share")


def check(repo):
    errors = set()
    _check_share_sources(repo, errors)
    parsed_builds = []
    for path in sorted(_iter_build_files(repo)):
        try:
            parsed_builds.append(_parse_build(path))
        except (OSError, ValueError) as error:
            errors.add("%s: %s" % (path, error))

    share_builds = [
        parsed for parsed in parsed_builds if _is_share_build(parsed.path, repo)
    ]
    if not share_builds:
        errors.add("%s: no Share BUILD file found" % (repo / "src/share"))
        return errors, 0, len(parsed_builds)

    broad_targets = set()
    internal_targets = set()
    for parsed in share_builds:
        broad, internal = _share_rule_catalog(parsed, errors)
        broad_targets.update(broad)
        internal_targets.update(internal)

    share_forbidden_targets = set(FORBIDDEN_EXACT_TARGETS) | broad_targets
    external_forbidden_targets = share_forbidden_targets | internal_targets
    for parsed in share_builds:
        _check_share_build(parsed, share_forbidden_targets, errors)
    for parsed in parsed_builds:
        if not _is_share_build(parsed.path, repo):
            _check_external_build(
                parsed,
                external_forbidden_targets,
                errors,
            )

    layers = repo / "tools/module_check/module_layers.conf"
    if layers.is_file():
        _check_layer_debt(layers, errors)
    else:
        errors.add("%s: module layer policy is missing" % layers)

    share_rule_count = sum(len(parsed.rules) for parsed in share_builds)
    external_count = len(parsed_builds) - len(share_builds)
    return errors, share_rule_count, external_count


def main():
    repo = (
        Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else Path(__file__).resolve().parents[2]
    )
    errors, share_rule_count, external_count = check(repo)
    if errors:
        for error in sorted(errors):
            print("[FAIL] " + error, file=sys.stderr)
        print(
            "share terminal architecture check: %d violation(s)"
            % len(errors),
            file=sys.stderr,
        )
        return 1

    print(
        "share terminal architecture: %d Share rules and %d external "
        "BUILD files checked"
        % (share_rule_count, external_count)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
