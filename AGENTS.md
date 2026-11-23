# Project Agent Guidance

## License Headers

- Add the repository's standard OceanBase copyright and Apache 2.0 license header to every new database source file, including C/C++ headers and Rust sources.
- Scripts and build files, including Bazel files, do not require license headers.

## Naming

- Do not add project-name prefixes (`Seekdb`, `SeekDB`, or `seekdb_`) to new type or function names. Use namespaces or modules to distinguish names.
- New C++ classes, including interface classes, do not need the legacy `Ob` prefix.
- Keep the `I` prefix for interface classes. For example, use `ICacheMemoryGetter` instead of `ObICacheMemoryGetter`.
- Do not rename existing types only to remove the `Ob` prefix unless the task explicitly requires it.

## C++ Headers

- Use `#pragma once` in new C++ header files instead of macro-based `#ifndef`/`#define` include guards.

## Rust Toolchain

- Write Rust code for the version pinned in `rust-toolchain.toml`. Prefer language features and standard library APIs stabilized in that version or earlier when they simplify the implementation. Compatibility with older compilers is not required. Do not rewrite existing code solely to adopt newer features.

## Rust–C++ Interoperability

- Before designing, implementing, or reviewing a Rust migration or refactor, read and follow [Rust refactoring guidelines](docs/developer-guide/en/rust-refactoring.md). This includes changes to C++ callers, CXX bridges, and build integration within that task.
- Use CXX (`#[cxx::bridge]`) for Rust–C++ interoperability in both directions.
- Extend the repository's existing CXX bridges and build integration; do not add handwritten C ABI bridges or another FFI framework for Rust–C++ calls.

## Product Compatibility and Design

- Unless a task explicitly states otherwise, all features target newly deployed clusters and do not require backward compatibility with previous product versions. This applies to persisted data and indexes, storage formats, interfaces, protocols, configuration, and observable behavior. Do not preserve legacy designs solely for compatibility or treat the absence of backward compatibility alone as a defect or migration blocker; evaluate new designs on their own correctness, simplicity, and maintainability.

## Code Review

- For pull request or diff review tasks, read and follow `.agents/skills/code-review/SKILL.md`.

## Pull Request Submission

- For pull request preparation, description, submission, and description-update tasks, read and follow `.agents/skills/submit-pull-request/SKILL.md`.

## Testing

- Add unit tests only for the internal Rust implementation of the seekdb product.
- Do not add unit tests for any other component, including C++ code and Rust FFI or CXX bridge layers exposed to C++, even when those layers live in a product Rust crate.
- Validate other components through integration tests, mysqltest cases, or actual execution instead of adding unit tests. Use the repository's established test suites and test directories where applicable.
- Keep test-only and benchmark-only files out of `tools/`.
- Validate tool and script changes by running the relevant commands and inspecting their outputs or generated artifacts.

### Downstream integration tests

Before submitting a PR that changes any of these areas, check the relevant
cases in the `seekdb_test` repository. The paths below are examples.

- Instance parameter definitions in `rust/config/parameters.yaml`, or code
  that changes a parameter's default, accepted values, or when it takes
  effect.
- Global or session variable definitions in
  `src/share/system_variable/ob_system_variable_init.json`, or variable
  initialization, assignment, inheritance, or reset in `src/sql/session/`.
- SQL and client behavior such as syntax, results, warnings, error codes,
  execution plans, metadata, or protocol responses. Examples include
  `src/sql/parser/`, `src/sql/optimizer/`, `src/sql/engine/`, and
  `src/observer/mysql/`.
- Critical runtime paths such as transaction commit and rollback in
  `src/storage/tx/`, MVCC in `src/storage/memtable/mvcc/`, compaction in
  `src/storage/compaction/`, and log replay in `src/logservice/replayservice/`.
  Check changes to ordering, concurrency, retries, timeouts, and recovery.
- Server startup, shutdown, or readiness, for example in
  `src/observer/ob_server.cpp`. Check changes to binaries, startup options,
  or deployment that affect how `seekdb_test` starts or connects to SeekDB.

Update affected cases and preserve assertions that still apply. In the PR's
`验证` section, provide one of the following.

- A link to the `seekdb_test` MR or commit that updates the affected cases.
  List the cases and report the result of running them against a build that
  includes the SeekDB PR changes.
- An explicit statement that `seekdb_test` needs no update. Identify the
  relevant suite, case, or behavior you checked and explain why no update
  is needed.

If you lack access to `seekdb_test`, ask a maintainer with access to confirm
the test impact. Record any pending validation.
