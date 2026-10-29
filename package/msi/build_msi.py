#!/usr/bin/env python3
"""Build a WiX v4 MSI from the Windows Bazel release outputs."""

import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("seekdb_package", ROOT / "package/package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
NAMESPACE = "http://wixtoolset.org/schemas/v4/wxs"


def runtime_dependencies(binaries, directories, destination):
    available = {}
    for directory in directories:
        for dll in directory.glob("*.dll"):
            available.setdefault(dll.name.lower(), dll)
    system = Path(os.environ["SystemRoot"]) / "System32"
    pending = list(binaries)
    visited = set()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in visited:
            continue
        visited.add(binary.name.lower())
        imports = subprocess.check_output(["dumpbin", "/nologo", "/dependents", str(binary)], text=True)
        for name in re.findall(r"^\s+([^\s]+\.dll)\s*$", imports, flags=re.MULTILINE | re.IGNORECASE):
            key = name.lower()
            if key.startswith(("api-ms-", "ext-ms-")):
                continue
            if key in available:
                if key not in visited:
                    target = destination / available[key].name
                    if available[key].resolve() != target.resolve():
                        package.copy(available[key], target, True)
                    pending.append(target)
            elif key.startswith(("msvcp", "vcruntime", "concrt", "vcomp")) or not (system / name).is_file():
                raise ValueError("unresolved dependency %s imported by %s; supply --dll-dir" % (name, binary))


def payload_source(payload, target):
    ET.register_namespace("", NAMESPACE)
    def element(parent, tag, **attributes):
        return ET.SubElement(parent, "{%s}%s" % (NAMESPACE, tag), attributes)
    document = ET.Element("{%s}Wix" % NAMESPACE)
    fragment = element(document, "Fragment")
    directory_ref = element(fragment, "DirectoryRef", Id="INSTALL_ROOT")
    directories = {Path("."): directory_ref}
    group = element(fragment, "ComponentGroup", Id="Payload")
    for source in sorted(payload.rglob("*")):
        relative = source.relative_to(payload)
        identifier = hashlib.sha256(relative.as_posix().encode()).hexdigest()[:24]
        if source.is_dir():
            directories[relative] = element(directories[relative.parent], "Directory", Id="D" + identifier, Name=source.name)
        else:
            component = element(directories[relative.parent], "Component", Id="C" + identifier, Guid="*")
            element(component, "File", Id="F" + identifier, Source=str(source), KeyPath="yes")
            element(group, "ComponentRef", Id="C" + identifier)
    ET.ElementTree(document).write(target, encoding="utf-8", xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--release", type=int, required=True)
    parser.add_argument("--artifacts", type=Path, default=ROOT / "build/bin")
    parser.add_argument("--dll-dir", type=Path, action="append", default=[],
                        help="additional runtime DLL directories for an external configurator")
    parser.add_argument("--configurator", type=Path)
    parser.add_argument("--timezone-file", type=Path)
    parser.add_argument("--output", type=Path, default=Path.cwd())
    options = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:\.\d+)?", options.version) or options.release < 0:
        parser.error("MSI requires a three- or four-part numeric version and nonnegative release")
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="seekdb-msi-") as temporary:
        work = Path(temporary)
        payload = work / "payload"
        package.stage(payload, options.artifacts.resolve(), None, "windows",
                      options.version, str(options.release), timezone=options.timezone_file)
        configurator = options.configurator
        if configurator:
            package.copy(configurator, payload / "bin/seekdbConfigurator.exe", True)
            runtime_dependencies([payload / "bin/seekdbConfigurator.exe"],
                                 [payload / "bin"] + options.dll_dir, payload / "bin")
        payload_source(payload, work / "payload.wxs")
        license_text = (ROOT / "LICENSE").read_text().replace("\\", "\\\\").replace("{", "\\{").replace("}", "\\}")
        license_file = work / "LICENSE.rtf"
        license_file.write_text("{\\rtf1\\ansi\n" + license_text.replace("\n", "\\par\n") + "}")
        subprocess.run(["wix", "build", "-arch", "x64", "-ext", "WixToolset.UI.wixext",
                        "-d", "Version=" + options.version, "-d", "License=" + str(license_file),
                        "-d", "Configurator=" + ("1" if configurator else "0"),
                        str(ROOT / "package/msi/seekdb.wxs"),
                        str(ROOT / "package/msi/wix_launch_configurator.wxs"), str(work / "payload.wxs"),
                        "-o", str(output / ("seekdb-%s-%s-x64.msi" % (options.version, options.release)))], check=True)


if __name__ == "__main__":
    main()
