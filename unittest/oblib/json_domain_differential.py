#!/usr/bin/env python3
"""Compare JSON SQL behavior on two local instances with equivalent syntax policies."""

import argparse
import collections
import itertools
import json
import uuid

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
    for schema, document, function in itertools.product(
            [None, "null", "[]", "true", '"schema"', "{", '[1,]',
             '{"type":"object","type":"array"}'],
            [None, "null", "{}", "[1]"],
            ["json_schema_valid", "json_schema_validation_report"]):
        yield "select " + function + "(%s, %s)", (schema, document)
    dynamic = [
        {"type": "object"}, {"type": "array"}, {"minimum": 1},
        {"properties": {"a": {"not": {"minimum": 0}}},
         "patternProperties": {"a": {"not": {"type": "integer"}}}},
        {"dependencies": {"a": {"required": ["b"]}}},
    ]
    for schema, document, function in itertools.product(
            dynamic, ["null", "{}", '{"ba":1}', '{"a":1}', '{"a":"x"}'],
            ["json_schema_valid", "json_schema_validation_report"]):
        text = json.dumps(schema)
        yield ("select " + function + "(s,d) from (select %s s,%s d union all select %s,%s "
               "union all select %s,%s) inputs"), (text, document, '{"type":"integer"}', "1", text, document)

    for function, temporal in itertools.product(
            ["json_schema_valid", "json_schema_validation_report"],
            ["time", "timestamp"]):
        prefix = "12:00:00" if temporal == "time" else "2024-01-01 12:00:00"
        first = temporal + " '" + prefix + ".000001'"
        second = temporal + " '" + prefix + ".000002'"
        yield "select " + function + "(json_object('enum',json_array(" + first + ")),json_extract(json_array(" + second + "),'$[0]'))", ()
        yield "select " + function + "(%s,json_array(" + first + "," + second + "))", ('{"uniqueItems":true}',)


def update_cases():
    for document, path, function in itertools.product(
            DOCUMENTS, PATHS,
            ["json_set", "json_insert", "json_replace", "json_array_append", "json_array_insert"]):
        yield "select " + function + "(%s, %s, 9)", (document, path)
    for document, path in itertools.product(DOCUMENTS, PATHS):
        yield "select json_remove(%s, %s)", (document, path)
    for document, function, value in itertools.product(
            DOCUMENTS,
            ["json_set", "json_insert", "json_replace", "json_array_append", "json_array_insert"],
            ["null", "json_array(1,2)", "cast('0.10' as decimal(10,2))",
             "time '12:34:56.123456'", "cast(%s as json)"]):
        for paths in [("$.a", "$.a[0]"), ("$[0]", "$[last]"), ("$.missing.x", "$.a"),
                      ("$", "$.a"), ("$[last-99]", "$[1]")]:
            arguments = (document, paths[0])
            if "%s" in value:
                arguments += ("invalid json",)
            arguments += (paths[1],)
            yield "select " + function + "(%s,%s," + value + ",%s,7)", arguments
    for document in DOCUMENTS:
        yield "select json_remove(%s,'$[0]','$[0]','$.a','$.a')", (document,)
    for function in ["json_set", "json_insert", "json_replace", "json_array_append", "json_array_insert"]:
        yield ("select " + function + "(d,p,9) from (select '{\"a\":[1]}' d,'$.a[0]' p "
               "union all select '[2,3]','$[1]' union all select '{\"b\":4}','$.b' "
               "union all select '{\"a\":[5]}','$.a[0]') inputs"), ()
    for left, right, function in itertools.product(
            DOCUMENTS, DOCUMENTS, ["json_merge_patch", "json_merge_preserve"]):
        yield "select " + function + "(%s, %s)", (left, right)


def construction_cases():
    scalars = ["null", "true", "false", "-9223372036854775808", "18446744073709551615",
               "1e200", "0.1e0", "cast('0.10' as decimal(10,2))", "cast('-0.001200' as decimal(10,6))",
               "date '2024-02-29'", "time '12:34:56.123456'", "timestamp '2024-01-01 12:34:56.123456'",
               "cast('abc' as binary)", "b'101'", "year(date '2024-02-29')"]
    for scalar, document in itertools.product(scalars, DOCUMENTS):
        expression = "json_array(" + scalar + ",cast(%s as json))"
        yield "select " + expression + ",json_storage_size(" + expression + ")", (document, document)
        expression = "json_object('a'," + scalar + ",'b',cast(%s as json),'a',7)"
        yield "select " + expression + ",json_storage_size(" + expression + ")", (document, document)
    for length in [0,1,115,116,117,118,119,120,121,122,123,124,125,126,127,128,230,240,250,255,256,257,65520,65530,65535,65536]:
        for expression in ["json_array(%s,1)", "json_object('long_key',%s,'a',1)",
                           "json_array(json_array(%s),1)", "json_object('',%s,'',9)"]:
            yield "select " + expression + ",json_storage_size(" + expression + ")", ("x" * length,) * 2
    for count in [0,1,2,10,127,128,255,256,300]:
        expression = "json_array(" + ','.join(str(index) for index in range(count)) + ")"
        yield "select " + expression + ",json_storage_size(" + expression + ")", ()


