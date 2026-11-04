"""Read-only independent review of original AMD recovery/replay receipts."""
from pathlib import Path, PurePosixPath
import ast
import hashlib
import json
import re

ROOT = Path('D:/ob2026_plan/runs/amd107/cloud-persisted108')
FORMAL = Path('D:/ob2026_plan/runs/amd107/cloud-final-formal')
OUT = Path('D:/ob2026_plan/validation/amd-build107/persistence-review108.json')
sha = lambda data: hashlib.sha256(data).hexdigest()
read = lambda path: json.loads(path.read_text(encoding='utf-8'))
manifest = read(ROOT / 'export-manifest.json')
index = {row['path']: row for row in manifest['files']}
assert len(index) == len(manifest['files']) == 871
assert manifest['snapshot_stable_during_export'] and not manifest['warnings']
assert manifest['root'] == '/workspace/amd107'
for name, row in index.items():
    path = PurePosixPath(name)
    assert not path.is_absolute() and '..' not in path.parts and ':' not in name
    assert row['compact_included']
    data = (ROOT / name).read_bytes()
    assert len(data) == row['size'] and sha(data) == row['sha256'], name

launch = read(ROOT / 'candidate-persisted-launch.json')
original = read(ROOT / 'candidate-quality-launch.json')
assert (ROOT / 'candidate-quality-launch.json').read_bytes() == (FORMAL / 'candidate-quality-launch.json').read_bytes()
assert original['pid'] == launch['original_pid'] == 14001
assert launch['pid'] == 33736 and launch['pid'] != original['pid'] and launch['sql_ready']
assert launch['original_launch'] == '/workspace/amd107/candidate-quality-launch.json'
for key in ('argv', 'settings', 'executable_sha256', 'bridge_sha256'):
    assert launch[key] == original[key], key
assert launch['argv_reused_exactly'] and launch['saved_settings_reused_exactly']
assert launch['argv'][0] == '/workspace/amd107/runtime108/observer'
assert '--base-dir=/workspace/amd107/candidate-quality64' in launch['argv']
assert launch['database_path'] == '/workspace/amd107/candidate-quality64'
assert not launch['database_directory_deleted_or_created'] and not launch['index_ddl_issued']
assert not launch['cloud_instance_restarted'] and not launch['new_cloud_instance_created']
for key in ('old_process_before_wait', 'old_process_at_recovery_start'):
    observed = launch[key]
    assert observed['pid'] == original['pid'] and observed['fully_exited_or_zombie_only']
    assert not observed['active_threads']
    assert all(thread['state'] in ('Z', 'X', 'x') for thread in observed['threads'])
assert not launch['signals_sent'] and not launch['port2882_listeners_before_start']
assert launch['port2882_connects_before_start'] is False
assert launch['complete_original_process_environment_reused'] is False
assert launch['historical_database_inode_continuity_proven'] is False
assert launch['environment_source'] == 'current shell environment overlaid with exact saved launch settings'
assert "base dir. path='/workspace/amd107/candidate-quality64/'" in (ROOT / 'candidate-persisted.out').read_text()
recovery_source = (ROOT / 'recover_persisted.py').read_text()
ast.parse(recovery_source)
assert 'env = os.environ.copy()' in recovery_source and 'env.update(settings)' in recovery_source
assert 'os.kill(' not in recovery_source and '.mkdir(' not in recovery_source

folder = ROOT / 'persisted-replay100k'
report, args, data = [read(folder / name) for name in ('report.json', 'arguments.json', 'input-manifest.json')]
source = FORMAL / 'quality64-formal100k-warn'
source_report, source_args, source_data = [read(source / name) for name in ('report.json', 'arguments.json', 'input-manifest.json')]
assert (folder / 'source-report.json').read_bytes() == (source / 'report.json').read_bytes()
assert read(folder / 'source-arguments.json') == source_args
assert data == source_data
assert report['source_report_sha256'] == sha((source / 'report.json').read_bytes())
assert report['script_sha256'] == sha((ROOT / 'replay_existing.py').read_bytes())
assert report['status'] == 'replay_passed_requires_separate_restart_receipt'
assert report['read_only_database_replay'] is True and report['restart_verified_by_this_script'] is False
assert launch['started_utc'] < report['started_utc'] < report['finished_utc']
assert (report['base_count'], report['dimension'], report['query_count'], report['k']) == (100000, 128, 100, 10)
assert args['ef_search'] == report['ef_search'] == source_args['ef_search'] == 1000
assert report['schema'] == source_report['schema']
gold = read(FORMAL / 'gt100k/gold.json')
assert sha((FORMAL / 'gt100k/gold.json').read_bytes()) == data['artifacts']['gold.json']['sha256']
assert set(gold) == {str(qid) for qid in range(100)}

events = [json.loads(line) for line in (folder / 'sql-events.jsonl').read_text().splitlines()]
assert len(events) == 205 and [row['number'] for row in events] == list(range(1, 206))
raw_sql = {}
raw_rows = {}
for event in events:
    number = event['number']
    stem = folder / ('sql-%06d' % number)
    sql = stem.with_suffix('.sql').read_text()
    session = read(stem.with_suffix('.session.json'))
    rows = read(stem.with_suffix('.rows.json'))
    assert event['sql'] == stem.name + '.sql'
    assert event['status'] == 'ok' and event['row_count'] == len(rows)
    assert event['actual_session_ob_hnsw_ef_search'] == 1000
    assert event['session_init_command'] == session['init_command'] == 'SET SESSION ob_hnsw_ef_search=1000'
    assert session['matches'] and session['requested_ef_search'] == session['actual_session_ob_hnsw_ef_search'] == 1000
    assert report['started_utc'] <= event['started_utc'] <= report['finished_utc']
    assert not stem.with_suffix('.stderr.txt').read_bytes()
    statements = [value.strip() for value in sql.split(';') if value.strip()]
    assert len(statements) == 2 and statements[0] == 'SELECT @@session.ob_hnsw_ef_search'
    assert statements[1].startswith(('SELECT ', 'EXPLAIN SELECT '))
    assert not re.search(r'\b(CREATE|ALTER|DROP|INSERT|UPDATE|DELETE|TRUNCATE|REPLACE|CALL)\b', statements[1], re.I)
    raw_sql[number], raw_rows[number] = statements[1], rows
