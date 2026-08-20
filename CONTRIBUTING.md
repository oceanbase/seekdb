# Contributing to seekdb

Thank you for contributing to seekdb. This document is the canonical contribution workflow for this repository.

## Before you start

- Search the [issue tracker](https://github.com/oceanbase/seekdb/issues) before opening a new issue.
- Bug fixes should include a regression test whenever practical.
- Discuss large features and user-visible design changes with maintainers before implementation.
- Read the [Developer Guide](docs/developer-guide/en/README.md) for the toolchain, build, test, and coding guidance.
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Prepare a fork

Fork the repository on GitHub, then clone your fork and add the official repository as `upstream`:

```bash
git clone https://github.com/<your-github-name>/seekdb.git
cd seekdb
git remote add upstream https://github.com/oceanbase/seekdb.git
git fetch upstream
git switch -c fix/issue-123 upstream/master
```

Use a focused branch name such as `fix/issue-123` or `feature/short-description`.

## Build and test

Install the supported toolchain first, then build the Release compatibility target:

```bash
source ~/.bashrc
./build.sh release --init --make
```

The seekdb binary is generated at `build_release/src/observer/seekdb`.

Bazel is the authoritative modular build and test graph. Query the available unit-test targets and run the affected module, for example:

```bash
./bazel.py query 'attr(name, ".*_tests", //unittest/...)'
./bazel.py test //unittest/observer:observer_tests
```

Run the affected mysqltest cases when a change alters SQL or server behavior. See [Running mysqltest](docs/developer-guide/en/mysqltest.md) for the current `obd.sh` workflow.

The CMake `pretest` target remains available for Farm compatibility, but it is not the primary way to add or run modular unit tests.

## Prepare the change

- Keep the change focused and preserve backward compatibility unless the issue explicitly approves a breaking change.
- Follow the existing code style and add or update documentation for user-visible behavior.
- Stage only the intended files instead of using `git add .`:

  ```bash
  git add path/to/changed_file path/to/test_file
  git diff --cached --check
  git diff --cached
  ```

- Use a clear commit message that explains the behavior changed.

## Submit a pull request

1. Rebase your branch on the latest `upstream/master`.
2. Push the branch to your fork.
3. Open a pull request targeting `oceanbase/seekdb:master`.
4. Link the related issue, describe what changed and why, and list the validation performed.
5. Complete the Contributor License Agreement when prompted.
6. Address review feedback and ensure the required compile and Farm checks pass.

Small, focused pull requests are easier to review and merge. If an unrelated CI job fails, report the failure details to the reviewer rather than hiding or bypassing the check.
