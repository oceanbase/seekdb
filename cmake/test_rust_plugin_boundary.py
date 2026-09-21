#!/usr/bin/env python3
"""Policy tests: local dependency declarations, not arbitrary code confinement."""
import pathlib
import tempfile
import unittest

from rust_plugin_boundary_check import validate


def metadata(packages, dependencies=None):
    dependencies = dependencies or {}
    return {
        "packages": packages,
        "resolve": {
            "root": "plugin",
            "nodes": [{"id": p["id"], "deps": [{"pkg": d} for d in dependencies.get(p["id"], [])]}
                      for p in packages],
        },
    }


def package(name, path, source=None, crate_type="rlib"):
    return {"id": name, "manifest_path": str(path), "source": source,
            "targets": [{"crate_types": [crate_type]}]}


class CargoBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="seekdb-rust-boundary-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.plugin = self.root / "plugins" / "example"
        self.sdk = self.root / "rust" / "extension-sdk"
        self.plugin.mkdir(parents=True)
        self.sdk.mkdir(parents=True)
        self.base = package("plugin", self.plugin / "Cargo.toml", crate_type="cdylib")

    def check(self, packages, deps=None):
        validate(metadata([self.base] + packages, deps), self.plugin, self.sdk)

    def test_sdk_plugin_owned_and_registry_dependencies_allowed(self):
        self.check([
            package("sdk", self.sdk / "Cargo.toml"),
            package("owned", self.plugin / "helpers" / "Cargo.toml"),
            package("registry", self.root / "registry" / "Cargo.toml", source="registry+test"),
        ], {"plugin": ["sdk", "owned"], "owned": ["registry"]})

    def test_transitive_host_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.check([package("sdk", self.sdk / "Cargo.toml"),
                        package("host", self.root / "rust" / "plugin-runtime" / "Cargo.toml")],
                       {"plugin": ["sdk"], "sdk": ["host"]})

    def test_similar_prefix_is_not_a_child(self):
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.check([package("escape", self.plugin.with_name("example-private") / "Cargo.toml")],
                       {"plugin": ["escape"]})

    def test_symlink_escape_rejected(self):
        outside = self.root / "private"
        outside.mkdir()
        link = self.plugin / "linked"
        link.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.check([package("escape", link / "Cargo.toml")], {"plugin": ["escape"]})

    def test_unreachable_workspace_package_is_not_linked(self):
        self.check([package("unused", self.root / "private" / "Cargo.toml")])

    def test_staticlib_is_not_a_plugin(self):
        self.base["targets"] = [{"crate_types": ["staticlib"]}]
        with self.assertRaisesRegex(ValueError, "cdylib"):
            self.check([])


if __name__ == "__main__":
    unittest.main()
