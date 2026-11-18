from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent


def run(arguments):
    subprocess.run([str(value) for value in arguments], check=True)


def case(label, scenario):
    sys.path.insert(0, str(ROOT / label / "package"))
    import symbols
    import binary
    work = Path(tempfile.mkdtemp(prefix=label + "-" + scenario + "-", dir=ROOT))
    original = work / "input/seekdb.exe"
    original.parent.mkdir()
    shutil.copyfile(ROOT / "pe/a/seekdb.exe", original)
    pdb = original.with_suffix(".pdb")
    shutil.copyfile(ROOT / ("pe/b/seekdb.pdb" if scenario == "wrong-guid" else "pe/a/seekdb.pdb"), pdb)
    if scenario == "wrong-age":
        data = bytearray(original.read_bytes())
        assert data.count(b"RSDS") == 1
        offset = data.index(b"RSDS") + 20
        assert int.from_bytes(data[offset:offset + 4], "little") == 1
        data[offset:offset + 4] = (2).to_bytes(4, "little")
        original.write_bytes(data)
    runtime = work / "runtime/bin"
    runtime.mkdir(parents=True)
    shutil.copyfile(original, runtime / original.name)
    destination = work / "debug"
    try:
        records = symbols.separate(runtime.parent, destination, "windows", original, Path("bin/seekdb.exe"))
        result = {"accepted": True, "records": records, "pdb_sha256": binary.digest(destination / "bin/seekdb.pdb")}
    except ValueError as error:
        result = {"accepted": False, "error": str(error), "published_pdb": (destination / "bin/seekdb.pdb").exists()}
    (work / "result.json").write_text(json.dumps(result, indent=2))
    print("RESULT=" + json.dumps(result), flush=True)


if len(sys.argv) > 1:
    case(sys.argv[1], sys.argv[2])
    raise SystemExit()

for name, value in (("a", 41), ("b", 42)):
    directory = ROOT / "pe" / name
    directory.mkdir(parents=True)
    (directory / "main.c").write_text("int main(void) { return %s; }\n" % value)
    run(["clang-cl", "/nologo", "/c", "/Z7", "/Od", directory / "main.c", "/Fo" + str(directory / "main.obj")])
    run(["lld-link", "/entry:main", "/subsystem:console", "/nodefaultlib", "/debug:full",
         "/pdb:" + str(directory / "seekdb.pdb"), "/out:" + str(directory / "seekdb.exe"), directory / "main.obj"])
results = {}
for label in ("before", "after"):
    for scenario in ("matching", "wrong-guid", "wrong-age"):
        output = subprocess.check_output([sys.executable, str(Path(__file__)), label, scenario], text=True)
        result = json.loads(next(line[7:] for line in output.splitlines() if line.startswith("RESULT=")))
        expected = label == "before" or scenario == "matching"
        assert result["accepted"] == expected, (label, scenario, result)
        if not expected:
            assert "PDB GUID/age does not match" in result["error"], result
            assert not result["published_pdb"], result
        if label == "after" and scenario == "matching":
            assert result["records"]["bin/seekdb.exe"]["pdb"]["age"] == 1
        results[label + "/" + scenario] = result
(ROOT / "pdb-results.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps({"status": "passed", "checks": len(results), "cases": list(results)}, indent=2))
