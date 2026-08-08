#!/usr/bin/env python3
"""Audit C++ unit-test ownership and direct module dependencies.

The authoritative policy lives in //bazel/architecture:module_policy.bzl.
This checker deliberately has no violation baseline: every reported item must
be moved, rewritten, or deleted before the strict gate can pass.
"""

import argparse
import ast
import collections
import dataclasses
import json
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, Tuple


SOURCE_EXTENSIONS = frozenset(
    [".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".ipp"]
)
BUILD_FILENAMES = frozenset(["BUILD", "BUILD.bazel"])
DEPENDENCY_ATTRIBUTES = frozenset(
    ["deps", "implementation_deps", "runtime_deps"]
)
INCLUDE_PATTERN = re.compile(
    r'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)'
)
IF_ZERO_PATTERN = re.compile(r"^[ \t]*#[ \t]*if[ \t]+0[ \t]*(?://.*)?$")
IF_PATTERN = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef)\b")
ELIF_PATTERN = re.compile(r"^[ \t]*#[ \t]*(elif|else)\b")
ENDIF_PATTERN = re.compile(r"^[ \t]*#[ \t]*endif\b")
TEST_CASE_PATTERN = re.compile(
    r"^[ \t]*(?:TEST|TEST_F|TEST_P|TYPED_TEST)[ \t]*"
    r"\([^,\n]+,[ \t]*([A-Za-z_][A-Za-z0-9_]*)",
    re.MULTILINE,
)
MAIN_FUNCTION_PATTERN = re.compile(
    r"^[ \t]*(?:int|int32_t)[ \t]+main[ \t]*\(",
    re.MULTILINE,
)


@dataclasses.dataclass(frozen=True, order=True)
class Violation:
    kind: str
    path: str
    line: int
    owner: str
    dependency: str
    detail: str

    def as_dict(self) -> Dict[str, object]:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class Policy:
    module_roots: Dict[str, str]
    unittest_module_roots: Dict[str, str]
    allowed_direct_deps: Dict[str, List[str]]
    runtime_deps: List[str]
    infrastructure_files: List[str]
    forbidden_path_patterns: List[str]
    forbidden_case_pattern: str


@dataclasses.dataclass(frozen=True)
class AuditResult:
    source_files: int
    build_files: int
    violations: Tuple[Violation, ...]

    def counts(self) -> Dict[str, int]:
        return dict(collections.Counter(item.kind for item in self.violations))


def _literal_assignments(path: Path, names: Sequence[str]) -> Dict[str, object]:
    """Read literal top-level assignments from the Starlark policy file."""
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    except (OSError, SyntaxError) as error:
        raise ValueError("cannot parse policy %s: %s" % (path, error))

    requested = set(names)
    values: Dict[str, object] = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name) or target.id not in requested:
            continue
        try:
            values[target.id] = ast.literal_eval(node.value)
        except (ValueError, TypeError, SyntaxError) as error:
            raise ValueError(
                "%s must be a literal assignment in %s: %s"
                % (target.id, path, error)
            )

    missing = sorted(requested - set(values))
    if missing:
        raise ValueError(
            "policy %s is missing literal assignments: %s"
            % (path, ", ".join(missing))
        )
    return values


def load_policy(path: Path) -> Policy:
    names = [
        "MODULE_ROOTS",
        "UNITTEST_ALLOWED_DIRECT_MODULE_DEPS",
        "UNITTEST_FORBIDDEN_PATH_PATTERNS",
        "UNITTEST_FORBIDDEN_CASE_PATTERN",
        "UNITTEST_INFRASTRUCTURE_FILES",
        "UNITTEST_MODULE_ROOTS",
        "UNITTEST_RUNTIME_DEPS",
    ]
    values = _literal_assignments(path, names)
    policy = Policy(
        module_roots=values["MODULE_ROOTS"],
        unittest_module_roots=values["UNITTEST_MODULE_ROOTS"],
        allowed_direct_deps=values["UNITTEST_ALLOWED_DIRECT_MODULE_DEPS"],
        runtime_deps=values["UNITTEST_RUNTIME_DEPS"],
        infrastructure_files=values["UNITTEST_INFRASTRUCTURE_FILES"],
        forbidden_path_patterns=values["UNITTEST_FORBIDDEN_PATH_PATTERNS"],
        forbidden_case_pattern=values["UNITTEST_FORBIDDEN_CASE_PATTERN"],
    )
    modules = sorted(policy.unittest_module_roots)
    policy_modules = sorted(policy.allowed_direct_deps)
    if modules != policy_modules:
        raise ValueError(
            "UNITTEST_MODULE_ROOTS and UNITTEST_ALLOWED_DIRECT_MODULE_DEPS "
            "must have identical keys"
        )
    unknown = sorted(
        {
            dependency
            for dependencies in policy.allowed_direct_deps.values()
            for dependency in dependencies
            if dependency not in policy.module_roots
        }
    )
    if unknown:
        raise ValueError(
            "unit-test policy names unknown production modules: %s"
            % ", ".join(unknown)
        )
    return policy


