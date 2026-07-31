# Building and Running Unit Tests with Bazel

Bazel owns the seekdb C++ unit-test suite. Every test under `unittest` belongs
to one production module. Each module produces one test binary, and Bazel runs
its GTest cases in parallel shards.

Run the complete suite:

```bash
./bazel.py test //unittest/...
```

Run one module, for example OBLib:

```bash
./bazel.py test //unittest/oblib:oblib_tests
```

Run matching GTest cases only:

```bash
./bazel.py test //unittest/sql:sql_tests \
  --test_arg='--gtest_filter=TestName.CaseName'
```

Remote compilation accepts the regular Bazel options:

```bash
./bazel.py test //unittest/... \
  --jobs=80 \
  --remote-executor="${REMOTE_EXECUTOR}"
```

`unittest/run_tests.sh` and `unittest/oblib/run_tests.sh` are convenience
entry points for the complete suite and OBLib tests. Both delegate to the
root-level `bazel.py`.

## Test Build Model

- Every test source has exactly one module owner. Ownership is defined
  centrally in `bazel/architecture/module_policy.bzl`.
- Every module uses the single `unittest/all_tests_main.cpp` entry point. Test
  sources must not define `main()`.
- A module test sees production headers only through that module's
  `*_unit_test_interface`. `liboceanbase.so` supplies implementation symbols
  without broadening source-level visibility.
- Test sources are Unity-compiled according to `unity_size`. A source that
  demonstrably cannot participate in Unity must appear in `unity_exceptions`
  with a reason. It still belongs to the same module test binary; it does not
  create a standalone test program.
- `shard_count` controls how many GTest shards Bazel may execute in parallel
  for the module binary.

## Adding or Changing a Test

1. Put the test under its `unittest/<module>` owner and do not add `main()`.
2. Test only that module. Direct access to a peer business module is a module
   or integration test and must instead use the owner's interface, fixture, or
   fake, or move outside the C++ unit-test suite.
3. Declare runtime files explicitly in the module `BUILD.bazel` `data`.
4. Keep sources in normal Unity groups by default. Only proven macro pollution
   or translation-unit symbol collisions justify a reasoned
   `unity_exceptions` entry.
5. Performance, stress, and benchmark programs do not belong to the default
   unit-test suite.

The central gate checks ownership, cross-module dependencies, legacy
`main()` functions, test sources omitted from the build, and performance or
stress naming. Architectural changes must update and review the central policy
instead of bypassing it in an individual `BUILD.bazel`.
