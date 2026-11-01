"""Numeric correctness and rejection cases; these tests never claim GPU execution."""
import copy
import hashlib
import io
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import numpy as np

from benchmark import (assess_results, exact_neighbors, fingerprint, generate_data,
                       identifier, match_evidence, parse_options, query_sql,
                       summarize, validate_comparable, log_position, evidence_from_log,
                       first_query_when_ready, coverage_retry_settings, coverage_query_result,
                       run_round, safe_failure_detail, scan_log_interval)


class CoverageDiagnosticTests(unittest.TestCase):
    def test_ten_thousand_node_retry_reaches_requested_breadth(self):
        ef, k = coverage_retry_settings(10000, 200)
        self.assertEqual((ef, k), (10000, 1000))
        # Regression: LIMIT 1 had silently capped the purported full traversal
        # at 1000. This assertion uses the actual seekdb adaptor's constraint.
        self.assertLess(min(ef, max(1000, 10 * 1)), 10000)
        self.assertEqual(min(ef, max(1000, 10 * k)), 10000)

    def test_diagnostic_query_stays_within_input_and_sql_limits(self):
        for rows, configured, expected_effective in ((128, 200, 200), (2, 200, 200),
                                                    (10001, 200, 10001), (200000, 200, 160000),
                                                    (10000, 160000, 100000)):
            with self.subTest(rows=rows, configured=configured):
                ef, k = coverage_retry_settings(rows, configured)
                self.assertGreaterEqual(k, 1)
                self.assertLessEqual(k, min(rows, 16384))
                self.assertEqual(min(ef, max(1000, 10 * k)), expected_effective)
                self.assertGreaterEqual(expected_effective, min(rows, 160000))

    def test_self_match_requires_correct_first_id_and_zero_distance(self):
        doc_id = 2**53 + 99
        matched, detail = coverage_query_result([(doc_id, 0), (17, 1)], doc_id, 2, {doc_id, 17})
        self.assertTrue(matched)
        self.assertEqual(detail, {"returned_id": doc_id, "distance": 0.0})
        self.assertFalse(coverage_query_result([(17, .2), (doc_id, .4)], doc_id, 2, {doc_id, 17})[0])
        self.assertFalse(coverage_query_result([(doc_id, .01), (17, 1)], doc_id, 2, {doc_id, 17})[0])

    def test_wider_retry_rejects_invalid_rows_including_later_neighbors(self):
        for own in ([(17, 0)], [(17, 0), (99, 1)], [(17, 0), (17, 1)],
                    [(17, 0), (21.0, 1)], [(17, 0), (21, math.nan)]):
            with self.subTest(own=own), self.assertRaises(ValueError):
                coverage_query_result(own, 17, 2, {17, 21})


class IndexReadinessTests(unittest.TestCase):
    @patch('benchmark.time.sleep')
    def test_transient_first_query_restarts_snapshot_and_records_error(self, sleep):
        connection, cursor = Mock(), Mock()
        cursor.execute.side_effect = [Exception(4016, 'not visible'), None]
        cursor.fetchall.return_value = [(99, 0.0)]
        rows, failures = first_query_when_ready(connection, cursor, 'SQL', ('v', 'v'))
        self.assertEqual(rows, [(99, 0.0)])
        self.assertEqual(failures, [4016])
        connection.rollback.assert_called_once()
        sleep.assert_called_once_with(0.1)

    @patch('benchmark.time.sleep')
    def test_permanent_failure_exhausts_the_bound(self, sleep):
        connection, cursor = Mock(), Mock()
        cursor.execute.side_effect = Exception(4016, 'still unavailable')
        with self.assertRaises(Exception):
            first_query_when_ready(connection, cursor, 'SQL', (), max_retries=2)
        self.assertEqual(cursor.execute.call_count, 3)
        self.assertEqual(connection.rollback.call_count, 2)
        cursor.fetchall.assert_not_called()

    @patch('benchmark.time.sleep')
    def test_other_errors_are_not_retried(self, sleep):
        connection, cursor = Mock(), Mock()
        cursor.execute.side_effect = Exception(1064, 'syntax error')
        with self.assertRaises(Exception):
            first_query_when_ready(connection, cursor, 'SQL', ())
        connection.rollback.assert_not_called()
        sleep.assert_not_called()


