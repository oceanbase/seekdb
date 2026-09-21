#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Configure-time generated-schema inventory regression tests, no DB needed."""
import tempfile
import subprocess
import sys
import unittest
from pathlib import Path

from emit_bazel_source_inventory import InventoryError, emit, validate_generated_schema_sources


class GeneratedSchemaInventoryTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="seekdb-schema-inventory-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.directory = self.root / "share" / "inner_table"
        self.directory.mkdir(parents=True)
        self.shard = "ob_inner_table_schema.1151_1200.cpp"
        self.owned = {"src/share/inner_table/" + self.shard: "share:schemas"}

    def test_matching_shard_set(self):
        (self.directory / self.shard).touch()
        validate_generated_schema_sources(self.owned, self.root)

    def test_new_uncompiled_range_is_rejected(self):
        (self.directory / self.shard).touch()
        extra = "ob_inner_table_schema.51151_51200.cpp"
        (self.directory / extra).touch()
        with self.assertRaisesRegex(InventoryError, "Uncompiled outputs: " + extra):
            validate_generated_schema_sources(self.owned, self.root)

    def test_deleted_shard_is_rejected_even_with_existing_headers(self):
        (self.directory / "ob_inner_table_schema.h").touch()
        with self.assertRaisesRegex(InventoryError, "missing outputs: " + self.shard):
            validate_generated_schema_sources(self.owned, self.root)

    def test_missing_output_directory_is_rejected(self):
        with self.assertRaisesRegex(InventoryError, "directory is missing"):
            validate_generated_schema_sources(self.owned, self.root / "missing")

    def test_other_generated_files_are_not_schema_shards(self):
        (self.directory / self.shard).touch()
        (self.directory / "ob_inner_table_schema.h").touch()
        (self.directory / "table_id_to_name").touch()
        (self.directory / "unrelated.cpp").touch()
        validate_generated_schema_sources(self.owned, self.root)

    def test_rejected_configure_preserves_previous_inventory(self):
        repo = Path(__file__).resolve().parents[2]
        output = self.root / "inventory.cmake"
        output.write_text("previous valid inventory\n", encoding="utf-8")
        with self.assertRaises(InventoryError):
            emit(repo, output, self.root)
        self.assertEqual(output.read_text(encoding="utf-8"), "previous valid inventory\n")

    def test_actual_definition_generates_exact_declared_shards(self):
        repo = Path(__file__).resolve().parents[2]
        generator_dir = repo / "src" / "share" / "inner_table"
        result = subprocess.run(
            [sys.executable, str(generator_dir / "generate_inner_table_schema.py"),
             "--def-file", str(generator_dir / "ob_inner_table_schema_def.py"),
             "--share-output-dir", str(self.directory),
             "--observer-output-dir", str(self.root / "observer" / "virtual_table"),
             "--quiet"],
            capture_output=True, text=True, timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        emit(repo, self.root / "fresh-inventory.cmake", self.root)


if __name__ == "__main__":
    unittest.main()
