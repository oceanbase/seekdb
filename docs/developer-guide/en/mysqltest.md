# Run SQL regression tests

`sqltest` runs the repository's mysqltest cases against disposable seekdb instances.

## Prepare the binaries

Build seekdb using the [build and run guide](build-and-run.md). On Linux x86_64, add the bundled `sqltest` to your path from the repository root.

```bash
export PATH="$PWD/tools/sqltest/bin/linux-amd64:$PATH"
```

On other platforms, put a `sqltest` binary for your platform on your path.

## Select and run cases

Run these commands from the seekdb repository root. List discovered cases with `--list-cases`.

```bash
sqltest --test-root tools/deploy/mysql_test --list-cases
```

Run one case by its qualified name.

```bash
sqltest --seekdb build/bin/src/observer/seekdb \
  --test-root tools/deploy/mysql_test --run-dir "$(mktemp -d "$PWD/build/sqltest.XXXXXX")" \
  expr.expr_ceil
```

Use a selector such as `expr/` to run a suite. Omit the selector to run all enabled cases. Add `-j 10` for 10 workers. Cases under `tools/deploy/mysql_test/disabled/` are outside automatic discovery. To reproduce one, temporarily move the test and its expected result into the corresponding directories under `cases/`, run it by name, and move the pair back.

## Update expected results

Put a suite's tests under `cases/<suite>/t/` and expected results under `cases/<suite>/r/`. Put cases outside a suite under `cases/t/` and `cases/r/`.

For a new case, create an empty `.result` with the same basename as the `.test` file. Run the case. When the actual output differs, `sqltest` writes the full output to a `.reject` file under `tools/deploy/mysql_test/rejects/`. Review it, copy it over the expected `.result`, and rerun the case to confirm the result.

## GitHub Actions

The `build-and-test` job runs all enabled cases with 24 workers. The job summary gives the NFS path to `sqltest.log` and any available failure diagnostics. The required `collect` check reports whether the `build-and-test` job passed.
