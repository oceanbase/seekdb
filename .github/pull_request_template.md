<!--
Thank you for contributing to **OceanBase SeekDB**!

**If this pull request has a significant impact, please make sure you have discussed with the OceanBase group.**

Help a reviewer who has not debugged this issue answer five questions:
what input triggers the problem, where it fails, why it fails, how this
change removes the cause, and what evidence proves the fix. Compress simple
changes; for bug fixes, keep the root cause concrete instead of shortening
it into labels.
-->

### Task Description

<!--
The problem or requirement resolved by this pull request.
Link the issue via "close #xxx" or "ref #xxx".
For a bug fix, describe the real business or upstream case and its trigger
conditions: platform and build, client version or mode, key configuration,
data type, input, and the execution stage where the failure appears.
-->

### Root Cause

<!--
Bug fixes only; omit this section for features.
Follow one real minimal input through the call chain: the entry file,
class/method, and branch; the concrete parameters received; the generated
value, type metadata, length, or bytes; and the downstream contract or
check that the actual output violates. Keep the original error code or
exception.
Do not stop at labels such as "encoding mismatch" or "overflow", and do
not treat the client-side failure location as the server-side root cause.
Mark unknown implementation details or evidence as unknown instead of
inventing calls, parameters, or results.
-->

### Solution Description

<!--
How the change removes each cause listed above: the replaced calls and
parameters, and the corresponding intermediate and final results. When
coupled stages must change together (for example, type inference and value
conversion, or length planning and packet writing), explain why fixing only
one place is not enough. List only alternatives and boundaries that were
actually considered and are supported by evidence.
-->

### Verification

<!--
Record what was actually run, not intentions. Compare the same input before
and after the fix: original failure, fixed result, and key semantic
assertions (returned rows, order, warnings, byte round-trips, type tags,
session liveness). List the scope explicitly (focused unit tests, mysqltest
cases, Rust tests, builds, static checks, and CI) and keep them separate.
Never describe a partial run (for example, 7 selected cases passing) as the
whole suite passing, and do not claim CI is green without checking it live.
Distinguish prior measurements, current re-verification, source-level
inference, and unverified items. If a verification build SHA differs from
the PR head, state how they correspond (same tree, same patch-id, or the
same patch applied). Keep passwords, private keys, and internal machine
addresses out of commands and outputs.
-->

<details>
<summary>Detailed reproduction and verification records</summary>

<!--
Optional for simple changes, expected for bug fixes. Write reproduction
steps in execution order: prepare the baseline/build, set up the environment
and dependencies, establish the connection path, create tables or prepare
input, execute, observe the original failure, switch to the fixed build,
then rerun the same steps and compare. Include original errors,
inputs/outputs, before/after matrices, and note where long outputs are
trimmed.
-->

</details>

### Impact Scope

<!--
Platforms and versions. For each relevant system (Windows, Linux, or
other), state whether the issue was reproduced or the fix was measured, or
whether the path is only inferred from shared source code. If only one
system was tested, say the other is untested; shared source proves a shared
code path, not a shared test result.
List the baseline SHAs or released versions where the issue reproduced, the
build used to verify the fix, and the versions that remain unverified.
Record migration-compatibility issues with separate source and target
versions and platforms.
-->

### Merge Rationale

<!--
Why this should merge now: the real user impact, the evidence that the root
cause is removed, the change scope, and the regression coverage. Include
known regressions and verification boundaries. Do not promise zero risk or
full coverage.
-->

### Upgrade Compatibility

<!-- Please make sure this is compatible with old version or you should give us upgrading solution. -->

### Other Information

<!--
Any information helping to review this pull request: directly related and
verified commits, baselines, or evidence links; unresolved discussions; and
anything blocked or not yet run. Omit this section if empty.
-->

### Release Note
<!--
A concise release note can help users to understand how your pull request makes difference.
-->
