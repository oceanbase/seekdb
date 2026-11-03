#!/usr/bin/env python3
"""Run local SeekDB mysqltest cases and merge CI slice results."""

from __future__ import print_function

import argparse
from collections import Counter, namedtuple
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time

import sdb


CASE_TIMEOUT = 3600
MAX_CASE_RETRIES = 3
READY_TIMEOUT = 600
MYSQLTEST_USER = "admin"
MYSQLTEST_PASSWORD = "admin"
MYSQLTEST_DATABASE = "test"
REPO_ROOT = Path(__file__).resolve().parents[2]
RESULT_MISMATCH_MESSAGES = (
    "Result content mismatch",
    "Result length mismatch",
)

MysqltestCase = namedtuple("MysqltestCase", ("name", "test_file", "result_file"))


class RunnerError(RuntimeError):
    pass


def absolute_path(value):
    return Path(os.path.abspath(os.path.expanduser(value)))


def format_command(command):
    return " ".join(shlex.quote(str(item)) for item in command)


def decode_output(output):
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", "replace")
    return output


def normalize_trailing_horizontal_whitespace(content):
    lines = content.split(b"\n")
    for index, line in enumerate(lines):
        if line.endswith(b"\r"):
            lines[index] = line[:-1].rstrip(b" \t") + b"\r"
        else:
            lines[index] = line.rstrip(b" \t")
    return b"\n".join(lines)


def files_equal_ignoring_trailing_whitespace(expected_path, actual_path):
    try:
        expected = expected_path.read_bytes()
        actual = actual_path.read_bytes()
    except OSError as exc:
        print(
            "warning: failed to compare mysqltest result files: {}".format(exc),
            file=sys.stderr,
        )
        return False
    return normalize_trailing_horizontal_whitespace(
        expected
    ) == normalize_trailing_horizontal_whitespace(actual)


def run_command(command, description, cwd=None, stdin=None):
    print("+ {}".format(format_command(command)), flush=True)
    try:
        result = subprocess.run(
            [str(item) for item in command],
            cwd=str(cwd) if cwd else None,
            stdin=stdin,
            check=False,
        )
    except OSError as exc:
        raise RunnerError("{}: {}".format(description, exc))
    if result.returncode != 0:
        raise RunnerError("{} exited with {}".format(description, result.returncode))


def run_sdb(sdb_script, command, arguments, description, cwd):
    run_command(
        [sys.executable, str(sdb_script), command] + [str(item) for item in arguments],
        description,
        cwd=cwd,
    )


def destroy_instance(sdb_script, base_dir, cwd, check=True):
    command = [
        sys.executable,
        str(sdb_script),
        "destroy",
        "--base-dir",
        str(base_dir),
    ]
    print("+ {}".format(format_command(command)), flush=True)
    try:
        result = subprocess.run(command, cwd=str(cwd), check=False)
    except OSError as exc:
        if check:
            raise RunnerError("failed to destroy seekdb: {}".format(exc))
        return "failed to destroy seekdb: {}".format(exc)
    if result.returncode != 0:
        message = "destroy seekdb exited with {}".format(result.returncode)
        if check:
            raise RunnerError(message)
        return message
    return None


def execute_init_sql(obclient, host, port, deploy_dir):
    init_files = (
        (deploy_dir / "init.sql", "oceanbase"),
        (deploy_dir / "init_user.sql", "test"),
    )
    for sql_file, database in init_files:
        command = [
            str(obclient),
            "-h",
            host,
            "-P",
            str(port),
            "-uroot",
            "-A",
            "-c",
            "-D{}".format(database),
        ]
        print("+ {} < {}".format(format_command(command), sql_file), flush=True)
        try:
            with sql_file.open("rb") as sql_input:
                result = subprocess.run(
                    command,
                    cwd=str(deploy_dir),
                    env=sdb.client_environment(obclient),
                    stdin=sql_input,
                    check=False,
                )
        except OSError as exc:
            raise RunnerError("failed to execute {}: {}".format(sql_file.name, exc))
        if result.returncode != 0:
            raise RunnerError(
                "{} exited with {}".format(sql_file.name, result.returncode)
            )


