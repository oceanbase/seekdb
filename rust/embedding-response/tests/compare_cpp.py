#!/usr/bin/env python3
"""Compare error codes, partial output and every float bit with the C++ backends.

Run after build.sh has installed the project's RapidJSON headers. No server link,
network, third-party Python packages, or performance measurement is required.
"""
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
import argparse
import base64
from decimal import Decimal, localcontext
import json
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
REPO = ROOT.parent


def cases():
    rng = random.Random(0x5345454B)

    def numeric(number):
        return 1, "f", ('{"data":[{"embedding":[' + number + ']}]}').encode()

    for number in ["-0", "-0.0", "0e999", "1e309", "1e-999", "-1e-999",
                   "18446744073709551615", "18446744073709551616",
                   "-9223372036854775808", "-9223372036854775809",
                   "9007199791611905", "1.000000059604644775390625",
                   "0." + "0" * 700 + "1e700", "9" * 400 + "e-999",
                   "01", "+1", ".1", "1.", "1e", "NaN", "Infinity"]:
        yield numeric(number)
    for _ in range(4000):
        number = str(rng.randrange(10 ** rng.randrange(1, 90)))
        if rng.randrange(2):
            number = "-" + number
        if rng.randrange(2):
            number += "." + "".join(str(rng.randrange(10)) for _ in range(rng.randrange(1, 90)))
        if rng.randrange(2):
            number += "e" + str(rng.randrange(-800, 350))
        yield numeric(number)
    # Decimal tokens around f32 midpoints detect changes in f64 rounding paths.
    with localcontext() as context:
        context.prec = 180
        for _ in range(400):
            bits = rng.randrange(1, 0x7f7fffff)
            a = Decimal(struct.unpack("!f", struct.pack("!I", bits))[0])
            b = Decimal(struct.unpack("!f", struct.pack("!I", bits + 1))[0])
            midpoint = (a + b) / 2
            epsilon = (b - a) / 10 ** 12
            for value in [midpoint - epsilon, midpoint, midpoint + epsilon]:
                yield numeric(str(value))
                yield numeric(str(-value))
    for _ in range(300):
        dimension = rng.randrange(1, 33)
        raw = rng.getrandbits(32 * dimension).to_bytes(4 * dimension, "little")
        encoded = base64.b64encode(raw).decode()
        for string in [encoded, encoded.rstrip("="), encoded + "=", encoded + "==",
                       encoded + "===", encoded[:-1] + "!", encoded + " "]:
            yield dimension, "b", json.dumps({"data": [{"embedding": string}]}).encode()
    for length in range(40):
        for padding in range(4):
            for dimension in range(4):
                encoded = "A" * length + "=" * padding
                yield dimension, "b", json.dumps({"data": [{"embedding": encoded}]}).encode()
    documents = [
        b"", b" ", b"null", b"[]", b"{}", b'{"data":null}', b'{"data":[{}]}',
        b'{"data":[],"data":null}', b'{"data":null,"data":[]}',
        b'{"data":[{"embedding":[1],"embedding":[2]}]}',
        b'{"data":[{"embedding":[]}]}',
        b'{"d\\u0061ta":[{"embedd\\u0069ng":[1]}]}',
        b'{"data\\u0000":[]}', b'{"data":[]}\x00garbage',
        b'{"data":[],"unused":"\xff"}', b'{"data":[],"unused":"a\\n\xff"}',
        b'{"data":[],"unused":"\\ud800"}', b'{"data":[],"unused":"\\udc00"}',
        b'{"data":[],"unused":"\\ud83d\\ude00"}',
        b'{"data":[{"embedding":[1]},{}]}',
        b'{"data":[{"embedding":[1]},{"embedding":[2,3]}]}',
        b'{"data":[{"embedding":[1]}],"unused":1e999}',
        b'{"data":[{"embedding":[1]}],"unused":[1,]}',
        b'{"data":[{"embedding":"AAAAAA=="},{"embedding":"bad"}]}',
    ]
    for depth in [99, 100, 101, 102]:
        documents.append(b'{"data":[],"unused":' + b'[' * depth + b'0' + b']' * depth + b'}')
    for document in documents:
        for dimension in [-1, 0, 1, 2, 2**63 - 1]:
            for encoding in ["f", "b"]:
                yield dimension, encoding, document
    # Structured byte mutations exercise parser failure paths and raw-byte input.
    seed = b'{"data":[{"embedding":[1,-2.5,3e-5]}],"meta":"a\\u0062"}'
    for _ in range(2000):
        data = bytearray(seed)
        for _ in range(rng.randrange(1, 5)):
            position = rng.randrange(len(data))
            if rng.randrange(3) == 0:
                del data[position]
            else:
                data[position] = rng.randrange(256)
        yield 3, "f", bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rapidjson-include", type=Path, default=REPO / "deps/3rd/usr/local/oceanbase/deps/devel/include")
    parser.add_argument("--adapter-probe", type=Path, help="production C++ adapter integration binary")
    args = parser.parse_args()
    if not (args.rapidjson_include / "rapidjson/document.h").is_file():
        parser.error("bundled RapidJSON missing; initialize build dependencies or pass --rapidjson-include")
    with tempfile.TemporaryDirectory(prefix="embedding-compat-") as directory:
        oracle = Path(directory) / "cpp-oracle"
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-O2", "-I" + str(args.rapidjson_include),
            "-I" + str(REPO / "src/oblib"), str(Path(__file__).with_name("cpp_oracle.cpp")),
            "-o", str(oracle)], check=True)
        subprocess.run(["cargo", "build", "--offline", "--locked", "-p", "embedding-response",
                        "--example", "compat_probe", "--target-dir", directory], cwd=ROOT, check=True)
        inputs = list(cases())
        wire = "".join(f"{dimension} {encoding} {document.hex()}\n" for dimension, encoding, document in inputs)
        expected = subprocess.run([str(oracle)], input=wire, text=True, capture_output=True, check=True).stdout.splitlines()
        actual = subprocess.run([str(Path(directory) / "debug/examples/compat_probe")], input=wire,
                                text=True, capture_output=True, check=True).stdout.splitlines()
        assert len(expected) == len(actual) == len(inputs), "probe returned the wrong number of results"
        for case, cpp, rust in zip(inputs, expected, actual):
            if cpp != rust:
                raise AssertionError(f"case={case!r}\nC++: {cpp}\nRust: {rust}")
        print(f"PASS: {len(inputs)} C++/Rust cases; identical error codes, partial output and float bits")
        if args.adapter_probe:
            adapter = subprocess.run([str(args.adapter_probe.resolve()), "--probe"], input=wire,
                                     text=True, capture_output=True, check=True).stdout.splitlines()
            assert len(adapter) == len(inputs), "adapter returned the wrong number of results"
            for case, cpp, result in zip(inputs, expected, adapter):
                if cpp != result:
                    raise AssertionError(f"case={case!r}\nC++: {cpp}\nAdapter: {result}")
            print(f"PASS: {len(inputs)} cases through the production C++ -> Rust adapter")


if __name__ == "__main__":
    main()
