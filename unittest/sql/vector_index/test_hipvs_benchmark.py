"""CPU-only regression checks for the GPU build benchmark's result contract."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "hipvs_benchmark", ROOT / "docs/gpu-hnsw-build/benchmark.py")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


class HipvsBenchmarkTest(unittest.TestCase):
    def setUp(self):
        self.expected = set(range(3, 33, 3))
        self.valid_ids = set(range(1, 121))

    def test_filter_passes_only_after_quality_checks(self):
        self.assertEqual(benchmark.validate_filter_result(
            list(self.expected), self.expected, self.valid_ids, 0.99), 1.0)

    def test_filter_low_recall_is_a_failure(self):
        got = list(self.expected - {30}) + [33]
        with self.assertRaisesRegex(RuntimeError, "below the local floor"):
            benchmark.validate_filter_result(got, self.expected, self.valid_ids, 0.99)

    def test_filter_rejects_unknown_and_unfiltered_ids(self):
        for invalid in (999, 31):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(RuntimeError, "invalid or unfiltered"):
                benchmark.validate_filter_result(
                    list(self.expected - {30}) + [invalid], self.expected, self.valid_ids, 0)

    def test_filter_rejects_duplicate_or_missing_results(self):
        for got in ([3] * 10, list(self.expected)[:-1], []):
            with self.subTest(got=got), self.assertRaisesRegex(RuntimeError, "missing or duplicate"):
                benchmark.validate_filter_result(got, self.expected, self.valid_ids, 0)

    def test_restart_failure_clears_previous_success_and_preserves_build(self):
        report = {"configuration": {"data_sha256": "dataset"}, "completed": True,
                  "restart_verification": {"recall_at_10": 1.0}}
        benchmark.begin_restart_verification(report, "dataset")
        self.assertFalse(report["completed"])
        self.assertEqual(report["restart_verification"], {"status": "running"})
        benchmark.record_failure(report, RuntimeError("restart lost rows"), restart=True)
        self.assertFalse(report["completed"])
        self.assertTrue(report["build_completed"])
        self.assertEqual(report["restart_verification"]["status"], "failed")
        self.assertNotIn("recall_at_10", report["restart_verification"])
        benchmark.begin_restart_verification(report, "dataset")
        self.assertNotIn("error", report)
        self.assertEqual(report["restart_verification"]["status"], "running")

    def test_restart_rejects_incomplete_campaign_or_different_dataset(self):
        report = {"configuration": {"data_sha256": "dataset"}, "completed": False}
        with self.assertRaisesRegex(RuntimeError, "successful build campaign"):
            benchmark.begin_restart_verification(report, "dataset")
        with self.assertRaisesRegex(RuntimeError, "different dataset"):
            benchmark.begin_restart_verification(report, "other")

    def test_failure_cannot_leave_campaign_completed(self):
        report = {"completed": True}
        benchmark.record_failure(report, RuntimeError("filter check failed"))
        self.assertFalse(report["completed"])
        self.assertEqual(report["error"], "filter check failed")


if __name__ == "__main__":
    unittest.main()