def prepare_instance(args, repo_root, sdb_script, deploy_dir):
    destroy_instance(sdb_script, args.base_dir, repo_root)
    start_arguments = [
        "--binary",
        args.seekdb,
        "--base-dir",
        args.base_dir,
        "--port",
        args.port,
    ]
    for parameter in args.parameter:
        start_arguments.extend(("--parameter", parameter))
    run_sdb(
        sdb_script,
        "start",
        start_arguments,
        "start seekdb",
        repo_root,
    )
    run_sdb(
        sdb_script,
        "wait-ready",
        (
            "--client",
            args.obclient,
            "--base-dir",
            args.base_dir,
            "--host",
            args.host,
            "--port",
            args.port,
            "--user",
            "root",
            "--timeout",
            READY_TIMEOUT,
        ),
        "wait for seekdb",
        repo_root,
    )
    execute_init_sql(args.obclient, args.host, args.port, deploy_dir)


def load_excluded_case_names(config_path):
    try:
        with config_path.open("r", encoding="utf-8") as config_file:
            lines = config_file.readlines()
    except OSError as exc:
        raise RunnerError("cannot read mysqltest config {}: {}".format(config_path, exc))

    case_names = []
    found_exclude_set = False
    in_runtime_configs = False
    in_psmall = False
    in_exclude_set = False
    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        if indent == 0:
            in_runtime_configs = stripped == "runtime_configs:"
            in_psmall = False
            in_exclude_set = False
        elif in_runtime_configs and indent == 2:
            in_psmall = stripped == "psmall:"
            in_exclude_set = False
        elif in_psmall and indent == 4:
            in_exclude_set = stripped == "exclude-set:"
            if in_exclude_set:
                found_exclude_set = True
        elif in_exclude_set and indent == 6 and stripped.startswith("- "):
            case_name = stripped[2:].strip()
            if not case_name:
                raise RunnerError(
                    "empty mysqltest case at {}:{}".format(config_path, line_number)
                )
            case_names.append(case_name)

    if not found_exclude_set:
        raise RunnerError(
            "runtime_configs.psmall.exclude-set is missing in {}".format(config_path)
        )
    duplicates = sorted(
        name for name, count in Counter(case_names).items() if count > 1
    )
    if duplicates:
        raise RunnerError(
            "duplicate mysqltest cases in {}: {}".format(
                config_path, ", ".join(duplicates)
            )
        )
    return case_names


def selection_values(values):
    return {item.strip() for value in values for item in value.split(",") if item.strip()}


