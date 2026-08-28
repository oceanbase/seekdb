# Write and run unit tests

seekdb uses [Google Test](https://google.github.io/googletest/) for C++ unit tests. Tests live under `unittest/` and are grouped into CMake module targets such as `observer_tests`, `storage_tests`, and `sql_tests`.

## Configure the test build

CMake unit-test targets are available on Linux. Initialize the dependencies and configure the Release build before building a test module:

```bash
./build.sh release --init
```

`--init` is normally required only for the first build or after dependency metadata changes. For later reconfiguration, run `./build.sh release` without `--init`.

## Choose and build a module

List the available module targets:

```bash
make -C build_release help | grep '_tests'
```

Build the module affected by the change, for example:

```bash
make -C build_release observer_tests
make -C build_release storage_tests
```

Each binary is generated at `build_release/unittest/<module>/<module>_tests`.

## Run a module

The generated `run_tests.sh` wrapper invokes CTest with the environment and working directories required by the tests. Run all shards of a module with a regular expression, for example:

```bash
./build_release/unittest/run_tests.sh \
  -R '^observer_tests_shard_[0-9]+$' \
  --output-on-failure
```

To run one Google Test case, set `GTEST_FILTER` while keeping the module shard selection:

```bash
GTEST_FILTER='ComponentName.handles_invalid_input' \
  ./build_release/unittest/run_tests.sh \
  -R '^observer_tests_shard_[0-9]+$' \
  --output-on-failure
```

Prefer the narrowest module and test filter that cover the change before running a wider test set.

## Write a test

Name test files `test_*.cpp`, place them under the matching `unittest/<module>/` directory, include `<gtest/gtest.h>`, and add focused `TEST` or `TEST_F` cases. CMake discovers the module's C++ test sources automatically. Follow the module's shared-main pattern; do not add a separate `main()`.

```cpp
TEST(ComponentName, handles_invalid_input)
{
  ASSERT_EQ(OB_INVALID_ARGUMENT, call_with_invalid_input());
}
```

Use `ASSERT_*` when the rest of the test cannot continue after a failure and `EXPECT_*` when subsequent checks remain meaningful.

## Build and run all modules

The `pretest` target builds all CMake unit-test modules. Run the registered CTest shards separately after the build completes:

```bash
make -C build_release pretest
./build_release/unittest/run_tests.sh --output-on-failure
```

Pass `-j <jobs>` to `run_tests.sh` when parallel execution is appropriate. CI also runs unit tests and mysqltest cases for pull requests. Record the exact targets and filters you ran in the pull-request description.
