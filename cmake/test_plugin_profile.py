#!/usr/bin/env python3
"""Native profile declarations and source dependency policy (no compilation)."""
import pathlib
import tempfile
import unittest

from plugin_profile import profile
from plugin_boundary_check import check_plugin_includes, check_core_includes


class ProfileTest(unittest.TestCase):
    def setUp(self):
        self.stage = tempfile.TemporaryDirectory(prefix="seekdb-native-profile-")
        self.addCleanup(self.stage.cleanup)
        self.root = pathlib.Path(self.stage.name)
        self.plugin = self.root / "plugins/example"
        self.plugin.mkdir(parents=True)
        self.header = self.root / "src/sql/private.h"
        self.header.parent.mkdir(parents=True)
        self.header.write_text("struct Private {};\n")
        self.manifest = self.plugin / "plugin.toml"

    def select(self, text):
        self.manifest.write_text(text)
        return profile(self.manifest, self.root)

    def test_legacy_public(self):
        self.assertEqual(self.select('plugin_id = "example"\n'),
                         {"profile": "public", "headers": [], "exports": []})

    def test_server_declarations(self):
        selected = self.select('api_profile="server-dev"\nserver_headers=["sql/private.h"]\nexports=["probe"]')
        self.assertEqual(selected["headers"], ["sql/private.h"])
        self.assertEqual(selected["exports"], ["probe"])

    def test_invalid_declarations(self):
        cases = ['api_profile="unknown"', 'api_profile=4',
                 'server_headers=["sql/private.h"]', 'exports=["probe"]',
                 'api_profile="server-dev"\nserver_headers="sql/private.h"']
        for header in ('sql/missing.h', 'sql//private.h', 'sql/../sql/private.h', '/sql/private.h'):
            cases.append('api_profile="server-dev"\nserver_headers=["' + header + '"]')
        for exports in ('["*"]', '["probe", "probe"]', '[3]',
                        '["seekdb_plugin_entry_v1"]', '["seekdb_plugin_server_dev_entry_impl"]'):
            cases.append('api_profile="server-dev"\nexports=' + exports)
        for text in cases:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.select(text)

    def test_ambiguous_and_escaping_header(self):
        second = self.root / "src/oblib/sql/private.h"
        second.parent.mkdir(parents=True)
        second.write_text("struct Other {};\n")
        declaration = 'api_profile="server-dev"\nserver_headers=["sql/private.h"]'
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            self.select(declaration)
        second.unlink()
        self.header.unlink()
        self.header.symlink_to(self.manifest)
        with self.assertRaisesRegex(ValueError, "outside core"):
            self.select(declaration)

    def test_only_declared_private_include_is_allowed(self):
        self.select('api_profile="server-dev"\nserver_headers=["sql/private.h"]')
        source = self.plugin / "entry.cpp"
        source.write_text('#include "sql/private.h"\n')
        errors = []
        check_plugin_includes(self.root, errors)
        self.assertEqual(errors, [])
        source.write_text('#include "sql/other.h"\n#include "query/private.h"\n')
        check_plugin_includes(self.root, errors)
        self.assertEqual(len(errors), 2)
        self.select('api_profile="public"')
        source.write_text('#include "sql/private.h"\n')
        errors = []
        check_plugin_includes(self.root, errors)
        self.assertEqual(len(errors), 1)

    def test_core_cannot_include_plugin(self):
        self.header.write_text('#include "plugins/example/entry.h"\n')
        errors = []
        check_core_includes(self.root, errors)
        self.assertEqual(len(errors), 1)


if __name__ == "__main__":
    unittest.main()