def _is_under(path: str, root: str) -> bool:
    return path == root or path.startswith(root + "/")


def _module_for_path(path: str, roots: Dict[str, str]) -> Optional[str]:
    match: Optional[str] = None
    match_length = -1
    for module, root in roots.items():
        if _is_under(path, root) and len(root) > match_length:
            match = module
            match_length = len(root)
    return match


def _production_module_for_include(
    include: str, module_roots: Dict[str, str]
) -> Optional[str]:
    normalized = include[2:] if include.startswith("./") else include
    explicit = _module_for_path(normalized, module_roots)
    if explicit is not None:
        return explicit

    # Source-tree includes normally omit the leading "src/".
    for module, root in module_roots.items():
        if not root.startswith("src/"):
            continue
        include_root = root[len("src/") :]
        if _is_under(normalized, include_root):
            return module
    return None


def _dependency_module(label: str, policy: Policy) -> Optional[str]:
    if not label.startswith("//"):
        return None
    package = label[2:].split(":", 1)[0]
    module = _module_for_path(package, policy.module_roots)
    if module is not None:
        return module
    return _module_for_path(package, policy.unittest_module_roots)


def _strip_comments(line: str, in_block_comment: bool) -> Tuple[str, bool]:
    """Remove C/C++ comments from one line while preserving directives."""
    result: List[str] = []
    cursor = 0
    while cursor < len(line):
        if in_block_comment:
            end = line.find("*/", cursor)
            if end < 0:
                return "".join(result), True
            cursor = end + 2
            in_block_comment = False
            continue
        block = line.find("/*", cursor)
        single = line.find("//", cursor)
        if single >= 0 and (block < 0 or single < block):
            result.append(line[cursor:single])
            break
        if block < 0:
            result.append(line[cursor:])
            break
        result.append(line[cursor:block])
        cursor = block + 2
        in_block_comment = True
    return "".join(result), in_block_comment


def _source_without_comments(source: str) -> str:
    """Remove comments while preserving source line numbers."""
    stripped_lines: List[str] = []
    in_block_comment = False
    for raw_line in source.splitlines():
        line, in_block_comment = _strip_comments(raw_line, in_block_comment)
        stripped_lines.append(line)
    return "\n".join(stripped_lines)


def iter_active_includes(source: str) -> Iterator[Tuple[int, str]]:
    """Yield direct includes outside comments and literal #if 0 blocks."""
    active_stack: List[Tuple[bool, bool]] = []
    active = True
    in_block_comment = False
    for line_number, raw_line in enumerate(source.splitlines(), 1):
        line, in_block_comment = _strip_comments(raw_line, in_block_comment)
        if IF_ZERO_PATTERN.match(line):
            active_stack.append((active, True))
            active = False
            continue
        if IF_PATTERN.match(line):
            active_stack.append((active, False))
            continue
        if ELIF_PATTERN.match(line) and active_stack:
            parent_active, literal_zero = active_stack[-1]
            active = parent_active if literal_zero else active
            continue
        if ENDIF_PATTERN.match(line) and active_stack:
            parent_active, _ = active_stack.pop()
            active = parent_active
            continue
        if not active:
            continue
        match = INCLUDE_PATTERN.match(line)
        if match:
            yield line_number, match.group(1) or match.group(2)


def _strings_in_expression(node: ast.AST) -> Iterator[str]:
    for child in ast.walk(node):
        if isinstance(child, ast.Constant) and isinstance(child.value, str):
            yield child.value


def iter_build_dependencies(source: str, path: Path) -> Iterator[Tuple[int, str]]:
    """Yield literal labels from dependency-bearing Starlark attributes."""
    try:
        tree = ast.parse(source, filename=str(path))
    except SyntaxError as error:
        raise ValueError("cannot parse BUILD file %s: %s" % (path, error))
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        for keyword in node.keywords:
            if keyword.arg not in DEPENDENCY_ATTRIBUTES:
                continue
            for value in _strings_in_expression(keyword.value):
                if value.startswith("//"):
                    yield getattr(keyword.value, "lineno", node.lineno), value


def _is_forbidden_program(path: str, patterns: Sequence[re.Pattern]) -> bool:
    return any(pattern.search(path) for pattern in patterns)


def _allowed_dependency(owner: str, dependency: str, policy: Policy) -> bool:
    return (
        dependency == owner
        or dependency in policy.allowed_direct_deps[owner]
    )


