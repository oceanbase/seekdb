#!/usr/bin/env python3
"""Fail when a seekdb plugin crosses the public C ABI boundary."""

from __future__ import annotations

import pathlib
import re
import sys


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".ipp"}
# Horizontal whitespace is intentional: ``\s`` can consume blank lines, which
# makes two include matchers disagree about the start offset of one directive.
INCLUDE_RE = re.compile(
    r"^[ \t]*#[ \t]*include[ \t]*([<\"])([^>\"]+)[>\"]", re.MULTILINE
)
INCLUDE_DIRECTIVE_RE = re.compile(
    r"^[ \t]*#[ \t]*include[ \t]+(.+?)[ \t]*$", re.MULTILINE
)
CORE_PREFIXES = (
    "src/",
    "share/",
    "sql/",
    "observer/",
    "storage/",
    "rootserver/",
    "logservice/",
    "objit/",
    "common/",
    "lib/",
    "deps/",
)
CORE_TARGET_RE = re.compile(r"^(?:oceanbase|seekdb|ob)")
PRIVATE_MARKER_RE = re.compile(
    r"\b(?:SEEKDB_PLUGIN_PRIVATE_LIBRARY|SEEKDB_PLUGIN_PRIVATE_ROOT|"
    r"SEEKDB_EXPLICIT_PLUGIN_PRIVATE_TARGETS|SEEKDB_MANAGED_PLUGIN_TARGETS|"
    r"SEEKDB_MANAGED_PLUGIN_ROOT)\b"
)


def source_files(root: pathlib.Path):
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            yield path


def check_plugin_includes(repo: pathlib.Path, errors: list[str]) -> None:
    plugin_root = repo / "plugins"
    if not plugin_root.is_dir():
        return
    for path in source_files(plugin_root):
        text = path.read_text(encoding="utf-8", errors="replace")
        relative_parts = path.relative_to(plugin_root).parts
        own_plugin_root = plugin_root / relative_parts[0]
        literal_starts = {match.start() for match in INCLUDE_RE.finditer(text)}
        for directive in INCLUDE_DIRECTIVE_RE.finditer(text):
            if directive.start() not in literal_starts:
                line = text.count("\n", 0, directive.start()) + 1
                errors.append(
                    f"{path.relative_to(repo)}:{line}: plugin macro/nonliteral include "
                    f"is forbidden: {directive.group(1)}"
                )
        for match in INCLUDE_RE.finditer(text):
            delimiter, include = match.groups()
            normalized = include.replace("\\", "/")
            line = text.count("\n", 0, match.start()) + 1
            if normalized.startswith("seekdb/"):
                continue
            if normalized.startswith(CORE_PREFIXES) or "/../src/" in f"/{normalized}":
                errors.append(
                    f"{path.relative_to(repo)}:{line}: plugin includes core-private '{include}'"
                )
                continue
            if delimiter == '"':
                # Quoted includes must resolve inside this plugin's own subtree;
                # sibling plugins communicate only through named SDK services.
                resolved = (path.parent / include).resolve()
                try:
                    resolved.relative_to(own_plugin_root.resolve())
                except ValueError:
                    errors.append(
                        f"{path.relative_to(repo)}:{line}: quoted include escapes its plugin "
                        f"subtree '{include}'"
                    )
                else:
                    if not resolved.is_file():
                        errors.append(
                            f"{path.relative_to(repo)}:{line}: quoted include is not a "
                            f"plugin-relative file: '{include}'"
                        )


