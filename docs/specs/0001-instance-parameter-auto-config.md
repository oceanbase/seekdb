# Instance parameter persistence in an auto-config file

## Problem Statement

An instance parameter is a declared setting for a seekdb instance, distinct from a SQL session system variable. Today seekdb stores saved instance parameter values in SQLite. Operators need a simpler deployment-local representation while retaining seekdb's existing parameter behavior and ensuring that a crash during a write cannot leave a half-written configuration file that prevents restart.

## Solution

Store persistent overrides for declared instance parameters in `<base_dir>/etc/seekdb.auto.conf`, where `base_dir` is the instance deployment directory. Each update replaces the complete file through PostgreSQL's durable temporary-file and rename protocol. Rust implements the file store; C++ continues to validate parameters and manage SQL, the effective instance value, and reload. An operator can inspect or edit the file while seekdb is stopped. A saved change is reloaded immediately where the parameter permits it, and a restart reads the saved overrides.

## User Stories

1. As an operator, I want saved instance parameters in the deployment directory's `etc` directory, so that each instance carries its own configuration.
2. As an operator, I want the file to contain only values for declared instance parameters, so that its contents have a clear meaning.
3. As an operator, I want a missing file to mean that no overrides are saved, so that a new instance starts from declared defaults.
4. As an operator, I want to read each saved value in `name = 'value'` form, so that I can inspect the configuration without SQLite tools.
5. As an operator, I want quotes and backslashes in values to round trip correctly, so that saved values are not changed by serialization.
6. As an operator, I want to add blank lines and `#` comments while the instance is stopped, so that I can annotate an offline edit.
7. As an operator, I want unsupported file syntax to fail startup with a line number, so that a malformed edit is visible.
8. As an operator, I want an unknown parameter name to fail startup with its name and line number, so that a typo cannot be silently ignored.
9. As an operator, I want an invalid parameter value to fail startup with its name and line number, so that the instance cannot run with an unintended value.
10. As an operator, I want the last occurrence of a duplicate name to win, so that an offline edit has a predictable result.
11. As an operator, I want the next automatic rewrite to collapse duplicate names, so that the generated file has one entry per parameter.
12. As an operator, I accept that automatic rewrites discard offline comments, so that generated files follow PostgreSQL's rewrite behavior.
13. As a DBA, I want `ALTER SYSTEM SET` to save the value and immediately use seekdb's reload path, so that change behavior remains familiar.
14. As a DBA, I want `ALTER SYSTEM RESET` to remove the saved override, so that the declared default takes effect for a dynamic parameter.
15. As a DBA, I want `SHOW PARAMETERS` to report the effective instance value, so that it reflects what the running instance is actually using.
16. As a DBA, I want a saved static parameter to become effective after restart, so that `SHOW PARAMETERS` does not claim an unapplied value is active.
17. As a DBA, I want a reload failure after a successful save to state that the value was saved but could not be applied, so that I know what a restart will read.
18. As a DBA, I want commands that set multiple instance parameters to keep seekdb's per-parameter sequence, so that a crash after one completed parameter can leave that completed prefix saved.
19. As an operator, I want startup command-line parameter changes written back, so that they remain saved on later starts without those options.
20. As an operator, I want declared internal settings such as server creation time and role information to use the same store, so that current internal behavior continues.
21. As an operator, I want concurrent in-process saves serialized across the read and replace sequence, so that one writer cannot discard another's completed change.
22. As an operator, I want a crash during a save to leave a complete old or new file, so that the next startup never reads a partial write.
23. As an operator, I want an abandoned temporary file ignored at startup, so that an interrupted save does not change the active configuration.
24. As an operator, I want a failed directory sync after replacement reported without a false rollback claim, so that the uncertain durability is explicit.
25. As an operator on a supported platform, I want a crash-safe replace operation or a clear build or startup failure, so that the implementation never silently truncates the live file.
26. As an operator, I want newly created configuration files restricted to the instance owner, so that saved settings are not exposed to other local users.
27. As a seekdb maintainer, I want other metadata to continue using SQLite, so that this change stays scoped to instance parameter persistence.

