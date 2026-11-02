#!/usr/bin/env python3
"""Compare JSON SQL behavior on two local instances with equivalent syntax policies."""

import argparse
import collections
import itertools
import json

import pymysql


DOCUMENTS = [
    None, "null", "true", "false", "0", "-0", "-0.0", "1.0", "1e30",
    "9223372036854775807", "9223372036854775808", "18446744073709551615",
    '"雪😀"', '"a\\u0000b"', "[]", "{}", "[1,2]",
    '[null,true,1,"a",{"a":2}]', '{"a":1}', '{"a":null}',
    '{"a":[1,2],"b":{"a":3}}', '{"a":{"x":1},"b":{"x":2}}',
    '{"a":1,"a":2}', '{"a":[]}', '{"a":{}}', '{"a.b":["雪"]}',
]
PATHS = ["$", "$.a", "$.missing", "$.a[0]", "$.a[*]", "$[*]",
         "$**.a", "$**.x", "$.a[1 to 2]", '$."a.b"', "$.a[last]", "$.a[last-1]"]


def query_cases():
    wrappers = ["", " without wrapper", " without array wrapper", " with wrapper",
                " with array wrapper", " with unconditional wrapper",
                " with conditional wrapper", " with unconditional array wrapper",
                " with conditional array wrapper"]
    for document, path, wrapper, scalar, binary in itertools.product(
            DOCUMENTS, PATHS, wrappers, ["", " disallow scalars"], [False, True]):
        source = "cast(%s as json)" if binary else "%s"
        yield ("select json_query(" + source + ", %s returning json" + scalar + wrapper + ")",
               (document, path))
    for document, path, clause in itertools.product(
            DOCUMENTS, ["$.a", "$.missing"],
            ["null on error", "error on error", "empty array on error",
             "empty object on error", "empty array on empty", "empty object on empty"]):
        yield "select json_query(%s, %s returning json " + clause + ")", (document, path)
    for document in ["{", "[1,]", "[" * 101 + "0" + "]" * 101]:
        for function in ["json_query", "json_value"]:
            yield "select " + function + "(%s, '$' null on error)", (document,)
    for document, path in itertools.product(DOCUMENTS, [None, "", "$**", "$.0a"]):
        yield "select json_query(%s, %s returning json)", (document, path)


def schema_cases():
    schemas = [{}] + [{"type": name} for name in
                      ["null", "boolean", "integer", "number", "string", "array", "object"]]
    schemas += [
        {"type": ["string", "null"]}, {"type": "unknown"},
        {"minimum": 0}, {"maximum": 1, "exclusiveMaximum": True},
        {"minimum": 1, "exclusiveMinimum": True}, {"multipleOf": 0.1},
        {"multipleOf": 0}, {"minLength": 2}, {"maxLength": 3},
        {"pattern": "^a"}, {"pattern": "a(?=b)"}, {"pattern": r"(a)\1"},
        {"pattern": "["}, {"minItems": 2}, {"maxItems": 1},
        {"uniqueItems": True}, {"items": {"type": "integer"}},
        {"items": [{"type": "integer"}], "additionalItems": False},
        {"properties": {"a": {"type": "integer"}}, "additionalProperties": False},
        {"patternProperties": {"^a": {"type": "integer"}}},
        {"required": ["a"]}, {"minProperties": 2}, {"maxProperties": 1},
        {"dependencies": {"a": ["b"]}}, {"dependencies": {"a": {"required": ["b"]}}},
        {"enum": [1, "a", None]}, {"allOf": [{"minimum": 0}, {"maximum": 1}]},
        {"anyOf": [{"type": "integer"}, {"type": "string"}]},
        {"oneOf": [{"type": "integer"}, {"type": "number"}]},
        {"not": {"type": "integer"}},
        {"definitions": {"a": {"type": "integer"}}, "$ref": "#/definitions/a"},
        {"$ref": "#/missing"}, {"$ref": "https://example.invalid/schema"},
    ]
    documents = DOCUMENTS + ['"ab"', '"aa"', '"a"', "[1,1]", "[1,1.0]",
                             '{"a":1,"b":2}', '{"a":"x"}', "0.3", "1.1", "-1"]
    for schema, document, function in itertools.product(
            schemas, documents, ["json_schema_valid", "json_schema_validation_report"]):
        yield "select " + function + "(%s, %s)", (json.dumps(schema), document)


def update_cases():
    for document, path, function in itertools.product(
            DOCUMENTS, PATHS,
            ["json_set", "json_insert", "json_replace", "json_array_append", "json_array_insert"]):
        yield "select " + function + "(%s, %s, 9)", (document, path)
    for document, path in itertools.product(DOCUMENTS, PATHS):
        yield "select json_remove(%s, %s)", (document, path)
    for left, right, function in itertools.product(
            DOCUMENTS, DOCUMENTS, ["json_merge_patch", "json_merge_preserve"]):
        yield "select " + function + "(%s, %s)", (left, right)


def table_cases():
    for document, path, columns in itertools.product(
            DOCUMENTS, ["$", "$[*]", "$.a[*]"],
            ["n for ordinality, a int path '$.a'", "a json path '$.a'",
             "a json path '$.a[*]'", "a int exists path '$.a'",
             "a int path '$.a', nested path '$.b[*]' columns(b int path '$')"]):
        yield "select * from json_table(%s, %s columns(" + columns + ")) jt", (document, path)


SECTIONS = {"query": query_cases, "schema": schema_cases,
            "update": update_cases, "table": table_cases}


def execute(connection, sql, arguments):
    try:
        with connection.cursor() as cursor:
            cursor.execute(sql, arguments)
            return "ok", cursor.fetchall()
    except pymysql.MySQLError as error:
        if not error.args or error.args[0] == 0 or 2000 <= error.args[0] < 3000:
            raise
        return "error", error.args[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-port", type=int, required=True)
    parser.add_argument("--candidate-port", type=int, required=True)
    parser.add_argument("--section", choices=list(SECTIONS), action="append")
    args = parser.parse_args()
    connections = [pymysql.connect(host="127.0.0.1", port=port, user="root",
                                    database="test", autocommit=True, read_timeout=30)
                   for port in [args.baseline_port, args.candidate_port]]
    counts = collections.Counter()
    failures = []
    try:
        for section in args.section or SECTIONS:
            for sql, arguments in SECTIONS[section]():
                before, after = [execute(connection, sql, arguments) for connection in connections]
                counts[section + ".runs"] += 1
                counts[section + "." + after[0]] += 1
                if before != after:
                    counts[section + ".differences"] += 1
                    if len(failures) < 30:
                        failures.append({"section": section, "sql": sql, "arguments": arguments,
                                         "baseline": before, "candidate": after})
            if counts[section + ".ok"] == 0:
                failures.append({"section": section, "error": "no successful SQL execution"})
    finally:
        for connection in connections:
            connection.close()
    print(json.dumps({"counts": counts, "first_failures": failures}, ensure_ascii=False))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
