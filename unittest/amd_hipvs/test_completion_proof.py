"""Synthetic evidence-contract checks; never a real SQL/GPU measurement."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools/benchmark/amd_hnsw"))
import struct
import tempfile
import unittest
from unittest import mock

import completion_proof as adapter
import sql_benchmark as benchmark


HERE = Path(__file__).resolve().parent


def dump(path, value):
    path.write_text(json.dumps(value) + '\n', encoding='utf-8')


class CompletionProofTests(unittest.TestCase):
    def fixture(self, backend='gpu', bad=None):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        output = root / ('00-' + backend)
        output.mkdir()
        table = backend + '_r00'
        index = 'ix_' + table
        vectors = {'base': [[1.0, -0.0], [2.0, 3.0], [4.0, 5.0]],
                   'query': [[1.0, 0.0], [4.0, 5.0]]}
        hashes = {label: hashlib.sha256(b''.join(struct.pack('<2f', *v) for v in rows)).hexdigest()
                  for label, rows in vectors.items()}
        manifest = {'status': 'complete', 'metric': 'l2', 'base_count': 3,
            'query_count': 2, 'dimension': 2, 'k': 2, 'recall_targets': 4,
            'inputs': {label: {'shape': [len(rows), 2], 'sha256': hashes[label]}
                       for label, rows in vectors.items()}}
        dump(root / 'input-manifest.json', manifest)
        parameters = {'m': 16, 'ef_construction': 200, 'ef_search': 512}
        dump(root / 'arguments.json', parameters)
        dump(root / 'version.json', [['synthetic-test-only', 512]])
        row = {'round': 0, 'backend': backend, 'table': table, 'index': index,
            'raw_data_ready_before_timer': True, 'target_index_absent_before_timer': True,
            'build_started_utc': '2026-10-28T08:00:00+00:00'}
        for label, rows in vectors.items():
            (output / (label + '-readback.jsonl')).write_text(''.join(
                json.dumps({'id': number, 'vector': json.dumps(vector)}) + '\n'
                for number, vector in enumerate(rows)), encoding='utf-8')
            row['raw_data_readback' if label == 'base' else 'query_data_readback'] = {
                'rows': len(rows), 'float32_values': 2 * len(rows), 'bitwise_equal': True,
                'readback_float32_sha256': hashes[label]}
        dump(root / 'report.json', {'schema': 'test107', 'rounds': [row]})
        dump(output / 'indexes-before.json', [])
        query = "SELECT c1 FROM `test107`.`%s` ORDER BY l2_distance(c2,'[1.0,0.0]') APPROXIMATE LIMIT 2" % table
        statements = [
            'CREATE VECTOR INDEX `%s` ON `test107`.`%s`(c2) WITH (distance=l2,type=hnsw,lib=%s,m=16,ef_construction=200,ef_search=512)' %
            (index, table, 'vsag' if backend == 'cpu' else 'cuvs'), query, 'EXPLAIN ' + query]
        results = [[], [[0], [1]], [['VECTOR INDEX SCAN', '%s(%s)' % (table, index)]]]
        events = []
        for number, (statement, result) in enumerate(zip(statements, results), 1):
            stem = root / ('sql-%06d' % number)
            stem.with_suffix('.sql').write_text('SELECT @@session.ob_hnsw_ef_search;\n' + statement + ';\n', encoding='utf-8')
            dump(stem.with_suffix('.rows.json'), result)
            dump(stem.with_suffix('.session.json'), {'matches': True, 'requested_ef_search': 512,
                 'actual_session_ob_hnsw_ef_search': 512})
            events.append({'number': number, 'sql': stem.name + '.sql', 'status': 'ok',
                'started_utc': '2026-10-28T08:00:0%d+00:00' % number,
                'actual_session_ob_hnsw_ef_search': 512})
        (root / 'sql-events.jsonl').write_text(''.join(json.dumps(x) + '\n' for x in events), encoding='utf-8')
        dump(output / 'first-query.json', results[1])
        dump(output / 'explain.json', results[2])
        prefix = '[seekdb_hipvs_hnsw] '
        sequence = [
            prefix + 'event=build_start n=3 dim=2 metric=L2Expanded hierarchy=gpu hnsw_m=16 search_ef=512 ef_construction=200 fallback=disabled',
            prefix + 'event=gpu_cagra_complete elapsed_ms=1.0',
            prefix + 'event=build_complete backend=cagra-to-hnsw hierarchy=gpu search_backend=cpu-hnsw fallback=disabled',
            prefix + 'event=serialize_complete format=SKHNSW01 payload_bytes=128 hnsw_data=included target=caller-proc-fd']
        if bad == 'missing_conversion':
            sequence.pop(2)
        elif bad == 'partial_n':
            sequence[0] = sequence[0].replace('n=3 ', 'n=2 ')
        elif bad == 'actual_fallback':
            sequence.append('[seekdb-cuvs] bridge unavailable; CPU fallback: missing')
        elif bad == 'regular_snapshot':
            sequence[-1] = sequence[-1].replace('caller-proc-fd', 'regular-path')
        elif bad == 'repeated_build':
            sequence = sequence + sequence
        log = root / 'observer.stderr'
        prior = '[seekdb_hipvs_hnsw] event=error stage=prior_test\n'
        log.write_text(prior + ('\n'.join(sequence) + '\n' if backend == 'gpu' else ''), encoding='utf-8')
        starts = [{'path': str(log), 'offset': len(prior.encode('utf-8')), 'inode': log.stat().st_ino}]
        if bad == 'no_new_sequence':
            starts[0]['offset'] = log.stat().st_size
        elif bad == 'wrong_inode':
            starts[0]['inode'] += 1
        dump(output / 'server-log-starts.json', starts)
        environment = root / 'environment.json'
        dump(environment, {'synthetic_only': 'gfx1100 AMD Radeon GPU'})
        library = root / 'synthetic-library-not-real-gpu.so'
        library.write_bytes(b'synthetic contract-validation bytes only')
        args = argparse.Namespace(schema='test107', table=table, index=index,
            backend=backend, round=0, base_count=3, output=output,
            ddl_start_utc=row['build_started_utc'],
            bridge_source=HERE.parents[1] / 'tools/hipvs_bridge/seekdb_cuvs_bridge.c',
            bridge_library=library, gpu_environment_file=environment, observer_pid=None)
        if bad == 'bad_readback':
            path = output / 'base-readback.jsonl'
            path.write_text(path.read_text(encoding='utf-8').replace('2.0, 3.0', '2.0, 3.1'), encoding='utf-8')
        elif bad == 'session_64':
            events[1]['actual_session_ob_hnsw_ef_search'] = 64
            (root / 'sql-events.jsonl').write_text(''.join(json.dumps(x) + '\n' for x in events), encoding='utf-8')
        elif bad == 'failed_ddl':
            events[0]['status'] = 'failed'
            (root / 'sql-events.jsonl').write_text(''.join(json.dumps(x) + '\n' for x in events), encoding='utf-8')
        elif bad == 'wrong_plan':
            dump(output / 'explain.json', [['TABLE FULL SCAN', table]])
            dump(root / 'sql-000003.rows.json', [['TABLE FULL SCAN', table]])
        elif bad == 'stale_sql':
            events[0]['started_utc'] = '2026-10-28T07:59:59+00:00'
            (root / 'sql-events.jsonl').write_text(''.join(json.dumps(x) + '\n' for x in events), encoding='utf-8')
        return args

    def test_gpu_full_sequence_and_exact_receipts(self):
        proof = adapter.prove(self.fixture())
        self.assertTrue(proof['gpu_build_participation'])
        self.assertEqual(proof['indexed_rows'], 3)
        self.assertFalse(proof['gpu_build_evidence']['profiled_kernel_names_available'])
        self.assertNotIn('build_kernel_names', proof)
        for evidence in proof['evidence']:
            self.assertEqual(adapter.sha256(evidence['path']), evidence['sha256'])

    def test_cpu_successful_ddl_and_target_plan(self):
        proof = adapter.prove(self.fixture('cpu'))
        self.assertTrue(proof['hnsw_semantics'])
        self.assertFalse(proof['cpu_fallback'])
        self.assertNotIn('gpu_build_evidence', proof)

    def test_invalid_evidence_rejected(self):
        for bad in ['missing_conversion', 'partial_n', 'actual_fallback', 'regular_snapshot',
                    'repeated_build', 'no_new_sequence', 'wrong_inode', 'bad_readback',
                    'session_64', 'failed_ddl', 'wrong_plan', 'stale_sql']:
            with self.subTest(case=bad):
                with self.assertRaises(ValueError):
                    adapter.prove(self.fixture(bad=bad))

    def test_harness_valid_api_proof_and_empty_gpu_object(self):
        args = self.fixture()
        proof = adapter.prove(args)
        context = {'schema': args.schema, 'table': args.table, 'index': args.index,
                   'backend': 'gpu', 'base_count': 3}
        def run_with(candidate):
            result = argparse.Namespace(returncode=0, stdout=json.dumps(candidate), stderr='')
            with mock.patch.object(benchmark.subprocess, 'run', return_value=result):
                return benchmark.completion_proof({'completion_command': ['synthetic-test']},
                                                  context, args.output, 1)
        self.assertEqual(run_with(proof)['indexed_rows'], 3)
        proof['gpu_build_evidence'] = {}
        with self.assertRaises(ValueError):
            run_with(proof)


if __name__ == '__main__':
    unittest.main(verbosity=2)
