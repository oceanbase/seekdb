#!/usr/bin/env python3
"""Compare SQL hash aggregation, DISTINCT and hash joins on JSON values."""

import argparse
import collections
import itertools
import json
import random

import pymysql
from json_domain_differential import execute


def cases():
    documents = [
        'null', 'true', 'false', '0', '-0', '0.0', '-0.0', '1', '1.0', '-1', '255', '256',
        '9223372036854775807', '9223372036854775808.0', '18446744073709551615',
        '0.1', '0.10', '1e-200', '1e200', '""', '"a"', '"A"', '"雪😀"', '"a\\u0000b"',
        '[]', '{}', '[1]', '[1.0]', '[1,2]', '[2,1]', '[null,true,false]',
        '{"a":1}', '{"a":1.0}', '{"a":1,"b":[2]}', '{"b":[2],"a":1}',
        '{"":null,"雪":"😀"}', '[[1],[2,[3]]]',
    ]
    groups = [documents[:19], documents[19:], documents]
    groups += [documents[index:index + 6] for index in range(0, len(documents), 5)]
    for values in groups:
        for repetitions in [1, 2, 5]:
            inputs = values * repetitions
            random.Random(42).shuffle(inputs)
            source = '(' + ' union all '.join('select cast(%s as json) as j' for _ in inputs) + ') inputs'
            for hint in ['USE_HASH_AGGREGATION', 'NO_USE_HASH_AGGREGATION']:
                yield 'group', 'select /*+ ' + hint + ' */ cast(j as char) as rendered,count(*) from ' + source + ' group by j order by rendered', tuple(inputs)
            for hint in ['USE_HASH_DISTINCT', 'NO_USE_HASH_DISTINCT']:
                yield 'distinct', 'select cast(j as char) as rendered from (select /*+ ' + hint + ' */ distinct j from ' + source + ') unique_values order by rendered', tuple(inputs)
    scalars = [
        "cast('0.10' as decimal(15,4))", "cast('0.1' as decimal(15,2))", '0.1e0', '0',
        "date '2024-02-29'", "timestamp '2024-02-29 00:00:00'",
        "time '12:34:56.000001'", "time '12:34:56.000002'", "cast('a' as binary)",
    ]
    for width in [1, 3, 9]:
        values = ["json_extract(json_array(" + value + "),'$[0]')" for value in scalars[:width]] * 3
        source = '(' + ' union all '.join('select ' + value + ' as j' for value in values) + ') inputs'
        yield 'database_group', 'select /*+ USE_HASH_AGGREGATION */ cast(j as char) as rendered,count(*) from ' + source + ' group by j order by rendered', ()
        yield 'database_distinct', 'select cast(j as char) as rendered from (select /*+ USE_HASH_DISTINCT */ distinct j from ' + source + ') unique_values order by rendered', ()
    for left, right in itertools.product(documents, repeat=2):
        yield 'join', 'select /*+ USE_HASH(a b) */ count(*) from (select cast(%s as json) j union all select cast(%s as json)) a join (select cast(%s as json) j union all select cast(%s as json)) b on a.j=b.j', (left, right, left, right)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-port', type=int, required=True)
    parser.add_argument('--candidate-port', type=int, required=True)
    args = parser.parse_args()
    connections = [pymysql.connect(host='127.0.0.1', port=port, user='root', database='test', autocommit=True)
                   for port in [args.baseline_port, args.candidate_port]]
    counts = collections.Counter()
    failures = []
    try:
        for section, query, arguments in cases():
            results = [execute(connection, query, arguments) for connection in connections]
            counts[section + '.runs'] += 1
            counts[section + '.' + results[0][0]] += 1
            if results[0] != results[1]:
                counts[section + '.differences'] += 1
                if len(failures) < 12:
                    failures.append({'query': query, 'arguments': arguments, 'results': results})
    finally:
        for connection in connections:
            connection.close()
    print(json.dumps({'counts': dict(counts), 'failures': failures}, ensure_ascii=False, default=str))
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
