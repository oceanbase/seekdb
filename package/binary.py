"""Create and verify reusable seekdb binary distributions."""

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath, PureWindowsPath
import os
import platform
import re
import shutil
import stat
import struct
import subprocess
import tarfile
import zipfile


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def architecture(binary, layout):
    with binary.open("rb") as stream:
        header = stream.read(64)
        if layout in ("linux", "android") and header[:6] == b"\x7fELF\x02\x01":
            machine = struct.unpack_from("<H", header, 18)[0]
            if machine in (62, 183):
                return {62: "x86_64", 183: "aarch64"}[machine]
        if layout == "windows" and header[:2] == b"MZ":
            stream.seek(struct.unpack_from("<I", header, 60)[0])
            signature = stream.read(6)
            if signature[:4] == b"PE\0\0":
                machine = struct.unpack_from("<H", signature, 4)[0]
                if machine in (0x8664, 0xAA64):
                    return {0x8664: "x86_64", 0xAA64: "aarch64"}[machine]
    if layout == "macos":
        arch = subprocess.check_output(["lipo", "-archs", str(binary)], text=True).strip()
        if arch in ("arm64", "x86_64"):
            return arch
    raise ValueError("unsupported %s executable: %s" % (layout, binary))


def dynamic(binary):
    return subprocess.check_output(["readelf", "-dW", str(binary)], text=True)


def elf_relocation_page_size(binary):
    with binary.open("rb") as stream:
        header = stream.read(64)
        executable = struct.unpack_from("<H", header, 16)[0] == 2
        offset = struct.unpack_from("<Q", header, 32)[0]
        size, count = struct.unpack_from("<HH", header, 54)
        alignment = 4096
        for index in range(count):
            stream.seek(offset + index * size)
            kind, _, position, address, _, _, _, boundary = struct.unpack("<IIQQQQQQ", stream.read(56))
            if kind == 1 and boundary:
                if position % boundary != address % boundary:
                    raise ValueError("misaligned ELF load segment: %s" % binary)
                alignment = max(alignment, boundary)
    # A large rewrite page can move an ET_EXEC header mapping below mmap_min_addr.
    # Shared objects instead need their existing maximum segment alignment.
    return (65536 if architecture(binary, "linux") == "aarch64" else 4096) if executable else alignment


def glibc_requirement(binaries):
    versions = set()
    for binary in binaries:
        output = subprocess.check_output(["readelf", "--version-info", "-W", str(binary)], text=True)
        special = re.findall(r"\bName: (GLIBC_[A-Za-z][A-Za-z0-9_]*)\b", output)
        if special:
            raise ValueError("unsupported glibc ABI requirement in %s: %s" % (binary, ", ".join(special)))
        versions.update(re.findall(r"\bGLIBC_(\d+(?:\.\d+)+)\b", output))
    return max(versions, key=version_tuple) if versions else "0.0"


def version_tuple(value):
    parts = tuple(int(part) for part in value.split("."))
    return parts + (0,) * (3 - len(parts))


def linux_libdir():
    import ctypes
    import tempfile
    with tempfile.TemporaryDirectory(prefix="seekdb-libdir-") as temporary:
        root = Path(temporary)
        probe = root / "$LIB/seekdb-probe.so"
        def failure():
            try:
                ctypes.CDLL(str(probe))
            except OSError as error:
                return str(error)
            raise ValueError("unexpected load of an empty ELF library probe")
        missing = failure()
        # An empty file changes the loader error without executing any library.
        for name in ("lib", "lib64", "lib/x86_64-linux-gnu", "lib/aarch64-linux-gnu"):
            target = root / name / probe.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.touch()
            found = failure() != missing
            target.unlink()
            if found:
                return name
    raise ValueError("unsupported native ELF $LIB directory")


