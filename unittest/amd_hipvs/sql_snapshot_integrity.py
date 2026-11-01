"""Read-only integrity proof for fresh, unpartitioned FP32 HNSW SQL indexes.

Completeness is checked in the stored graph, independently of ANN reachability.
This deliberately understands the pinned VSAG old native format and fails closed
for another format, a rebuilt/multi-version snapshot, or ambiguous metadata.
"""
import hashlib
import math
import re
import struct

import numpy as np


NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_]{0,255}\Z')
KEY = re.compile(r'([0-9]+)_([0-9]+)_hnsw_data_part([0-9]{5,})\Z')
CHUNK_LIMIT = 2 * 1024 * 1024


def require(value, message):
    if not value:
        raise ValueError(message)


def quoted(value):
    require(isinstance(value, str) and NAME.fullmatch(value), 'Unsafe SQL metadata identifier')
    return '`' + value + '`'


def text(value):
    if isinstance(value, bytes):
        return value.decode('ascii')
    require(isinstance(value, str), 'Expected textual metadata')
    return value


def chunk_order(items, expected_tablet, maximum_bytes):
    """Return exact ordered keys; reject gaps, duplicated or mixed snapshots."""
    require(items, 'Stored snapshot is empty')
    prefix = None
    total = 0
    keys = []
    for position, (raw_key, size) in enumerate(items):
        key = text(raw_key)
        match = KEY.fullmatch(key)
        require(match is not None, 'Unexpected snapshot chunk key')
        current = (int(match[1]), int(match[2]))
        require(current[0] == expected_tablet and current[1] > 0,
                'Snapshot key does not belong to the requested tablet')
        if prefix is None:
            prefix = current
        require(current == prefix, 'Multiple snapshot versions present')
        require(int(match[3]) == position, 'Snapshot chunk sequence has gaps or duplicates')
        require(isinstance(size, int) and not isinstance(size, bool) and 0 < size <= CHUNK_LIMIT,
                'Invalid snapshot chunk size')
        total += size
        require(total <= maximum_bytes, 'Snapshot exceeds bounded native graph size')
        keys.append(key)
    return keys, total


