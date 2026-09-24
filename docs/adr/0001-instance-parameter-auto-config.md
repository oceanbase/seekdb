# Persist instance parameters in a PostgreSQL-style auto-config file

Status: proposed

## Decision

Replace SQLite-backed persistence of instance parameters with `<base_dir>/etc/seekdb.auto.conf`. `base_dir` is the instance deployment directory. Only declared instance parameters may appear in this file. Do not migrate values from the old SQLite table. This includes declared internal values such as `server_create_time` and `server_role_info`.

Rust owns file parsing, serialization, the process-local writer lock, and durable replacement. C++ retains parameter declarations, validation, SQL handling, application to the live configuration, and module reload. The Rust/C++ interface carries names and values; it does not expose C++ configuration objects to Rust.

## File and write protocol

Write each entry in PostgreSQL's canonical `name = 'value'` form, doubling single quotes and backslashes within values. Read that form, blank lines, and `#` comments. Unsupported syntax, an unknown name, or an invalid value prevents startup; report the line number and the name when it can be parsed. When duplicate names occur, the last one wins. An automatic rewrite emits each name once and discards user comments, as PostgreSQL does. Operators may edit the file only while seekdb is stopped.

For each saved parameter, hold the process-local writer lock while reading the current file, replacing that parameter's entry, writing the complete result to a temporary file in the same directory, syncing and closing the temporary file, atomically replacing the live file, and syncing the containing directory. Match PostgreSQL's durable-rename steps, including syncing an existing target before replacement and syncing the new target afterward. The implementation must never overwrite the live file in place. On a failed write before replacement, the old file remains authoritative; an orphan temporary file is ignored at startup. If replacement succeeded but the final directory sync failed, report failure without claiming which complete version survived.

`ALTER SYSTEM RESET` removes the saved entry, and the effective dynamic value returns to the declared default immediately. Loading a snapshot must validate all entries before publishing any value to the live configuration, so an invalid file cannot leave a partly updated instance. A missing file means no saved overrides.

## Existing seekdb behavior retained

Startup option changes are written back to the file and remain effective on later starts without those options. `ALTER SYSTEM SET` persists a parameter and immediately runs seekdb's reload path. A multi-parameter command keeps the current per-parameter sequence: a crash between A and B may leave only A saved, while each individual file version remains complete. If persistence succeeds but reload fails, keep the new file and report that the value was saved but could not be applied. `SHOW PARAMETERS` reports the effective in-memory value; for a static parameter, that may be the old value until restart.

## Platform and verification requirements

Preserve the project's current supported platforms. Each platform must provide a crash-safe replacement with the stated guarantee or fail explicitly at build or startup; no platform may fall back to truncating the live file. New files should be accessible only to the instance owner.

Verify round trips for quoting and backslashes, duplicate and invalid entries, reset, startup writeback, concurrent writers, static-parameter display, and reload failure. Inject failures or process termination before and after temporary-file sync, rename, and directory sync; after restart, the file must always parse as one complete old or new version.

## Rationale

Instance parameters change infrequently and do not require SQL transaction rollback. A complete-file replacement provides the required crash behavior without using SQLite for parameter persistence. SQLite remains available to other seekdb metadata users.