def linux_loader_help():
    # Older loaders have no hwcaps directories and do not recognize --help.
    if version_tuple(os.confstr("CS_GNU_LIBC_VERSION").split()[-1]) < version_tuple("2.33"):
        return ""
    # Use the packaging process's trusted loader, never an input ELF's interpreter.
    headers = subprocess.check_output(["readelf", "-lW", str(Path("/proc/self/exe").resolve())], text=True)
    interpreter = re.search(r"\[Requesting program interpreter: (.*?)\]", headers)
    if not interpreter:
        return ""
    result = subprocess.run([interpreter.group(1), "--help"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return result.stdout


def linux_hwcaps(output=None):
    output = linux_loader_help() if output is None else output
    section = output.partition("Subdirectories of glibc-hwcaps directories, in priority order:")[2]
    section = section.split("\n\n", 1)[0]
    return tuple("glibc-hwcaps/" + name for name in re.findall(
        r"^\s+(\S+) \(supported, searched\)$", section, re.MULTILINE))


def linux_libraries(prefix, original, directories, extra_libraries):
    libraries = prefix / "lib/seekdb"
    libraries.mkdir(parents=True)
    executable = prefix / "bin/seekdb"
    system = re.compile(r"^(ld-linux[^/]*|lib(c|m|mvec|dl|rt|pthread|resolv|util|anl|thread_db|BrokenLocale|nss_[^.]*)\.so)(\.|$)")
    cache = {}
    ldconfig = shutil.which("ldconfig") or "/sbin/ldconfig"
    for name, path in re.findall(r"^\s*(\S+)\s+\([^\n]*\)\s+=>\s+(\S+)",
                                 subprocess.check_output([ldconfig, "-p"], text=True), re.MULTILINE):
        cache.setdefault(name, []).append(Path(path))
    pending = [(original, executable, ())]
    origins = {}
    executable_arch = architecture(executable, "linux")
    libdir = None
    loader_help = linux_loader_help()
    subdirectories = linux_hwcaps(loader_help) + ("",)
    defaults = [Path(name) for name in re.findall(r"^\s+(/\S+) \(system search path\)$", loader_help, re.MULTILINE)]
    if not defaults:
        libdir = linux_libdir()
        defaults = [Path("/") / libdir, Path("/usr") / libdir]
        if "/" in libdir:
            defaults += [Path("/lib"), Path("/usr/lib")]

    def expand(entry, source):
        def replace(match):
            nonlocal libdir
            if (match.group(1) or match.group(2)) == "ORIGIN":
                return str(source.parent)
            if libdir is None:
                libdir = linux_libdir()
            return libdir
        return Path(re.sub(r"\$(?:\{(ORIGIN|LIB)\}|(ORIGIN|LIB)\b)", replace, entry))

    def matching(path):
        try:
            return path.is_file() and architecture(path, "linux") == executable_arch
        except ValueError:
            return False

    def enqueue(source, name, inherited=()):
        if system.match(name):
            raise ValueError("system runtime libraries must not be bundled: %s" % name)
        source = source.absolute()
        canonical = source.resolve()
        if name in origins:
            if origins[name] != canonical:
                raise ValueError("conflicting runtime libraries named %s" % name)
            return
        if architecture(source, "linux") != executable_arch:
            raise ValueError("runtime library architecture mismatch: %s" % source)
        origins[name] = canonical
        target = libraries / name
        shutil.copyfile(source, target)
        target.chmod(0o755)
        pending.append((source, target, inherited))

    root_paths = dict(re.findall(r"\((RPATH|RUNPATH)\).*?\[(.*?)\]", dynamic(original)))
    inherited = () if "RUNPATH" in root_paths else tuple(
        expand(entry, original) for entry in root_paths.get("RPATH", "").split(":") if entry)
    for source in extra_libraries:
        enqueue(source, source.name, inherited)
    while pending:
        source, target, inherited = pending.pop()
        metadata = dynamic(source)
        tags = re.findall(r"\((RPATH|RUNPATH)\).*?\[(.*?)\]", metadata)
        paths = {kind: tuple(expand(entry, source) for entry in value.split(":") if entry) for kind, value in tags}
        if "RUNPATH" not in paths:
            inherited = paths.get("RPATH", ()) + inherited
        search = list(paths.get("RUNPATH", inherited))
        search.extend(directories)
        rpaths = [value for _, value in tags]
        needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", metadata)
        for name in needed:
            if system.match(name):
                continue
            if Path(name).name != name:
                raise ValueError("runtime dependency must use a library basename: %s" % name)
            candidates = [directory / subdirectory / name for directory in search
                          for subdirectory in subdirectories] + cache.get(name, [])
            candidates += [directory / subdirectory / name for directory in defaults
                           for subdirectory in subdirectories]
            resolved = next((path for path in candidates if matching(path)), None)
            if resolved is None:
                raise ValueError("unresolved dependency %s imported by %s; supply --runtime-lib-dir" % (name, source))
            enqueue(resolved, name, inherited)
        rpath = "$ORIGIN/../lib/seekdb:$ORIGIN/../libexec/seekdb/lib" if target == executable else "$ORIGIN"
        alignment = elf_relocation_page_size(target)
        # Libraries also need their own search path when they dlopen plugins.
        if rpaths != [rpath]:
            subprocess.run(["patchelf", "--page-size", str(alignment), "--set-rpath", rpath, str(target)], check=True)
        elf_relocation_page_size(target)
    return [executable] + sorted(libraries.iterdir())


def macos_binaries(prefix, arch, minimum_os="13.0", executable=None):
    versions = []
    for binary in [executable or prefix / "bin/seekdb"] + sorted((prefix / "lib/seekdb").iterdir()):
        if architecture(binary, "macos") != arch:
            raise ValueError("macOS runtime architecture mismatch: %s" % binary)
        commands = subprocess.check_output(["otool", "-l", str(binary)], text=True)
        target = re.findall(
            r"cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform (?:1|MACOS)\s+minos (\d+(?:\.\d+)+)",
            commands, re.IGNORECASE) or re.findall(
                r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\d+(?:\.\d+)+)", commands)
        if not target:
            raise ValueError("cannot determine the macOS deployment target: %s" % binary)
        required = max(target, key=version_tuple)
        if version_tuple(required) > version_tuple(minimum_os):
            raise ValueError("binary requires macOS %s, exceeding target %s: %s; rebuild it for the target OS" %
                             (required, minimum_os, binary))
        versions.extend(target)
    return max(versions, key=version_tuple)


def windows_imports(binary):
    imports = subprocess.check_output(["dumpbin", "/nologo", "/dependents", str(binary)], text=True)
    libraries = {name.lower() for name in re.findall(r"^\s+([^\s]+\.dll)\s*$", imports,
                                                    re.MULTILINE | re.IGNORECASE)}
    if any(re.fullmatch(r"(?:vcruntime|msvcp)\d+(?:_\d+)*d(?:_\w+)?\.dll|ucrtbased\.dll", name)
           for name in libraries):
        raise ValueError("Windows binary distributions require release /MD CRT imports: %s" % binary)
    return libraries


def windows_runtime(binary):
    libraries = windows_imports(binary)
    if not any(name == "ucrtbase.dll" or name.startswith("api-ms-win-crt-") for name in libraries):
        raise ValueError("Windows binary distributions require /MD dynamic CRT imports: %s" % binary)
    return "MD"


def windows_binaries(prefix, arch):
    binaries = [prefix / "bin/seekdb.exe"] + sorted(
        path for path in (prefix / "bin").iterdir() if path.suffix.lower() == ".dll")
    for binary in binaries:
        if architecture(binary, "windows") != arch:
            raise ValueError("Windows runtime architecture mismatch: %s" % binary)
        with binary.open("rb") as stream:
            header = stream.read(64)
            stream.seek(struct.unpack_from("<I", header, 60)[0] + 24)
            optional = stream.read(52)
        if len(optional) < 52 or struct.unpack_from("<H", optional)[0] != 0x20b:
            raise ValueError("unsupported Windows PE optional header: %s" % binary)
        # PE OS/subsystem versions are lower bounds, not an imported-API audit.
        required = max(struct.unpack_from("<HH", optional, offset) for offset in (40, 48))
        if required > (10, 0):
            raise ValueError("Windows binary requires %s.%s, exceeding the Windows 10 target: %s" %
                             (required[0], required[1], binary))
    return binaries


def windows_dependencies(binaries):
    available = {path.name.lower() for path in binaries}
    system = Path(os.environ["SystemRoot"]) / "System32"
    for binary in binaries:
        for name in windows_imports(binary):
            if name in available or name.startswith(("api-ms-", "ext-ms-")):
                continue
            if name.startswith(("msvcp", "vcruntime", "concrt", "vcomp", "vccorlib")) or not (system / name).is_file():
                raise ValueError("unbundled Windows dependency %s imported by %s; include it in seekdb.exe.runtime.json" % (name, binary))


def android_binaries(prefix, arch):
    binaries = [prefix / "bin/seekdb"] + sorted((prefix / "lib/seekdb").iterdir())
    available = {path.name for path in binaries}
    system = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libz.so"}
    for binary in binaries:
        if architecture(binary, "android") != arch:
            raise ValueError("Android runtime architecture mismatch: %s" % binary)
        versions = []
        with binary.open("rb") as stream:
            header = stream.read(64)
            offset = struct.unpack_from("<Q", header, 32)[0]
            size, count = struct.unpack_from("<HH", header, 54)
            for index in range(count):
                stream.seek(offset + index * size)
                kind, _, position, _, _, length, _, alignment = struct.unpack("<IIQQQQQQ", stream.read(56))
                if kind == 1 and alignment < 16384:
                    raise ValueError("Android load alignment must be at least 16 KiB: %s" % binary)
                if kind != 4:
                    continue
                stream.seek(position)
                notes = stream.read(length)
                cursor = 0
                while cursor + 12 <= len(notes):
                    namesz, descsz, tag = struct.unpack_from("<III", notes, cursor)
                    name = notes[cursor + 12:cursor + 12 + namesz].rstrip(b"\0")
                    description = cursor + 12 + ((namesz + 3) & ~3)
                    end = description + ((descsz + 3) & ~3)
                    if end > len(notes):
                        raise ValueError("invalid Android ELF note: %s" % binary)
                    if name == b"Android" and tag == 1 and descsz >= 4:
                        versions.append(struct.unpack_from("<I", notes, description)[0])
                    cursor = end
        if not versions or max(versions) > 26:
            raise ValueError("Android binary must declare a minimum API no newer than 26: %s" % binary)
        imports = set(re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic(binary)))
        if imports - system - available:
            raise ValueError("unbundled Android dependencies in %s: %s; link non-system libraries statically" %
                             (binary, ", ".join(sorted(imports - system - available))))
    return binaries


def prepare(prefix, original, layout, directories, extra_libraries, glibc):
    arch = architecture(prefix / "bin" / ("seekdb.exe" if layout == "windows" else "seekdb"), layout)
    host_layout = {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(platform.system())
    machine = platform.machine().lower()
    host_arch = {"arm64": "aarch64", "amd64": "x86_64"}.get(machine, machine)
    cross_android = layout == "android" and host_layout in ("linux", "macos")
    if not cross_android and (layout != host_layout or arch.replace("arm64", "aarch64") != host_arch):
        raise ValueError("binary distributions must be assembled on a matching native OS and architecture")
    metadata = {"os": layout, "arch": arch}
    if layout == "linux":
        binaries = linux_libraries(prefix, original, directories, extra_libraries)
        required = glibc_requirement(binaries)
        if glibc and version_tuple(required) > version_tuple(glibc):
            raise ValueError("binary requires GLIBC_%s, exceeding requested baseline %s" % (required, glibc))
        metadata.update(glibc=glibc or required, required_glibc=required)
    elif layout == "macos":
        from pkg.bundle_dylibs import bundle
        bundle(prefix, original, prefix / "bin/seekdb")
        metadata["minimum_os"] = macos_binaries(prefix, arch)
    elif layout == "android":
        (prefix / "lib/seekdb").mkdir(parents=True, exist_ok=True)
        android_binaries(prefix, arch)
        metadata["minimum_api"] = 26
    else:
        metadata["msvc_runtime"] = windows_runtime(prefix / "bin/seekdb.exe")
        windows_dependencies(windows_binaries(prefix, arch))
        metadata["minimum_os"] = "10.0"
    (prefix / "lib/seekdb").mkdir(parents=True, exist_ok=True)
    return metadata


def write(prefix, output, version, release, source_commit, metadata, kind="seekdb-binary", built_at=None, **extra):
    built_at = built_at or datetime.now(timezone.utc).replace(microsecond=0)
    files = {path.relative_to(prefix).as_posix(): {"sha256": digest(path), "size": path.stat().st_size}
             for path in sorted(prefix.rglob("*")) if path.is_file()}
    manifest = {"format": kind, "schema_version": 1, "version": version,
                "release": release, "source_commit": source_commit, "built_at": built_at.isoformat().replace("+00:00", "Z"),
                "platform": metadata, "files": files}
    manifest.update(extra)
    (prefix / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    checksums = {name: info["sha256"] for name, info in files.items()}
    checksums["manifest.json"] = digest(prefix / "manifest.json")
    (prefix / "SHA256SUMS").write_text("".join("%s  %s\n" % (value, name) for name, value in sorted(checksums.items())))
    abi = "-glibc" + metadata["glibc"] if metadata["os"] == "linux" else ""
    if metadata["os"] == "windows":
        abi = "-" + metadata["msvc_runtime"].lower()
    if metadata["os"] == "android":
        abi = "-api" + str(metadata["minimum_api"])
    system = metadata["os"] + (metadata["minimum_os"] if metadata["os"] == "macos" else "")
    name = "%s-%s-%s-%s%s-%s-sdb%s" % (
        kind, version, system, metadata["arch"], abi, built_at.strftime("%Y%m%dT%H%M%SZ"), source_commit[:12])
    if metadata["os"] == "windows":
        target = output / (name + ".zip")
        with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(prefix.rglob("*")):
                archive.write(path, path.relative_to(prefix).as_posix())
    else:
        target = output / (name + ".tar.gz")
        def owner(info):
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mode &= 0o777
            return info
        with tarfile.open(target, "w:gz") as archive:
            for path in sorted(prefix.iterdir()):
                archive.add(path, arcname=path.name, filter=owner)
    target.with_name(target.name + ".sha256").write_text("%s  %s\n" % (digest(target), target.name))
    print(target)
    return target


def relative(name):
    path = PurePosixPath(name)
    if not path.parts or path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or any(
            part.endswith((".", " ")) or PureWindowsPath(part).is_reserved() for part in path.parts):
        raise ValueError("invalid binary package path: %s" % name)
    return path


def entries(root):
    pending = [root]
    result = []
    while pending:
        path = pending.pop()
        mode = path.lstat()
        if stat.S_ISLNK(mode.st_mode) or getattr(mode, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0):
            raise ValueError("links are not allowed in binary packages: %s" % path)
        if not (stat.S_ISDIR(mode.st_mode) or stat.S_ISREG(mode.st_mode)):
            raise ValueError("unsupported binary package file type: %s" % path)
        if stat.S_IMODE(mode.st_mode) & 0o7000:
            raise ValueError("special permission bits are not allowed in binary packages: %s" % path)
        result.append((path, mode))
        if stat.S_ISDIR(mode.st_mode):
            pending.extend(path.iterdir())
    return result


def load(source, destination, version=None):
    if source.is_dir():
        root = source
    else:
        root = destination
        root.mkdir()
        root.chmod(root.stat().st_mode & 0o777)
        seen = set()
        if zipfile.is_zipfile(source):
            with zipfile.ZipFile(source) as archive:
                for member in archive.infolist():
                    path = relative(member.filename)
                    if path.as_posix() in seen or (member.external_attr >> 16) & 0o170000 == 0o120000:
                        raise ValueError("unsupported or duplicate binary package member: %s" % member.filename)
                    seen.add(path.as_posix())
                    target = root / path
                    if member.is_dir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with archive.open(member) as stream, target.open("wb") as output:
                            shutil.copyfileobj(stream, output)
        else:
            with tarfile.open(source, "r:*") as archive:
                for member in archive:
                    path = relative(member.name)
                    if path.as_posix() in seen or not (member.isdir() or member.isfile()):
                        raise ValueError("unsupported or duplicate binary package member: %s" % member.name)
                    seen.add(path.as_posix())
                    target = root / path
                    if member.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                        target.chmod(0o755)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with archive.extractfile(member) as stream, target.open("wb") as output:
                            shutil.copyfileobj(stream, output)
                        target.chmod(member.mode & 0o777)
    payload = entries(root)
    manifest = json.loads((root / "manifest.json").read_text())
    if not isinstance(manifest, dict) or manifest.get("format") != "seekdb-binary" or manifest.get("schema_version") != 1:
        raise ValueError("unsupported binary package manifest")
    if not isinstance(manifest.get("version"), str) or not re.fullmatch(r"[0-9A-Za-z.+~_]+", manifest["version"]):
        raise ValueError("invalid binary package version")
    if version is not None and manifest["version"] != version:
        raise ValueError("binary package version %s does not match requested version %s" % (manifest.get("version"), version))
    metadata = manifest.get("platform")
    if not isinstance(metadata, dict) or metadata.get("os") not in ("linux", "macos", "windows", "android"):
        raise ValueError("unsupported binary package OS")
    if not isinstance(manifest.get("source_commit"), str) or not re.fullmatch(r"[0-9a-f]{40}", manifest["source_commit"]):
        raise ValueError("binary package requires a full source commit")
    if metadata["os"] == "linux" and (not isinstance(metadata.get("glibc"), str) or
                                        not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", metadata["glibc"])):
        raise ValueError("binary package requires a numeric glibc baseline")
    if metadata["os"] == "windows" and metadata.get("msvc_runtime") != "MD":
        raise ValueError("Windows binary packages require msvc_runtime=MD")
    if metadata["os"] == "windows" and metadata.get("minimum_os") != "10.0":
        raise ValueError("Windows binary packages require minimum_os=10.0")
    if metadata["os"] == "android" and metadata.get("minimum_api") != 26:
        raise ValueError("Android binary packages require minimum_api=26")
    if metadata["os"] == "macos" and (not isinstance(metadata.get("minimum_os"), str) or
                                        not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", metadata["minimum_os"]) or
                                        version_tuple(metadata["minimum_os"]) > version_tuple("13.0")):
        raise ValueError("macOS binary packages require a numeric deployment target no newer than 13.0")
    files = manifest.get("files")
    if not isinstance(files, dict):
        raise ValueError("binary package requires a file manifest")
    for name, info in files.items():
        if not isinstance(info, dict) or not isinstance(info.get("sha256"), str) or not re.fullmatch(
                r"[0-9a-f]{64}", info["sha256"]) or not isinstance(info.get("size"), int) or info["size"] < 0:
            raise ValueError("invalid binary package file metadata: %s" % name)
        path = root / relative(name)
        try:
            path.resolve().relative_to(root.resolve())
        except ValueError:
            raise ValueError("binary package file escapes its directory: %s" % name)
        if path.is_symlink() or not path.is_file():
            raise ValueError("missing or linked binary package file: %s" % name)
        if path.stat().st_size != info["size"] or digest(path) != info["sha256"]:
            raise ValueError("binary package checksum mismatch: %s" % name)
    actual = {path.relative_to(root).as_posix() for path, mode in payload if stat.S_ISREG(mode.st_mode)}
    if actual != set(files) | {"manifest.json", "SHA256SUMS"}:
        raise ValueError("binary package contains unlisted or missing files")
    executable = root / "bin" / ("seekdb.exe" if manifest["platform"]["os"] == "windows" else "seekdb")
    if architecture(executable, manifest["platform"]["os"]) != manifest["platform"]["arch"]:
        raise ValueError("binary package architecture does not match its executable")
    if metadata["os"] == "windows":
        windows_binaries(root, metadata["arch"])
    elif metadata["os"] == "macos":
        macos_binaries(root, metadata["arch"], metadata["minimum_os"])
    elif metadata["os"] == "android":
        android_binaries(root, metadata["arch"])
    else:
        binaries = [executable] + sorted((root / "lib/seekdb").iterdir())
        for path in binaries:
            if architecture(path, "linux") != metadata["arch"]:
                raise ValueError("Linux runtime architecture mismatch: %s" % path)
        required = glibc_requirement(binaries)
        if version_tuple(required) > version_tuple(metadata["glibc"]):
            raise ValueError("binary package requires GLIBC_%s, exceeding manifest baseline %s" %
                             (required, metadata["glibc"]))
    for path in ("share/seekdb/admin", "share/seekdb/srs", "share/seekdb/timezone", "etc/seekdb", "libexec/seekdb", "lib/seekdb"):
        if not (root / path).is_dir():
            raise ValueError("missing binary package directory: %s" % path)
    if not list((root / "share/seekdb/admin").glob("*.sql")):
        raise ValueError("binary package has no generated SQL resources")
    for path in ("share/seekdb/srs/default_srs_data_mysql.sql", "share/seekdb/timezone/timezone_V1.log",
                 "etc/seekdb/ob_system_variable_init.json", "etc/seekdb/seekdb.cnf"):
        if path not in files:
            raise ValueError("missing binary package resource: %s" % path)
    return root, manifest
