# Project Agent Guidance

## License Headers

- Add the repository's standard OceanBase copyright and Apache 2.0 license header to every new database source file, including C/C++ headers and Rust sources.
- Scripts and build files, including Bazel files, do not require license headers.

## C++ Type Naming

- New C++ classes, including interface classes, do not need the legacy `Ob` prefix.
- Keep the `I` prefix for interface classes. For example, use `ICacheMemoryGetter` instead of `ObICacheMemoryGetter`.
- Do not rename existing types only to remove the `Ob` prefix unless the task explicitly requires it.

## C++ Headers

- Use `#pragma once` in new C++ header files instead of macro-based `#ifndef`/`#define` include guards.

## Rust–C++ Interoperability

- Use CXX (`#[cxx::bridge]`) for Rust–C++ interoperability in both directions.
- Extend the repository's existing CXX bridges and build integration; do not add handwritten C ABI bridges or another FFI framework for Rust–C++ calls.

## Code Review

- For pull request or diff review tasks, read and follow `.agents/skills/code-review/SKILL.md`.

## Testing

- Prefer adding regression and correctness coverage to the repository's established test suites and test directories.
- Keep test-only and benchmark-only files out of `tools/`.
- Do not add tests for developer tools or scripts, including build, code-generation, packaging, and dependency-preparation tools, regardless of where the tests would live.
- Validate tool and script changes by running the relevant commands and inspecting their outputs or generated artifacts.