def discover_cases(repo_root, suites=(), test_set=(), include_excluded=False,
                   check_results=True):
    config_path = repo_root / "tools" / "deploy" / "mysqltest_config.yaml"
    mysql_test_dir = repo_root / "tools" / "deploy" / "mysql_test"
    test_dir = mysql_test_dir / "t"
    result_dir = mysql_test_dir / "r" / "mysql"
    suite_dir = mysql_test_dir / "test_suite"
    if not test_dir.is_dir():
        raise RunnerError("mysqltest case directory does not exist: {}".format(test_dir))
    excluded_cases = set(load_excluded_case_names(config_path))
    available_cases = {}

    for test_file in sorted(test_dir.glob("*.test")):
        if not test_file.is_file():
            continue
        name = test_file.stem
        available_cases[name] = MysqltestCase(
            name, test_file, result_dir / (name + ".result")
        )

    if suite_dir.is_dir():
        for test_file in sorted(suite_dir.glob("*/t/*.test")):
            if not test_file.is_file():
                continue
            suite_name = test_file.parent.parent.name
            name = "{}.{}".format(suite_name, test_file.stem)
            if name in available_cases:
                raise RunnerError("duplicate mysqltest case name: {}".format(name))
            available_cases[name] = MysqltestCase(
                name,
                test_file,
                test_file.parent.parent / "r" / "mysql" / (test_file.stem + ".result"),
            )

    unknown_exclusions = sorted(excluded_cases - set(available_cases))
    if unknown_exclusions:
        raise RunnerError(
            "mysqltest exclude-set references missing cases: {}".format(
                ", ".join(unknown_exclusions)
            )
        )

    suites = selection_values(suites)
    requested_cases = selection_values(test_set)
    known_suites = {case.test_file.parent.parent.name for case in available_cases.values()
                    if case.test_file.parent.parent.parent == suite_dir}
    unknown_suites = sorted(suites - known_suites)
    if unknown_suites:
        raise RunnerError("unknown mysqltest suites: {}".format(", ".join(unknown_suites)))
    candidates = {
        name: case for name, case in available_cases.items()
        if not suites or (case.test_file.parent.parent.parent == suite_dir
                          and case.test_file.parent.parent.name in suites)
    }
    if requested_cases:
        selected = set()
        for requested in sorted(requested_cases):
            matches = ([requested] if requested in candidates else
                       [name for name, case in candidates.items()
                        if case.test_file.stem == requested])
            if not matches:
                raise RunnerError("unknown mysqltest case: {}".format(requested))
            if len(matches) > 1:
                raise RunnerError("ambiguous mysqltest case {}: {}; use suite.case".format(
                    requested, ", ".join(sorted(matches))))
            selected.update(matches)
        candidates = {name: case for name, case in candidates.items() if name in selected}
    if not include_excluded:
        explicitly_excluded = requested_cases and sorted(set(candidates) & excluded_cases)
        if explicitly_excluded:
            raise RunnerError("selected mysqltest cases are excluded: {}; "
                              "use --include-excluded to run them".format(
                                  ", ".join(explicitly_excluded)))
        candidates = {name: case for name, case in candidates.items()
                      if name not in excluded_cases}
    case_names = list(candidates)
    if not case_names:
        raise RunnerError(
            "no mysqltest cases left after applying exclude-set in {}".format(
                config_path
            )
        )
    missing_results = [
        name for name in case_names
        if check_results and not available_cases[name].result_file.is_file()
    ]
    if missing_results:
        raise RunnerError(
            "mysqltest cases have no result files: {}".format(
                ", ".join(missing_results)
            )
        )
    return [candidates[name] for name in case_names]


def mysqltest_environment(args):
    environment = sdb.client_environment(args.obclient)
    environment.update(
        {
            "OBMYSQL_PORT": str(args.port),
            "OBMYSQL_MS0": args.host,
            "OBMYSQL_MS0_DEV": args.host,
            "OBMYSQL_PWD": MYSQLTEST_PASSWORD,
            "OBMYSQL_USR": MYSQLTEST_USER,
            "OBSERVER_DIR": str(args.base_dir),
            "IS_BUSINESS": "0",
            "TENANT": "mysql",
        }
    )
    return environment


def run_case(args, deploy_dir, case, tmp_dir, log_dir):
    command = [
        str(args.mysqltest),
        "--host={}".format(args.host),
        "--port={}".format(args.port),
        "--user={}".format(MYSQLTEST_USER),
        "--password={}".format(MYSQLTEST_PASSWORD),
        "--database={}".format(MYSQLTEST_DATABASE),
        "--tmpdir={}".format(tmp_dir),
        "--logdir={}".format(log_dir),
        "--silent",
        "--test-file={}".format(case.test_file),
        "--result-file={}".format(case.result_file),
        "--timer-file={}".format(log_dir / "timer"),
        "--tail-lines=20",
    ]
    if args.record:
        case.result_file.parent.mkdir(parents=True, exist_ok=True)
        command.append("--record")
    case_name = case.name
    reject_file = log_dir / (case.result_file.stem + ".reject")
    try:
        reject_file.unlink()
    except FileNotFoundError:
        pass
    except OSError as exc:
        print(
            "warning: failed to remove stale reject file {}: {}".format(
                reject_file, exc
            ),
            file=sys.stderr,
        )
        reject_file = None
    print("[ RUN      ] {}".format(case_name), flush=True)
    started = time.monotonic()
    try:
        result = subprocess.run(
            command,
            cwd=str(deploy_dir),
            env=mysqltest_environment(args),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=CASE_TIMEOUT,
            universal_newlines=True,
            check=False,
        )
        return_code = result.returncode
        output = decode_output(result.stdout)
    except subprocess.TimeoutExpired as exc:
        return_code = 124
        output = decode_output(exc.stdout)
        output += "\n{} seconds timeout\n".format(CASE_TIMEOUT)
    except OSError as exc:
        return_code = 255
        output = "failed to run mysqltest: {}\n".format(exc)

    trailing_whitespace_ignored = False
    if (
        return_code != 0
        and reject_file is not None
        and any(message in output for message in RESULT_MISMATCH_MESSAGES)
        and files_equal_ignoring_trailing_whitespace(
            case.result_file, reject_file
        )
    ):
        return_code = 0
        trailing_whitespace_ignored = True
        try:
            reject_file.unlink()
        except OSError as exc:
            print(
                "warning: failed to remove ignored reject file {}: {}".format(
                    reject_file, exc
                ),
                file=sys.stderr,
            )

    if output and not trailing_whitespace_ignored:
        print(output, end="" if output.endswith("\n") else "\n", flush=True)
    elapsed = time.monotonic() - started
    if return_code == 0:
        suffix = ", trailing whitespace ignored" if trailing_whitespace_ignored else ""
        print(
            "[       OK ] {} ({:.3f}s{})".format(case_name, elapsed, suffix),
            flush=True,
        )
    else:
        print(
            "[  FAILED  ] {} ({:.3f}s, exit={})".format(
                case_name, elapsed, return_code
            ),
            flush=True,
        )
    return return_code, output


