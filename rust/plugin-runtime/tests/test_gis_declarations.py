#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Offline checks of GIS SQL declaration coverage; not installation tests."""
import pathlib
import unittest

from gis_sql import validate_declaration_inventory


class DeclarationInventory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = pathlib.Path(__file__).resolve().parents[3]
        module = (source / 'plugins/gis/seekdb_gis_plugin.c').read_text()
        cls.descriptors = module.split('static const seekdb_plugin_function_descriptor_v2_t gis_functions[] = {', 1)[1].split('\n};', 1)[0]
        cls.sql = (source / 'plugins/gis/sql/gis--1.0.sql').read_text()

    def test_complete(self):
        self.assertEqual(validate_declaration_inventory(self.descriptors, self.sql), (82, 106))

    def test_missing_arity(self):
        start = self.sql.index('CREATE FUNCTION `st_makepoint`(arg1 DOUBLE, arg2 DOUBLE, arg3 DOUBLE)')
        end = self.sql.index(';', start) + 1
        with self.assertRaisesRegex(ValueError, 'missing or ambiguous'):
            validate_declaration_inventory(self.descriptors, self.sql[:start] + self.sql[end:])

    def test_duplicate_arity(self):
        start = self.sql.index('CREATE FUNCTION `st_makepoint`(')
        end = self.sql.index(';', start) + 1
        with self.assertRaisesRegex(ValueError, 'missing or ambiguous'):
            validate_declaration_inventory(self.descriptors, self.sql + self.sql[start:end])

    def test_wrong_element_type(self):
        with self.assertRaisesRegex(ValueError, 'element types'):
            validate_declaration_inventory(self.descriptors, self.sql.replace(
                '`st_linestring`(VARIADIC arg1 GEOMETRY[])', '`st_linestring`(VARIADIC arg1 DOUBLE[])'))

    def test_wrong_implementation(self):
        with self.assertRaisesRegex(ValueError, 'wrong implementation'):
            validate_declaration_inventory(self.descriptors, self.sql.replace(
                "'org.seekdb.gis.function.st_x'", "'org.seekdb.gis.function.st_y'"))

    def test_missing_name(self):
        with self.assertRaisesRegex(ValueError, 'SQL names differ'):
            validate_declaration_inventory(self.descriptors, self.sql.replace('`point`', '`other_point`'))

    def test_unrecognized_declaration(self):
        with self.assertRaisesRegex(ValueError, 'unrecognized GIS SQL'):
            validate_declaration_inventory(self.descriptors, self.sql.replace('LANGUAGE C;', 'LANGUAGE python;', 1))

    def test_variadic_must_be_last(self):
        with self.assertRaisesRegex(ValueError, 'invalid GIS argument'):
            validate_declaration_inventory(self.descriptors, self.sql.replace(
                '`st_point`(arg1 DOUBLE, arg2 DOUBLE)', '`st_point`(VARIADIC arg1 DOUBLE[], arg2 DOUBLE)'))


if __name__ == '__main__':
    unittest.main()
