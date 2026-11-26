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

Check the relevant `seekdb_test` cases when a PR can affect any behavior
below. The listed paths are examples.

- Instance parameter definitions in `rust/config/parameters.yaml`, or code
  that changes a parameter's default, accepted values, or when it takes
  effect.
- Global or session variable definitions in
  `src/share/system_variable/ob_system_variable_init.json`, or variable
  initialization, assignment, inheritance, or reset in `src/sql/session/`.
- Changes related to internal tables, for example in `src/share/inner_table/`.
- SQL and client behavior such as syntax, results, warnings, error codes,
  execution plans, metadata, or protocol responses. Examples include
  `src/sql/parser/`, `src/sql/optimizer/`, `src/sql/engine/`, and
  `src/observer/mysql/`.
- Shared runtime behavior such as thread or coroutine startup and teardown,
  stack management, scheduling, synchronization, memory allocation, and
  resource limits. For example, trace changes in `src/oblib/lib/thread/` to
  their effects on instance startup, concurrent workloads, and shutdown.
- Critical runtime paths such as transaction commit and rollback in
  `src/storage/tx/`, MVCC in `src/storage/memtable/mvcc/`, compaction in
  `src/storage/compaction/`, and log replay in `src/logservice/replayservice/`.
  Check changes to ordering, concurrency, retries, timeouts, and recovery.
- Server startup, shutdown, or readiness, for example in
  `src/observer/ob_server.cpp`. Check changes to binaries, startup options,
  or deployment that affect how `seekdb_test` starts or connects to seekdb.

Decide whether existing `seekdb_test` cases need updates. If they do, preserve
assertions that still apply and link the test MR or commit in the PR's `验证`
section. If the cases need no update, name the cases or scenarios you checked
and explain why their assertions still apply.

Record the regression decision in `验证` even when the cases need no update.

- **Run the relevant regression jobs.** Link the `seekdb_test` pipeline and
  record the tested SeekDB SHA, the jobs or cases that ran, and their results.
  Choose jobs that exercise the affected behavior. Record relevant platforms
  and runtime conditions. If the tested SHA differs from the PR head, show
  that the tested source and relevant build inputs match the current head.
- **Request a skip.** Name the affected behavior and the cases you checked.
  Explain why the available jobs are unnecessary or cannot exercise that
  behavior. If you rely on an equivalent test, link its result for the same
  behavior, revision, and runtime conditions. State any remaining coverage gaps.

Mark missing access, a queued run, or an infrastructure failure as pending.
Ask a maintainer with access to help when needed. Reviewers must verify the
test result or confirm the skip justification before approval.