def validate_native_graph(data, vectors, ids, mapping, m):
    """Validate every node/vector against the bijective SQL VID-to-PK map."""
    vectors = np.asarray(vectors, dtype='<f4', order='C')
    require(vectors.ndim == 2 and len(vectors) == len(ids), 'Invalid expected input shape')
    rows, dim = vectors.shape
    require(0 < rows <= 2147483647 and 0 < dim <= 16384 and 5 <= m <= 64,
            'Unsupported native graph shape or M')
    require(bool(np.isfinite(vectors).all()), 'Nonfinite expected vector')
    expected = {int(pk): pos for pos, pk in enumerate(ids)}
    require(len(expected) == rows, 'Duplicate expected primary keys')
    require(len(mapping) == rows and len(set(mapping.values())) == rows
            and set(mapping.values()) == set(expected), 'SQL VID mapping is not complete and bijective')
    require(len(data) >= 96, 'Truncated native header')
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    u64 = lambda p: struct.unpack_from('<Q', data, p)[0]
    vector_offset = 4 + 8 * m
    label_offset = vector_offset + 4 * dim
    stride = label_offset + 8
    require(u64(0) == 0 and rows <= u64(8) <= 2147483647 and u64(16) == rows
            and u64(24) == stride and u64(32) == label_offset and u64(40) == vector_offset,
            'Native header has wrong count, dimensions or layout')
    require(u32(48) <= 64 and u32(52) < rows and u64(56) == m
            and u64(64) == 2 * m and u64(72) == m and u64(88) > m,
            'Invalid native HNSW parameters')
    multiplier = struct.unpack_from('<d', data, 80)[0]
    require(math.isfinite(multiplier) and multiplier > 0, 'Invalid hierarchy multiplier')
    require(len(data) >= 96 + rows * (stride + 4), 'Truncated native nodes')
    seen = set()
    for node in range(rows):
        offset = 96 + node * stride
        degree = u32(offset)
        require(degree <= 2 * m, 'Invalid level-zero degree or deleted node')
        for neighbor in range(degree):
            require(u32(offset + 4 + 4 * neighbor) < rows, 'Invalid level-zero neighbor')
        vid = struct.unpack_from('<q', data, offset + label_offset)[0]
        require(vid in mapping and vid not in seen, 'Unknown or repeated native VID')
        seen.add(vid)
        expected_vector = vectors[expected[mapping[vid]]].tobytes()
        require(data[offset + vector_offset:offset + label_offset] == expected_vector,
                'Stored vector bytes do not match the SQL primary key')
    require(seen == set(mapping), 'Native snapshot omitted SQL VIDs')
    upper_stride = 4 + 4 * m
    position = 96 + rows * stride
    for node in range(rows):
        require(len(data) - position >= 4, 'Missing upper-layer length')
        length = u32(position)
        position += 4
        require(length <= len(data) - position and length % upper_stride == 0
                and length // upper_stride <= u32(48), 'Invalid upper-layer extent')
        for level in range(0, length, upper_stride):
            degree = u32(position + level)
            require(degree <= m, 'Invalid upper-layer degree')
            for neighbor in range(degree):
                require(u32(position + level + 4 + 4 * neighbor) < rows,
                        'Invalid upper-layer neighbor')
        position += length
    require(position == len(data), 'Unexpected trailing native graph bytes')
    mapping_bytes = b''.join(struct.pack('<qq', vid, pk) for vid, pk in sorted(mapping.items()))
    return {'stored_count': rows, 'mapped_count': len(mapping), 'vectors_exact': True,
            'all_ids_present': True, 'snapshot_sha256': hashlib.sha256(data).hexdigest(),
            'mapping_sha256': hashlib.sha256(mapping_bytes).hexdigest(),
            'snapshot_bytes': len(data), 'dim': dim, 'm': m}


def verify_sql_snapshot(connection, table, index, vectors, ids, m):
    """Only inspect auxiliary tables attached to the named current-database table."""
    require(len(table) <= 63 and len(index) <= 63, 'Unexpected public table/index name length')
    quoted(table)
    quoted(index)
    vectors = np.asarray(vectors, dtype='<f4', order='C')
    require(vectors.ndim == 2 and len(vectors) == len(ids), 'Invalid expected shape')
    rows, dim = vectors.shape
    require(0 < rows <= 2147483647 and 0 < dim <= 16384 and 5 <= m <= 64,
            'Unsupported expected graph shape or M')
    expected_ids = {int(pk) for pk in ids}
    require(len(expected_ids) == rows, 'Duplicate expected primary keys')
    with connection.cursor() as cursor:
        cursor.execute('SELECT DATABASE()')
        database = text(cursor.fetchone()[0])
        quoted(database)
        cursor.execute('SELECT t.table_id FROM oceanbase.__all_table t '
                       'JOIN oceanbase.__all_database d ON d.database_id=t.database_id '
                       'WHERE d.database_name=%s AND t.table_name=%s', (database, table))
        owners = cursor.fetchall()
        require(len(owners) == 1 and int(owners[0][0]) > 0, 'Ambiguous owner table metadata')
        owner_id = int(owners[0][0])
        snapshot = f'__idx_{owner_id}_{index}_index_snapshot_data_table'
        rowkeys = f'__idx_{owner_id}_rowkey_vid_table'
        cursor.execute('SELECT table_id,table_name,tablet_id FROM oceanbase.__all_table '
                       'WHERE data_table_id=%s AND table_name IN (%s,%s)',
                       (owner_id, snapshot, rowkeys))
        auxiliary_rows = cursor.fetchall()
        require(len(auxiliary_rows) == 2, 'Ambiguous auxiliary table metadata')
        auxiliary = {text(name): (int(tid), int(tablet)) for tid, name, tablet in auxiliary_rows}
        require(set(auxiliary) == {snapshot, rowkeys}
                and all(tid > 0 and tablet > 0 for tid, tablet in auxiliary.values()),
                'Required unpartitioned auxiliary tables not found')

        def one_column(name, prefix):
            cursor.execute(f'SHOW COLUMNS FROM {quoted(database)}.{quoted(name)}')
            columns = [text(row[0]) for row in cursor.fetchall()]
            matches = [col for col in columns if col.startswith(prefix)]
            require(len(matches) == 1, 'Ambiguous auxiliary column metadata')
            quoted(matches[0])
            return matches[0]

        vid_col = one_column(rowkeys, '__vid_')
        cursor.execute(f'SELECT id,{quoted(vid_col)} FROM {quoted(database)}.{quoted(rowkeys)}')
        mapping = {}
        mapped_ids = set()
        for pk, vid in cursor.fetchall():
            require(pk is not None and vid is not None, 'NULL in SQL VID mapping')
            pk, vid = int(pk), int(vid)
            require(-(1 << 63) <= pk < (1 << 63) and 0 <= vid < (1 << 63), 'Out-of-range SQL ID')
            require(pk in expected_ids and pk not in mapped_ids and vid not in mapping,
                    'Extra or duplicate SQL primary key/VID')
            mapping[vid] = pk
            mapped_ids.add(pk)
        require(mapped_ids == expected_ids, 'SQL mapping omitted expected primary keys')
        key_col = one_column(snapshot, '__key_')
        data_col = one_column(snapshot, '__data_')
        target = f'{quoted(database)}.{quoted(snapshot)}'
        cursor.execute(f'SELECT {quoted(key_col)},OCTET_LENGTH({quoted(data_col)}) '
                       f'FROM {target} WHERE {quoted(key_col)} IS NOT NULL ORDER BY {quoted(key_col)}')
        sizes = cursor.fetchall()
        maximum = 96 + rows * (4 + 8 * m + 4 * dim + 8 + 4 + 64 * (4 + 4 * m))
        keys, total = chunk_order(sizes, auxiliary[snapshot][1], maximum)
        cursor.execute(f'SELECT {quoted(key_col)},CAST({quoted(data_col)} AS BINARY) '
                       f'FROM {target} WHERE {quoted(key_col)} IS NOT NULL ORDER BY {quoted(key_col)}')
        chunks = cursor.fetchall()
        require(len(chunks) == len(keys), 'Snapshot chunk count changed during validation')
        payloads = []
        for (key, payload), expected_key, (_, expected_size) in zip(chunks, keys, sizes):
            require(text(key) == expected_key and isinstance(payload, (bytes, bytearray))
                    and len(payload) == expected_size, 'Snapshot chunk bytes changed or were decoded as text')
            payloads.append(bytes(payload))
        data = b''.join(payloads)
        require(len(data) == total, 'Snapshot size mismatch')
    proof = validate_native_graph(data, vectors, ids, mapping, m)
    proof.update(method='sql_snapshot_native_hnsw', source_table=table, index_name=index,
                 data_table_id=owner_id, snapshot_table=snapshot, mapping_table=rowkeys,
                 snapshot_tablet_id=auxiliary[snapshot][1], chunk_keys=keys)
    return proof
