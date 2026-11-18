"""Separate native debug symbols and keep distributable binaries stripped."""

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import uuid
import zlib

import binary_distribution

ROOT = Path(__file__).resolve().parents[1]


def tool(name, layout):
    directories = []
    if layout == "android" and os.environ.get("ANDROID_NDK_HOME"):
        directories += sorted((Path(os.environ["ANDROID_NDK_HOME"]) / "toolchains/llvm/prebuilt").glob("*/bin"))
    if os.environ.get("BAZEL_LLVM"):
        directories.append(Path(os.environ["BAZEL_LLVM"]) / "bin")
    if layout == "windows":
        directories.append(ROOT / "deps/3rd/tools/llvm18/bin")
    directories.append(ROOT / "deps/3rd/usr/local/oceanbase/devtools/bin")
    for directory in directories:
        for suffix in ("", ".exe"):
            candidate = directory / ("llvm-" + name + suffix)
            if candidate.is_file():
                return str(candidate)
    result = shutil.which("llvm-" + name) or (shutil.which(name) if layout == "linux" else None)
    if not result:
        raise ValueError("Symbol packaging requires llvm-" + name)
    return result


def elf_section_headers(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        if header[:6] != b"\x7fELF\x02\x01":
            raise ValueError("Expected little-endian ELF64: " + str(path))
        offset = struct.unpack_from("<Q", header, 40)[0]
        size, count, strings = struct.unpack_from("<HHH", header, 58)
        stream.seek(offset + strings * size)
        table = stream.read(size)
        stream.seek(struct.unpack_from("<Q", table, 24)[0])
        names = stream.read(struct.unpack_from("<Q", table, 32)[0])
        result = {}
        for index in range(count):
            stream.seek(offset + index * size)
            section = stream.read(size)
            start = struct.unpack_from("<I", section)[0]
            name = names[start:].split(b"\0", 1)[0].decode()
            result[name] = struct.unpack_from("<QQ", section, 24)
        return result


def elf_sections(path):
    return set(elf_section_headers(path))


def verify_debuglink(path, symbol):
    section = elf_section_headers(path).get(".gnu_debuglink")
    if section is None:
        raise ValueError("Missing GNU debuglink: " + str(path))
    with path.open("rb") as stream:
        stream.seek(section[0])
        data = stream.read(section[1])
    end = data.find(b"\0")
    checksum_offset = (end + 4) & ~3
    if end < 0 or data[:end] != os.fsencode(symbol.name) or len(data) < checksum_offset + 4:
        raise ValueError("GNU debuglink does not name its exported symbols: " + str(path))
    checksum = 0
    with symbol.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            checksum = zlib.crc32(block, checksum)
    if struct.unpack_from("<I", data, checksum_offset)[0] != checksum:
        raise ValueError("GNU debuglink CRC does not match its exported symbols: " + str(path))


def matching_pdb(path, pdb, layout):
    executable = subprocess.check_output([tool("readobj", layout), "--coff-debug-directory", str(path)], text=True)
    records = re.findall(r"PDBGUID:\s*\(([0-9A-Fa-f ]+)\)\s+PDBAge:\s*(\d+)", executable)
    if len(records) != 1:
        raise ValueError("Expected one CodeView PDB identity in " + str(path))
    expected = (uuid.UUID(bytes_le=bytes.fromhex(records[0][0])), int(records[0][1]))
    summary = subprocess.check_output([tool("pdbutil", layout), "dump", "-summary", str(pdb)], text=True)
    guid = re.search(r"^\s*GUID:\s*(\{[0-9A-Fa-f-]+\})", summary, re.MULTILINE)
    age = re.search(r"^\s*Age:\s*(\d+)", summary, re.MULTILINE)
    if not guid or not age:
        raise ValueError("Missing PDB GUID/age in " + str(pdb))
    actual = (uuid.UUID(guid.group(1)), int(age.group(1)))
    if actual != expected:
        raise ValueError("PDB GUID/age does not match its binary: %s (expected %s/%s, got %s/%s)" %
                         (pdb, *expected, *actual))
    return {"guid": str(actual[0]), "age": actual[1]}


def macho_debug(path):
    with path.open("rb") as stream:
        header = stream.read(32)
        if header[:4] != b"\xcf\xfa\xed\xfe":
            raise ValueError("Expected a thin little-endian Mach-O64: " + str(path))
        position = 32
        symbols = None
        for _ in range(struct.unpack_from("<I", header, 16)[0]):
            stream.seek(position)
            command, size = struct.unpack("<II", stream.read(8))
            data = stream.read(size - 8)
            if command == 0x19 and data[:16].rstrip(b"\0") == b"__DWARF":
                return True
            if command == 2:
                symbols = struct.unpack_from("<II", data)
            position += size
        if symbols:
            stream.seek(symbols[0])
            for _ in range(symbols[1]):
                kind = stream.read(16)[4]
                # Apple strip retains the N_OPT radr marker, which has no debug map.
                if kind & 0xe0 and kind != 0x3c:
                    return True
    return False


def pe_debug(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        header = stream.read(24)
        count = struct.unpack_from("<H", header, 6)[0]
        symbols = struct.unpack_from("<I", header, 16)[0]
        stream.seek(struct.unpack_from("<H", header, 20)[0], 1)
        sections = [stream.read(40)[:8].rstrip(b"\0") for _ in range(count)]
    # The CodeView GUID/path points to an external PDB; it is not debug payload.
    return bool(symbols or any(name.startswith((b".debug", b".zdebug", b"/")) for name in sections))


def native_files(root):
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            magic = stream.read(4)
        kind = {b"\x7fELF": "elf", b"\xcf\xfa\xed\xfe": "macho"}.get(magic)
        if magic[:2] == b"MZ":
            kind = "pe"
        if kind:
            yield path, kind


def has_debug(path, kind):
    if kind == "elf":
        return any(name.startswith((".debug", ".zdebug", ".stab")) or name in (
            ".symtab", ".gdb_index", ".gnu_debugdata") for name in elf_sections(path))
    return macho_debug(path) if kind == "macho" else pe_debug(path)


def strip_file(path, kind, layout):
    if not has_debug(path, kind):
        return
    if kind == "elf":
        subprocess.run([tool("strip", layout), "--strip-unneeded", "--remove-section=.gnu_debugdata", str(path)], check=True)
    elif kind == "macho":
        subprocess.run(["xcrun", "strip", "-S", "-x", str(path)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(path)], check=True)
    else:
        raise ValueError("Windows binaries must store debug information in an external PDB: " + str(path))
    if has_debug(path, kind):
        raise ValueError("Debug information remains after stripping: " + str(path))


def verify_stripped(root):
    for path in root.rglob("*"):
        if path.suffix.lower() in (".pdb", ".debug", ".dsym"):
            raise ValueError("Debug symbols must be distributed separately: " + str(path))
    for path, kind in native_files(root):
        if has_debug(path, kind):
            raise ValueError("Distributable contains debug information: " + str(path))


def strip_tree(root, layout):
    for path, kind in native_files(root):
        strip_file(path, kind, layout)
    verify_stripped(root)


def uuids(path):
    output = subprocess.check_output(["xcrun", "dwarfdump", "--uuid", str(path)], text=True)
    result = set(re.findall(r"UUID: ([0-9A-Fa-f-]+) \(([^)]+)\)", output))
    if not result:
        raise ValueError("Missing Mach-O UUID: " + str(path))
    return result


def separate(root, destination, layout, original, primary):
    records = {}
    # Bazel's Mach-O debug map contains paths relative to the execution root.
    working = next((path.parent for path in original.resolve().parents if path.name == "bazel-out"), ROOT)
    for path, kind in native_files(root):
        relative = path.relative_to(root)
        symbol = None
        identity = None
        if kind == "elf" and has_debug(path, kind):
            symbol = destination / relative.with_name(path.name + ".debug")
            symbol.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([tool("objcopy", layout), "--only-keep-debug", "--keep-section=.gnu_debugdata",
                            str(path), str(symbol)], check=True)
            strip_file(path, kind, layout)
            subprocess.run([tool("objcopy", layout), "--remove-section=.gnu_debuglink",
                            "--add-gnu-debuglink=" + str(symbol), str(path)], check=True)
            verify_debuglink(path, symbol)
        elif kind == "macho" and has_debug(path, kind):
            symbol = destination / relative.with_name(path.name + ".dSYM")
            symbol.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["xcrun", "dsymutil", str(path), "-o", str(symbol)], cwd=working, check=True)
            identity = sorted(uuids(path))
            if uuids(symbol) != set(identity):
                raise ValueError("dSYM UUID does not match its binary: " + str(path))
            strip_file(path, kind, layout)
            if sorted(uuids(path)) != identity:
                raise ValueError("Stripping changed the Mach-O UUID: " + str(path))
        elif kind == "pe":
            source = original if relative == primary else original.parent / path.name
            pdb = source.resolve().with_suffix(".pdb")
            if pdb.is_file():
                identity = matching_pdb(path, pdb, layout)
                symbol = destination / relative.with_suffix(".pdb")
                symbol.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(pdb, symbol)
            elif relative == primary:
                raise ValueError("Windows symbol export requires the matching PDB: " + str(pdb))
            strip_file(path, kind, layout)
        if symbol is not None:
            records[relative.as_posix()] = {"binary_sha256": binary_distribution.digest(path),
                "symbols": symbol.relative_to(destination).as_posix()}
            if identity is not None:
                records[relative.as_posix()]["uuids" if kind == "macho" else "pdb"] = identity
    verify_stripped(root)
    return records
