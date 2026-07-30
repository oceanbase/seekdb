#!/usr/bin/env python3
"""Audit a built seekdb plugin's dynamic exports and core dependencies."""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys


ENTRY = "seekdb_plugin_entry_v1"
ELF_RUNTIME_MARKERS = {"__bss_start", "_edata", "_end"}


def run(command: list[str]) -> str:
    result = subprocess.run(command, check=False, text=True, capture_output=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"{' '.join(command)} failed: {detail}")
    return result.stdout


def exported_symbols(binary: pathlib.Path, nm: str) -> set[str]:
    if sys.platform == "darwin":
        output = run([nm, "-gU", str(binary)])
    elif sys.platform == "win32":
        output = run([nm, "--defined-only", "--extern-only", str(binary)])
    else:
        output = run([nm, "-D", "--defined-only", str(binary)])
    symbols: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if fields:
            symbol = fields[-1].split("@", 1)[0]
            if sys.platform == "darwin" and symbol.startswith("_"):
                symbol = symbol[1:]
            symbols.add(symbol)
    return symbols


def first_tool(candidates: list[pathlib.Path | str]) -> str | None:
    for candidate in candidates:
        value = str(candidate)
        if pathlib.Path(value).is_file():
            return value
        resolved = shutil.which(value)
        if resolved:
            return resolved
    return None


def windows_needed_libraries(binary: pathlib.Path, nm: str) -> list[str]:
    nm_path = pathlib.Path(nm).resolve()
    llvm_readobj = first_tool([
        nm_path.with_name("llvm-readobj.exe"),
        nm_path.with_name("llvm-readobj"),
        "llvm-readobj.exe",
        "llvm-readobj",
    ])
    if llvm_readobj:
        output = run([llvm_readobj, "--coff-imports", str(binary)])
        return re.findall(r"^\s*Name:\s*([^\s]+\.dll)\s*$", output,
                          flags=re.IGNORECASE | re.MULTILINE)

    dumpbin = first_tool(["dumpbin.exe", "dumpbin"])
    if dumpbin:
        output = run([dumpbin, "/nologo", "/dependents", str(binary)])
        return re.findall(r"^\s*([^\s]+\.dll)\s*$", output,
                          flags=re.IGNORECASE | re.MULTILINE)

    objdump = first_tool([
        nm_path.with_name("llvm-objdump.exe"),
        nm_path.with_name("llvm-objdump"),
        "llvm-objdump.exe",
        "llvm-objdump",
        "objdump.exe",
        "objdump",
    ])
    if objdump:
        output = run([objdump, "-p", str(binary)])
        return re.findall(r"DLL Name:\s*([^\s]+)", output, flags=re.IGNORECASE)

    raise RuntimeError(
        "Windows plugin dependency audit requires llvm-readobj, dumpbin, or objdump"
    )


def needed_libraries(binary: pathlib.Path, nm: str) -> list[str]:
    if sys.platform == "darwin":
        tool = shutil.which("otool")
        if not tool:
            raise RuntimeError("otool is required for plugin dependency audit")
        lines = run([tool, "-L", str(binary)]).splitlines()[1:]
        return [line.strip().split()[0] for line in lines if line.strip()]
    if sys.platform == "win32":
        return windows_needed_libraries(binary, nm)
    tool = shutil.which("readelf")
    if not tool:
        raise RuntimeError("readelf is required for plugin dependency audit")
    output = run([tool, "-d", str(binary)])
    return re.findall(r"\(NEEDED\).*?\[([^]]+)]", output)


def is_core_library(library: str) -> bool:
    name = pathlib.Path(library).name.lower()
    return (
        "seekdb" in name
        or "oceanbase" in name
        or (
            re.match(r"^(?:lib)?ob(?:lib|[_-])", name) is not None
            and not name.startswith("libobjc")
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--nm", required=True)
    args = parser.parse_args()

    binary = pathlib.Path(args.binary).resolve()
    if not binary.is_file():
        print(f"plugin binary does not exist: {binary}", file=sys.stderr)
        return 2

    try:
        exports = exported_symbols(binary, args.nm)
        allowed = {ENTRY} | ELF_RUNTIME_MARKERS
        unexpected = sorted(exports - allowed)
        errors: list[str] = []
        if ENTRY not in exports:
            errors.append(f"missing required dynamic export {ENTRY}")
        if unexpected:
            errors.append("unexpected dynamic exports: " + ", ".join(unexpected))

        core_dependencies = sorted(
            library for library in needed_libraries(binary, args.nm)
            if is_core_library(library)
        )
        if core_dependencies:
            errors.append("links seekdb core libraries: " + ", ".join(core_dependencies))
    except (OSError, RuntimeError) as error:
        print(f"plugin binary audit failed: {error}", file=sys.stderr)
        return 2

    if errors:
        for error in errors:
            print(f"plugin binary audit: {error}", file=sys.stderr)
        return 1
    print(f"seekdb plugin binary audit passed: {binary.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
