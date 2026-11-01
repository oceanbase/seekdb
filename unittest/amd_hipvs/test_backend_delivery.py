"""Exercise real dlopen and CMake delivery without claiming GPU computation.

The tiny ABI fixture intentionally returns 77, never a successful GPU result.
Run these Linux integration tests with python -m unittest test_backend_delivery.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SUPPORTED = sys.platform == "linux" and all(shutil.which(x) for x in ("g++", "cmake", "make"))


@unittest.skipUnless(SUPPORTED, "Linux C++/CMake delivery test")
class BackendDelivery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hipvs delivery ")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.bin = self.base / "usr/bin"
        self.bin.mkdir(parents=True)
        self.env = dict(os.environ)
        self.env.pop("SEEKDB_HIPVS_BUILD_LIBRARY", None)
        self.loader_source = self.base / "loader.cpp"
        self.loader_source.write_text('''#include "lib/vector/hipvs_backend.h"
#include <cstdio>
int main() {
  oceanbase::common::obvsag::HipvsBackend backend;
  if (!backend.build) { std::puts(backend.error); return 3; }
  return backend.build(nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, 0);
}
''')
        self.run_ok(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     "-I" + str(ROOT / "src/oblib"), str(self.loader_source), "-ldl",
                     "-o", str(self.bin / "seekdb")])

    def run_ok(self, args, **kwargs):
        return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs)

    def make_library(self, directory, symbol=True):
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / "fixture.cpp"
        source.write_text('#include <cstdint>\n#include <cstddef>\nextern "C" int '
                          + ("seekdb_hipvs_build_hnsw_v1" if symbol else "unrelated")
                          + '(const float *, int64_t, int32_t, int32_t, int32_t, int32_t, '
                            'const char *, char *, size_t) { return 77; }\n')
        library = directory / "libseekdb_hipvs_build.so"
        self.run_ok(["g++", "-shared", "-fPIC", str(source), "-o", str(library)])
        return library

    def invoke(self):
        return subprocess.run([str(self.bin / "seekdb")], cwd=self.base,
                              env=self.env, capture_output=True, text=True)

    def test_adjacent_without_environment(self):
        self.make_library(self.bin)
        self.assertEqual(self.invoke().returncode, 77)

    def test_installed_without_environment(self):
        self.make_library(self.base / "usr/lib/seekdb")
        self.assertEqual(self.invoke().returncode, 77)

    def test_explicit_override(self):
        self.env["SEEKDB_HIPVS_BUILD_LIBRARY"] = str(self.make_library(self.base / "override"))
        self.assertEqual(self.invoke().returncode, 77)

    def test_missing_override_does_not_fall_back(self):
        self.make_library(self.bin)
        self.env["SEEKDB_HIPVS_BUILD_LIBRARY"] = str(self.base / "missing.so")
        result = self.invoke()
        self.assertEqual(result.returncode, 3)
        self.assertIn("dlopen(", result.stdout)
        self.assertIn("missing.so", result.stdout)

    def test_relative_override_is_rejected(self):
        self.env["SEEKDB_HIPVS_BUILD_LIBRARY"] = "./libseekdb_hipvs_build.so"
        self.assertEqual(self.invoke().returncode, 3)
        self.assertIn("absolute path", self.invoke().stdout)

    def test_does_not_search_working_directory(self):
        self.make_library(self.base)
        result = self.invoke()
        self.assertEqual(result.returncode, 3)
        self.assertIn("not found", result.stdout)

    def test_wrong_abi_reports_symbol(self):
        self.make_library(self.bin, symbol=False)
        result = self.invoke()
        self.assertEqual(result.returncode, 3)
        self.assertIn("dlsym(", result.stdout)
        self.assertIn("seekdb_hipvs_build_hnsw_v1", result.stdout)

    def test_broken_adjacent_does_not_mask_error_with_installed_copy(self):
        self.make_library(self.bin, symbol=False)
        self.make_library(self.base / "usr/lib/seekdb")
        self.assertEqual(self.invoke().returncode, 3)

    def cmake_project(self):
        project = self.base / "project"
        project.mkdir()
        (project / "tools").mkdir()
        shutil.copytree(ROOT / "tools/hipvs_bridge_build", project / "tools/hipvs_bridge_build",
                        ignore=shutil.ignore_patterns("build"))
        (project / "main.cpp").write_text("int main() { return 0; }\n")
        (project / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(delivery CXX)
add_executable(seekdb main.cpp)
include("{(ROOT / 'cmake/Hipvs.cmake').as_posix()}")
seekdb_configure_hipvs(seekdb)
install(TARGETS seekdb RUNTIME DESTINATION usr/bin COMPONENT server)
''')
        return project

    def configure(self, project, mode="AUTO", present=False):
        hipvs = self.base / "hipvs"
        rocm = self.base / "rocm"
        if present:
            for path in (rocm / "include/hip/hip_runtime.h", hipvs / "include/cuvs/core/c_api.h",
                         hipvs / "include/cuvs/neighbors/cagra.h", hipvs / "include/cuvs/neighbors/hnsw.h",
                         hipvs / "lib/libcuvs_c.so"):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            fixture = self.make_library(self.base / "fixture")
            (rocm / "bin").mkdir()
            hipcc = rocm / "bin/hipcc"
            hipcc.write_text('''#!/usr/bin/env python3
import pathlib, shutil, sys
args = sys.argv[1:]
assert "-Wl,-z,defs" in args
shutil.copyfile(''' + repr(str(fixture)) + ''', args[args.index("-o") + 1])
''')
            hipcc.chmod(0o755)
        return subprocess.run(["cmake", "-S", str(project), "-B", str(self.base / "build"),
                               "-DSEEKDB_WITH_HIPVS=" + mode,
                               "-DSEEKDB_HIPVS_PREFIX=" + str(hipvs),
                               "-DSEEKDB_ROCM_PATH=" + str(rocm)], capture_output=True, text=True)

    def test_auto_without_sdk_builds_cpu(self):
        configured = self.configure(self.cmake_project())
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        self.run_ok(["cmake", "--build", str(self.base / "build"), "--target", "seekdb"])
        self.assertFalse((self.base / "build/libseekdb_hipvs_build.so").exists())

    def test_required_sdk_fails_at_configure(self):
        result = self.configure(self.cmake_project(), mode="ON")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dependencies are missing", result.stderr)

    def test_auto_build_and_install_deliver_bridge(self):
        configured = self.configure(self.cmake_project(), present=True)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        self.run_ok(["cmake", "--build", str(self.base / "build"), "--target", "seekdb"])
        self.assertTrue((self.base / "build/libseekdb_hipvs_build.so").is_file())
        self.run_ok(["cmake", "--install", str(self.base / "build"), "--component", "server",
                     "--prefix", str(self.base / "installed")])
        self.assertTrue((self.base / "installed/usr/lib/seekdb/libseekdb_hipvs_build.so").is_file())

    def test_disabled_does_not_build_bridge_even_with_sdk(self):
        configured = self.configure(self.cmake_project(), mode="OFF", present=True)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        self.run_ok(["cmake", "--build", str(self.base / "build"), "--target", "seekdb"])
        self.assertFalse((self.base / "build/libseekdb_hipvs_build.so").exists())


if __name__ == "__main__":
    unittest.main()