def check_core_includes(repo: pathlib.Path, errors: list[str]) -> None:
    core_root = repo / "src"
    if not core_root.is_dir():
        return
    for path in source_files(core_root):
        text = path.read_text(encoding="utf-8", errors="replace")
        literal_starts = {match.start() for match in INCLUDE_RE.finditer(text)}
        for directive in INCLUDE_DIRECTIVE_RE.finditer(text):
            if directive.start() not in literal_starts:
                line = text.count("\n", 0, directive.start()) + 1
                errors.append(
                    f"{path.relative_to(repo)}:{line}: core macro/nonliteral include "
                    f"cannot prove the plugin boundary: {directive.group(1)}"
                )
        for match in INCLUDE_RE.finditer(text):
            include = match.group(2).replace("\\", "/")
            if include.startswith("plugins/") or "/plugins/" in f"/{include}":
                line = text.count("\n", 0, match.start()) + 1
                errors.append(
                    f"{path.relative_to(repo)}:{line}: core includes plugin implementation '{include}'"
                )


def cmake_invocations(text: str, command: str):
    pattern = re.compile(rf"\b{re.escape(command)}\s*\((.*?)\)", re.DOTALL | re.IGNORECASE)
    yield from pattern.finditer(text)


def plugin_cmake_files(plugin_root: pathlib.Path):
    yield from plugin_root.rglob("CMakeLists.txt")
    yield from plugin_root.rglob("*.cmake")


def check_plugin_links(repo: pathlib.Path, errors: list[str]) -> None:
    plugin_root = repo / "plugins"
    if not plugin_root.is_dir():
        return
    for path in plugin_cmake_files(plugin_root):
        text = re.sub(r"#.*", "", path.read_text(encoding="utf-8", errors="replace"))
        for command in (
            "include_directories",
            "add_compile_options",
            "add_link_options",
            "link_directories",
            "add_definitions",
            "add_compile_definitions",
            "target_compile_options",
            "target_link_directories",
            "target_precompile_headers",
        ):
            for invocation in cmake_invocations(text, command):
                line = text.count("\n", 0, invocation.start()) + 1
                errors.append(
                    f"{path.relative_to(repo)}:{line}: plugin CMake may not use "
                    f"{command}; express dependencies through audited targets"
                )
        for marker in PRIVATE_MARKER_RE.finditer(text):
            line = text.count("\n", 0, marker.start()) + 1
            errors.append(
                f"{path.relative_to(repo)}:{line}: plugin CMake may not set or "
                "reference private-library trust properties directly; use "
                "seekdb_mark_plugin_private_library"
            )
        for invocation in cmake_invocations(text, "add_library"):
            words = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", invocation.group(1))
            if any(word.upper() in {"MODULE", "SHARED"} for word in words[1:]):
                line = text.count("\n", 0, invocation.start()) + 1
                errors.append(
                    f"{path.relative_to(repo)}:{line}: loadable plugins must use seekdb_add_plugin"
                )
        for invocation in cmake_invocations(text, "target_sources"):
            line = text.count("\n", 0, invocation.start()) + 1
            errors.append(
                f"{path.relative_to(repo)}:{line}: declare plugin sources in seekdb_add_plugin"
            )
        for invocation in cmake_invocations(text, "target_link_libraries"):
            words = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", invocation.group(1))
            # The first argument is the plugin target being configured, not a
            # linked dependency.  seekdb_plugin_sdk is the one public target
            # deliberately exposed to plugins.
            dependencies = words[1:]
            forbidden = sorted({
                word for word in dependencies
                if word != "seekdb_plugin_sdk" and CORE_TARGET_RE.match(word)
            })
            if forbidden:
                line = text.count("\n", 0, invocation.start()) + 1
                errors.append(
                    f"{path.relative_to(repo)}:{line}: plugin links core targets {', '.join(forbidden)}"
                )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: plugin_boundary_check.py <seekdb-source-root>", file=sys.stderr)
        return 2
    repo = pathlib.Path(sys.argv[1]).resolve()
    errors: list[str] = []
    check_plugin_includes(repo, errors)
    check_core_includes(repo, errors)
    check_plugin_links(repo, errors)
    if errors:
        print("seekdb plugin boundary violations:", file=sys.stderr)
        for error in sorted(errors):
            print(f"  {error}", file=sys.stderr)
        return 1
    print("seekdb plugin boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
