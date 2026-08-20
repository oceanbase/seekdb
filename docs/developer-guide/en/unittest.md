# Write and run unit tests

seekdb uses [Google Test](https://google.github.io/googletest/) for C++ unit tests. Tests live under `unittest/` and are organized into module targets such as `observer_tests`, `storage_tests`, and `sql_tests`.

## Find a test target

Bazel is the authoritative modular build and test graph. List the available unit-test rules with:

```bash
./bazel.py query 'attr(name, ".*_tests", //unittest/...)'
```

Each module defines its targets in `unittest/<module>/BUILD.bazel`. When adding a test source, add it to the appropriate module's Bazel target instead of creating a separate executable.

## Build and run a module

Run the affected module target directly, for example:

```bash
./bazel.py test //unittest/observer:observer_tests
./bazel.py test //unittest/storage:storage_tests
```

Pass standard Bazel test options after the command when you need filtered output or additional diagnostics. Prefer the narrowest module that covers the change before running a wider test set.

## Write a test

Name test files `test_*.cpp`, include `<gtest/gtest.h>`, and add focused `TEST` or `TEST_F` cases. Follow the setup and shared-main pattern already used by the selected module; do not add a separate `main()` unless that target explicitly requires one.

```cpp
TEST(ComponentName, handles_invalid_input)
{
  ASSERT_EQ(OB_INVALID_ARGUMENT, call_with_invalid_input());
}
```

Use `ASSERT_*` when the rest of the test cannot continue after a failure and `EXPECT_*` when subsequent checks remain meaningful.

## Run a wider test set

When the focused module passes and the change has broader impact, run all Bazel unit-test targets:

```bash
./bazel.py test //unittest/...
```

CI runs unit tests and mysqltest cases for pull requests. Record the exact targets and cases you ran in the pull-request description.
