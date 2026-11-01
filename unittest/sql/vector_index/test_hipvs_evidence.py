"""Reject tampered/incomplete recorded results without requiring ROCm."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "hipvs_evidence", ROOT / "docs/gpu-hnsw-build/verify_evidence.py")
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


class HipvsEvidenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.recorded = json.loads((ROOT / "docs/gpu-hnsw-build/results/sql-50000x128.json").read_text(encoding="utf-8"))

    def setUp(self):
        self.report = copy.deepcopy(self.recorded)

    def test_recorded_results_recompute(self):
        medians, speedup = evidence.verify_runs(self.report)
        self.assertAlmostEqual(speedup, 8.207957197102173)
        self.assertGreater(medians["vsag"], medians["hipvs"])

    def test_changed_median_or_speedup_is_rejected(self):
        self.report["speedup"] = 999
        with self.assertRaisesRegex(ValueError, "speedup"):
            evidence.verify_runs(self.report)

    def test_missing_round_is_rejected(self):
        self.report["runs"].pop()
        with self.assertRaisesRegex(ValueError, "build rounds"):
            evidence.verify_runs(self.report)

    def test_low_filter_recall_is_rejected(self):
        self.report["dml_and_filter"]["filter_recall_at_10"] = 0
        with self.assertRaisesRegex(ValueError, "filtered recall"):
            evidence.verify_runs(self.report)

    def test_wrong_dataset_gpu_event_is_rejected(self):
        run = next(r for r in self.report["runs"] if r["backend"] == "hipvs")
        run["gpu_events"][0] = run["gpu_events"][0].replace("rows=50000", "rows=64")
        with self.assertRaisesRegex(ValueError, "dataset parameter rows"):
            evidence.verify_runs(self.report)

    def test_failed_restart_cannot_reuse_previous_recall(self):
        self.report["restart_verification"]["status"] = "failed"
        with self.assertRaisesRegex(ValueError, "restart verification"):
            evidence.verify_runs(self.report)

    def test_nonfinite_recall_is_rejected(self):
        self.report["runs"][0]["per_query_recall"][0] = float('nan')
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            evidence.verify_runs(self.report)


if __name__ == "__main__":
    unittest.main()
