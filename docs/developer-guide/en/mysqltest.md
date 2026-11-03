# Running mysqltest

`tools/deploy/mysqltest.py` runs SQL regression cases directly against seekdb. Local development and CI use the same runner. The runner manages its own disposable instance on Linux and macOS.

## Prepare the binaries

Follow [Build and run](build-and-run.md) to prepare dependencies and build seekdb:

```bash
./build.py init
./build.py build seekdb
```

The default executables are `build/bin/src/observer/seekdb`, `deps/3rd/u01/obclient/bin/obclient`, and `deps/3rd/u01/obclient/bin/mysqltest`. Override them with `--seekdb`, `--obclient`, and `--mysqltest` when using another build or client installation.

## Select and run cases

Run commands from the repository root. List available cases without starting an instance:

```bash
python3 tools/deploy/mysqltest.py list --suite expr
```

Run one case using its qualified `suite.case` name:

```bash
python3 tools/deploy/mysqltest.py run \
  --base-dir "$PWD/build/mysqltest-instance" \
  --work-dir "$PWD/build/mysqltest-results" \
  --port 2881 --test-set expr.expr_ceil
```

Use a free local port. `--base-dir` must be a dedicated disposable directory: the runner destroys the managed instance before the run and again when it finishes. It refuses to adopt a non-empty directory it does not manage. Use `sdb.py` from [Build and run](build-and-run.md) for instances whose data you want to retain.

Replace `--test-set expr.expr_ceil` with `--suite expr` to run a suite, or omit both options to run all enabled cases. Both options accept comma-separated values and may be repeated. Combining them selects cases within the specified suites. Bare case names are accepted when unambiguous; prefer `suite.case` for suite cases. Top-level cases, such as `minitest`, use their bare names.

Cases in `tools/deploy/mysqltest_config.yaml`'s exclusion list are skipped by default. Use `--include-excluded` to reproduce a disabled case. Unknown or ambiguous selections fail before starting or deleting an instance.

## Record expected results

After adding a `.test` file, record its expected `.result` file:

```bash
python3 tools/deploy/mysqltest.py run \
  --base-dir "$PWD/build/mysqltest-instance" \
  --work-dir "$PWD/build/mysqltest-results" \
  --port 2881 --test-set expr.expr_ceil --record
```

`--record` requires an explicit case or suite selection and writes the corresponding source-tree result files. Missing result files are allowed in this mode. Review the generated diff, then run again without `--record` to verify it.

## Output and CI slices

The runner initializes the test database and users, invokes the mysqltest binary, and exits nonzero if a case or infrastructure step fails. Results are written to `--work-dir/seekdb_result.json`; client logs and failure diagnostics are saved under the same work directory. Instance data is removed after the run.

| Option | Meaning |
| --- | --- |
| `--suite`, `--test-set` | Select suites or cases; omitted means all enabled cases. |
| `--record` | Write expected results for an explicit selection. |
| `--include-excluded` | Include cases disabled in the shared configuration. |
| `--parameter key=value` | Pass a server setting on every start or retry; may be repeated. |
| `--slice-index`, `--slice-count` | Zero-based CI slice index and total count; defaults are `0` and `1`. |

CI continues to use `.github/script/seekdb/mysqltest_slice.sh`, which invokes this runner. Merge slice results with `python3 tools/deploy/mysqltest.py merge --help` for the required arguments.
