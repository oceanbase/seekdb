#!/usr/bin/env python3

from collections import deque
from pathlib import Path
import re
import shutil
import subprocess
import sys


def output(*command):
    return subprocess.check_output(command, text=True)


def loader_path(path, binary, executable):
    return Path(path.replace("@loader_path", str(binary.parent))
                .replace("@executable_path", str(executable.parent)))


def bundle(prefix, executable):
    libraries = prefix / "lib/seekdb"
    libraries.mkdir(parents=True, exist_ok=True)
    pending = deque([(executable, prefix / "bin" / executable.name, ())])
    visited = set()
    origins = {}
    signed = []
    while pending:
        original, staged, inherited = pending.popleft()
        if staged in visited:
            continue
        visited.add(staged)
        metadata = output("otool", "-l", str(original))
        rpaths = tuple(loader_path(value, original, executable) for value in re.findall(
            r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", metadata)) + inherited
        identity = re.search(r"cmd LC_ID_DYLIB\s+cmdsize \d+\s+name (.*?) \(offset", metadata)
        imports = output("otool", "-L", str(original)).splitlines()[1:]
        for line in imports:
            dependency = line.strip().split(" (compatibility version", 1)[0]
            if (identity and dependency == identity.group(1)) or dependency.startswith(("/usr/lib/", "/System/")):
                continue
            if dependency.startswith("@rpath/"):
                candidates = [directory / dependency[len("@rpath/"):] for directory in rpaths]
            else:
                candidates = [loader_path(dependency, original, executable)]
            resolved = next((path.resolve() for path in candidates if path.is_file()), None)
            if resolved is None:
                raise ValueError("unresolved dylib %s imported by %s" % (dependency, original))
            target = libraries / resolved.name
            if target in origins and origins[target] != resolved:
                raise ValueError("conflicting dylibs for %s: %s and %s" % (target.name, origins[target], resolved))
            if target not in origins:
                origins[target] = resolved
                shutil.copyfile(resolved, target)
                target.chmod(0o755)
                pending.append((resolved, target, rpaths))
            relative = "@executable_path/../lib/seekdb/" if staged.parent == prefix / "bin" else "@loader_path/"
            subprocess.run(["install_name_tool", "-change", dependency, relative + target.name, str(staged)], check=True)
        if staged.parent == libraries:
            subprocess.run(["install_name_tool", "-id", "@loader_path/" + staged.name, str(staged)], check=True)
        signed.append(staged)
    for binary in signed:
        subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)


if __name__ == "__main__":
    bundle(Path(sys.argv[1]), Path(sys.argv[2]))
