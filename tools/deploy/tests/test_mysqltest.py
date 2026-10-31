"""Regression tests for the shared developer/CI mysqltest runner."""

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "mysqltest.py"
SPEC = importlib.util.spec_from_file_location("seekdb_mysqltest", SCRIPT)
runner = importlib.util.module_from_spec(SPEC)
with mock.patch.object(sys, "path", [str(SCRIPT.parent)] + sys.path):
    SPEC.loader.exec_module(runner)


class MysqltestTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.deploy = self.root / "tools/deploy"
        self.deploy.mkdir(parents=True)
        (self.deploy / "mysqltest_config.yaml").write_text(
            "runtime_configs:\n  psmall:\n    exclude-set:\n      - expr.disabled\n"
        )
        self.add_case("minitest")
        self.add_case("expr.ceil")
        self.add_case("expr.disabled")
        self.add_case("alpha.shared")
        self.add_case("beta.shared")

    def add_case(self, name, result=True):
        if "." in name:
            suite, name = name.split(".", 1)
            directory = self.deploy / "mysql_test/test_suite" / suite
        else:
            directory = self.deploy / "mysql_test"
        test_file = directory / "t" / (name + ".test")
        result_file = directory / "r/mysql" / (name + ".result")
        test_file.parent.mkdir(parents=True, exist_ok=True)
        test_file.write_text("select 1;\n")
        if result:
            result_file.parent.mkdir(parents=True, exist_ok=True)
            result_file.write_text("select 1;\n1\n1\n")
        return runner.MysqltestCase(name, test_file, result_file)

    def names(self, **kwargs):
        return [case.name for case in runner.discover_cases(self.root, **kwargs)]

    def run_arguments(self, *extra):
        return runner.create_parser().parse_args([
            "run", "--seekdb", "/bin/true", "--obclient", "/bin/true",
            "--mysqltest", "/bin/true", "--base-dir", str(self.root / "instance"),
            "--work-dir", str(self.root / "results"), *extra,
        ])

    def test_default_discovery_preserves_ci_order_and_exclusions(self):
        self.assertEqual(self.names(), [
            "minitest", "alpha.shared", "beta.shared", "expr.ceil",
        ])

    def test_repeated_and_comma_separated_selections(self):
        self.assertEqual(self.names(test_set=["expr.ceil,minitest", "expr.ceil"]),
                         ["minitest", "expr.ceil"])

    def test_suite_limits_unqualified_case_lookup(self):
        self.assertEqual(self.names(suites=["alpha"], test_set=["shared"]),
                         ["alpha.shared"])

    def test_ambiguous_case_requires_qualification(self):
        with self.assertRaisesRegex(runner.RunnerError, "ambiguous.*suite.case"):
            self.names(test_set=["shared"])

    def test_unknown_suite_or_case_is_an_error(self):
        with self.assertRaisesRegex(runner.RunnerError, "unknown mysqltest suites"):
            self.names(suites=["typo"])
        with self.assertRaisesRegex(runner.RunnerError, "unknown mysqltest case"):
            self.names(suites=["alpha"], test_set=["expr.ceil"])

    def test_disabled_case_requires_explicit_opt_in(self):
        with self.assertRaisesRegex(runner.RunnerError, "include-excluded"):
            self.names(test_set=["expr.disabled"])
        self.assertEqual(self.names(test_set=["expr.disabled"], include_excluded=True),
                         ["expr.disabled"])

    def test_missing_result_only_blocks_selected_cases(self):
        self.add_case("expr.new", result=False)
        self.assertEqual(self.names(test_set=["expr.ceil"]), ["expr.ceil"])
        with self.assertRaisesRegex(runner.RunnerError, "no result files"):
            self.names(test_set=["expr.new"])
        self.assertEqual(self.names(test_set=["expr.new"], check_results=False),
                         ["expr.new"])

    def test_local_run_defaults_to_one_slice(self):
        args = self.run_arguments("--test-set", "expr.ceil")
        self.assertEqual((args.slice_index, args.slice_count), (0, 1))

    def test_invalid_selection_does_not_destroy_existing_instance(self):
        args = self.run_arguments("--test-set", "typo")
        instance = Path(args.base_dir)
        instance.mkdir()
        sentinel = instance / "keep"
        sentinel.write_text("existing data")
        with mock.patch.object(runner, "REPO_ROOT", self.root), \
                mock.patch.object(runner, "prepare_instance") as prepare, \
                mock.patch.object(runner, "destroy_instance") as destroy, \
                contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(runner.command_run(args), 1)
        prepare.assert_not_called()
        destroy.assert_not_called()
        self.assertEqual(sentinel.read_text(), "existing data")

    def test_missing_binary_does_not_destroy_instance(self):
        args = self.run_arguments("--test-set", "expr.ceil")
        args.seekdb = str(self.root / "missing-seekdb")
        with mock.patch.object(runner, "REPO_ROOT", self.root), \
                mock.patch.object(runner, "destroy_instance") as destroy, \
                contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(runner.command_run(args), 1)
        destroy.assert_not_called()

    def test_record_creates_result_directory_and_forwards_record_mode(self):
        case = self.add_case("new_suite.new_case", result=False)
        args = self.run_arguments("--test-set", "new_suite.new_case", "--record")
        args.obclient = Path(args.obclient)
        args.base_dir = Path(args.base_dir)
        log_dir = self.root / "log"
        log_dir.mkdir()

        def record(command, **kwargs):
            self.assertIn("--record", command)
            result_path = next(arg.split("=", 1)[1] for arg in command
                               if arg.startswith("--result-file="))
            Path(result_path).write_text("recorded result\n")
            return subprocess.CompletedProcess(command, 0, stdout="")

        with mock.patch.object(runner.subprocess, "run", side_effect=record), \
                contextlib.redirect_stdout(io.StringIO()):
            result, _ = runner.run_case(args, self.deploy, case, self.root / "tmp", log_dir)
        self.assertEqual(result, 0)
        self.assertEqual(case.result_file.read_text(), "recorded result\n")

    def test_record_requires_explicit_selection(self):
        args = ["run", "--base-dir", str(self.root / "instance"),
                "--work-dir", str(self.root / "results"), "--record"]
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as exc:
            runner.main(args)
        self.assertEqual(exc.exception.code, 2)

    def test_client_environment_adds_packaged_libraries_and_preserves_existing_paths(self):
        client = self.root / "client/bin/obclient"
        (client.parent.parent / "lib").mkdir(parents=True)
        (client.parent.parent / "lib64").mkdir()
        inherited = {"PATH": "/existing/bin", "LD_LIBRARY_PATH": "/existing/lib"}
        with mock.patch.dict(runner.sdb.os.environ, inherited, clear=True), \
                mock.patch.object(runner.sdb.sys, "platform", "linux"):
            environment = runner.sdb.client_environment(client)
        self.assertEqual(environment["PATH"], str(client.parent) + ":/existing/bin")
        self.assertEqual(environment["LD_LIBRARY_PATH"], ":".join([
            str(client.parent.parent / "lib"), str(client.parent.parent / "lib64"),
            "/existing/lib",
        ]))

    def test_client_environment_uses_darwin_loader_path(self):
        client = self.root / "client/bin/obclient"
        (client.parent.parent / "lib").mkdir(parents=True)
        with mock.patch.dict(runner.sdb.os.environ, {"PATH": "/existing/bin"}, clear=True), \
                mock.patch.object(runner.sdb.sys, "platform", "darwin"):
            environment = runner.sdb.client_environment(client)
        self.assertEqual(environment["DYLD_LIBRARY_PATH"], str(client.parent.parent / "lib"))
        self.assertNotIn("LD_LIBRARY_PATH", environment)

    def test_empty_selection_cannot_run_or_record_all_cases(self):
        for command in ("run", "list"):
            args = [command, "--test-set", " , "]
            if command == "run":
                args += ["--base-dir", str(self.root / "instance"),
                         "--work-dir", str(self.root / "results"), "--record"]
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as exc:
                runner.main(args)
            self.assertEqual(exc.exception.code, 2)

    def test_ci_merge_still_checks_complete_nonduplicated_coverage(self):
        cases = self.names()
        for index in range(2):
            runner.write_json(self.root / "slices" / ("slice_{}".format(index)) /
                              "seekdb_result.json", {
                                  "slice_index": index, "slice_count": 2,
                                  "cases": cases[index::2], "failed_cases": [],
                                  "success": True, "error": None,
                              })
        args = argparse.Namespace(results_dir=str(self.root / "slices"),
                                  output=str(self.root / "merged.json"),
                                  slice_count=2, run_id="test")
        with mock.patch.object(runner, "REPO_ROOT", self.root), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.command_merge(args), 0)
            result = self.root / "slices/slice_1/seekdb_result.json"
            payload = json.loads(result.read_text())
            payload["cases"].append(cases[0])
            runner.write_json(result, payload)
            self.assertEqual(runner.command_merge(args), 1)
        errors = json.loads(Path(args.output).read_text())["errors"]
        self.assertTrue(any("duplicate cases" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