def audit_repository(root: Path, policy: Policy) -> AuditResult:
    unittest_root = root / "unittest"
    patterns = [re.compile(pattern, re.IGNORECASE) for pattern in policy.forbidden_path_patterns]
    forbidden_case_pattern = re.compile(
        policy.forbidden_case_pattern,
        re.IGNORECASE,
    )
    infrastructure = set(policy.infrastructure_files)
    violations: List[Violation] = []
    source_files = 0
    build_files = 0

    for path in sorted(unittest_root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if relative in infrastructure:
            continue
        if path.suffix in SOURCE_EXTENSIONS:
            source_files += 1
            owner = _module_for_path(relative, policy.unittest_module_roots)
            if owner is None:
                violations.append(
                    Violation(
                        "unowned",
                        relative,
                        1,
                        "-",
                        "-",
                        "move the file under its production Module or delete it",
                    )
                )
                continue
            if _is_forbidden_program(relative, patterns):
                violations.append(
                    Violation(
                        "non_unit_program",
                        relative,
                        1,
                        owner,
                        "-",
                        "move a maintained benchmark out of unittest or delete it",
                    )
                )
            try:
                source = path.read_text(encoding="utf-8", errors="surrogateescape")
            except OSError as error:
                raise ValueError("cannot read %s: %s" % (path, error))
            source_without_comments = _source_without_comments(source)
            case_matches = list(TEST_CASE_PATTERN.finditer(source_without_comments))
            if MAIN_FUNCTION_PATTERN.search(source_without_comments):
                main_match = MAIN_FUNCTION_PATTERN.search(
                    source_without_comments
                )
                violations.append(
                    Violation(
                        "non_unit_executable",
                        relative,
                        source_without_comments.count(
                            "\n", 0, main_match.start()
                        ) + 1,
                        owner,
                        "-",
                        "module tests must use unittest/all_tests_main.cpp",
                    )
                )
            for case_match in case_matches:
                case_name = case_match.group(1)
                if forbidden_case_pattern.search(case_name):
                    violations.append(
                        Violation(
                            "non_unit_case",
                            relative,
                            source_without_comments.count(
                                "\n", 0, case_match.start()
                            ) + 1,
                            owner,
                            "-",
                            case_name,
                        )
                    )
            for line_number, include in iter_active_includes(source):
                dependency = _production_module_for_include(
                    include, policy.module_roots
                )
                if dependency is None:
                    dependency = _module_for_path(
                        include, policy.unittest_module_roots
                    )
                if dependency is None or _allowed_dependency(
                    owner, dependency, policy
                ):
                    continue
                violations.append(
                    Violation(
                        "cross_module_include",
                        relative,
                        line_number,
                        owner,
                        dependency,
                        include,
                    )
                )
        elif path.name in BUILD_FILENAMES:
            build_files += 1
            package = path.parent.relative_to(root).as_posix()
            owner = _module_for_path(package, policy.unittest_module_roots)
            if owner is None:
                violations.append(
                    Violation(
                        "unowned_build",
                        relative,
                        1,
                        "-",
                        "-",
                        "BUILD package has no unit-test Module owner",
                    )
                )
                continue
            try:
                source = path.read_text(encoding="utf-8")
            except OSError as error:
                raise ValueError("cannot read %s: %s" % (path, error))
            for line_number, label in iter_build_dependencies(source, path):
                if label in policy.runtime_deps:
                    continue
                dependency = _dependency_module(label, policy)
                if dependency is None or _allowed_dependency(
                    owner, dependency, policy
                ):
                    continue
                violations.append(
                    Violation(
                        "cross_module_dep",
                        relative,
                        line_number,
                        owner,
                        dependency,
                        label,
                    )
                )

    return AuditResult(
        source_files=source_files,
        build_files=build_files,
        violations=tuple(sorted(set(violations))),
    )


def _print_report(result: AuditResult, limit: int) -> None:
    counts = result.counts()
    print(
        "unit-test module audit: %d source files, %d BUILD files, %d violations"
        % (result.source_files, result.build_files, len(result.violations))
    )
    for kind, count in sorted(counts.items()):
        print("  %-24s %d" % (kind + ":", count))
    for item in result.violations[:limit]:
        location = "%s:%d" % (item.path, item.line)
        print(
            "%s: %s [%s] -> [%s] %s"
            % (item.kind, location, item.owner, item.dependency, item.detail)
        )
    if len(result.violations) > limit:
        print("... %d more; use --limit to show more" % (len(result.violations) - limit))


def main(arguments: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "repo",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    parser.add_argument(
        "--policy",
        type=Path,
        help="override bazel/architecture/module_policy.bzl",
    )
    parser.add_argument(
        "--audit",
        action="store_true",
        help="report violations without returning a failing status",
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--limit", type=int, default=200)
    options = parser.parse_args(arguments)

    root = options.repo.resolve()
    policy_path = (
        options.policy.resolve()
        if options.policy
        else root / "bazel" / "architecture" / "module_policy.bzl"
    )
    try:
        policy = load_policy(policy_path)
        result = audit_repository(root, policy)
    except ValueError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        return 2

    _print_report(result, max(options.limit, 0))
    if options.json_out:
        payload = {
            "source_files": result.source_files,
            "build_files": result.build_files,
            "counts": result.counts(),
            "violations": [item.as_dict() for item in result.violations],
        }
        options.json_out.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return 0 if options.audit or not result.violations else 1


if __name__ == "__main__":
    sys.exit(main())