assert raw_sql[1] == 'SELECT VERSION(),@@session.ob_hnsw_ef_search'
assert read(folder / 'version.json') == raw_rows[1]
assert raw_rows[1] == [[launch['version'], '1000']]

backends = []
query_vectors = None
for backend, count_number in (('cpu', 2), ('gpu', 104)):
    row = next(item for item in report['backends'] if item['backend'] == backend)
    original_row = next(item for item in source_report['rounds'] if item['backend'] == backend and item['round'] == 2)
    assert row['source_round'] == 2 and row['status'] == 'replay_passed'
    assert (row['table'], row['index']) == (original_row['table'], original_row['index'])
    table, index_name = row['table'], row['index']
    qualified_table = '`%s`.`%s`' % (report['schema'], table)
    assert raw_sql[count_number] == 'SELECT COUNT(*), MIN(c1), MAX(c1) FROM ' + qualified_table
    assert read(folder / backend / 'table-count.json') == raw_rows[count_number] == [['100000', '0', '99999']]
    plan = read(folder / backend / 'explain.json')
    assert plan == raw_rows[count_number + 1]
    plan_text = json.dumps(plan)
    assert 'VECTOR INDEX SCAN' in plan_text and table + '(' + index_name + ')' in plan_text
    assert raw_sql[count_number + 1] == 'EXPLAIN ' + raw_sql[count_number + 2]
    predictions = read(folder / backend / 'predictions.json')
    assert set(predictions) == set(gold)
    vectors, hits_per_query = [], {}
    for qid in range(100):
        number = count_number + 2 + qid
        pattern = re.escape('SELECT c1 FROM ' + qualified_table) + r" ORDER BY l2_distance\(c2,'(\[[^']+\])'\) APPROXIMATE LIMIT 10"
        match = re.fullmatch(pattern, raw_sql[number])
        assert match, (backend, qid)
        vector = match[1]
        assert len(json.loads(vector)) == 128
        vectors.append(vector)
        ids = predictions[str(qid)]
        assert ids == [int(cells[0]) for cells in raw_rows[number]]
        assert len(ids) == len(set(ids)) == 10 and all(type(value) is int and 0 <= value < 100000 for value in ids)
        hits_per_query[str(qid)] = len(set(ids) & set(gold[str(qid)][:10]))
    if query_vectors is None:
        query_vectors = vectors
    else:
        assert query_vectors == vectors
    query_bytes = ('\n'.join(vectors) + '\n').encode()
    assert len(query_bytes) == data['artifacts']['query.sql-vectors.txt']['bytes']
    assert sha(query_bytes) == data['artifacts']['query.sql-vectors.txt']['sha256']
    recall = read(folder / backend / 'recall.json')
    assert recall == row['recall'] and recall['hits_per_query'] == hits_per_query
    assert sum(hits_per_query.values()) == recall['hits'] == recall['targets'] == 1000
    assert recall['pooled'] == recall['minimum_query'] == 1.0 and recall['query_count'] == 100
    assert len(read(folder / backend / 'query-times-sec.json')) == 100
    backends.append({'backend': backend, 'source_round': 2, 'table': table, 'index': index_name,
        'rows': 100000, 'min_id': 0, 'max_id': 99999, 'complete_queries': 100,
        'hits': 1000, 'targets': 1000, 'recall': 1.0, 'minimum_query_recall': 1.0,
        'plan_uses_original_vector_index': True, 'raw_query_sql_rows_predictions_bound': True})

output = {'status': 'PASS', 'scope': 'Independent manifest, saved-launch recovery and read-only original-GT replay review',
    'manifest_originals_sha_size_verified': len(index), 'omitted_originals': 0,
    'manifest_sha256': sha((ROOT / 'export-manifest.json').read_bytes()),
    'original_pid': original['pid'], 'new_pid': launch['pid'], 'existing_database_path': launch['database_path'],
    'same_saved_argv_settings_and_artifact_identity': True,
    'old_process_fully_stopped_port_free_before_recovery': True,
    'recovery_sends_no_signals': True, 'same_cloud_instance': True,
    'recovery_creates_no_database_directory_or_index_ddl': True,
    'historical_database_inode_continuity_proven': False,
    'complete_original_process_environment_reused': False,
    'environment_source': launch['environment_source'],
    'recovery_script_sha256': sha((ROOT / 'recover_persisted.py').read_bytes()),
    'source_formal_report_sha256': report['source_report_sha256'],
    'gold_sha256': data['artifacts']['gold.json']['sha256'],
    'query_text_reconstructed_sha256': sha(('\n'.join(query_vectors) + '\n').encode()),
    'read_only_sql_events_sessions_rows_checked': len(events),
    'sql_ddl_or_dml_issued': False, 'backends': backends,
    'initial_restart_refusal_original_diagnostic_in_packet': False,
    'initial_restart_refusal_history_source': 'operator report, separate from independently checked recovery receipt',
    'full_formal_archive_local_download_verified_by_this_review': False}
OUT.write_bytes((json.dumps(output, indent=2) + '\n').encode())
print(json.dumps(output, indent=2))
