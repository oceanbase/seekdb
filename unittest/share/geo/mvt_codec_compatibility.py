#!/usr/bin/env python3
"""Independent protobuf-c <-> prost interoperability and construction benchmark.

Run from any directory after the release Rust archive/CXX bridge and the
rust/mvt/examples/roundtrip.rs example have been built. No downloads are made.
Legacy sources are extracted from BASELINE into --artifacts, never into product
sources. All parser comparisons inspect decoded property values and presence;
Wire field order is immaterial; controlled construction cases also compare
dictionary indices and insertion order.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

BASELINE = "4b1aa306d"
ROOT = Path(__file__).resolve().parents[3]


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, capture_output=True,
                          text=True, **kwargs)


def varint(n):
    out = bytearray()
    while n > 127:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return bytes(out)


def field(n, value, wire=2):
    return varint(n * 8 + wire) + (varint(len(value)) + value if wire == 2 else varint(value))


def fields(data):
    pos = 0

    def integer():
        nonlocal pos
        n = 0
        for shift in range(0, 70, 7):
            if pos >= len(data):
                raise AssertionError("truncated varint")
            b = data[pos]
            pos += 1
            n |= (b & 127) << shift
            if not b & 128:
                return n
        raise AssertionError("oversize varint")

    result = {}
    while pos < len(data):
        tag = integer()
        n, wire = tag >> 3, tag & 7
        assert n > 0
        if wire == 0:
            v = integer()
        elif wire in (1, 2, 5):
            size = integer() if wire == 2 else (8 if wire == 1 else 4)
            assert pos + size <= len(data)
            v = data[pos:pos + size]
            pos += size
        else:
            raise AssertionError(f"unsupported wire type {wire}")
        result.setdefault(n, []).append((wire, v))
    return result


def scalar(message, number, default=None):
    return message.get(number, [(None, default)])[-1][1]


def repeated_ints(message, number):
    result = []
    for wire, value in message.get(number, []):
        if wire == 0:
            result.append(value)
        else:
            assert wire == 2
            # Packed values can be decoded as field-one varints with a small scanner.
            pos = 0
            while pos < len(value):
                n, shift = 0, 0
                while True:
                    b = value[pos]
                    pos += 1
                    n |= (b & 127) << shift
                    shift += 7
                    if not b & 128:
                        break
                result.append(n)
    return tuple(result)


def semantic(data):
    layers = []
    for _, raw_layer in fields(data).get(3, []):
        layer = fields(raw_layer)
        assert all(n in layer for n in (1, 5, 15)), "required layer field absent"
        keys = [v for _, v in layer.get(3, [])]
        values = []
        for _, value in layer.get(4, []):
            v = fields(value)
            known = [(n, entries[-1]) for n, entries in v.items() if 1 <= n <= 7]
            assert len(known) <= 1, "multiple oneof fields in emitted message"
            values.append(known[0] if known else None)
        features = []
        for _, feature in layer.get(2, []):
            f = fields(feature)
            assert 3 in f, "required feature type absent"
            tags = repeated_ints(f, 2)
            assert len(tags) % 2 == 0
            props = tuple((keys[tags[i]], values[tags[i + 1]]) for i in range(0, len(tags), 2))
            features.append((1 in f, scalar(f, 1), scalar(f, 3), repeated_ints(f, 4), props))
        layers.append((scalar(layer, 15), scalar(layer, 1), scalar(layer, 5), tuple(keys), tuple(features)))
    return tuple(layers)



def dictionary_layout(data):
    layouts = []
    for _, raw_layer in fields(data).get(3, []):
        layer = fields(raw_layer)
        layouts.append((tuple(value for _, value in layer.get(3, [])),
                        tuple(value for _, value in layer.get(4, [])),
                        tuple(repeated_ints(fields(feature), 2) for _, feature in layer.get(2, []))))
    return tuple(layouts)


def compile_harness(args):
    artifacts = args.artifacts
    legacy = artifacts / "legacy"
    legacy.mkdir(parents=True, exist_ok=True)
    for name in ("ob_vector_tile.pb-c.c", "ob_vector_tile.pb-c.h", "ob_vector_tile.proto"):
        data = subprocess.check_output(["git", "show", f"{BASELINE}:src/share/geo/{name}"], cwd=ROOT)
        (legacy / name).write_bytes(data)
    sdk = args.sdk
    source = ROOT / "unittest/share/geo/mvt_codec_harness.cpp"
    obj = artifacts / "legacy.o"
    run([args.cc, "-O2", "-I" + str(sdk / "include"), "-I" + str(legacy), "-c",
         legacy / "ob_vector_tile.pb-c.c", "-o", obj])
    old = artifacts / "legacy_codec"
    run([args.cxx, "-O2", "-std=c++17", "-DLEGACY", "-I" + str(sdk / "include"),
         "-I" + str(legacy), source, obj, sdk / "lib/libprotobuf-c.a", "-o", old])
    new = artifacts / "prost_codec"
    run([args.cxx, "-O2", "-std=c++17", "-I" + str(args.build), source,
         args.build / "rust/mvt.cc", args.archive, "-lpthread", "-ldl", "-lm", "-o", new])
    return old, new


def check(args, old, new):
    artifacts = args.artifacts
    cases = [("all_values", 4, 24, 32), ("empty", 0, 0, 0),
             ("no_properties", 3, 0, 3), ("empty_geometry", 3, 12, 0)]
    report = []
    for name, features, props, geometry in cases:
        old_file, new_file = artifacts / (name + ".old.pbf"), artifacts / (name + ".new.pbf")
        for codec, output in ((old, old_file), (new, new_file)):
            run([codec, "encode", output, features, props, geometry, 1])
        expected = semantic(old_file.read_bytes())
        if name == "all_values":
            decoded_features = expected[0][-1]
            assert [(f[0], f[1]) for f in decoded_features] == [(False, None), (True, 0), (True, 2**64 - 1), (False, None)]
            properties = decoded_features[0][-1]
            assert [v[1][0] for v in properties[:12]] == [1, 2, 3, 4, 5, 6, 7, 4, 6, 2, 3, 7]
            assert properties[0][1][1][1] == b"value-\xff"
            assert properties[1][1][1][1] == bytes.fromhex("00000080")
            assert properties[2][1][1][1] == bytes.fromhex("420000000000f87f")
            assert [properties[n][1][1][1] for n in (3, 4, 5, 6, 7, 8, 11)] == [2**63, 2**64 - 1, 2**64 - 1, 0, 2**63 - 1, 2**64 - 2, 1]
            tile_fields = fields(old_file.read_bytes())
            packed_feature = fields(fields(tile_fields[3][0][1])[2][0][1])
            assert packed_feature[2][0][0] == packed_feature[4][0][0] == 2
            assert all(3 in fields(raw) for _, raw in fields(tile_fields[3][0][1])[2])
        assert semantic(new_file.read_bytes()) == expected, name
        assert dictionary_layout(new_file.read_bytes()) == dictionary_layout(old_file.read_bytes()), name + " dictionary indices"
        # New construction is decoded by real old protobuf-c, and vice versa.
        old_decoded, new_decoded = artifacts / (name + ".old_decoded.pbf"), artifacts / (name + ".new_decoded.pbf")
        run([old, "decode", new_file, old_decoded])
        run([args.rust_decoder, old_file, new_decoded])
        assert semantic(old_decoded.read_bytes()) == expected, name + " new->old"
        assert semantic(new_decoded.read_bytes()) == expected, name + " old->new"
        report.append({"case": name, "old_bytes": old_file.stat().st_size,
                       "new_bytes": new_file.stat().st_size, "directions": "both"})
    # Independent explicit defaults, unpacked repeated input, unknown extensions.
    feature = field(1, 0, 0) + field(3, 0, 0) + field(4, 9, 0) + field(4, 0xffffffff, 0)
    layer = field(15, 1, 0) + field(1, b"") + field(5, 4096, 0) + field(2, feature)
    default_tile = field(3, layer)
    for name, fixture in (("defaults", default_tile),
                          ("unknown_extensions", field(3, layer + field(16, b"layer-extension")) + field(16, b"tile-extension"))):
        source = artifacts / (name + ".pbf")
        source.write_bytes(fixture)
        for decoder, command in (("old", [old, "decode"]), ("new", [args.rust_decoder])):
            output = artifacts / (name + "." + decoder + ".pbf")
            run(command + [source, output])
            assert semantic(output.read_bytes()) == semantic(default_tile), name + decoder
        report.append({"case": name, "directions": "both decoders"})
    enum_features = b"".join(field(2, field(3, kind, 0)) for kind in range(4))
    enum_tile = field(3, field(15, 2, 0) + field(1, b"enum") + field(5, 8192, 0) + enum_features)
    enum_source = artifacts / "geometry_enums.pbf"
    enum_source.write_bytes(enum_tile)
    for decoder, command in (("old", [old, "decode"]), ("new", [args.rust_decoder])):
        output = artifacts / ("geometry_enums." + decoder + ".pbf")
        run(command + [enum_source, output])
        assert semantic(output.read_bytes()) == semantic(enum_tile)
    report.append({"case": "all_geometry_enums", "directions": "both decoders"})
    # Required scalar fields with explicit schema defaults are accepted by this
    # protobuf-c SDK; absent required name is rejected. Prost defaults all four.
    missing = [("version", field(1, b"x") + field(5, 4096, 0)),
               ("name", field(15, 1, 0) + field(5, 4096, 0)),
               ("extent", field(15, 1, 0) + field(1, b"x")),
               ("type", field(15, 1, 0) + field(1, b"x") + field(5, 4096, 0) + field(2, b""))]
    for name, raw in missing:
        source = artifacts / ("missing_" + name + ".pbf")
        source.write_bytes(field(3, raw))
        old_result = subprocess.run([str(old), "decode", str(source), str(artifacts / "rejected.pbf")], capture_output=True)
        assert old_result.returncode == (2 if name == "name" else 0), "unexpected legacy presence behavior: " + name
        output = artifacts / ("missing_" + name + ".prost.pbf")
        run([args.rust_decoder, source, output])
        semantic(output.read_bytes())  # Re-encoder must emit all required fields.
        report.append({"case": "missing_" + name, "old": "reject" if name == "name" else "default and emit", "prost": "default and emit"})
    return report


def benchmark(args, old, new):
    rows = []
    workloads = [("small", 10, 6, 12, 2000), ("properties", 100, 48, 12, 100),
                 ("geometry", 100, 12, 2048, 30), ("large", 1000, 24, 128, 20)]
    for name, features, props, geometry, iterations in workloads:
        samples = {"old": [], "new": []}
        old_file = args.artifacts / ("bench_" + name + ".old.pbf")
        new_file = args.artifacts / ("bench_" + name + ".new.pbf")
        for codec, output in ((old, old_file), (new, new_file)):
            run([codec, "encode", output, features, props, geometry, 1])
        assert semantic(old_file.read_bytes()) == semantic(new_file.read_bytes()), name
        # Warm up both, then alternate order each repeat to reduce order bias.
        for codec in (old, new):
            run([codec, "bench", "unused", features, props, geometry, 2])
        for repeat in range(args.repeats):
            order = (("old", old), ("new", new))
            if repeat % 2:
                order = order[::-1]
            for label, codec in order:
                result = json.loads(run([codec, "bench", "unused", features, props, geometry, iterations]).stdout)
                assert result["iterations"] == iterations and result["checksum"] > 0
                samples[label].append(result)
        assert samples["old"][0]["bytes"] == samples["new"][0]["bytes"], name
        assert samples["old"][0]["checksum"] == samples["new"][0]["checksum"], name
        old_ns = statistics.median(x["ns"] / iterations for x in samples["old"])
        new_ns = statistics.median(x["ns"] / iterations for x in samples["new"])
        rows.append({"workload": name, "features": features, "properties": props,
                     "geometry_vertices": geometry, "geometry_words": 2 * geometry + 1, "old_ns_per_tile": old_ns,
                     "new_ns_per_tile": new_ns, "new_over_old": new_ns / old_ns,
                     "old_ns_range": [min(x["ns"] / iterations for x in samples["old"]), max(x["ns"] / iterations for x in samples["old"])],
                     "new_ns_range": [min(x["ns"] / iterations for x in samples["new"]), max(x["ns"] / iterations for x in samples["new"])],
                     "samples": samples})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--sdk", type=Path, default=ROOT / "deps/3rd/usr/local/oceanbase/deps/devel")
    parser.add_argument("--build", type=Path, default=ROOT / "build/bin")
    parser.add_argument("--archive", type=Path, default=ROOT / "build/bin/rust/cargo/libseekdb_rust.a")
    parser.add_argument("--rust-decoder", type=Path, required=True)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--skip-benchmark", action="store_true")
    args = parser.parse_args()
    assert args.repeats >= 5, "use at least five alternating repeats"
    if args.artifacts is None:
        args.artifacts = Path(tempfile.mkdtemp(prefix="mvt-compatibility-"))
    args.artifacts.mkdir(parents=True, exist_ok=True)
    old, new = compile_harness(args)
    result = {"baseline": BASELINE, "artifact_directory": str(args.artifacts),
              "measurement_scope": "standalone construction model with protobuf-c vs CXX/prost: owned feature geometry, linear legacy key lookup, repeated typed dictionary properties, encoding, final malloc/memcpy/free; excludes actual SQL aggregation, expression evaluation and geometry conversion",
              "compiler": run([args.cxx, "--version"]).stdout.splitlines()[0],
              "machine_load": os.getloadavg(), "logical_cpus": os.cpu_count(),
              "compatibility": check(args, old, new)}
    if not args.skip_benchmark:
        result["benchmark"] = benchmark(args, old, new)
    (args.artifacts / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
