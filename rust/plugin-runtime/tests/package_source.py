#!/usr/bin/env python3
"""Private filesystem fixtures for the real C++/Rust package source reader."""
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="seekdb-extension-package-") as temporary:
    root = pathlib.Path(temporary) / "packages"
    root.mkdir()

    # PG-style shared directory; exercise Rust preparation through the real
    # C ABI and C++ owned-source adapter, not only Rust unit tests.
    (root / "flat.control").write_text(
        "default_version = '2'\ndirectory = 'flat_sql'\n"
        "module_pathname = '$libdir/flat'\nnative_module = 'org.flat'\n", encoding="utf-8")
    flat_sql = root / "flat_sql"
    flat_sql.mkdir()
    (flat_sql / "flat--1.sql").write_text("SELECT 'MODULE_PATHNAME';", encoding="utf-8")
    (flat_sql / "flat--1--2.sql").write_text("SELECT 'MODULE_PATHNAME';", encoding="utf-8")
    (flat_sql / "flat--2.control").write_text("module_pathname = '$libdir/flat_v2'\n", encoding="utf-8")

    (root / "policy.control").write_text("default_version = '3'\nsuperuser = false\n", encoding="utf-8")
    (root / "policy--1.sql").write_text("SELECT 1;", encoding="utf-8")
    (root / "policy--1--2.sql").write_text("SELECT 2;", encoding="utf-8")
    (root / "policy--2--3.sql").write_text("SELECT 3;", encoding="utf-8")
    (root / "policy--2.control").write_text("superuser = true\n", encoding="utf-8")

    native = root / "native_only"
    native.mkdir()
    (native / "native_only.control").write_text("default_version = '1.0'\nnative_module = 'org.test'\ninstall_source = 'native'\n", encoding="utf-8")
    (native / "native_only--1.0--1.1.sql").write_text("SELECT 2;", encoding="utf-8")

    def package(name, control="default_version = '1.0'\n", sql=b"SELECT 1;\n"):
        directory = root / name
        directory.mkdir()
        (directory / f"{name}.control").write_text(control, encoding="utf-8")
        (directory / f"{name}--1.0.sql").write_bytes(sql)
        return directory

    plain = package("plain", "default_version = '1.0'\nrequires = 'dep1, dep2'\nrelocatable = true\n", b"SELECT 'first';\n")
    package("ambiguous")
    (root / "ambiguous.control").write_text("default_version = '1.0'\n", encoding="utf-8")
    (plain / "plain--2.0.sql").write_text("SELECT 'second';\n", encoding="utf-8")
    package("bad_utf8", sql=b"\xff")
    package("sql_nul", sql=b"SELECT 1;\x00SELECT 2;")
    package("empty_sql", sql=b" \n\t")
    package("control_utf8", "default_version = '1.0'\n").joinpath("control_utf8.control").write_bytes(b"\xff")
    package("control_large", "#" + "x" * (64 * 1024))
    package("exact_limit", sql=b"x" * (4 * 1024 * 1024))
    package("large", sql=b"x" * (4 * 1024 * 1024 + 1))
    package("bad_control", "default_version = '1.0'\ntrusted = true\n")
    package("selfdep", "default_version = '1.0'\nrequires = 'selfdep'\n")
    directory = package("directory")
    script = directory / "directory--1.0.sql"
    script.unlink()
    script.mkdir()
    no_default = package("no_default", "# caller must select a version\n")
    (no_default / "no_default--1.sql").write_text("SELECT 1;\n", encoding="utf-8")
    outside = pathlib.Path(temporary) / "outside"
    outside.mkdir()
    (outside / "escape.control").write_text("default_version = '1.0'\n", encoding="utf-8")
    (root / "escape").symlink_to(outside, target_is_directory=True)
    script_escape = package("script_escape") / "script_escape--1.0.sql"
    script_escape.unlink()
    (outside / "script.sql").write_text("SELECT 1;\n", encoding="utf-8")
    script_escape.symlink_to(outside / "script.sql")
    chain = package("chain", "default_version = 'tip'\n", b"SELECT 'base'; -- tail without newline")
    (chain / "chain--1.0--middle.sql").write_text("SELECT 'middle';", encoding="utf-8")
    (chain / "chain--middle--tip.sql").write_text("SELECT 'tip';", encoding="utf-8")
    # A direct installation wins over the longer chain; reading it must not
    # execute or try to read scripts from the longer path.
    (chain / "chain--direct.sql").write_text("SELECT 'direct';", encoding="utf-8")
    (chain / "chain--tip--direct.sql").write_text("SELECT 'unused';", encoding="utf-8")
    versioned = package("versioned", "default_version = 'tip'\nrequires = 'base'\n")
    (versioned / "versioned--1.0--middle.sql").write_text("SELECT 2;", encoding="utf-8")
    (versioned / "versioned--middle--tip.sql").write_text("SELECT 3;", encoding="utf-8")
    for version, dependency in (("1.0", "alpha"), ("middle", "beta"), ("tip", "gamma")):
        (versioned / f"versioned--{version}.control").write_text(f"requires = '{dependency}'\n", encoding="utf-8")
    # Runtime reads selected controls only; packaging validates all controls.
    (versioned / "versioned--unused.control").write_text("trusted = true\n", encoding="utf-8")
    for name, override in (
        ("secondary_default", "default_version = '2'"),
        ("secondary_directory", "directory = 'sql'"),
        ("secondary_self", "requires = 'secondary_self'"),
        ("secondary_schema", "schema = 'other'"),
        ("secondary_module", "native_module = 'org.other'"),
        ("secondary_large", "#" * (64 * 1024 + 1)),
    ):
        directory = package(name, "default_version = '2'\n")
        (directory / f"{name}--1.0--2.sql").write_text("SELECT 2;", encoding="utf-8")
        (directory / f"{name}--2.control").write_text(override, encoding="utf-8")
    directory = package("secondary_escape")
    (directory / "secondary_escape--1.0.control").symlink_to(outside / "escape.control")
    for name, count in (("dependency_limit", 32), ("dependency_overflow", 33)):
        directory = package(name, "default_version = '2'\n")
        (directory / f"{name}--1.0--2.sql").write_text("SELECT 2;", encoding="utf-8")
        for version, prefix in (("1.0", "initial"), ("2", "final")):
            names = ",".join(f"{prefix}_{i}" for i in range(count))
            (directory / f"{name}--{version}.control").write_text(f"requires = '{names}'\n", encoding="utf-8")
    for name in ("chain_escape", "chain_invalid", "chain_total", "chain_override", "chain_empty"):
        directory = package(name, "default_version = '2'\n")
        update = directory / f"{name}--1.0--2.sql"
        if name == "chain_escape":
            update.symlink_to(outside / "script.sql")
        elif name == "chain_invalid":
            update.write_bytes(b"\xff")
        elif name == "chain_total":
            update.write_bytes(b"x" * (4 * 1024 * 1024))
        elif name == "chain_empty":
            update.write_text(" \n", encoding="utf-8")
        else:
            update.write_text("SELECT 2;", encoding="utf-8")
            (directory / f"{name}--2.control").write_text("requires = 'hidden_dependency'\n", encoding="utf-8")
    orphan = package("orphan", "default_version = 'tip'\n")
    (orphan / "orphan--elsewhere--tip.sql").write_text("SELECT 2;", encoding="utf-8")
    update_only = package("update_only", "default_version = 'old'\n")
    (update_only / "update_only--1.0.sql").unlink()
    (update_only / "update_only--new--middle.sql").write_text("SELECT 'middle'; -- tail", encoding="utf-8")
    (update_only / "update_only--middle--old.sql").write_text("SELECT 'old';", encoding="utf-8")
    # Target installation bytes must not be read by an update path.
    (update_only / "update_only--old.sql").write_bytes(b"\xff")
    for name in ("noop", "noop_override"):
        directory = package(name, "default_version = 'v1'\n")
        (directory / f"{name}--1.0.sql").unlink()
        if name == "noop_override":
            (directory / f"{name}--v1.control").write_text("requires = 'hidden'\n", encoding="utf-8")
    blank = package("blank_update", "default_version = '2'\n")
    (blank / "blank_update--1.0--2.sql").write_bytes(b"")
    update_total = package("update_total", "default_version = '3'\n")
    (update_total / "update_total--1.0--2.sql").write_bytes(b"x" * (4 * 1024 * 1024))
    (update_total / "update_total--2--3.sql").write_bytes(b"x")
    subprocess.run([sys.argv[1], str(root), sys.argv[2]], check=True, timeout=20)