## Implementation Decisions

- Scope is declared instance parameters only. SQL session system variables and tenant parameters are separate concerns.
- Replace SQLite as the instance parameter store without migrating existing stored parameter values. Other SQLite metadata remains in place.
- Rust owns parsing, serialization, the process-local writer lock, and durable file replacement. C++ owns the declarations, value validation, SQL handling, live configuration, and module reload. The language boundary exchanges names, values, source line numbers, and results rather than C++ configuration objects.
- On startup, parse a complete snapshot, validate every entry, then publish the snapshot to the live configuration. An error must not leave a partly applied snapshot. A missing file supplies an empty override set.
- The reader accepts canonical quoted assignments, blank lines, and `#` comments. The writer doubles single quotes and backslashes, writes each name once, and omits comments. The last duplicate assignment wins.
- A save holds the process-local lock from reading the current file through replacement. It writes the entire result to a temporary file in the same directory, syncs and closes it, syncs an existing target, atomically replaces the target, then syncs the new target and parent directory. The live file is never truncated in place.
- A failure before replacement leaves the old file authoritative. An orphan temporary file is ignored. A failure after replacement reports failure without claiming whether the old or new complete version is durable.
- SQL `SET` persists before immediate reload. If reload fails, retain the new persistent override and report saved-but-not-applied. SQL `RESET` removes the override and immediately restores the declared default for a dynamic parameter.
- Keep per-parameter persistence for a SQL command that changes multiple instance parameters; there is no command-wide file transaction. Keep startup option writeback. `SHOW PARAMETERS` continues to show the effective instance value, including the old value for a static parameter until restart.
- Preserve supported platforms. A platform without the required crash-safe replacement must fail at build or startup rather than fall back to in-place writes. New files are accessible only to the instance owner.

## Testing Decisions

- Use one principal seam: a deployed seekdb instance exercised through SQL, startup options, stop/restart, and the deployment-local configuration file. Tests assert returned errors, effective values, file contents, and restart behavior rather than Rust or C++ call sequences.
- Extend the existing obtest style that already sets multiple parameters and stops and restarts instances. Existing mysqltest configuration cases provide prior art for SQL result assertions.
- Cover `SET`, `RESET`, dynamic and static values, startup writeback, missing files, declared internal parameters, duplicate entries, comments, invalid syntax, unknown names, invalid values, and quote/backslash round trips.
- Exercise concurrent saves and verify that all completed changes survive. For a multi-parameter command interrupted between parameters, verify that the completed prefix remains saved and every observed file is complete.
- Inject failure or terminate the instance around temporary-file sync, replacement, new-target sync, and directory sync. Restart after each point and assert that the file parses as one complete old or new version; assert the reported outcome where the process survives.
- Force reload failure after a successful save and verify the saved-but-not-applied response, the retained override, and the effective value shown by `SHOW PARAMETERS`.
- Run platform-specific build and startup checks for the durable replacement capability and owner-only permissions.

## Out of Scope

- Migration or backward compatibility for parameter values in the old SQLite store.
- Transactional rollback or all-or-nothing persistence for a multi-parameter SQL command.
- Concurrent edits by an external process while seekdb is running.
- PostgreSQL's full configuration-file grammar or its delayed-reload semantics.
- Changes to SQL session system variables, tenant parameters, or unrelated SQLite metadata.

## Further Notes

This specification follows the proposed instance-parameter auto-config ADR and the repository's instance-parameter glossary. PostgreSQL's `AutoFileLock` is an internal shared-memory lock; seekdb uses a process-local writer lock because the agreed deployment model has one process writing its instance file. The durable-replacement protocol addresses torn configuration files, not SQL transaction atomicity.
