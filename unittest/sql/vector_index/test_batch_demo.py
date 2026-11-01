import importlib.util
import ast
import json
from types import SimpleNamespace
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch


root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('batch_demo',root/'notebooks/seekdb-hipvs-batch-demo/lib/seekdb_demo.py')
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)
verify_spec = importlib.util.spec_from_file_location('course_path_checks', root/'notebooks/seekdb-hipvs-batch-demo/verify_course_paths.py')
course_checks = importlib.util.module_from_spec(verify_spec)
verify_spec.loader.exec_module(course_checks)


def original_notebook_runtime():
    path = root/'notebooks/seekdb-hipvs-batch-demo/original-file/seekdb_hipvs_batch_demo_executed.ipynb'
    notebook = json.loads(path.read_text(encoding='utf-8'))
    code = ''.join(notebook['cells'][2]['source']).replace('%matplotlib inline', '')
    tree = ast.parse(code)
    source = next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                  and any(isinstance(target, ast.Name) and target.id == '_COURSE_RUNTIME_FIXES' for target in node.targets))
    namespace = dict(vars(demo))
    exec(compile(source, 'course_notebook_runtime_fixes', 'exec'), namespace)
    return namespace


class BatchDemoTest(unittest.TestCase):
    def test_original_notebook_repair_reports_sql_errors(self):
        runtime = original_notebook_runtime()
        error = subprocess.CompletedProcess(['mysql'], 1, stdout='', stderr='ERROR: query failed')
        with patch.object(subprocess, 'run', return_value=error):
            with self.assertRaisesRegex(RuntimeError, 'query failed'):
                runtime['sql']('select 1', timed=True)

    def test_original_notebook_repair_checks_missing_probe(self):
        runtime = original_notebook_runtime()
        runtime['sql_rows'] = lambda query: []
        with self.assertRaisesRegex(RuntimeError, 'section 3'):
            runtime['single_query']('t_cuvs')

    def test_original_notebook_does_not_replace_foreign_server(self):
        runtime = original_notebook_runtime()
        with patch('pathlib.Path.is_file', return_value=True), patch('pathlib.Path.iterdir', return_value=[]), \
             patch('socket.create_connection'), patch.object(subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError, 'belongs to another server'):
                runtime['start_observer']('/workspace/run/obs1')
            spawn.assert_not_called()

    def test_agreement_rejects_missing_or_duplicate_neighbors(self):
        for rows in ([], [('0', '1')], [('0', '1'), ('0', '1')]):
            with self.subTest(rows=rows):
                with self.assertRaisesRegex(RuntimeError, 'exactly k distinct'):
                    course_checks.group_results(rows, probes=1, k=2)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected probe'):
            course_checks.group_results([('1', '2')], probes=1, k=1)

    def test_agreement_reports_different_results_instead_of_success(self):
        result = course_checks.agreement({0: {1, 2}, 1: {3, 4}}, {0: {1, 2}, 1: {3, 5}})
        self.assertEqual(result['identical_topk_sets'], 1)
        self.assertEqual(result['mean_topk_overlap'], 0.75)
        for left, right in (({}, {}), ({0: set()}, {0: set()}), ({0: {1}}, {1: {1}})):
            with self.assertRaises(RuntimeError):
                course_checks.agreement(left, right)

    def test_missing_probe_has_actionable_error(self):
        with patch.object(demo,'sql_rows',return_value=[]):
            with self.assertRaisesRegex(RuntimeError,'section 3'):
                demo.single_query('t_cuvs')
            with self.assertRaisesRegex(RuntimeError,'section 3'):
                demo.wait_index_ready(floor=0,timeout=0)

    def test_index_timeout_is_not_ready(self):
        with patch.object(demo,'_probe_vector',return_value='[0,0,0]'):
            with self.assertRaisesRegex(TimeoutError,'did not return stable'):
                demo.wait_index_ready(floor=0,timeout=0)

    def test_startup_cell_imports_path_without_previous_cell(self):
        notebook=json.loads((root/'notebooks/seekdb-hipvs-batch-demo/seekdb_hipvs_batch_demo.ipynb').read_text(encoding='utf-8'))
        startup=''.join(notebook['cells'][7]['source'])
        # A failed server start must be reported as such, never NameError(Path).
        with self.assertRaisesRegex(RuntimeError,'observer failed to start'):
            exec(startup,{'sk':SimpleNamespace(start_observer=lambda base:False)})

    def test_out_of_order_result_cells_explain_prerequisites(self):
        notebook=json.loads((root/'notebooks/seekdb-hipvs-batch-demo/seekdb_hipvs_batch_demo.ipynb').read_text(encoding='utf-8'))
        for index in (10,12,13,15,17,19):
            with self.subTest(cell=index):
                with self.assertRaisesRegex(RuntimeError,'prerequisites missing'):
                    exec(''.join(notebook['cells'][index]['source']),{'sk':demo})

    def test_failed_sweep_does_not_keep_old_results(self):
        notebook=json.loads((root/'notebooks/seekdb-hipvs-batch-demo/seekdb_hipvs_batch_demo.ipynb').read_text(encoding='utf-8'))
        def failed_sweep(*args):
            raise RuntimeError('sweep failed')
        state={'sk':SimpleNamespace(require_notebook_state=demo.require_notebook_state,sweep=failed_sweep),
               '_DATA_READY':True,'PROBE_SIZES':(100,),'df':'old result','score':'old score'}
        with self.assertRaisesRegex(RuntimeError,'sweep failed'):
            exec(''.join(notebook['cells'][12]['source']),state)
        self.assertNotIn('df',state)
        self.assertNotIn('score',state)

    def test_failed_data_setup_invalidates_previous_run(self):
        notebook=json.loads((root/'notebooks/seekdb-hipvs-batch-demo/seekdb_hipvs_batch_demo.ipynb').read_text(encoding='utf-8'))
        state={'sk':SimpleNamespace(setup_data=lambda *args:(False,'load failed')),
               '_DATA_READY':True,'df':'old result','score':'old score'}
        with self.assertRaisesRegex(AssertionError,'data load failed'):
            exec(''.join(notebook['cells'][8]['source']),state)
        self.assertNotIn('_DATA_READY',state)
        self.assertNotIn('df',state)
        self.assertNotIn('score',state)

    def test_timed_sql_failure_cannot_be_a_speedup(self):
        failure = subprocess.CompletedProcess(['mysql'],1,stdout='',stderr='ERROR 4012: query timeout')
        with patch.object(demo.subprocess,'run',return_value=failure):
            with self.assertRaisesRegex(RuntimeError,'query timeout'):
                demo.sql('select 1',timed=True)

    def test_partial_batch_results_are_rejected(self):
        for call in (demo.cpu_lateral,demo.gpu_batch):
            with self.subTest(backend=call.__name__):
                with patch.object(demo,'sql',return_value=(0.5,0)),patch.object(demo,'sql_rows',return_value=[['999']]):
                    with self.assertRaisesRegex(RuntimeError,'Incomplete batch'):
                        call(100,k=10)

    def test_successful_batch_keeps_the_measured_sql_time(self):
        for call in (demo.cpu_lateral,demo.gpu_batch):
            with self.subTest(backend=call.__name__):
                with patch.object(demo,'sql',return_value=(0.5,0)),patch.object(demo,'sql_rows',return_value=[['1000']]):
                    self.assertEqual(call(100,k=10),0.5)


if __name__=='__main__':
    unittest.main()
