#!/usr/bin/env python3
"""Exercise the production Release install rule with a small Linux executable.

These tests require CMake and a C++ compiler, not seekdb/ROCm. The separate
check_hipvs_install.py validates a real built seekdb and its GPU dependencies.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cmake") and shutil.which("c++"),
                     "requires Linux, CMake, and a C++ compiler")
class ReleaseInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="seekdb-install-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "main.cpp").write_text(
            '#include <iostream>\nint main() { std::cout << "installed-server\\n"; }\n',
            encoding="utf-8")
        # Import the same rule used by src/observer/CMakeLists.txt. No copied
        # install declaration or stubbed CMake install function is involved.
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(InstallContract LANGUAGES CXX)\n'
            'add_executable(seekdb main.cpp)\n'
            f'include("{ROOT.as_posix()}/cmake/InstallReleaseServer.cmake")\n'
            'seekdb_install_release_server(seekdb)\n', encoding="utf-8")
        self.build = self.root / "build"
        self.prefix = self.root / "installed"

    def run_command(self, *command, success=True):
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def configure(self, package=False):
        self.run_command("cmake", "-S", str(self.source), "-B", str(self.build),
                         "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                         "-DSEEKDB_CMAKE_PACKAGE_BUILD=" + ("ON" if package else "OFF"))

    def install(self, component="server", success=True):
        return self.run_command("cmake", "--install", str(self.build), "--component", component,
                                "--prefix", str(self.prefix), success=success)

    def test_server_component_contains_runnable_main(self):
        self.configure()
        self.run_command("cmake", "--build", str(self.build))
        self.install(component="pretest")
        self.assertFalse((self.prefix / "usr/bin/seekdb").exists())
        self.install()
        result = self.run_command(str(self.prefix / "usr/bin/seekdb"))
        self.assertEqual(result.stdout, "installed-server\n")

    def test_unbuilt_main_cannot_produce_successful_install(self):
        self.configure()
        result = self.install(success=False)
        self.assertIn("seekdb", result.stdout + result.stderr)

    def test_package_layout_is_left_to_pack_cmake(self):
        self.configure(package=True)
        self.run_command("cmake", "--build", str(self.build))
        self.install()
        self.assertFalse((self.prefix / "usr/bin/seekdb").exists())


if __name__ == "__main__":
    unittest.main()
