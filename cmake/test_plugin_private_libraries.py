#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
"""Configure-time private dependency checks, including deferred mutations."""
import pathlib
import subprocess
import tempfile
import unittest


class PrivateLibraryTest(unittest.TestCase):
    def configure(self, body, success):
        source = pathlib.Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory(prefix="seekdb-private-library-") as directory:
            root = pathlib.Path(directory)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\n'
                'project(PrivateLibraryProbe VERSION 1.0.0 LANGUAGES C)\n'
                # Supply the SDK target without installing a fake SDK. All
                # actual dependency validation is the production helper.
                'add_library(seekdb_plugin_sdk INTERFACE)\n'
                f'include("{source}/Plugin.cmake")\n'
                'add_library(vendor INTERFACE)\n' + body)
            completed = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
            self.assertEqual(completed.returncode == 0, success, completed.stdout)
            if not success:
                self.assertRegex(completed.stdout, r"unverifiable|unregistered|unreviewable|not explicitly registered")

    def test_system_leaves(self):
        self.configure('target_link_libraries(vendor INTERFACE pthread dl m rt)\n'
                       'seekdb_mark_plugin_private_library(vendor)\n', True)

    def test_unknown_raw_edges(self):
        for edge in ("-lpthread", "pthread_fake", "/tmp/libforeign.a", "$<LINK_ONLY:pthread>"):
            with self.subTest(edge=edge):
                self.configure(f'target_link_libraries(vendor INTERFACE "{edge}")\n'
                               'seekdb_mark_plugin_private_library(vendor)\n', False)

    def test_target_cannot_impersonate_system_library(self):
        self.configure('add_library(pthread INTERFACE)\n'
                       'target_link_libraries(vendor INTERFACE pthread)\n'
                       'seekdb_mark_plugin_private_library(vendor)\n', False)

    def test_late_impersonation(self):
        self.configure('target_link_libraries(vendor INTERFACE pthread)\n'
                       'seekdb_mark_plugin_private_library(vendor)\n'
                       'add_library(pthread INTERFACE)\n', False)

    def test_late_raw_edge(self):
        self.configure('seekdb_mark_plugin_private_library(vendor)\n'
                       'target_link_libraries(vendor INTERFACE /tmp/libforeign.a)\n', False)

    def test_system_search_path_override(self):
        self.configure('target_link_libraries(vendor INTERFACE pthread)\n'
                       'seekdb_mark_plugin_private_library(vendor)\n'
                       'target_link_directories(vendor INTERFACE /tmp)\n', False)


if __name__ == "__main__":
    unittest.main()
