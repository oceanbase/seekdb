#!/usr/bin/env python3
"""Compare JSON formatting and value ordering on two local database instances."""

import argparse
import collections
import itertools
import json
import math
import random
import struct

import pymysql
from json_domain_differential import DOCUMENTS, execute


def cases():
    documents = [value for value in DOCUMENTS if value is not None]
    documents += [
        "1e-16", "1e-15", "1e14", "1e15", "1e23", "5e-324", "1e308",
        "9007199254740992.0", "9007199254740993", "9223372036854775808.0",
        "18446744073709551616.0", "-9223372036854775808.0",
        json.dumps({"": [], "a": "\x00\x01\b\t\n\f\r\"\\/雪😀", "xy": {"z": 1}}, ensure_ascii=False),
    ]
    random_values = random.Random(314159)
    for _ in range(300):
        number = struct.unpack("d", random_values.getrandbits(64).to_bytes(8, "little"))[0]
        if math.isfinite(number):
            documents.append(repr(number))
    for value in documents:
        yield "format", "select cast(%s as json), json_pretty(%s), json_unquote(cast(%s as json))", (value,) * 3
    values = documents[:len(DOCUMENTS) + 12]
    for a, b in itertools.product(values, repeat=2):
        yield "order", "select cast(%s as json) < cast(%s as json), cast(%s as json) = cast(%s as json), cast(%s as json) > cast(%s as json)", (a, b) * 3
    for a, b in itertools.product(values, repeat=2):
        yield "equality", "select json_contains(cast(%s as json), cast(%s as json)), json_overlaps(cast(%s as json), cast(%s as json)), cast(%s as json) member of(cast(%s as json))", (a, b) * 3
    expressions = [
        "json_array(cast('1.2300' as decimal(15,4)))",
        "json_array(date '2024-02-29', time '12:34:56.123456', timestamp '2024-01-01 01:02:03.123456')",
        "json_array(cast('abc' as binary))",
        "json_object('t', timestamp '2024-01-01 01:02:03.123456', 'd', cast('-0.001200' as decimal(10,6)))",
    ]
    for expression in expressions:
        yield "database_format", "select " + expression + ", json_pretty(" + expression + ")", ()
    scalars = [
        "cast('0' as decimal(10,2))", "cast('1' as decimal(10,2))",
        "cast('-1' as decimal(10,2))", "cast('0.10' as decimal(10,2))",
        "1e200", "-1e200", "0e0", "1e-200", "-1e-200", "0.1e0",
        "date '2024-02-29'", "timestamp '2024-02-29 00:00:00'",
        "time '12:34:56'", "cast('abc' as binary)", "cast('abd' as binary)",
        "time '12:34:56.000001'", "time '12:34:56.000002'",
        "timestamp '2024-02-29 00:00:00.000001'", "timestamp '2024-02-29 00:00:00.000002'",
    ]
    for scalar in scalars + ["null", "true", "false", "-9223372036854775808", "18446744073709551615", "b'101'"]:
        expression = "cast(" + scalar + " as json)"
        yield "database_cast", "select " + expression + ",json_storage_size(" + expression + "),json_type(" + expression + ")", ()
    for a, b in itertools.product(scalars, repeat=2):
        left = "json_extract(json_array(" + a + "), '$[0]')"
        right = "json_extract(json_array(" + b + "), '$[0]')"
        yield "database_order", "select " + left + " < " + right + ", " + left + " = " + right + ", " + left + " > " + right, ()
        yield "database_equality", "select json_contains(" + left + "," + right + "), json_overlaps(" + left + "," + right + "), " + left + " member of(" + right + ")", ()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-port", type=int, required=True)
    parser.add_argument("--candidate-port", type=int, required=True)
    args = parser.parse_args()
    connections = [pymysql.connect(host="127.0.0.1", port=port, user="root", database="test", autocommit=True)
                   for port in [args.baseline_port, args.candidate_port]]
    counts = collections.Counter()
    failures = []
    try:
        for section, sql, arguments in cases():
            before, after = [execute(connection, sql, arguments) for connection in connections]
            counts[section + ".runs"] += 1
            counts[section + "." + after[0]] += 1
            if before != after:
                counts[section + ".differences"] += 1
                if len(failures) < 30:
                    failures.append(dict(sql=sql, arguments=arguments, baseline=before, candidate=after))
    finally:
        for connection in connections:
            connection.close()
    print(json.dumps(dict(counts=counts, failures=failures), ensure_ascii=False))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