def table_cases():
    for document, path, columns in itertools.product(
            DOCUMENTS, ["$", "$[*]", "$.a[*]"],
            ["n for ordinality, a int path '$.a'", "a json path '$.a'",
             "a json path '$.a[*]'", "a int exists path '$.a'",
             "a int path '$.a', nested path '$.b[*]' columns(b int path '$')"]):
        yield "select * from json_table(%s, %s columns(" + columns + ")) jt", (document, path)
    for count in [2, 3, 10, 100]:
        document = json.dumps([{"a": [{"b": [index]}]} for index in range(count)])
        yield ("select * from json_table(%s,'$[*]' columns(nested path '$.a[*]' "
               "columns(nested path '$.b[*]' columns(v int path '$')))) jt"), (document,)


STORAGE_TABLE = "json_domain_delta_" + uuid.uuid4().hex


def storage_cases():
    yield "set session log_row_value_options='PARTIAL_JSON'", ()
    yield "create table " + STORAGE_TABLE + "(id int primary key, j json)", ()
    document = {"a": [1, 2, 3], "number": 1, "large": "x" * 10000,
                "nested": {"keep": "y" * 1000, "change": "z" * 500}}
    yield "insert into " + STORAGE_TABLE + " values (1, %s)", (json.dumps(document),)
    changes = [
        "json_set(j,'$.number',9,'$.a[1]',42)",
        "json_replace(j,'$.nested.change','small')",
        "json_set(j,'$.nested.added',json_object('x',7))",
        "json_remove(j,'$.a[0]','$.nested.keep')",
        "json_set(json_set(j,'$.number',11),'$.a[0]',99)",
        "json_remove(json_replace(j,'$.nested.change',repeat('w',2000)),'$.nested.added')",
    ]
    inspect = ("select id,json_extract(j,'$.a','$.number','$.nested.change'),"
               "json_storage_size(j),json_storage_free(j),md5(cast(j as char)) from " + STORAGE_TABLE)
    yield inspect, ()
    for change in changes:
        yield "update " + STORAGE_TABLE + " set j=" + change + " where id=1", ()
        yield inspect, ()
    yield "set @json_domain_bad='not-json'", ()
    yield ("update " + STORAGE_TABLE + " set j=json_set(j,'$.number',100,'$.bad',"
           "cast(@json_domain_bad as json)) where id=1"), ()
    yield inspect, ()
    yield "drop table " + STORAGE_TABLE, ()


SECTIONS = {"query": query_cases, "schema": schema_cases,
            "update": update_cases, "construction": construction_cases, "table": table_cases, "storage": storage_cases}


def execute(connection, sql, arguments):
    try:
        with connection.cursor() as cursor:
            cursor.execute(sql, arguments or None)
            return "ok", cursor.fetchall()
    except pymysql.MySQLError as error:
        if not error.args or error.args[0] == 0 or 2000 <= error.args[0] < 3000:
            raise
        return "error", (error.args[0], error.args[1] if len(error.args) > 1 else "")


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
                if section == "storage" and after[0] == "ok" and sql.startswith("select"):
                    counts["storage.partial_results"] += sum(row[3] > 0 for row in after[1])
                if before != after:
                    counts[section + ".differences"] += 1
                    if len(failures) < 30:
                        failures.append({"section": section, "sql": sql, "arguments": arguments,
                                         "baseline": before, "candidate": after})
            if counts[section + ".ok"] == 0:
                failures.append({"section": section, "error": "no successful SQL execution"})
            if section == "storage" and counts["storage.partial_results"] == 0:
                failures.append({"section": section, "error": "no retained partial-update space observed"})
    finally:
        for connection in connections:
            try:
                if connection.open and "storage" in (args.section or SECTIONS):
                    with connection.cursor() as cursor:
                        cursor.execute("drop table if exists " + STORAGE_TABLE)
            finally:
                connection.close()
    print(json.dumps({"counts": counts, "first_failures": failures}, ensure_ascii=False))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