def copy_instance_diagnostics(base_dir, destination):
    destination.mkdir(parents=True, exist_ok=True)
    log_dir = base_dir / "log"
    if log_dir.is_dir():
        try:
            shutil.copytree(str(log_dir), str(destination / "seekdb_log"))
        except OSError as exc:
            print("warning: failed to copy seekdb logs: {}".format(exc), file=sys.stderr)

    core_dir = destination / "core"
    copied = set()
    for pattern in ("core", "core.*", "core-*"):
        if not base_dir.exists():
            break
        for core_file in base_dir.rglob(pattern):
            if not core_file.is_file() or str(core_file) in copied:
                continue
            copied.add(str(core_file))
            core_dir.mkdir(parents=True, exist_ok=True)
            relative_name = "__".join(core_file.relative_to(base_dir).parts)
            try:
                shutil.copy2(str(core_file), str(core_dir / relative_name))
            except OSError as exc:
                print("warning: failed to copy {}: {}".format(core_file, exc), file=sys.stderr)


def save_case_failure(args, case_name, output):
    destination = args.work_dir / "failures" / case_name
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "mysqltest.log").write_text(output, encoding="utf-8")
    save_instance_diagnostics(args)


def save_instance_diagnostics(args):
    destination = args.work_dir / "failures" / "instance"
    if not destination.exists():
        copy_instance_diagnostics(args.base_dir, destination)


def save_infrastructure_failure(args, message):
    destination = args.work_dir / "failures" / "infrastructure"
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "error.txt").write_text(message + "\n", encoding="utf-8")
    save_instance_diagnostics(args)


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(payload, output, ensure_ascii=False, sort_keys=True)
        output.write("\n")
    os.replace(str(temporary), str(path))


