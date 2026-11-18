from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

REPO = Path(os.environ["GITHUB_WORKSPACE"])
ROOT = Path(tempfile.mkdtemp(prefix="symbols-review-", dir=os.environ["RUNNER_TEMP"]))
sys.path.insert(0, str(REPO / "package"))
import binary
import symbols

readobj = Path(symbols.tool("readobj", "windows"))
pdbutil = Path(symbols.tool("pdbutil", "windows"))
os.environ["PATH"] = str(readobj.parent) + os.pathsep + os.environ["PATH"]
tool_version = subprocess.check_output([str(readobj), "--version"], text=True)


def run(arguments):
    subprocess.run([str(value) for value in arguments], check=True)


for label in ("before", "after"):
    package = ROOT / label / "package"
    package.mkdir(parents=True)
    for name in ("binary.py", "package.py", "symbols.py"):
        data = subprocess.check_output(["git", "show", "3e8686705ac66a5157f6568076a670ea3e865f9f:package/" + name], cwd=REPO) if label == "before" else (REPO / "package" / name).read_bytes()
        (package / name).write_bytes(data)
    resources = {"tools/default_srs_data_mysql.sql": "SELECT 1;\n",
                 "src/share/system_variable/ob_system_variable_init.json": "{}\n",
                 "package/msi/seekdb.cnf": "[seekdb]\n", "LICENSE": "fixture\n", "NOTICE": "fixture\n",
                 "deps/prebuilt/native/share/licenses/fixture.txt": "fixture\n",
                 "deps/prebuilt/native/manifest.json": "{}\n", "deps.lock.json": "{}\n"}
    for name, data in resources.items():
        path = ROOT / label / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data)
shutil.copyfile(Path(__file__).with_name("check_pdb.py"), ROOT / "check_pdb.py")
run([sys.executable, ROOT / "check_pdb.py"])
cross = json.loads((ROOT / "pdb-results.json").read_text())

original = ROOT / "md-input"
original.mkdir()
(original / "main.c").write_text('#include <stdio.h>\nint main(void) { puts("42"); return 0; }\n')
run([readobj.parent / "clang-cl.exe", "/nologo", "/MD", "/Z7", "/O2", original / "main.c",
     "/Fo" + str(original / "main.obj"), "/Fe" + str(original / "seekdb.exe"),
     "/link", "/DEBUG:FULL", "/INCREMENTAL:NO", "/PDB:" + str(original / "seekdb.pdb")])
libraries = set()
for name in binary.windows_imports(original / "seekdb.exe"):
    if name.startswith(("vcruntime", "msvcp", "concrt")):
        shutil.copyfile(Path(os.environ["SystemRoot"]) / "System32" / name, original / name)
        libraries.add(name)
(original / "seekdb.exe.runtime.json").write_text(json.dumps(sorted(libraries)))
artifacts = ROOT / "artifacts/src/share/inner_table/sys_package/bazel_generated/syspack_release"
artifacts.mkdir(parents=True)
(artifacts / "fixture.sql").write_text("SELECT 1;\n")
(ROOT / "timezone").write_text("fixture\n")
cases = {}
for name in ("matching", "wrong-guid", "wrong-age"):
    source = ROOT / ("md-" + name)
    shutil.copytree(original, source)
    if name == "wrong-guid":
        shutil.copyfile(ROOT / "pe/b/seekdb.pdb", source / "seekdb.pdb")
    if name == "wrong-age":
        path = source / "seekdb.exe"
        data = bytearray(path.read_bytes())
        assert data.count(b"RSDS") == 1
        age = data.index(b"RSDS") + 20
        data[age:age + 4] = (2).to_bytes(4, "little")
        path.write_bytes(data)
    output = ROOT / ("dist-" + name)
    command = [sys.executable, ROOT / "after/package/package.py", "binary", "--version", "0.0.0",
               "--source-commit", "0" * 40, "--package-timestamp", "20261118T000000Z", "--layout", "windows",
               "--seekdb-bin", source / "seekdb.exe", "--artifacts", ROOT / "artifacts",
               "--timezone-file", ROOT / "timezone", "--output", output]
    process = subprocess.run([str(value) for value in command], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if name != "matching":
        assert process.returncode and "PDB GUID/age does not match" in process.stderr, process.stderr
        assert not list(output.glob("*.zip"))
        cases[name] = {"rejected": True, "archives_published": 0}
        continue
    assert process.returncode == 0, process.stderr
    runtime, debug = output / "runtime", output / "debug"
    for archive in output.glob("*.zip"):
        with zipfile.ZipFile(archive) as stream:
            stream.extractall(debug if archive.name.startswith("seekdb-debug-info-") else runtime)
    binary.load(runtime, output / "unused", "0.0.0")
    symbols.verify_stripped(runtime)
    manifest = json.loads((debug / "manifest.json").read_text())
    record = manifest["binaries"]["bin/seekdb.exe"]
    assert record["pdb"] == symbols.matching_pdb(runtime / "bin/seekdb.exe", debug / record["symbols"], "windows")
    assert binary.digest(runtime / "bin/seekdb.exe") == record["binary_sha256"]
    assert subprocess.check_output([str(runtime / "bin/seekdb.exe")], text=True).strip() == "42"
    cases[name] = {"runtime_stripped": True, "pdb": record["pdb"], "execution": "42"}
result = {"platform": "Windows 2022 x86_64", "readobj": str(readobj), "pdbutil": str(pdbutil),
          "llvm_version": tool_version.strip(), "low_level_checks": cross, "cli_checks": cases,
          "check_count": len(cross) + len(cases), "status": "passed"}
destination = REPO / "dist/symbols-review-results.json"
destination.parent.mkdir(exist_ok=True)
destination.write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2), flush=True)
