# Contributing to OceanBase seekdb

First off, thank you for considering contributing to OceanBase seekdb! We value your time and effort.

The following are a set of guidelines for contributing to OceanBase seekdb. Following these guidelines makes contributing to this project easy and transparent. These are mostly guidelines, not rules. Use your best judgment, and feel free to propose changes to this document in a pull request.

**Content**

- [Contributing to OceanBase seekdb](#contributing-to-oceanbase-seekdb)
  - [Code of Conduct](#code-of-conduct)
  - [Contribution Workflow](#contribution-workflow)
    - [Step 1: Start with an Issue](#step-1-start-with-an-issue)
    - [Step 2: Bug Fixes and New Features](#step-2-bug-fixes-and-new-features)
    - [Step 3: Setup & Coding](#step-3-setup--coding)
    - [Step 4: Build & Test](#step-4-build--test)
    - [Step 5: Submit Pull Request](#step-5-submit-pull-request)
    - [Step 6: Review & Merge](#step-6-review--merge) 
  - [General Guidelines](#general-guidelines)
    - [Best Practices](#best-practices) 
    - [CI Checks](#ci-checks) 
    - [PR Style Guides](#pr-style-guides) 

## Code of Conduct

By participating, you are expected to uphold our [Code of Conduct](CODE_OF_CONDUCT.md). Please report unacceptable behavior to the project maintainers.

## Contribution Workflow

**Our workflow differs depending on whether you are fixing a bug or implementing a new feature. We prioritize design and discussion for new features to ensure architectural consistency.**

### Step 1: Start with an Issue
All contributions must be associated with a GitHub Issue.

+ **Search first**: Check if the issue already exists.
+ **Create new**: If not, [open a new issue](https://github.com/oceanbase/seekdb/issues).
    - Use `[Bug]` in the title for bugs.
    - Use `[Feature]` in the title for new capabilities.

### Step 2: Bug Fixes and New Features
#### Bug Fixes
For verified bugs, you can proceed directly to coding after the issue is acknowledged.

1. **Fork** the repository to your own GitHub account.
2. **Branch** from `develop` (e.g., `fix/issue-123`).
3. **Fix** the bug and add a regression test case.
4. **Submit PR**.

#### New Features (The "Design-First" Rule)
For new features, we strictly enforce a **Design-First** workflow. **Do not submit code PRs for large features without an approved design.**

1. **Discuss**: Share your initial thoughts in the Issue comments. Wait for a maintainer to validate the requirement.
2. **Draft Design**: Write a design document in Markdown.
3. **Submit Design PR**:
    - Place your file in the `docs/design/` directory.
    - Filename convention: `YYYY-MM-DD-feature-name.md`.
    - Submit a Pull Request targeting the `develop` branch.
    - **Title**: `[Design] Proposal for <Feature Name>`.
4. **Design Review**: Maintainers will review the architecture, API changes, and compatibility.
5. **Implementation**: Once the Design PR is **merged**, you may start coding.

### Step 3: Setup & Coding
We follow a standard "fork-and-pull" workflow. All development should be based on the develop branch.

1. Fork the [seekdb repository](https://github.com/oceanbase/seekdb) on GitHub.
2. Clone your fork:
   ```bash
   git clone https://github.com/<your-github-name>/seekdb.git
   cd seekdb
   ```
3. [Configure](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/working-with-forks/configuring-a-remote-repository-for-a-fork) your local repo by adding the remote official repo as upstream:
   ```bash
   git remote add upstream https://github.com/oceanbase/seekdb.git
   ```
4. Create a Branch Sync with upstream and create a descriptive branch:
   ```bash
   git fetch upstream
   git checkout upstream/develop -b fix/issue-123
   ```
   > Naming convention: type/description-issue-number

### Step 4: Build & Test
Before submitting, you must ensure the code builds and passes tests locally.The development guide is located under the [docs](docs/README.md) folder. Please refer to it for detailed build instructions.

**Basic build commands**:

```bash
# Configure the project (Generate build files)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build for debugging
cmake debug

# Build the project
cmake --build build -- -j$(nproc)

```

* Coverage: Write unit tests for all new features and bug fixes, including edge cases.
* Regression: Ensure all local tests pass before submitting.
* Consistency: Follow existing test patterns.

**Run tests**:
```bash
# Run all tests via [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html):
cd build
ctest --output-on-failure

# Alternatively, for Debug build:
cd build_debug
ctest --output-on-failure
```

### Step 5: Submit Pull Request
When you are ready to submit your code:

1. Push your branch to your forked repository.
2. Open a Pull Request against the `develop` branch of `oceanbase/seekdb`.
3. **Link the Issue**: In the PR description, explicitly link the issue (e.g., `Fixes #123`) and the Design PR (if applicable).
4. **Sign the CLA**: You will be prompted to sign the OceanBase Contributor License Agreement (CLA) if you haven't already.

### Step 6: Review & Merge
1. **Code Review**: Community maintainers will review your code. Be prepared to make changes based on feedback.
2. **CI**: Your PR must pass all automated CI checks (Build, Test, Linter).
3. **Merge**: Once approved by maintainers and passing CI, your code will be merged!

>By default, pull requests are merged into the `develop` branch, which is the default branch of [seekdb](https://github.com/oceanbase/seekdb). The maintainers will merge `develop` into `master` branch periodically. If you want to get the latest code, you can pull the `master` branch.


## General Guidelines

Before submitting your pull requests for review, make sure that:

- Your changes are consistent with the project's coding style.
- You have run all relevant tests and they pass.
- Your code is well-documented and commented where necessary.
- You have considered backward compatibility when making changes.
- The maintenance burden of new features is acceptable.

### Best Practices

- **Avoid large pull requests**: Large PRs are much less likely to be merged as they are incredibly hard to review. Split large changes into smaller, focused PRs.
- **Discuss first**: For major changes or new features, discuss your intended changes with the core team on GitHub before starting implementation.
- **Announce your work**: If you're working on an existing issue, announce that you are working on it to avoid duplicate work.
- **Update documentation**: If your changes affect user-facing functionality, update the relevant documentation.

### CI Checks

All pull requests must pass continuous integration (CI) checks before merging. Currently, there are two types of CI checks:

- **Compile**: This check will compile the code on CentOS and Ubuntu to ensure your changes compile successfully on different platforms.
- **Farm**: This check will run the unit tests and some MySQL test cases to ensure your changes don't break existing functionality.

> **Note**: If the farm check fails and you think it is not related to your changes, you can ask the reviewer to re-run the farm check, or the reviewer will re-run it if needed.


### PR Style Guides

Follow the same guidelines as commit messages. Provide a clear description of:
- What changes were made
- Why the changes were necessary
- How the changes were tested
- Any breaking changes or migration notes

For more guidance on writing good commit messages, refer to [this guide](https://chris.beams.io/posts/git-commit/).

---

Thank you for contributing to OceanBase seekdb! Your help is essential for making it better.