---
name: submit-pull-request
description: Prepare, describe, submit, and update seekdb pull requests with concrete root cause, before/after verification evidence, explicit platform and version scope, and honest merge rationale. Use for preparing, opening, or updating a seekdb pull request, filling the pull request template, or organizing a PR summary; not for reviewing another author's pull request.
---

# SeekDB Pull Request Submission

## Submission Goal

Deliver a pull request that a maintainer can review without re-deriving the
investigation: reproduce the problem from the description, trace it to a
concrete root cause, and decide the merge from evidence rather than
promises.

Follow the repository pull request template,
`.github/pull_request_template.md`. Write the description in Simplified
Chinese, matching the template, unless the user explicitly requests another
language.

## Establish the Facts Before Writing

1. Read the linked issue, the full diff, the head SHA, and the changed
   implementation. Follow callers, callees, and serializers far enough to
   explain the mechanism; do not explain implementation from the title.
2. If the change has an associated Multica issue, include its link or
   identifier (for example, `SEEK-xxx`) in the Task Description section.
3. Reuse verifiable original logs and upstream test cases when available.
4. Label four evidence classes separately: prior measurement, current
   re-verification, source-level inference, and unverified.
5. Keep the change focused. Do not expand a description task into code
   changes, full test runs, or unrelated fixes, and do not silently rewrite
   tests to make them pass.
6. If the user asks for a draft only, return the draft. If the user asked
   to replace or update the description, do it without asking again.

## Root Cause Standard for Bug Fixes

Explain one real minimal input along the call chain, not labels:

1. **Trigger conditions:** platform and build, client version and mode, key
   configuration, data types, input, and execution stage. State whether the
   failure occurs at connect, execute, result return, or decode.
2. **Concrete entry:** file, class/method, and branch, with the necessary
   original code snippet.
3. **Parameters and actions:** the concrete parameters the method received,
   how it processed them, and the value, type metadata, length, or bytes it
   produced. Use a call/parameter/result table when it clarifies the trace.
4. **Error propagation:** the downstream consumer, its contract or check,
   how the actual input violates it, and the original error code or
   exception. Never present a client-side failure location as the
   server-side root cause.
5. **Fix correspondence:** the replaced calls and parameters, the new
   intermediate and final results, and why one edit is insufficient when
   coupled stages must change together.

Protocol, binary, JSON, and charset defects warrant byte-level before/after
comparisons; other defects show values, indices, branches, or state
transitions instead. Do not force byte examples where they do not exist.
Distinguish content length from packet length and character values from
numeric values. State unknowns explicitly; never invent parameters, calls,
or results.

## Verification Evidence Rules

- Write reproduction steps in execution order: prepare the baseline or
  build, set up the environment, establish the connection path, prepare the
  input, execute, observe the original failure, switch to the fixed build,
  then rerun identically and compare.
- Record case names, versions, platforms, modes, and pass/fail/skip/block
  counts with key semantic assertions such as returned rows, order,
  warnings, byte round-trips, type tags, and session liveness. Selected
  cases passing are not a whole-suite pass, and "no error" is not full
  semantic verification.
- State whether upstream assertions were modified. DDL adaptations,
  dependency pins, and temporary workarounds must be disclosed, including
  whether they were removed.
- Explain any difference between verification build SHAs and the PR head:
  same tree, same patch-id, or the same patch applied. Never call them
  identical source without proof, and disclose cross-baseline comparisons.
- Keep builds, static checks, local runs, and CI separate. Do not claim CI
  is green without checking it live.
- For introduction analysis, distinguish author date, committer date,
  release date, earliest measured failing version, and the commit that
  actually introduced the defect. An old version failing only proves the
  defect existed then; a client adding a check does not date the server
  defect.
- Fill the template's platform and version section separately: measured
  reproduction per platform, inferred-only paths, fix verification builds,
  and untested versions. Shared source proves a shared code path, not a
  shared test result. Record migration compatibility with separate source
  and target versions and platforms.
- Commands must use explicit placeholders or safe inputs. Never include
  passwords, private keys, or internal machine addresses, and distinguish
  SSH ports from SQL ports.

## Prepare the Branch

1. Rebase onto the latest `upstream/master` (or `origin/master`) and use a
   focused branch name such as `fix/issue-123`.
2. Prefer regression coverage in the repository's established suites for
   behavior changes: focused C++ unit tests, mysqltest cases, or Rust tests.
3. Stage only intended files. Verify with `git diff --cached --check` and
   review the staged diff.
4. Use a commit message in the repository's existing style, for example
   `fix(scope): ...`, that states the behavior changed.
5. Run validation proportionate to the change before opening the pull
   request, and record exactly what ran.

## Create the Pull Request

1. Push the branch to the contributor fork or another authorized remote.
2. Title the pull request in the repository's existing style, such as
   `fix(regex): preserve position errors for invalid capture references`.
3. Write the body as UTF-8 Markdown in a temporary file based on the
   current template, then run `gh pr create --title ... --body-file ...`
   or the platform equivalent. Never pass a complex body through inline
   shell strings that can mangle backticks, newlines, or byte notation.
4. Verify the created pull request: re-read the rendered body; check
   sections, code blocks, tables, links, the linked issue, and the target
   branch `oceanbase/seekdb:master`.
5. Report the pull request link, what was submitted, and what remains
   unverified.

## Update an Existing Description

1. Fetch the current body first and keep the original for recovery. Work
   from the latest version to preserve concurrent edits.
2. Follow the user's instruction to append or replace the summary. When
   replacing, remove superseded summary sections so two conflicting
   descriptions cannot coexist.
3. Keep requested historical verification records, preferably in the
   collapsible details section, annotated with the baseline they belong to.
   Never present old runs as current results.
4. Write the updated body to a UTF-8 file and apply it with
   `gh pr edit --body-file` or the structured API equivalent.
5. Re-read after writing and verify the full text, code blocks, tables, and
   links. If someone edited concurrently, merge again instead of
   overwriting.
6. Report the pull request link and the completed change. Never describe a
   description update as rerunning tests.
