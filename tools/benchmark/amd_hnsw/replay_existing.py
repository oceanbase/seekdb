#!/usr/bin/env python3
"""Read-only replay of existing CPU/GPU indexes after an external restart.

Does not stop/start a service or prove a restart occurred. The operator must
save the separate actual restart receipt. No CREATE/ALTER/DROP/DML is issued.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time
import traceback

import sql_benchmark as bench


def require(value, message):
    if not value:
        raise ValueError(message)


def target_in_plan(plan, table, index):
    text = json.dumps(plan, default=str)
    return bool(re.search(r'VECTOR\s+INDEX\s+SCAN', text, re.I)
        and re.search(r'(?<![a-z0-9_])' + re.escape(table) + r'(?![a-z0-9_])', text)
        and re.search(r'(?<![a-z0-9_])' + re.escape(index) + r'(?![a-z0-9_])', text))


def select_existing(source, original_manifest):
    require(source.get('mode') == 'formal'
        and source.get('status') == 'formal_measured_requires_evidence_review'
        and source.get('qualified_gpu_build') is True,
        'Require a successful complete formal source report')
    schema = source['schema']
    bench.ident(schema)
    require(source.get('dataset_sha256') == original_manifest['inputs']['base']['sha256']
        and source.get('query_count') == original_manifest['query_count']
        and source.get('k') == original_manifest['k']
        and source.get('recall_targets') == original_manifest['recall_targets'],
        'Source report dataset/query/GT identity mismatch')
    selected = {}
    for backend in ('cpu', 'gpu'):
        rows = [row for row in source['rounds'] if row.get('backend') == backend]
        require(len(rows) == 3 and all(row.get('status') == 'verified_round_complete' for row in rows),
                'Source formal report lacks three complete ' + backend + ' rounds')
        row = max(rows, key=lambda value: value['round'])
        bench.ident(row['table'])
        bench.ident(row['index'])
        proof = row.get('completion_proof', {})
        require(proof.get('schema') == schema and proof.get('table') == row['table']
            and proof.get('index') == row['index'] and proof.get('backend') == backend
            and proof.get('indexed_rows') == original_manifest['base_count']
            and proof.get('complete_index_ready') is True and proof.get('hnsw_semantics') is True,
            'Source target is not its proven complete full-N HNSW index')
        selected[backend] = row
    return schema, selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--gt-dir', type=Path, required=True)
    parser.add_argument('--base')
    parser.add_argument('--query')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=2882)
    parser.add_argument('--user', default='root')
    parser.add_argument('--password-env', default='SEEKDB_PASSWORD')
    parser.add_argument('--driver', choices=['mysql', 'pymysql'], default='mysql')
    parser.add_argument('--mysql', default='mysql')
    parser.add_argument('--sql-timeout', type=int, default=300)
    parser.add_argument('--ef-search', type=int, default=1000)
    parser.add_argument('--recall-threshold', type=float, default=0.96)
    args = parser.parse_args()
    require(args.ef_search > 0 and 0 < args.recall_threshold <= 1, 'Invalid ef-search/threshold')
    args.report = args.report.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    output_report = {'status': 'running', 'started_utc': bench.utc(),
        'script_sha256': bench.sha256(__file__), 'read_only_database_replay': True,
        'restart_verified_by_this_script': False,
        'restart_evidence': 'Operator must separately save the actual graceful-restart receipt.',
        'source_report': str(args.report), 'backends': []}
    sql = None
    error = None
    try:
        bench.write_json(args.output / 'arguments.json', vars(args))
        source = json.loads(args.report.read_text(encoding='utf-8'))
        manifest, arrays, texts, gold = bench.inputs(args.gt_dir.resolve(), args.base, args.query)
        original_manifest = json.loads((args.report.parent / 'input-manifest.json').read_text(encoding='utf-8'))
        for key in ('base_count', 'dimension', 'query_count', 'k', 'recall_targets', 'metric'):
            require(manifest[key] == original_manifest[key], 'Replay input manifest differs: ' + key)
        for label in ('base', 'query'):
            require(manifest['inputs'][label]['sha256'] == original_manifest['inputs'][label]['sha256'],
                    'Replay raw hash differs from original formal input: ' + label)
        require(manifest['artifacts']['gold.json']['sha256'] == original_manifest['artifacts']['gold.json']['sha256'],
                'Replay gold differs from original formal gold')
        parameters = json.loads((args.report.parent / 'arguments.json').read_text(encoding='utf-8'))
        require(parameters['ef_search'] == args.ef_search, 'Replay must retain original formal query ef_search')
        schema, selected = select_existing(source, original_manifest)
        bench.write_json(args.output / 'source-report.json', source)
        bench.write_json(args.output / 'input-manifest.json', manifest)
        bench.write_json(args.output / 'source-arguments.json', parameters)
        output_report.update(source_report_sha256=bench.sha256(args.report), schema=schema,
            base_count=manifest['base_count'], dimension=manifest['dimension'],
            query_count=manifest['query_count'], k=manifest['k'],
            recall_targets=manifest['recall_targets'], ef_search=args.ef_search,
            required_recall_threshold=args.recall_threshold)
        sql = bench.SQL(args, args.output)
        version = sql.execute('SELECT VERSION(),@@session.ob_hnsw_ef_search')
        bench.write_json(args.output / 'version.json', version)
        require(int(version[0][1]) == args.ef_search, 'Actual replay session ef_search mismatch')
        for backend in ('cpu', 'gpu'):
            original = selected[backend]
            table_name, index_name = original['table'], original['index']
            table = bench.ident(schema) + '.' + bench.ident(table_name)
            folder = args.output / backend
            folder.mkdir()
            row = {'backend': backend, 'source_round': original['round'], 'table': table_name,
                'index': index_name, 'status': 'running'}
            output_report['backends'].append(row)
            bench.write_json(args.output / 'report.json', output_report)
            count = sql.execute('SELECT COUNT(*), MIN(c1), MAX(c1) FROM ' + table)
            require(tuple(map(int, count[0])) == (manifest['base_count'], 0, manifest['base_count'] - 1),
                    'Persisted table is incomplete or has changed ID range: ' + backend)
            bench.write_json(folder / 'table-count.json', count)
            def query(qid):
                return ('SELECT c1 FROM %s ORDER BY l2_distance(c2,%s) APPROXIMATE LIMIT %d' %
                    (table, bench.vector_literal(texts['query'][qid]), manifest['k']))
            plan = sql.execute('EXPLAIN ' + query(0))
            bench.write_json(folder / 'explain.json', plan)
            require(target_in_plan(plan, table_name, index_name),
                    'Replay EXPLAIN does not use requested VECTOR INDEX SCAN: ' + backend)
            predictions, durations = {}, []
            for qid in range(manifest['query_count']):
                started = time.monotonic()
                result = sql.execute(query(qid))
                durations.append(time.monotonic() - started)
                predictions[str(qid)] = [int(cells[0]) for cells in result]
            bench.write_json(folder / 'predictions.json', predictions)
            bench.write_json(folder / 'query-times-sec.json', durations)
            row['recall'] = bench.recall(gold, predictions, manifest['k'], manifest['base_count'], manifest['query_count'])
            bench.write_json(folder / 'recall.json', row['recall'])
            require(row['recall']['pooled'] >= args.recall_threshold, 'Replay recall below threshold: ' + backend)
            row.update(status='replay_passed', plan_uses_requested_vector_index=True,
                       complete_query_ids_verified=True, persisted_row_count=manifest['base_count'])
        output_report['status'] = 'replay_passed_requires_separate_restart_receipt'
    except BaseException as caught:
        error = caught
        output_report.update(status='failed', error=type(caught).__name__ + ': ' + str(caught))
        (args.output / 'failure.txt').write_text(traceback.format_exc(), encoding='utf-8')
    finally:
        output_report['finished_utc'] = bench.utc()
        bench.write_json(args.output / 'report.json', output_report)
        if sql:
            sql.close()
    print(json.dumps({'status': output_report['status'], 'report': str(args.output / 'report.json'),
        'restart_verified_by_this_script': False,
        'recall': {row['backend']: row.get('recall') for row in output_report['backends']}}))
    if error:
        raise error


if __name__ == '__main__':
    main()
