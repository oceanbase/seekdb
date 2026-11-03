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
import re
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
        self.assertEqual(validate_declaration_inventory(self.descriptors, self.sql), (82, 112))

    def test_no_kernel_gis_expression_types(self):
        source = pathlib.Path(__file__).resolve().parents[3]
        legacy = r'\bT_FUN_SYS_(?:ST_\w+|PRIV_ST_\w+|SPATIAL_\w+|POINT|LINESTRING|MULTIPOINT|MULTILINESTRING|POLYGON|MULTIPOLYGON|GEOMCOLLECTION)\b'
        for header in ('src/query/api/query/parser/ob_item_type.h',
                       'src/objit/include/objit/common/ob_item_type.h'):
            self.assertIsNone(re.search(legacy, (source / header).read_text()), header)
        exprs = source / 'src/sql/engine/expr'
        for pattern in ('ob_expr_st_*', 'ob_expr_priv_st_*', 'ob_expr_point.*',
                        'ob_expr_spatial_collection.*', 'ob_expr_spatial_cellid.*',
                        'ob_expr_spatial_mbr.*'):
            self.assertEqual(list(exprs.glob(pattern)), [], pattern)
        factory = (exprs / 'ob_expr_operator_factory.cpp').read_text()
        self.assertNotIn('REG_PLUGIN_GIS_OP', factory)

    def test_index_columns_use_generic_typed_slots(self):
        source = pathlib.Path(__file__).resolve().parents[3]
        builder = (source / 'src/sql/resolver/ddl/ob_index_builder_util.cpp').read_text()
        self.assertIn('__seekdb_index_value(`%s`, CAST(NULL AS UNSIGNED))', builder)
        self.assertIn('__seekdb_index_value(`%s`, CAST(NULL AS BINARY(32)))', builder)
        self.assertNotIn('"SPATIAL_CELLID(', builder)
        self.assertNotIn('"SPATIAL_MBR(', builder)

    def test_nullable_descriptor_is_in_inventory(self):
        self.assertIn('GIS_SQL_FUNCTION_NULLABLE("org.seekdb.gis.function.st_asmvtgeom"', self.descriptors)
        self.assertIn('GIS_SQL_FUNCTION_NULLABLE("org.seekdb.gis.function.st_geohash"', self.descriptors)
        with self.assertRaisesRegex(ValueError, 'unrecognized GIS descriptor'):
            validate_declaration_inventory(self.descriptors.replace(
                '"org.seekdb.gis.function.st_asmvtgeom", 2, 5',
                '"org.seekdb.gis.function.st_asmvtgeom", invalid, 5'), self.sql)

    def test_geohash_signed_precision(self):
        self.assertIn('"org.seekdb.gis.function.st_geohash", 1, 2, GIS_BYTES, gi, 0)', self.descriptors)
        with self.assertRaisesRegex(ValueError, 'element types'):
            validate_declaration_inventory(self.descriptors, self.sql.replace(
                '`_st_geohash`(arg1 GEOMETRY, arg2 BIGINT)', '`_st_geohash`(arg1 GEOMETRY, arg2 DOUBLE)'))

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
