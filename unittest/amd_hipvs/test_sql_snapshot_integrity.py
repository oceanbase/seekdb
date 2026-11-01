"""Stored graph completeness must not depend on approximate graph reachability."""
import struct
import unittest
from unittest.mock import MagicMock

import numpy as np

from sql_snapshot_integrity import chunk_order, validate_native_graph, verify_sql_snapshot


def fixture():
    m = 5
    vectors = np.array([[1, 2], [3, 4], [5, 6]], dtype='<f4')
    ids = [2**53 + 101, -77, 42]
    mapping = {10: ids[0], 20: ids[1], 30: ids[2]}
    vector_offset, label_offset, stride = 44, 52, 60
    data = bytearray(struct.pack('<6Q2I3QdQ', 0, 3, 3, stride, label_offset,
                                 vector_offset, 0, 0, m, 2*m, m, .5, 200))
    # All nodes are present but deliberately disconnected: this must pass the
    # storage proof, independently of a separate ANN quality threshold.
    for row, vid in enumerate(mapping):
        record = bytearray(stride)
        record[vector_offset:label_offset] = vectors[row].tobytes()
        struct.pack_into('<q', record, label_offset, vid)
        data.extend(record)
    data.extend(b'\0' * 12)
    return data, vectors, ids, mapping, m


class NativeIntegrityTests(unittest.TestCase):
    def test_disconnected_complete_graph_passes_storage_proof(self):
        proof = validate_native_graph(*fixture())
        self.assertEqual(proof['stored_count'], 3)
        self.assertTrue(proof['all_ids_present'] and proof['vectors_exact'])
        self.assertEqual(len(proof['snapshot_sha256']), 64)

    def test_wrong_vector_or_label_rejected(self):
        for offset in (96 + 44, 96 + 52):
            data, vectors, ids, mapping, m = fixture()
            data[offset] ^= 1
            with self.assertRaises(ValueError):
                validate_native_graph(data, vectors, ids, mapping, m)

    def test_duplicate_native_vid_rejected(self):
        data, vectors, ids, mapping, m = fixture()
        struct.pack_into('<q', data, 96 + 60 + 52, 10)
        with self.assertRaisesRegex(ValueError, 'repeated native VID'):
            validate_native_graph(data, vectors, ids, mapping, m)

    def test_omitted_extra_or_aliased_mapping_rejected(self):
        for replacement in ({10: 2**53 + 101, 20: -77},
                            {10: 2**53 + 101, 20: -77, 30: -77},
                            {10: 2**53 + 101, 20: -77, 30: 43}):
            data, vectors, ids, mapping, m = fixture()
            with self.assertRaises(ValueError):
                validate_native_graph(data, vectors, ids, replacement, m)

    def test_neighbor_and_degree_bounds_rejected(self):
        for degree, neighbor in ((11, 0), (1, 3), (0x10000, 0)):
            data, vectors, ids, mapping, m = fixture()
            struct.pack_into('<II', data, 96, degree, neighbor)
            with self.assertRaises(ValueError):
                validate_native_graph(data, vectors, ids, mapping, m)

    def test_truncation_trailing_bytes_and_wrong_header_rejected(self):
        original, vectors, ids, mapping, m = fixture()
        wrong = bytearray(original)
        struct.pack_into('<Q', wrong, 16, 2)
        for data in (original[:90], original[:-1], original+b'X', wrong):
            with self.assertRaises(ValueError):
                validate_native_graph(data, vectors, ids, mapping, m)

    def test_invalid_upper_link_rejected(self):
        data, vectors, ids, mapping, m = fixture()
        struct.pack_into('<I', data, 48, 1)
        data = data[:-12] + bytearray(struct.pack('<I6I', 24, 1, 3, 0, 0, 0, 0)) + bytearray(8)
        with self.assertRaisesRegex(ValueError, 'upper-layer neighbor'):
            validate_native_graph(data, vectors, ids, mapping, m)


class ChunkTests(unittest.TestCase):
    def test_contiguous_single_snapshot(self):
        keys, total = chunk_order([('456_999_hnsw_data_part00000', 8),
                                   ('456_999_hnsw_data_part00001', 9)], 456, 20)
        self.assertEqual((len(keys), total), (2, 17))

    def test_mixed_gapped_duplicate_or_wrong_tablet_rejected(self):
        for second in ('456_999_hnsw_data_part00000', '456_999_hnsw_data_part00002',
                       '456_1000_hnsw_data_part00001', '457_999_hnsw_data_part00001'):
            with self.assertRaises(ValueError):
                chunk_order([('456_999_hnsw_data_part00000', 8), (second, 8)], 456, 20)

    def test_empty_oversize_or_missing_payload_rejected(self):
        for items in ([], [('456_999_hnsw_data_part00000', None)],
                      [('456_999_hnsw_data_part00000', 0)],
                      [('456_999_hnsw_data_part00000', 2097153)]):
            with self.assertRaises(ValueError):
                chunk_order(items, 456, 10_000_000)


class SQLProofTests(unittest.TestCase):
    def connection(self, wrong_aux=False):
        data, vectors, ids, mapping, m = fixture()
        snapshot = '__idx_123_ix_index_snapshot_data_table'
        rowkeys = '__idx_123_rowkey_vid_table'
        columns = [('__key_17_100',), ('__data_17_100',), ('__vid_100',)]
        key = '456_999_hnsw_data_part00000'
        cursor = MagicMock()
        cursor.fetchone.return_value = ('test',)
        cursor.fetchall.side_effect = [
            [(123,)], [(321, 'unrelated' if wrong_aux else snapshot, 456), (322, rowkeys, 457)],
            [('id',), ('__vid_100',)], [(pk, vid) for vid, pk in mapping.items()],
            columns, columns, [(key, len(data))], [(key, bytes(data))]]
        connection = MagicMock()
        connection.cursor.return_value.__enter__.return_value = cursor
        return connection, cursor, vectors, ids, m

    def test_metadata_mapping_and_blob_roundtrip(self):
        connection, cursor, vectors, ids, m = self.connection()
        proof = verify_sql_snapshot(connection, 'docs', 'ix', vectors, ids, m)
        self.assertEqual(proof['stored_count'], 3)
        self.assertEqual(proof['source_table'], 'docs')
        self.assertEqual(proof['snapshot_tablet_id'], 456)
        self.assertTrue(all(call.args[0].startswith(('SELECT ', 'SHOW '))
                            for call in cursor.execute.call_args_list))
        connection.commit.assert_not_called()

    def test_wrong_auxiliary_metadata_rejected_before_blob_read(self):
        connection, cursor, vectors, ids, m = self.connection(wrong_aux=True)
        with self.assertRaises(ValueError):
            verify_sql_snapshot(connection, 'docs', 'ix', vectors, ids, m)
        self.assertEqual(cursor.execute.call_count, 3)

    def test_unsafe_table_name_rejected_without_sql(self):
        connection, cursor, vectors, ids, m = self.connection()
        with self.assertRaises(ValueError):
            verify_sql_snapshot(connection, 'docs` WHERE 1=1', 'ix', vectors, ids, m)
        cursor.execute.assert_not_called()


if __name__ == '__main__':
    unittest.main()