class RawBuildEvidenceTests(unittest.TestCase):
    record = ('[seekdb-hipvs-build] backend=hipvs device_vendor=AMD '
              'gpu_name="AMD Radeon Graphics" arch=gfx1100 rows=100 dim=8 '
              'gpu_compute=true cpu_fallback=false export_completed=true\n')

    def test_only_new_record_matches_the_timed_index(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stderr.log"
            path.write_bytes(self.record.encode())
            start = log_position(path)
            with path.open("a", encoding="utf-8", newline="\n") as out:
                out.write(self.record)
            evidence = evidence_from_log(path, start, "table1", "idx1", 100, 8)
            self.assertEqual(evidence["table_name"], "table1")
            self.assertEqual(evidence["index_name"], "idx1")
            self.assertEqual(evidence["stderr_start"], len(self.record))
            self.assertEqual(evidence["raw_record"], self.record.rstrip())
            self.assertEqual(len(evidence["stderr_interval_sha256"]), 64)
            self.assertEqual(evidence["stderr_interval_bytes"], len(self.record))

    def test_missing_ambiguous_fallback_or_wrong_shape_is_rejected(self):
        for data in ("", self.record * 2, self.record.replace("fallback=false", "fallback=true"),
                     self.record.replace("rows=100", "rows=101"),
                     self.record.replace("arch=gfx1100", "arch=sm80")):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "stderr.log"
                path.touch()
                start = log_position(path)
                path.write_bytes(data.encode())
                with self.assertRaises(RuntimeError):
                    evidence_from_log(path, start, "t", "i", 100, 8)

    def test_log_truncation_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stderr.log"
            path.write_bytes((self.record * 2).encode())
            start = log_position(path)
            path.write_bytes(self.record.encode())
            with self.assertRaises(RuntimeError):
                evidence_from_log(path, start, "t", "i", 100, 8)

    @patch('benchmark.LOG_READ_CHUNK_BYTES', 11)
    def test_marker_and_line_cross_chunk_boundaries_without_hash_changes(self):
        data = b'unrelated log line\n' + self.record.encode() + b'last unrelated line'
        record, digest = scan_log_interval(io.BytesIO(data), len(data))
        self.assertEqual(record, self.record.rstrip())
        self.assertEqual(digest, hashlib.sha256(data).hexdigest())

    def test_short_reads_are_consumed_until_the_exact_interval_end(self):
        class ShortReader(io.BytesIO):
            def read(self, length):
                return super().read(min(length, 3))
        data = self.record.encode()
        record, digest = scan_log_interval(ShortReader(data + b'not in captured interval'), len(data))
        self.assertEqual(record, self.record.rstrip())
        self.assertEqual(digest, hashlib.sha256(data).hexdigest())

    def test_early_eof_is_rejected_even_after_a_complete_gpu_record(self):
        data = self.record.encode()
        with self.assertRaisesRegex(RuntimeError, 'truncated'):
            scan_log_interval(io.BytesIO(data), len(data) + 1)

    @patch('benchmark.LOG_READ_CHUNK_BYTES', 7)
    def test_duplicate_record_split_across_chunks_is_rejected(self):
        data = (self.record + 'unrelated\n' + self.record).encode()
        with self.assertRaisesRegex(RuntimeError, 'ambiguous'):
            scan_log_interval(io.BytesIO(data), len(data))

    @patch('benchmark.LOG_MAX_LINE_BYTES', 256)
    @patch('benchmark.LOG_READ_CHUNK_BYTES', 17)
    def test_overlong_complete_and_unterminated_lines_are_rejected(self):
        for suffix in (b'\n', b''):
            data = self.record.encode() + b'x' * 257 + suffix
            with self.subTest(suffix=suffix), self.assertRaisesRegex(RuntimeError, 'Oversized'):
                scan_log_interval(io.BytesIO(data), len(data))

    def test_overall_limit_rejects_before_reading(self):
        source = Mock()
        with self.assertRaisesRegex(RuntimeError, 'large stderr interval'):
            scan_log_interval(source, 512 * 1024 * 1024 + 1)
        source.read.assert_not_called()

    def test_log_interval_larger_than_old_limit_is_streamed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stderr.log'
            path.touch()
            start = log_position(path)
            block = b'unrelated log record\n' * 4096
            digest = hashlib.sha256()
            with path.open('wb') as out:
                for _ in range(210):
                    out.write(block)
                    digest.update(block)
                out.write(self.record.encode())
                digest.update(self.record.encode())
            evidence = evidence_from_log(path, start, 't', 'i', 100, 8)
            self.assertGreater(evidence['stderr_interval_bytes'], 16 * 1024 * 1024)
            self.assertEqual(evidence['stderr_interval_bytes'], path.stat().st_size)
            self.assertEqual(evidence['stderr_interval_sha256'], digest.hexdigest())

    def test_path_rotation_during_scan_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stderr.log'
            path.touch()
            start = log_position(path)
            path.write_bytes(self.record.encode())
            swapped = Mock(st_dev=start[0], st_ino=start[1] + 1, st_size=len(self.record))
            with patch('benchmark.Path.stat', return_value=swapped):
                with self.assertRaisesRegex(RuntimeError, 'rotated'):
                    evidence_from_log(path, start, 't', 'i', 100, 8)


class NumericTests(unittest.TestCase):
    def test_exact_l2_uses_doc_ids_not_rows(self):
        vectors = np.array([[3, 0], [0, 0], [0, 2]], dtype=np.float32)
        ids = np.array([2**53 + 201, 2**53 + 17, 2**53 + 99], dtype=np.int64)
        result = exact_neighbors(vectors, np.array([[0, .5]], dtype=np.float32), ids, 2)
        self.assertEqual(result, [[2**53 + 17, 2**53 + 99]])

    def test_exact_l2_handles_large_offsets_without_dot_product_cancellation(self):
        vectors = np.array([[1e7, 1], [1e7, 2], [1e7, 3]], dtype=np.float32)
        self.assertEqual(exact_neighbors(vectors, np.array([[1e7, 2.9]], np.float32), np.array([9, 20, 31]), 1), [[31]])

    def test_exact_ties_have_deterministic_id_order(self):
        self.assertEqual(exact_neighbors(np.array([[1], [-1]], np.float32), np.array([[0]], np.float32), np.array([9, 3]), 2), [[3, 9]])

    def test_exact_rejects_nonfinite_or_duplicate_identity(self):
        with self.assertRaises(ValueError):
            exact_neighbors(np.array([[math.nan]]), np.array([[0]]), np.array([7]), 1)
        with self.assertRaises(ValueError):
            exact_neighbors(np.array([[0], [1]]), np.array([[0]]), np.array([7, 7]), 1)
        with self.assertRaises(ValueError):
            exact_neighbors(np.array([[0]]), np.array([[0]]), np.array([7]), 2)

    def test_data_seed_and_large_irregular_ids(self):
        a = generate_data(31, 4, 7, 12)
        b = generate_data(31, 4, 7, 12)
        c = generate_data(31, 4, 7, 13)
        self.assertEqual(fingerprint(*a), fingerprint(*b))
        self.assertNotEqual(fingerprint(*a), fingerprint(*c))
        self.assertGreater(int(a[2].min()), 2**53)
        self.assertEqual(len(set(map(int, a[2]))), 31)
        self.assertEqual(a[0].dtype, np.float32)
        self.assertFalse(np.all(np.diff(a[2]) == 1))

    def test_recall_does_not_depend_on_rank_order(self):
        result = assess_results([[(99, .1), (17, .2)], [(99, .1), (21, .2)]], [[17, 99], [17, 21]], [17, 99, 21], 2)
        self.assertEqual(result["recall_at_k"], .75)
        self.assertEqual(result["min_query_recall"], .5)

    def test_rejects_missing_duplicate_unknown_lossy_or_bad_distance_results(self):
        for results in ([], [[(17, 0)]], [[(17, 0), (17, 1)]], [[(17, 0), (42, 1)]],
                        [[(17.0, 0), (99, 1)]], [[(17, math.inf), (99, 1)]],
                        [[(17, -1), (99, 1)]], [[(17, 2), (99, 1)]]):
            with self.subTest(results=results), self.assertRaises(ValueError):
                assess_results(results, [[17, 99]], [17, 99], 2)


class InputTests(unittest.TestCase):
    def test_identifier_rejects_fragments(self):
        self.assertEqual(identifier("ahb_123_cpu_1"), "`ahb_123_cpu_1`")
        for bad in ("test;DROP", "a`", "db.table", "a b", "", "x" * 65):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                identifier(bad)

    def test_with_grammar_rejects_injection_and_duplicates(self):
        for bad in ("lib=vsag; DROP TABLE t", "lib='vsag'", "lib=vsag,lib=other", "x=(select 1)", "lib=vsag --comment", ""):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                parse_options(bad)
        self.assertEqual(parse_options(" TYPE = HNSW ,m=16"), {"type": "hnsw", "m": "16"})

    def test_rejects_quality_parameter_changes(self):
        cpu = parse_options("distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=200,ef_search=200")
        for key, value in (("type", "hnsw_sq"), ("distance", "ip"), ("m", "8"), ("ef_search", "10"), ("ef_construction", "50")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_comparable(cpu, dict(cpu, **{key: value}), 10)

    def test_query_does_not_interpolate_vectors(self):
        query = query_sql("ahb_t", "ahb_idx", 10)
        self.assertEqual(query.count("%s"), 2)
        self.assertIn("INDEX(t ahb_idx)", query)
        self.assertIn("APPROXIMATE LIMIT 10", query)
        with self.assertRaises(ValueError):
            query_sql("ahb_t", "x) */ DROP", 10)


class AcceptanceGateTests(unittest.TestCase):
    @staticmethod
    def report():
        # Deliberately no GPU evidence: fictitious timings are unit-test inputs only.
        report = {"config": {"repeats": 3, "cpu_only": False, "min_recall": .9,
                           "rows": 100, "dim": 8, "target_speedup": 2},
                "server_environment": {}, "rounds": [
                    {"mode": mode, "build_sec": duration, "index_name": f"idx_{mode}_{i}",
                     "table_name": f"table_{mode}_{i}", "coverage_found": 100,
                     "quality": {"recall_at_k": .99}, "vector_plan_verified": True, "with": {"m": "16"}}
                    for mode, times in (("cpu", [10., 20., 90.]), ("gpu", [4., 5., 12.]))
                    for i, duration in enumerate(times)]}
        for row in report["rounds"]:
            # Synthetic proof fields are confined to acceptance-unit-test inputs.
            row["storage_integrity"] = {
                "method": "sql_snapshot_native_hnsw", "source_table": row["table_name"],
                "index_name": row["index_name"], "stored_count": 100, "mapped_count": 100,
                "dim": 8, "m": 16, "vectors_exact": True, "all_ids_present": True,
                "snapshot_sha256": "a" * 64, "mapping_sha256": "b" * 64,
                "snapshot_bytes": 17696, "data_table_id": 900, "snapshot_tablet_id": 1000,
                "snapshot_table": f"__idx_900_{row['index_name']}_index_snapshot_data_table",
                "mapping_table": "__idx_900_rowkey_vid_table", "chunk_keys": ["1000_10_hnsw_data_part00000"]}
        return report

    def test_no_gpu_evidence_never_produces_speedup(self):
        result = summarize(self.report(), [])
        self.assertEqual(result["status"], "UNVERIFIED")
        self.assertIsNone(result["speedup"])
        self.assertFalse(result["meets_target"])
        self.assertEqual(result["cpu_median_build_sec"], 20)
        self.assertEqual(result["gpu_median_build_sec"], 5)

    def test_incomplete_run_never_produces_speedup(self):
        report = self.report()
        report["rounds"].pop()
        self.assertEqual(summarize(report, [])["status"], "INCOMPLETE")

    def test_mean_recall_does_not_hide_a_failed_round(self):
        report = self.report()
        report["rounds"][-1]["quality"]["recall_at_k"] = .8
        self.assertEqual(summarize(report, [])["status"], "QUALITY_OR_FUNCTION_FAILED")

    def test_quality_does_not_hide_unused_index(self):
        for value in (False, 1, None):
            report = self.report()
            report["rounds"][0]["vector_plan_verified"] = value
            self.assertEqual(summarize(report, [])["status"], "QUALITY_OR_FUNCTION_FAILED")

    def test_self_ann_miss_is_diagnostic_when_storage_is_exact(self):
        report = self.report()
        report["rounds"][0]["coverage_found"] = 99
        # Still UNVERIFIED without GPU evidence; storage completeness itself passed.
        self.assertEqual(summarize(report, [])["status"], "UNVERIFIED")
        self.assertEqual(report["rounds"][0]["coverage_found"], 99)

    def test_both_modes_require_actual_sql_snapshot_proof(self):
        for mode in ("cpu", "gpu"):
            report = self.report()
            row = next(row for row in report["rounds"] if row["mode"] == mode)
            del row["storage_integrity"]
            self.assertEqual(summarize(report, [])["status"], "QUALITY_OR_FUNCTION_FAILED")

    def test_storage_omissions_corruption_and_wrong_index_fail_closed(self):
        for key, value in (("stored_count", 99), ("mapped_count", 99), ("vectors_exact", False),
                           ("all_ids_present", False), ("vectors_exact", 1), ("dim", 7), ("m", 8),
                           ("method", "standalone_native_probe"), ("source_table", "another_table"),
                           ("index_name", "another_index"), ("snapshot_sha256", ""),
                           ("mapping_sha256", "not-a-hash"), ("data_table_id", 901),
                           ("snapshot_tablet_id", 0), ("snapshot_bytes", -1),
                           ("snapshot_table", "unrelated_table"), ("mapping_table", "unrelated_map"),
                           ("chunk_keys", []), ("chunk_keys", ["1001_10_hnsw_data_part00000"]),
                           ("chunk_keys", ["1000_10_hnsw_data_part00001"]),
                           ("chunk_keys", ["1000_10_hnsw_data_part00000", "1000_11_hnsw_data_part00001"]),
                           ("chunk_keys", [True])):
            with self.subTest(key=key, value=value):
                report = self.report()
                report["rounds"][0]["storage_integrity"][key] = value
                summary = summarize(report, [])
                self.assertEqual(summary["status"], "QUALITY_OR_FUNCTION_FAILED")
                self.assertIsNone(summary["speedup"])

    def test_all_gates_can_pass_with_preserved_self_ann_miss(self):
        report = self.report()
        report["server_environment"] = {key: "unit-test-only" for key in (
            "cpu_model", "cpu_threads", "cpu_affinity", "gpu_model", "gpu_architecture",
            "rocm_version", "hipvs_version", "seekdb_commit")}
        report["rounds"][0]["coverage_found"] = 99
        records = [{"producer": "seekdb-hipvs-build", "backend": "hipvs", "device_vendor": "AMD",
                    "gpu_compute": True, "cpu_fallback": False, "completed": True,
                    "rows": 100, "dim": 8, "gpu_name": "unit-test-only",
                    "table_name": row["table_name"], "index_name": row["index_name"]}
                   for row in report["rounds"] if row["mode"] == "gpu"]
        result = summarize(report, records)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["speedup"], 4)
        self.assertEqual(report["rounds"][0]["coverage_found"], 99)

    def test_invalid_recall_cannot_pass_with_complete_storage(self):
        for recall in (math.nan, math.inf, 1.01, -1):
            report = self.report()
            report["rounds"][0]["quality"]["recall_at_k"] = recall
            self.assertEqual(summarize(report, [])["status"], "QUALITY_OR_FUNCTION_FAILED")

    def test_cpu_only_never_claims_gpu_acceptance(self):
        report = self.report()
        report["config"]["cpu_only"] = True
        report["rounds"] = report["rounds"][:3]
        result = summarize(report, [])
        self.assertEqual(result["status"], "CPU_ONLY")
        self.assertIsNone(result["speedup"])
        self.assertFalse(result["meets_target"])

    def test_invalid_timing_is_rejected(self):
        for invalid in (0, -1, math.nan, math.inf):
            report = self.report()
            report["rounds"][0]["build_sec"] = invalid
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                summarize(report, [])

    def test_fallback_or_wrong_build_evidence_is_rejected(self):
        row = self.report()["rounds"][-1]
        rejected = [
            {"table_name": "another_table", "index_name": row["index_name"]},
            {"table_name": row["table_name"], "index_name": row["index_name"], "backend": "cpu"},
            {"table_name": row["table_name"], "index_name": row["index_name"], "producer": "seekdb-hipvs-build",
             "backend": "hipvs", "device_vendor": "AMD", "gpu_compute": True, "cpu_fallback": True},
        ]
        for evidence in rejected:
            with self.subTest(evidence=evidence):
                verified, reason = match_evidence(row, [evidence], 100, 8)
                self.assertIsNone(verified)
                self.assertTrue(reason)


class RoundSnapshotIntegrationTests(unittest.TestCase):
    @staticmethod
    def fixture():
        connection = Mock()
        cursor = connection.cursor.return_value
        cursor.fetchone.return_value = (200,)
        cursor.fetchall.side_effect = [
            [(101,), (103,)], [(None, None, "PRIMARY")], [(101, 0.0)],
            [("VECTOR INDEX ahb_idx_test_cpu_1",)], [(101, 0.0)],
            [(101, 0.0)], [(103, 0.0)]]
        report = {"run_id": "test", "config": {"rows": 2, "dim": 2, "k": 1, "batch": 2}, "rounds": []}
        values = (np.array([[0, 0], [1, 1]], np.float32), np.array([[0, 0]], np.float32),
                  np.array([101, 103], np.int64), [[101]],
                  parse_options("distance=l2,type=hnsw,lib=vsag,m=5,ef_construction=200,ef_search=200"))
        return connection, report, values

    @patch("benchmark.verify_sql_snapshot")
    def test_actual_connection_and_complete_input_are_used(self, verify):
        connection, report, values = self.fixture()
        verify.return_value = {"stored_count": 2}
        run_round(connection, report, "cpu", 1, *values)
        verify.assert_called_once_with(connection, "ahb_test_cpu_1", "ahb_idx_test_cpu_1",
                                       values[0], values[2], 5)
        self.assertEqual(report["rounds"][0]["storage_integrity"], verify.return_value)
        self.assertEqual(report["rounds"][0]["coverage_found"], 2)
        self.assertIn("storage_verification_sec", report["rounds"][0])
        connection.cursor.return_value.execute.assert_called_with("DROP TABLE `ahb_test_cpu_1`")

    @patch("benchmark.verify_sql_snapshot", side_effect=ValueError("stored vector corruption"))
    def test_snapshot_failure_preserves_diagnostics_but_cannot_pass(self, verify):
        connection, report, values = self.fixture()
        with self.assertRaisesRegex(ValueError, "stored vector corruption"):
            run_round(connection, report, "cpu", 1, *values)
        self.assertEqual(report["rounds"][0]["coverage_found"], 2)
        self.assertNotIn("storage_integrity", report["rounds"][0])
        self.assertEqual(report["active_operation"], "cpu/1: SQL snapshot integrity")
        connection.cursor.return_value.execute.assert_called_with("DROP TABLE `ahb_test_cpu_1`")


class SafeFailureDetailTests(unittest.TestCase):
    def test_missing_table_keeps_exact_safe_identifier(self):
        detail = safe_failure_detail(Exception(1146, "Table 'test.__idx_900_rowkey_vid_table' doesn't exist"))
        self.assertEqual(detail["message"], "Table 'test.__idx_900_rowkey_vid_table' doesn't exist")

    def test_unknown_column_keeps_safe_identifier_and_scope(self):
        detail = safe_failure_detail(Exception(1054, "Unknown column 't.tablet_id' in 'field list'"))
        self.assertEqual(detail["message"], "Unknown column 't.tablet_id' in 'field list'")

    def test_untrusted_driver_text_is_never_echoed(self):
        for code in (1045, 1064, 1146, 1054, 2003):
            error = Exception(code, "user=secret-user password=secret-password https://secret-user:secret-token@host")
            text = json.dumps(safe_failure_detail(error))
            self.assertNotIn("secret-", text)
            self.assertNotIn("https://", text)

    def test_local_validation_keeps_message_and_locations_without_source_or_locals(self):
        try:
            identifier("unsafe`sql")
        except ValueError as error:
            detail = safe_failure_detail(error)
        self.assertEqual(detail["message"], "Unsafe SQL identifier")
        self.assertEqual(detail["trace"][-1]["file"], "benchmark.py")
        self.assertEqual(detail["trace"][-1]["function"], "identifier")
        self.assertIsInstance(detail["trace"][-1]["line"], int)
        self.assertNotIn("unsafe`sql", json.dumps(detail))
        self.assertTrue(all(set(frame) == {"file", "line", "function"} for frame in detail["trace"]))

    @patch.dict("os.environ", {"AMD_BENCH_PASSWORD": "test_password"})
    def test_configured_password_is_redacted_even_in_an_identifier(self):
        detail = safe_failure_detail(Exception(1146, "Table 'test.test_password' doesn't exist"))
        self.assertNotIn("test_password", json.dumps(detail))


if __name__ == "__main__":
    unittest.main()
