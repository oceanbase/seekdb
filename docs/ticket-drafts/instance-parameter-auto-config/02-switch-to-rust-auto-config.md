# 02: Switch declared instance parameters to the Rust auto-config store

**What to build:** An operator can set a declared instance parameter, restart seekdb, and see the saved value loaded from the deployment-local auto-config file. Startup option changes and declared internal values use the same store. Rust owns the file operations and C++ retains declaration checks, validation, SQL handling, and reload. SQLite remains available for unrelated metadata but is no longer the instance parameter store.

**Blocked by:** 01: Consolidate instance parameter persistence access.

**Status:** Approved draft; tracker publication pending.

- [ ] A successful `ALTER SYSTEM SET` writes the declared value in canonical quoted form, immediately follows the existing reload path, and survives restart.
- [ ] Startup option changes are written back and remain saved on a later start without those options.
- [ ] Declared internal values, including server creation time and role information, save and load through the new store.
- [ ] A missing auto-config file means no saved overrides; old SQLite parameter values are not migrated or read as overrides.
- [ ] Each save writes and syncs a complete temporary file, syncs an existing target, atomically replaces it, then syncs the new target and directory; the live file is never truncated in place.
- [ ] Writers are serialized in-process across the complete read-modify-replace sequence, and new files are accessible only to the instance owner.
- [ ] Every supported platform has the required durable replacement or fails clearly at build or startup; other SQLite metadata remains usable.
