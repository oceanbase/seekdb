"""Small real-GPU bridge check, NOT the SQL end-to-end build benchmark."""
import ctypes
import argparse
import json
import pathlib
import struct
import time
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=pathlib.Path, default=pathlib.Path('/workspace/amd-build'))
parser.add_argument('--library', type=pathlib.Path)
args = parser.parse_args()
root = args.output_dir.resolve()
root.mkdir(parents=True, exist_ok=True)
library = ctypes.CDLL(str(args.library.resolve() if args.library else root / 'libseekdb_hipvs_build.so'))
build = library.seekdb_hipvs_build_hnsw_v1
build.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int64, ctypes.c_int32,
                 ctypes.c_int32, ctypes.c_int32, ctypes.c_int32, ctypes.c_char_p,
                 ctypes.c_char_p, ctypes.c_size_t]
build.restype = ctypes.c_int
reports = []
for rows, dim in [(128, 8), (1000, 32)]:
    vectors = np.random.default_rng(20261031).normal(size=(rows, dim)).astype('<f4')
    vectors.tofile(root / f'probe_{rows}.f32')
    path = root / f'probe_{rows}.hnsw'
    error = ctypes.create_string_buffer(2048)
    started = time.perf_counter()
    status = build(vectors.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), rows, dim,
                   16, 200, 8, str(path).encode(), error, len(error))
    elapsed = time.perf_counter() - started
    if status:
        raise RuntimeError(error.value.decode())
    data = path.read_bytes()
    offset0, capacity, count, stride, label_offset, vector_offset = struct.unpack_from('<6Q', data)
    assert offset0 == 0 and count == rows and capacity >= rows
    assert vector_offset == 132 and label_offset == 132 + dim * 4
    assert stride == label_offset + 8
    labels = []
    for i in range(rows):
        node = 96 + i * stride
        label = struct.unpack_from('<Q', data, node + label_offset)[0]
        assert 0 <= label < rows
        assert data[node + vector_offset:node + label_offset] == vectors[label].tobytes()
        labels.append(label)
    assert len(set(labels)) == rows
    reports.append(dict(rows=rows, dim=dim, bridge_seconds=elapsed, bytes=len(data),
                        all_vectors_preserved=True, all_source_labels_unique=True,
                        scope='GPU bridge export only; SQL integration not yet validated'))
(root / 'gpu-probe.json').write_text(json.dumps(reports, indent=2))
print(json.dumps(reports, indent=2))