def command_run(args):
    repo_root = REPO_ROOT
    deploy_dir = repo_root / "tools" / "deploy"
    sdb_script = Path(__file__).resolve().with_name("sdb.py")
    args.seekdb = absolute_path(args.seekdb)
    args.obclient = absolute_path(args.obclient)
    args.mysqltest = absolute_path(args.mysqltest)
    args.base_dir = absolute_path(args.base_dir)
    args.work_dir = absolute_path(args.work_dir)
    result_path = args.work_dir / "seekdb_result.json"

    selected_cases = []
    failed_cases = []
    error = None
    instance_prepared = False
    args.work_dir.mkdir(parents=True, exist_ok=True)

    try:
        all_cases = discover_cases(
            repo_root, args.suite, args.test_set, args.include_excluded,
            check_results=not args.record,
        )
        selected_cases = all_cases[args.slice_index :: args.slice_count]
        for executable in (args.seekdb, args.obclient, args.mysqltest):
            if not executable.is_file() or not os.access(str(executable), os.X_OK):
                raise RunnerError("missing or non-executable binary: {}".format(executable))
        instance_prepared = True
        prepare_instance(args, repo_root, sdb_script, deploy_dir)
        tmp_dir = args.work_dir / "tmp"
        log_dir = args.work_dir / "mysqltest_log"
        tmp_dir.mkdir(parents=True, exist_ok=True)
        log_dir.mkdir(parents=True, exist_ok=True)

        for index, case in enumerate(selected_cases):
            return_code = None
            output = ""
            retries = 0 if args.record else MAX_CASE_RETRIES
            for retry_index in range(retries + 1):
                if retry_index > 0:
                    print(
                        "[ RETRY   ] {} ({}/{})".format(
                            case.name, retry_index, MAX_CASE_RETRIES
                        ),
                        flush=True,
                    )
                    prepare_instance(args, repo_root, sdb_script, deploy_dir)
                return_code, output = run_case(
                    args, deploy_dir, case, tmp_dir, log_dir
                )
                if return_code == 0:
                    break
            if return_code != 0:
                failed_cases.append(case.name)
                save_case_failure(args, case.name, output)
                if index + 1 < len(selected_cases):
                    prepare_instance(args, repo_root, sdb_script, deploy_dir)
    except Exception as exc:
        error = str(exc)
        print("[mysqltest][ERROR] {}".format(error), file=sys.stderr)
        try:
            save_infrastructure_failure(args, error)
        except OSError as save_error:
            print(
                "warning: failed to save infrastructure diagnostics: {}".format(
                    save_error
                ),
                file=sys.stderr,
            )
    finally:
        cleanup_error = (destroy_instance(
            sdb_script, args.base_dir, repo_root, check=False
        ) if instance_prepared else None)
        if cleanup_error:
            error = "{}; {}".format(error, cleanup_error) if error else cleanup_error

    success = not failed_cases and error is None
    payload = {
        "success": success,
        "slice_index": args.slice_index,
        "slice_count": args.slice_count,
        "case_count": len(selected_cases),
        "cases": [case.name for case in selected_cases],
        "failed_cases": failed_cases,
        "error": error,
    }
    write_json(result_path, payload)
    print(
        "slice {} finished: cases={}, failed={}, success={}".format(
            args.slice_index, len(selected_cases), len(failed_cases), success
        ),
        flush=True,
    )
    return 0 if success else 1


def unique(items):
    result = []
    seen = set()
    for item in items:
        if item not in seen:
            seen.add(item)
            result.append(item)
    return result


def command_merge(args):
    repo_root = REPO_ROOT
    results_dir = absolute_path(args.results_dir)
    output_path = absolute_path(args.output)
    failed_cases = []
    errors = []
    executed_cases = []

    try:
        expected_cases = [case.name for case in discover_cases(repo_root)]
    except RunnerError as exc:
        expected_cases = []
        errors.append(str(exc))

    for slice_index in range(args.slice_count):
        result_path = (
            results_dir / "slice_{}".format(slice_index) / "seekdb_result.json"
        )
        try:
            with result_path.open("r", encoding="utf-8") as result_file:
                result = json.load(result_file)
        except (OSError, ValueError) as exc:
            errors.append("slice {} result unavailable: {}".format(slice_index, exc))
            continue

        if result.get("slice_index") != slice_index:
            errors.append("slice {} has invalid slice_index".format(slice_index))
        if result.get("slice_count") != args.slice_count:
            errors.append("slice {} has invalid slice_count".format(slice_index))

        cases = result.get("cases")
        if not isinstance(cases, list):
            errors.append("slice {} has no case list".format(slice_index))
        else:
            executed_cases.extend(cases)

        failed = result.get("failed_cases")
        if not isinstance(failed, list):
            errors.append("slice {} has invalid failed_cases".format(slice_index))
        else:
            failed_cases.extend(failed)

        run_error = result.get("error")
        if run_error:
            errors.append("slice {}: {}".format(slice_index, run_error))
        if result.get("success") is not True and not failed and not run_error:
            errors.append("slice {} reported failure without details".format(slice_index))

    case_counts = Counter(executed_cases)
    duplicates = sorted(case for case, count in case_counts.items() if count > 1)
    missing = sorted(set(expected_cases) - set(executed_cases))
    unexpected = sorted(set(executed_cases) - set(expected_cases))
    if duplicates:
        errors.append("duplicate cases: {}".format(",".join(duplicates)))
    if missing:
        errors.append("missing cases: {}".format(",".join(missing)))
    if unexpected:
        errors.append("unexpected cases: {}".format(",".join(unexpected)))

    failed_cases = unique(failed_cases)
    success = not failed_cases and not errors
    payload = {
        "success": success,
        "run_id": str(args.run_id),
        "slice_count": args.slice_count,
        "case_count": len(expected_cases),
        "failed_cases": failed_cases,
        "errors": errors,
    }
    write_json(output_path, payload)
    print(json.dumps(payload, ensure_ascii=False, sort_keys=True), flush=True)
    return 0 if success else 1


