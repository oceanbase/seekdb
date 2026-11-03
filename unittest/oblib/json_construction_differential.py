#!/usr/bin/env python3
"""Compare JSON constructors and aggregates against a local database baseline."""

import argparse
import collections
import itertools
import json

import pymysql
from json_domain_differential import execute


def cases():
    values = [
        "null", "true", "false", "0", "-1", "255", "256", "65535", "65536",
        "18446744073709551615", "cast('0.10' as decimal(15,4))", "1e-200", "1e200",
        "''", "'雪😀'", "char(0,1,9,10,13,34,92 using utf8mb4)", "cast('abc' as binary)",
        "date '2024-02-29'", "time '12:34:56.123456'", "timestamp '2024-01-01 01:02:03.123456'",
        "cast('[1,null,{}]' as json)", "cast('{\"a\":2,\"bb\":[3]}' as json)",
    ]
    for a, b in itertools.product(values, repeat=2):
        for expression in ["json_array(" + a + "," + b + ")", "json_object('bb'," + a + ",'a'," + b + ",'bb'," + b + ")"]:
            yield "constructor", "select " + expression + ",json_storage_size(" + expression + "),json_depth(" + expression + ")", ()
    for length in [0, 1, 230, 240, 250, 255, 256, 257, 65520, 65536]:
        expression = "json_array(repeat('x',%s),cast('[1,2]' as json),null)"
        yield "size_boundary", "select json_storage_size(" + expression + "),sha1(cast(" + expression + " as char))", (length, length)
    for length, number in itertools.product([300, 65536], [-1, -128, -129, -32768, -32769, -2147483648]):
        for expression in ["json_array(repeat('x',%s)," + str(number) + ")", "cast(%s as json)"]:
            argument = length if expression.startswith('json_array') else json.dumps(['x' * length, number])
            yield "signed_width", "select json_extract(" + expression + ",'$[1]'),json_storage_size(" + expression + ")", (argument, argument)
    for count in [29, 30, 31, 100]:
        document = json.dumps({str(index): index for index in range(count)})
        for expression, parameters in [
            ("json_query(%s,'$')", (document,)),
            ("json_extract(json_array(cast(%s as json)),'$[0]')", (document,)),
            ("json_set(%s,'$.new',1)", (document,)),
            ("json_merge_patch(%s,'{\"new\":1}')", (document,)),
            ("json_merge_preserve(%s,'{\"new\":1}')", (document,)),
        ]:
            yield "constructed_size", "select " + expression + ",json_storage_size(" + expression + ")", parameters * 2
    for value in values:
        for count in [1, 2, 10, 50, 100]:
            source = "(" + " union all ".join("select " + str(index) + " as k," + value + " as v" for index in range(count)) + ") inputs"
            for function in ["json_arrayagg(v)", "json_objectagg(cast(k as char),v)", "json_objectagg(cast(k % 3 as char),v)"]:
                yield "aggregate", "select " + function + ",json_storage_size(" + function + "),json_depth(" + function + ") from " + source, ()
    for expression in ["json_object(null,1)", "json_object('',null)", "json_object('a',1,'a',2,'a',3)", "json_array()", "json_object()"]:
        yield "edge", "select " + expression + ",json_storage_size(" + expression + ")", ()


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
        for section, query, parameters in cases():
            counts[section + ".runs"] += 1
            results = [execute(connection, query, parameters) for connection in connections]
            if results[0] != results[1]:
                counts[section + ".differences"] += 1
                if len(failures) < 12:
                    failures.append({"query": query, "parameters": parameters, "results": results})
    finally:
        for connection in connections:
            connection.close()
    print(json.dumps({"counts": dict(counts), "failures": failures}, ensure_ascii=False, default=str))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
