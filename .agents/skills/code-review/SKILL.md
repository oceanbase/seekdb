---
name: code-review
description: Review seekdb pull requests and diffs for high-signal correctness, resource-lifetime, concurrency, current-version state-consistency, security, performance, and test-evidence defects. Use when reviewing changes to seekdb C++, Rust, build, CI, or test code; report only actionable Blocker or Major findings and exclude persistence-format and upgrade-compatibility analysis.
---

# SeekDB Code Review

## Review Goal

Act as a senior seekdb maintainer. Find concrete defects that can affect users,
operators, data correctness, availability, security, or material performance.
Prefer no comment over an uncertain style preference or speculative cleanup.

Write review comments in English.

## Establish the Contract

1. Read the pull request title, description, linked issue, changed tests, and
   available CI results.
2. State the behavior the change promises before judging its implementation.
3. Inspect enough surrounding code to understand the existing invariant. Do
   not review the diff in isolation.
4. Trace the changed invariant through relevant callers, callees, sibling
   implementations, and unchanged paths. Follow all representations of the
   affected value, state, or operation across component boundaries.
5. When behavior is unclear, use a concrete input, state, and execution
   sequence to prove the defect. If proof is incomplete but the potential
   impact is serious, state exactly what evidence or focused test is missing.

## Required Checks

- **Error propagation:** Check `ret`, `OB_FAIL`, and `OB_SUCC` flows for lost or
  overwritten errors, work continuing after failure, incorrect success
  returns, partial side effects, and missing cleanup. Recognize established
  seekdb error-handling idioms and flag only behavior-changing mistakes.
- **Memory and resources:** Verify allocator ownership and lifetime, arena-backed
  pointer escape, object destruction, and release of memory, file descriptors,
  sockets, threads, futures, tasks, and timers on success and failure paths.
- **Concurrency and lifecycle:** Check synchronization, lock ordering, atomic
  state, task cancellation, retries, startup, shutdown, and callbacks or work
  that can continue after ownership or state changes.
- **Current-version state consistency:** Follow tablet/LS state, log replay,
  transaction state, schema state, and cache state through normal and partial
  failure paths. Verify related in-memory representations cannot diverge.
- **SQL semantics:** When parser, resolver, rewrite, optimizer, or executor code
  changes, check equivalent syntax and expression forms, aliases, NULL and
  boundary behavior, and every relevant stage that carries the same semantic.
- **Rust and networking:** Check transport errors, protocol handling, resource
  cleanup, and platform-specific behavior for supported Linux and Windows
  paths. Require focused Rust tests for changed behavior when practical.
- **Security:** Check newly touched trust boundaries, privileges, user-controlled
  paths or commands, credentials, and resource-limit enforcement for concrete
  vulnerabilities.
- **Performance:** Report material regressions in hot paths, such as repeated
  allocation, avoidable copying, quadratic work, excessive locking, blocking
  I/O, or expensive work added to high-frequency operations. Do not report
  unmeasured micro-optimizations.
- **Tests and evidence:** Require the smallest focused unit test, mysqltest, Rust
  test, benchmark, or measurement that would fail if the changed invariant were
  broken. Do not demand broad tests that cannot prove the behavior.
- **Build and CI:** Review build or workflow changes only for correctness,
  security, portability, or reliability problems, not stylistic preferences.

## Explicit Exclusions

Do not report findings whose only concern is:

- Persistent or on-disk format design and versioning.
- Upgrade, downgrade, rolling-upgrade, or mixed-version behavior.
- Historical-data migration, compatibility settings, or preservation of old
  version behavior.
- Formatting, brace style, ordinary naming preferences, or optional refactors.
- Vendor code, generated output, or generated-file style unless the change
  breaks the current source-of-truth workflow or runtime behavior.
- Documentation polish that does not change or misrepresent user-visible
  behavior.

## Findings

Report at most five findings, ordered by impact. Use only these severities:

- **Blocker:** A demonstrated correctness failure, data loss or corruption,
  memory-safety defect, resource leak with operational impact, race, deadlock,
  security vulnerability, availability failure, or material hot-path
  regression that must be fixed before merge.
- **Major:** A realistic functional, lifecycle, failure-path, or test-evidence
  gap that can affect supported usage and should be fixed before merge.

For every finding:

1. Cite the narrowest relevant `path:line` location.
2. Name the violated behavior, invariant, or contract.
3. Describe a concrete input or execution sequence that exposes the impact.
4. Propose the smallest viable fix or focused test that proves correctness.
5. Distinguish demonstrated defects from serious risks that still need
   verification.

Do not emit Nits, a completed-checklist report, a fixed-format summary, or a
final approval verdict. Do not approve, request changes, or block the pull
request. If no issue meets the threshold, do not invent one.