def non_negative_int(value):
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return number


def positive_int(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def command_list(args):
    try:
        for case in discover_cases(REPO_ROOT, args.suite, args.test_set,
                                   args.include_excluded, check_results=False):
            print(case.name)
    except RunnerError as exc:
        print("[mysqltest][ERROR] {}".format(exc), file=sys.stderr)
        return 1
    return 0


def add_selection_arguments(parser):
    parser.add_argument("--suite", action="append", default=[],
                        help="suite names, comma-separated or repeated")
    parser.add_argument("--test-set", action="append", default=[],
                        help="case names (prefer suite.case), comma-separated or repeated")
    parser.add_argument("--include-excluded", action="store_true",
                        help="include cases disabled in mysqltest_config.yaml")


def create_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command")

    listing = subparsers.add_parser("list", help="list cases without starting an instance")
    add_selection_arguments(listing)
    listing.set_defaults(handler=command_list)

    run = subparsers.add_parser("run", help="run mysqltest cases in a disposable instance")
    add_selection_arguments(run)
    run.add_argument("--seekdb", default=str(REPO_ROOT / "build/bin/src/observer/seekdb"),
                     help="seekdb executable (default: build/bin/src/observer/seekdb)")
    run.add_argument("--obclient", default=str(REPO_ROOT / "deps/3rd/u01/obclient/bin/obclient"),
                     help="obclient executable from build.py init")
    run.add_argument("--mysqltest", default=str(REPO_ROOT / "deps/3rd/u01/obclient/bin/mysqltest"),
                     help="mysqltest executable from build.py init")
    run.add_argument("--base-dir", required=True, help="seekdb base directory")
    run.add_argument("--work-dir", required=True, help="slice output directory")
    run.add_argument("--host", default="127.0.0.1")
    run.add_argument("--port", type=positive_int, default=2881)
    run.add_argument("--slice-index", type=non_negative_int, default=0)
    run.add_argument("--slice-count", type=positive_int, default=1)
    run.add_argument("--record", action="store_true",
                     help="write selected cases' expected .result files")
    run.add_argument("--parameter", action="append", default=[],
                     help="seekdb key=value parameter; may be repeated")
    run.set_defaults(handler=command_run)

    merge = subparsers.add_parser("merge", help="merge mysqltest slice results")
    merge.add_argument("--results-dir", required=True)
    merge.add_argument("--slice-count", type=positive_int, required=True)
    merge.add_argument("--run-id", default="0")
    merge.add_argument("--output", required=True)
    merge.set_defaults(handler=command_merge)

    return parser


def main(argv=None):
    parser = create_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "handler"):
        parser.print_usage(sys.stderr)
        return 2
    if args.command == "run" and args.slice_index >= args.slice_count:
        parser.error("slice-index must be smaller than slice-count")
    if args.command in ("run", "list"):
        if ((args.suite and not selection_values(args.suite)) or
                (args.test_set and not selection_values(args.test_set))):
            parser.error("--suite and --test-set must contain at least one name")
    if args.command == "run" and args.record and not (args.test_set or args.suite):
        parser.error("--record requires --test-set or --suite")
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
