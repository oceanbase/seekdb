# 03: Restore defaults with RESET and report effective values

**What to build:** A DBA can remove a persistent override with `ALTER SYSTEM RESET`. Dynamic parameters return to their declared defaults immediately. `SHOW PARAMETERS` continues to show the effective instance value, including the old value of a static parameter until restart.

**Blocked by:** 02: Switch declared instance parameters to the Rust auto-config store.

**Status:** Approved draft; tracker publication pending.

- [ ] `ALTER SYSTEM RESET` removes the named saved entry rather than writing the default as a new override.
- [ ] After reset, a dynamic parameter uses its declared default immediately and still uses it after restart.
- [ ] A saved static parameter remains at its old effective value until restart; `SHOW PARAMETERS` reports that effective value.
- [ ] A multi-parameter SQL command still saves parameters one at a time, with no command-wide file transaction.
