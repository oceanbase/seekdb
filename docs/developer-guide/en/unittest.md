# Write and run unit tests

C++ unit tests use Google Test and explicit Bazel `cc_test` targets. The current checkout contains two allocator tests under `unittest/oblib/`.

## Build tests on Linux

```bash
./build.py init
./build.py build //unittest/oblib:test_malloc_backend
./build.py build --sanity //unittest/oblib:test_jemalloc_hook
```

`build` compiles and links the tests without running them. The jemalloc hook test requires `--sanity`, which enables the bundled jemalloc backend. Test binaries appear under `build/bin/` in their target's package directory.

List the checked-in test targets with:

```bash
./build.py query 'kind(cc_test, //unittest/...)'
```

## Run a test

```bash
./build.py test //unittest/oblib:test_malloc_backend --test_output=errors
./build.py test --sanity //unittest/oblib:test_jemalloc_hook --test_output=errors
```

## Add a test

Place module tests under `unittest/<module>/`, name the source `test_*.cpp`, and declare its dependencies explicitly in `BUILD.bazel`. Reuse the module's test interfaces and runtime; do not add dependencies on another module's private implementation. Each executable needs one Google Test main. Use `ASSERT_*` when subsequent checks cannot proceed after failure and `EXPECT_*` when they can.

SQL integration tests use the separate [mysqltest workflow](mysqltest.md). Record the exact build or test targets used in the pull-request description.

## Run native Rust tests

```bash
./build.py test //rust/json:test --test_output=errors
```

Native `test` commands compile the Rust test executable and its target dependencies with `panic=unwind`. The standard Rust test runner catches assertion failures and reports the failed test. Ordinary `build` commands retain `panic=abort`. The Wasm configuration retains its existing abort policy.

To compile a native Rust test executable without running it, apply the test policy explicitly to the build command.

```bash
./build.py build //rust/json:test --@rules_rust//rust/settings:extra_rustc_flags=-Cpanic=unwind
```
