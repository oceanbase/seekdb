#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import sys

manifest = json.loads(Path(sys.argv[1]).read_text())
package, profile = sys.argv[2:]
patches = manifest["patches"]
selected = []
if package == "boost":
    selected += patches["boost_ios_numeric"]["files"]
if package == "s2" and profile == "wasm":
    selected.append(patches["s2_wasm_port"])
if package == "vsag" and profile == "wasm":
    selected += [patches[name] for name in (
        "vsag_dataset_paths", "vsag_stream_sizes", "vsag_allocator_sizes")]
    selected += patches["vsag_source_patches"]["files"]
if package in ("clapack", "openblas") or (package == "vsag" and profile == "wasm"):
    selected += [p for p in patches["vsag_blas_patches"]["files"] if p["dependency"] == package]
for patch in selected:
    path = Path(patch["path"])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
        raise SystemExit(f"Patch input checksum mismatch: {path}")
    for before, after in patch["replacements"]:
        before, after = before.encode(), after.encode()
        if before not in data:
            raise SystemExit(f"Patch context mismatch: {path}")
        data = data.replace(before, after)
    data = patch.get("prepend", "").encode() + data
    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
        raise SystemExit(f"Patch output checksum mismatch: {path}")
    path.write_bytes(data)
if package == "abseil":
    for path in Path("absl").rglob("BUILD.bazel"):
        content = path.read_text()
        loads = ''.join(f'load("@rules_cc//cc:{name}.bzl", "{name}")\n'
                        for name in ("cc_library", "cc_binary", "cc_test") if name + "(" in content)
        content = content.replace("copts = ABSL_DEFAULT_COPTS", 'copts = select({"@platforms//os:emscripten": ["-std=c++20", "-pthread", "-fexceptions"], "//conditions:default": ["-std=c++17"]}) + ABSL_DEFAULT_COPTS')
        path.write_text(loads + content)
if package == "roaring":
    for path in Path("cpp").glob("*.hh"):
        Path("include/roaring", path.name).symlink_to("../../" + str(path))
if package == "vsag":
    Path("version.h").write_text(Path("src/version.h.in").read_text().replace(
        "@VSAG_VERSION@", manifest["sources"][package]["version"] + "-" + profile))
    Path("antlr4-autogen").symlink_to("extern/antlr4/fc", target_is_directory=True)
if package == "antlr4":
    Path("antlr4-runtime").symlink_to("runtime/Cpp/runtime/src", target_is_directory=True)
    # DiskANN's exceptions.h collides with ANTLR's Exceptions.h on macOS.
    for path in Path("runtime/Cpp/runtime/src").rglob("*"):
        if path.suffix in (".h", ".cpp"):
            path.write_text(path.read_text().replace('#include "Exceptions.h"', '#include "antlr4-runtime/Exceptions.h"'))
if package == "openblas":
    source = Path("lapack-netlib/CBLAS/include/cblas_mangling_with_flags.h.in")
    Path("lapack-netlib/CBLAS/include/cblas_mangling.h").write_bytes(source.read_bytes())
Path("source.json").write_text(json.dumps({
    "package": package,
    "profile": profile,
    **manifest["sources"][package],
    "patches": selected,
}, indent=2) + "\n")
