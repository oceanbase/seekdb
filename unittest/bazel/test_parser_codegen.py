#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
"""Compile and exercise the type-name generator without building the engine."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ParserTypeNamesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        compiler = compiler or shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            raise unittest.SkipTest("a C++ compiler is required")
        cls.temporary = tempfile.TemporaryDirectory(prefix="seekdb-type-names-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        header = directory / "sql/parser/ob_item_type.h"
        header.parent.mkdir(parents=True)
        header.write_text(
            "enum ObItemType {\n"
            "  T_INVALID = -1, // the real enum uses this negative sentinel\n"
            "  T_NULL = 0,\n"
            "  T_POSITIVE = 7,\n"
            "  T_IMPLICIT,\n"
            "  T_TABBED\t=\t9,\n"
            "  T_ALIAS = T_POSITIVE,\n"
            "};\n",
            encoding="utf-8",
        )
        generated = directory / "type_name.cpp"
        subprocess.run(
            [sys.executable, str(ROOT / "bazel/parser_codegen.py"),
             "type_names", str(header), str(generated)],
            check=True, capture_output=True, text=True,
        )
        main = directory / "main.cpp"
        main.write_text(
            '#include <cstdio>\n#include <cstdlib>\n'
            'const char* get_type_name(int);\n'
            'int main(int argc, char** argv) {\n'
            '  if (argc != 2) return 1;\n'
            '  std::puts(get_type_name(std::atoi(argv[1])));\n'
            '}\n',
            encoding="utf-8",
        )
        cls.binary = directory / ("type_names.exe" if os.name == "nt" else "type_names")
        cls.environment = os.environ.copy()
        cls.environment["PATH"] = str(Path(compiler).parent) + os.pathsep + cls.environment.get("PATH", "")
        subprocess.run(
            [compiler, "-std=c++11", "-I", str(directory), str(generated),
             str(main), "-o", str(cls.binary)],
            check=True, capture_output=True, text=True, env=cls.environment,
        )

    def type_name(self, value):
        return subprocess.check_output(
            [str(self.binary), str(value)], text=True, env=self.environment,
        ).strip()

    def test_negative_sentinel_keeps_its_name(self):
        self.assertEqual(self.type_name(-1), "T_INVALID")

    def test_tab_separated_initializer_keeps_its_name(self):
        self.assertEqual(self.type_name(9), "T_TABBED")

    def test_nonnegative_implicit_and_aliased_values_still_work(self):
        for value, name in ((0, "T_NULL"), (7, "T_POSITIVE"), (8, "T_IMPLICIT")):
            with self.subTest(value=value):
                self.assertEqual(self.type_name(value), name)

    def test_unknown_value_keeps_the_fallback(self):
        self.assertEqual(self.type_name(-2), "Unknown")


if __name__ == "__main__":
    unittest.main()
