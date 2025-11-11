# Contributing to OceanBase

Contributions to OceanBase are welcome from everyone. We strive to make the contribution process simple and straightforward.

The following are a set of guidelines for contributing to OceanBase. Following these guidelines makes contributing to this project easy and transparent. These are mostly guidelines, not rules. Use your best judgment, and feel free to propose changes to this document in a pull request.

**Content**

- [Contributing to OceanBase](#contributing-to-oceanbase)
  - [Code of Conduct](#code-of-conduct)
  - [How can you contribute?](#how-can-you-contribute)
    - [Did you find a bug?](#did-you-find-a-bug)
    - [Did you write a patch that fixes a bug?](#did-you-write-a-patch-that-fixes-a-bug)
    - [Requesting a new feature](#requesting-a-new-feature)
    - [Feature Development](#feature-development)
  - [GitHub Workflow](#github-workflow)
  - [Pull Requests](#pull-requests)
  - [CI Checks](#ci-checks)
  - [Building](#building)
  - [Testing](#testing)
  - [General Guidelines](#general-guidelines)
  - [Commits and PRs](#commits-and-prs)

## Code of Conduct

This project and everyone participating in it is governed by a [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to the project maintainers.

Before you start contributing, please make sure you have read and understood our [Code of Conduct](CODE_OF_CONDUCT.md).

## How can you contribute?

### Did you find a bug?

* **Ensure the bug was not already reported** by searching on GitHub under [Issues](https://github.com/oceanbase/seekdb/issues).
* If you're unable to find an open issue addressing the problem, [open a new one](https://github.com/oceanbase/seekdb/issues/new/choose). Be sure to include:
  - A **clear and descriptive title**
  - A **detailed description** of the problem
  - **Steps to reproduce** the issue
  - **Expected behavior** vs **actual behavior**
  - **Environment information** (OS, version, etc.)
  - **Code samples** or **executable test cases** demonstrating the issue

### Did you write a patch that fixes a bug?

* Great! We appreciate your contribution.
* If possible, add a unit test case to make sure the issue does not occur again.
* Make sure you run the code formatter and static analysis tools before submitting.
* Open a new GitHub pull request with the patch.
* Ensure the PR description clearly describes the problem and solution. Include the relevant issue number if applicable.

### Requesting a new feature

If you require a new feature or major enhancement, you can:

* (**Recommended**) File an issue about the feature/enhancement with:
  - Clear description of the feature and its use cases
  - Reasoning for why this feature would be beneficial
  - Potential implementation approach (if applicable)
* Wait for the maintainers to review and discuss the proposal
* Once approved, you can proceed with implementation

### Feature Development

If you want to develop a new feature, follow this process:

1. **Create a discussion**: Start a [discussion](https://github.com/oceanbase/seekdb/discussions/new/choose) to discuss your feature idea with the community.
2. **Create an issue**: If your idea is accepted, create a new issue to track the feature development.
3. **Feature branch**: The maintainers will create a feature branch for you on the main repository.
4. **Fork and clone**: Fork the repository and clone your fork to your local machine.
5. **Develop**: Make your changes and commit them to your fork.
6. **Pull request**: Create a pull request to merge your code into the feature branch.
7. **Merge**: After your pull request is merged, the feature branch will be merged into the develop branch, and eventually into master.

## GitHub Workflow

Generally, we follow the "fork-and-pull" Git workflow.

### Setup

1. [Fork](https://docs.github.com/en/github/getting-started-with-github/fork-a-repo) the [SeekDB repository](https://github.com/oceanbase/seekdb) on GitHub.
2. Clone your fork to your local machine:
   ```bash
   git clone https://github.com/<your-github-name>/seekdb.git
   cd seekdb
   ```
3. [Configure](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/working-with-forks/configuring-a-remote-repository-for-a-fork) your local repo by adding the remote official repo as upstream:
   ```bash
   git remote add upstream https://github.com/oceanbase/seekdb.git
   ```

### Making Changes

1. **Sync with upstream**: Before starting, fetch the latest changes from upstream:
   ```bash
   git fetch upstream
   git checkout upstream/develop -b your-feature-branch
   ```
   Or if you're working on an existing branch:
   ```bash
   git checkout your-feature-branch
   git pull upstream develop
   ```

2. **Create a branch**: Create a new branch for your feature or bug fix:
   ```bash
   git checkout -b feature-branch
   ```
   > **Note**: Choose a descriptive name for your branch. For example: `fix-memory-leak`, `add-index-optimization`, etc.

3. **Make changes**: Make your changes and commit them:
   ```bash
   git add .
   git commit -m "Description of your changes"
   ```

4. **Push to your fork**: Push your changes to your fork:
   ```bash
   git push origin feature-branch
   ```

5. **Create Pull Request**: Click on `Compare & Pull request` on GitHub to create a pull request.

Remember to [sync your forked repository](https://docs.github.com/en/github/getting-started-with-github/fork-a-repo#keep-your-fork-synced) _before_ submitting proposed changes upstream. If you have an existing local repository, please update it before you start, to minimize the chance of merge conflicts.

## Pull Requests

### Before Submitting

* Do not commit/push directly to the main branch. Instead, create a fork and file a pull request.
* When maintaining a branch, merge frequently with the develop branch.
* If you are working on a bigger issue, try to split it up into several smaller pull requests.
* Please do not open "Draft" pull requests unless necessary. Rather, use issues or discussions to discuss whatever needs discussing.
* Make sure your code follows the project's coding style and guidelines.
* Ensure all tests pass before submitting.

### PR Description

Ensure the PR description clearly describes:
- **Problem**: What issue does this PR solve?
- **Solution**: How does this PR solve the problem?
- **Testing**: How was this PR tested?
- **Related Issues**: Link to any related issues (e.g., `Fixes #123`)

### Review Process

* After you create the pull request, a member of the OceanBase team will review your changes and provide feedback.
* Address any review comments by making additional commits to your branch.
* Once all reviewers are satisfied, they will approve and merge your pull request.

### After Merge

By default, pull requests are merged into the `develop` branch, which is the default branch of [SeekDB](https://github.com/oceanbase/seekdb). The maintainers will merge `develop` into `master` branch periodically. If you want to get the latest code, you can pull the `master` branch.

## CI Checks

All pull requests must pass continuous integration (CI) checks before merging. Currently, there are two types of CI checks:

- **Compile**: This check will compile the code on CentOS and Ubuntu to ensure your changes compile successfully on different platforms.
- **Farm**: This check will run the unit tests and some MySQL test cases to ensure your changes don't break existing functionality.

> **Note**: If the farm check fails and you think it is not related to your changes, you can ask the reviewer to re-run the farm check, or the reviewer will re-run it if needed.

## Building

The development guide is located under the [docs](docs/README.md) folder. Please refer to it for detailed build instructions.

Basic build commands:
```bash
# Build the project
make

# Build for debugging
make debug

# For parallel builds
make -j$(nproc)
```

## Testing

* Write unit tests for new features and bug fixes.
* Ensure all existing tests pass before submitting your pull request.
* Add test cases that cover edge cases and error scenarios.
* Follow the existing test patterns and conventions in the codebase.

Run tests:
```bash
# Run unit tests
make test

# Run specific test suites
make test-unit
```

## General Guidelines

Before submitting your pull requests for review, make sure that:

- Your changes are consistent with the project's coding style.
- You have run all relevant tests and they pass.
- Your code is well-documented and commented where necessary.
- You have considered backward compatibility when making changes.
- The maintenance burden of new features is acceptable.

### Code Quality

- Include unit tests when you contribute new features, as they help to prove that your code works correctly, and also guard against future breaking changes to lower the maintenance cost.
- Bug fixes also require unit tests, because the presence of bugs usually indicates insufficient test coverage.
- Keep API compatibility in mind when you change code in OceanBase. Reviewers of your pull request will comment on any API compatibility issues.
- When you contribute a new feature to OceanBase, the maintenance burden is (by default) transferred to the OceanBase team. This means that the benefit of the contribution must be compared against the cost of maintaining the feature.

### Best Practices

- **Avoid large pull requests**: Large PRs are much less likely to be merged as they are incredibly hard to review. Split large changes into smaller, focused PRs.
- **Discuss first**: For major changes or new features, discuss your intended changes with the core team on GitHub before starting implementation.
- **Announce your work**: If you're working on an existing issue, announce that you are working on it to avoid duplicate work.
- **Update documentation**: If your changes affect user-facing functionality, update the relevant documentation.

## Commits and PRs

### Commit Messages

Write clear and meaningful commit messages. A good commit message should:

- Have a concise subject line (50 characters or less)
- Explain **what** and **why** rather than **how**
- Reference related issues when applicable

Example:
```
Fix memory leak in query executor

The query executor was not properly releasing memory after
processing queries, causing memory leaks during long-running
sessions.

Fixes #123
```

### PR Description Style

Follow the same guidelines as commit messages. Provide a clear description of:
- What changes were made
- Why the changes were necessary
- How the changes were tested
- Any breaking changes or migration notes

For more guidance on writing good commit messages, refer to [this guide](https://chris.beams.io/posts/git-commit/).

---

Thank you for contributing to OceanBase! Your help is essential for making it better.
