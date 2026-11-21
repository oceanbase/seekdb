"""Verify IN-list plan shape and SQL semantics against a real disposable database."""
import argparse
import json
import random
import subprocess
import uuid
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--client', default='obclient')
parser.add_argument('--port', default=2881, type=int)
parser.add_argument('--expect-compact', action='store_true')
parser.add_argument('--output', default='inlist-regression.json')
args = parser.parse_args()
database = 'inlist_check_' + uuid.uuid4().hex[:12]
base = [args.client, '-h127.0.0.1', '-P' + str(args.port), '-uroot', '-N', '-B']


def sql(statement, use_database=True):
    command = base + ([database] if use_database else []) + ['-e', statement]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout.strip().splitlines()


def query(rhs, operator='IN', threshold=200, left='category'):
    return ("SELECT /*+ OPT_PARAM('inlist_rewrite_threshold' %d) */ id FROM docs "
            'WHERE %s %s (%s) ORDER BY id') % (threshold, left, operator, rhs)


report = {'cases': []}
sql('CREATE DATABASE ' + database, False)
try:
    sql('CREATE TABLE docs(id INT PRIMARY KEY, category INT NULL, KEY category_idx(category))')
    rows = [(i, 'NULL' if i % 17 == 0 else str(i % 11 - 5)) for i in range(1000)]
    sql('INSERT INTO docs VALUES ' + ','.join('(%d,%s)' % row for row in rows))
    repeated = ['1', '2', '3'] * 1024
    random.Random(20261002).shuffle(repeated)
    for operator in ('IN', 'NOT IN'):
        for nulls in (False, True):
            rhs = ','.join(repeated + (['NULL'] if nulls else []))
            observed = sql(query(rhs, operator))
            expected = sql(query('1,2,3' + (',NULL' if nulls else ''), operator, 100000))
            assert observed == expected, (operator, nulls, 'literal membership changed')
            plan = '\n'.join(sql('EXPLAIN ' + query(rhs, operator)))
            values = 'VALUES TABLE ACCESS' in plan
            report['cases'].append({'operator': operator, 'nulls': nulls, 'rows': len(observed),
                                    'values_table': values, 'same_ids': True, 'plan': plan})
            if args.expect_compact:
                assert not values, (operator, nulls, 'duplicate domain still materialized')
    # Same parameterized statement shape with different values, including a
    # high-cardinality execution after the small-domain plan has been cached.
    for values in ([str(i % 3 - 5) for i in range(3072)],
                   [str(i % 256 - 128) for i in range(3072)], repeated):
        rhs = ','.join(values)
        assert sql(query(rhs)) == sql(query(rhs, threshold=100000)), 'cached bindings lost'
    # Explicit force and the bounded-set fallback must still permit rewriting.
    for rhs, threshold in (('1,2,3', 1), (','.join(str(i) for i in range(256)), 200)):
        plan = '\n'.join(sql('EXPLAIN ' + query(rhs, threshold=threshold)))
        assert 'VALUES TABLE ACCESS' in plan, ('rewrite unexpectedly disabled', threshold)
        assert sql(query(rhs, threshold=threshold)) == sql(query(rhs, threshold=100000))
    # Strings, tuple operands and signed extremes remain governed by SQL itself.
    for left, rhs in [('category', "'1','2','3'"),
                      ('category', '-9223372036854775808,9223372036854775807,NULL'),
                      ('(id,category)', '(1,-4),(2,-3),(1,-4)')]:
        for operator in ('IN', 'NOT IN'):
            assert sql(query(rhs, operator, 2, left)) == sql(query(rhs, operator, 100000, left))
    # SQL prepared statements bind at EXECUTE time. Reuse one prepared statement
    # for small and large domains; preserve every parameter slot and NULL rule.
    names = ','.join('@p%d' % i for i in range(64))
    prepared = query(','.join('?' for _ in range(64)), threshold=8)
    commands = ["PREPARE membership FROM '" + prepared.replace("'", "''") + "'"]
    expected_batches = []
    for values in ([str(i % 3) for i in range(64)],
                   [str(i - 32) for i in range(64)], ['NULL'] * 64):
        commands.append('SET ' + ','.join('@p%d=%s' % (i, value) for i, value in enumerate(values)))
        commands.extend(['EXECUTE membership USING ' + names, "SELECT 'batch_end'"])
        expected_batches.append(sql(query(','.join(values), threshold=100000)))
    commands.append('DEALLOCATE PREPARE membership')
    output = sql(';'.join(commands))
    batches, current = [], []
    for line in output:
        if line == 'batch_end':
            batches.append(current)
            current = []
        else:
            current.append(line)
    assert batches == expected_batches and not current, 'prepared statement bindings changed'
    report['prepared_statement_batches'] = [len(batch) for batch in batches]
    report['status'] = 'passed'
finally:
    sql('DROP DATABASE ' + database, False)
    Path(args.output).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'status': report['status'], 'literal_cases': len(report['cases']),
                  'prepared_rows': report['prepared_statement_batches']}))
