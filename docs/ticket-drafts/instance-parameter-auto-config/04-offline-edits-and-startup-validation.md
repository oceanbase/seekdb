# 04: Accept offline edits and validate the full startup snapshot

**What to build:** An operator can edit the auto-config file while seekdb is stopped and get predictable startup behavior. Supported syntax is narrow and explicit; malformed or invalid entries stop startup with a useful location, without leaving some new values applied.

**Blocked by:** 02: Switch declared instance parameters to the Rust auto-config store.

**Status:** Approved draft; tracker publication pending.

- [ ] Canonical quoted assignments round trip single quotes and backslashes; blank lines and `#` comments are accepted.
- [ ] For duplicate names, the last value wins; the next automatic rewrite emits one entry per name and drops comments.
- [ ] Unsupported syntax, unknown names, and invalid values prevent startup and report the line number plus the name when it can be parsed.
- [ ] All entries are validated before any new value is published to the live configuration.
- [ ] An abandoned temporary file is ignored at startup; a missing live file means no saved overrides.
