# Native SQL binding reference package

This package exercises the native-routine installation path before GIS's full
SQL package migration. It is not the GIS package and does not replace that work.
The control and versioned SQL files are installed flat under
`share/seekdb/extension`, using the normal `plugins` install component. Delivery
does not build or load the optional reference DSO.

`native_module` associates the installation with `org.seekdb.sql_extension`.
`MODULE_PATHNAME` expands to that **logical manifest ID**, not a library path.
The implementation IDs use the distinct `org.seekdb.sql-extension` namespace.
Both implementations have `IMPLEMENTATION_ONLY`: their labels are not public
SQL functions. The scripts create database-local routine identities and bind
those identities directly to the C ABI; there are no SQL RETURN wrappers.

Version 1.0 creates `native_add_one` and `native_increment`. Version 1.1 changes
the first routine's comment and adds `native_successor`, sharing the same
implementation without replacing existing routine identities. All take a
BIGINT and return its successor, or NULL for a NULL input. Avoid BIGINT overflow
inputs: this package tests binding/lifecycle, not arithmetic range behavior.
`native_increment` additionally declares `DEFAULT 41`: `native_increment()`
returns 42, whereas `native_increment(NULL)` returns NULL. The default belongs
to the SQL routine, not the shared implementation descriptor.

Version 1.2 changes `native_add_one`'s comment again, then drops and recreates
`native_increment` with `DEFAULT 99` in the same update script. Its new routine
identity must not inherit the old object's EXECUTE grants; cached default calls
must now return 100. Explicit arguments and the implementation binding remain
unchanged. The default installation version remains 1.0, and the earlier SQL
files are unchanged.

Prerequisites for server testing:

- A current-schema disposable experimental server, with all native-routine
  catalog columns; this package is not an existing-instance upgrade procedure.
- The rebuilt `plugins/sql_extension` reference module loaded by an administrator.
  Its CMake target is `seekdb_sql_extension_plugin`; it is intentionally not part
  of normal binary installation.
- `--extension-dir` pointing at the installed flat extension directory, or this
  repository's `plugins/sql_packages` directory (legacy directory discovery).
- `SUPER` plus ordinary database/routine creation privileges. Merely shipping
  a control file grants no permission to load code or install SQL objects.

Source reading and kernel parsing tests do not prove server commit, rollback,
recovery or permissions. Those require the separate disposable-server test.

## Disposable-server regression

After preparing a disposable current-schema server with the prerequisites above,
load the rebuilt reference module as an administrator if it is not already loaded:

```sql
INSTALL PLUGIN `org.seekdb.sql_extension` SONAME 'sql_extension/seekdb_sql_extension.so';
```

From the repository root, run:

```sh
python3 rust/plugin-runtime/tests/native_extension_server.py \
  --port 2881 --user root --confirm-disposable-server
```

Use `--unix-socket /absolute/base-dir/run/sql.sock` instead of `--port` for a
Unix-only server. TCP is restricted to loopback. Set `SEEKDB_TEST_PASSWORD` in
the environment if needed; no password argument is accepted. This runner does
not load/unload modules, restart the server, or change package/global settings.
Do not use it on an instance with valuable data.

The runner creates two uniquely named databases and a limited-privilege user.
That user first installs, updates and removes the shipped `text_ops` package,
which explicitly declares `superuser = false`; the runner checks that its
extension owner remains the ordinary user. `native_math` retains the default
SUPER requirement, so the same user must be denied its installation.
It checks native CREATE admission, database-local names, exact routine and
implementation identities, Extension membership, module dependency edges,
EXECUTE grant/revoke (including NULL), SHOW CREATE, updates to 1.1 and 1.2,
prepared-call rebinding, member-drop protection, Extension removal and
database-drop cleanup. The mixed 1.2 update additionally checks the replacement's
new identity, old member/dependency/ACL removal, unchanged other identities,
the new default result in both fresh and prepared calls, and rejection of the
old grantee until permission is granted explicitly on the new object.
Successful runs delete only their own fixtures; failures retain them and print
their names for diagnosis. No concurrency, injected commit failure or restart
recovery testing is included.

The dependency-free `test_native_extension_server.py` checks runner safeguards
only. Source reading, flat package delivery, kernel parsing and direct/snapshot
DSO execution have passed locally. The attempted disposable-server run could
not reach SQL clients because the execution sandbox forbids socket bind/listen;
**the server regression above has not passed yet**. Preparing a package and
passing its parser tests must not be reported as a committed installation.
