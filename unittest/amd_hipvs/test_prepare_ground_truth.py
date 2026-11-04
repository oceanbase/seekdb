"""Tiny independent known-answer tests; never downloads or runs a large corpus."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools/benchmark/amd_hnsw"))
import importlib.util
import json
import tempfile
import unittest

import numpy as np

from prepare_ground_truth import prepare, write_sql_vectors

ROOT = Path(__file__).resolve().parent


class GroundTruthTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'selftest').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'selftest')
        self.root = Path(self.temp.name)
        self.base = self.root / 'base.f32'
        self.query = self.root / 'query.f32'
        np.array([[-1,0],[1,0],[0,2],[0,-2],[0,0]],dtype='<f4').tofile(self.base)
        np.array([[0,0],[0,2],[0,-2]],dtype='<f4').tofile(self.query)

    def tearDown(self):
        self.temp.cleanup()

    def run_gt(self, **overrides):
        options = dict(base_path=self.base, query_path=self.query, dimension=2,
                       base_count=5, query_count=3, k=3, output=self.root/'result',
                       metric='l2', block_rows=2)
        options.update(overrides)
        return prepare(**options)

    def test_known_answers_ties_partial_blocks_and_k_equal_n(self):
        manifest = self.run_gt()
        gold = json.loads((self.root/'result/gold.json').read_text())
        self.assertEqual(gold, {'0':[4,0,1], '1':[2,4,0], '2':[3,4,0]})
        self.assertEqual(manifest['recall_targets'],9)
        self.assertEqual(manifest['sql_text']['values_checked'],{'base':10,'query':6})
        self.run_gt(k=5,output=self.root/'all')
        self.assertEqual(len(json.loads((self.root/'all/gold.json').read_text())['2']),5)

    def test_truncated_and_trailing_bytes_rejected_before_output(self):
        original=self.base.read_bytes()
        for suffix,data in [('short',original[:-1]),('extra',original+b'\x00')]:
            self.base.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'expected exactly'):
                self.run_gt(output=self.root/suffix)
            self.assertFalse((self.root/suffix).exists())

    def test_nan_in_last_base_row_and_inf_query_rejected(self):
        values=np.array([[-1,0],[1,0],[0,2],[0,-2],[0,np.nan]],dtype='<f4')
        values.tofile(self.base)
        with self.assertRaisesRegex(ValueError,'NaN/Inf'):
            self.run_gt()
        values[-1,-1]=0
        values.tofile(self.base)
        np.array([[0,0],[0,2],[np.inf,0]],dtype='<f4').tofile(self.query)
        with self.assertRaisesRegex(ValueError,'NaN/Inf'):
            self.run_gt()

    def test_invalid_k_counts_dimension_block_and_metric_rejected(self):
        for options in [dict(k=0),dict(k=6),dict(query_count=0),dict(base_count=-1),
                        dict(dimension=0),dict(block_rows=0),dict(metric='cosine')]:
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.run_gt(**options)

    def test_declared_query_count_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError,'expected exactly'):
            self.run_gt(query_count=2)

    def test_nine_digits_roundtrip_signed_zero_subnormal_and_extremes(self):
        values=np.array([[-0.0,0.0], [np.nextafter(np.float32(0),np.float32(1)),
                                    np.finfo(np.float32).max],
                         [np.nextafter(np.float32(1),np.float32(2)),np.float32(-1/3)]],dtype='<f4')
        output=self.root/'precision.txt'
        self.assertEqual(write_sql_vectors(values,output),6)
        restored=np.array([json.loads(line) for line in output.read_text().splitlines()],dtype='<f4')
        np.testing.assert_array_equal(restored.view('<u4'),values.view('<u4'))
        self.assertIn('-0.00000000e+00',output.read_text())

    def test_sql_full_query_denominator_and_missing_query_rejected(self):
        self.run_gt()
        gold=json.loads((self.root/'result/gold.json').read_text())
        import sql_benchmark as module
        predictions={**gold,'2':[0,1,2]}
        result=module.recall(gold,predictions,3,5,3)
        self.assertEqual((result['query_count'],result['hits'],result['targets']),(3,7,9))
        self.assertAlmostEqual(result['pooled'],7/9)
        with self.assertRaisesRegex(ValueError,'Missing/extra query IDs'):
            module.recall(gold,{'0':gold['0'],'1':gold['1']},3,5,3)


if __name__=='__main__':
    unittest.main(verbosity=2)
