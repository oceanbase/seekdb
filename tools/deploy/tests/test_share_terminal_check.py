"""Regression tests for the share terminal module boundary check."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "module_check" / "share_terminal_check.py"
REPO_ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("seekdb_share_terminal_check", SCRIPT)
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class ShareTerminalCheckTest(unittest.TestCase):
    def build_tree(self, *sources):
        root = Path(self.temporary.name)
        share = root / "src" / "share"
        share.mkdir(parents=True, exist_ok=True)
        for name, content in sources:
            path = share / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        return root

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)

    def test_repository_share_module_stays_terminal(self):
        violations = []
        for path in checker.iter_sources(str(REPO_ROOT)):
            violations.extend(
                (path, lineno, include)
                for lineno, include, _ in checker.check_file(path, str(REPO_ROOT))
            )
        self.assertEqual([], violations)

    def test_upper_module_include_is_reported(self):
        root = self.build_tree(
            ("ob_ddl_common.cpp", '#include "sql/ob_sql_utils.h"\n'),
            ("ob_other.cpp", '#include "share/ob_share_util.h"\n'),
        )
        violations = []
        for path in checker.iter_sources(str(root)):
            violations.extend(checker.check_file(path, str(root)))
        self.assertEqual(1, len(violations))
        self.assertEqual("sql", violations[0][2])
        self.assertEqual(1, violations[0][0])

    def test_lower_module_includes_are_allowed(self):
        root = self.build_tree(
            ("ob_ddl_common.cpp",
             '#include "lib/string/ob_string.h"\n#include "common/object/ob_object.h"\n'),
            ("sub/ob_more.cpp", '#include "share/ob_share_util.h"\n'),
        )
        violations = []
        for path in checker.iter_sources(str(root)):
            violations.extend(checker.check_file(path, str(root)))
        self.assertEqual([], violations)

    def test_every_upper_module_is_guarded(self):
        for module in checker.UPPER_MODULES:
            root = self.build_tree(("ob_probe.cpp", '#include "%s/ob_probe.h"\n' % module))
            violations = []
            for path in checker.iter_sources(str(root)):
                violations.extend(checker.check_file(path, str(root)))
            self.assertEqual(1, len(violations), module)
            self.assertEqual(module, violations[0][2])


if __name__ == "__main__":
    unittest.main()
