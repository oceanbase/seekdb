# Rust-backed SQL Extension example

This package defines database-local SQL functions using the existing Rust text
module. `rust_text_length` calls its byte-input character counter;
`rust_unicode_length` composes the custom-text constructor and typed overload.
The SQL objects are ordinary routines with `SQL SECURITY INVOKER`, not aliases
inserted into a second runtime catalog.

Deploy/install the [Rust text plugin](../../rust_text/README.md) first. Its
logical module ID is `org.seekdb.rust-text`; the control file names that ID, never
a library path or runtime generation. Deploy this directory under the configured
Extension package root, select the intended database, then use:

```sql
CREATE EXTENSION rust_text_ops;
SELECT rust_text_length('A中🙂'), rust_unicode_length('A中🙂');
-- With the catalog-install version of the Rust module:
SELECT rust_runtime_length('A中🙂');
ALTER EXTENSION rust_text_ops UPDATE TO '1.1';
SELECT rust_text_nonempty('A中🙂');
DROP EXTENSION rust_text_ops RESTRICT;
```

These are intended server usage examples. The development regression checks
package delivery, real SQL/PL semantic resolution and native object lookup;
it does not yet establish successful live-server installation/execution/rollback.

The module's optional installation callback adds `rust_runtime_length` to the
two static routines. That declaration is generated at installation time and is
not present in these delivery files. A module without this optional service
installs only the static objects. The callback is not rerun by ALTER UPDATE;
the additional routine remains an ordinary recorded Extension member.

The install coordinator records the module association and SQL membership in
the same schema transaction. Catalog recording locks the provider and requires
it to be stably ACTIVE. Module RESTRICT removal checks installed Extensions.
An SQL version update preserves the associated module ID; it cannot swap native
code or ABI by editing the control file. Dropping the Extension removes its
routine members and association, but does not stop or unload the shared module.

This is a declared dependency, not automatic discovery of every module used
inside a routine body. Extension-to-Extension `requires` can declare already
installed providers in the same database, with transactional recording and
RESTRICT removal; it does not auto-install packages or grant SQL privileges.
Multiple native module declarations, CASCADE, arbitrary native-symbol function DDL and non-routine
catalog builders remain separate work. Ordinary routine permissions, dependency
checks and transaction restrictions still apply. Merely listing a native module
does not authorize loading a library or bypassing a SQL object's permissions.

CMake installs these small SQL/control files with the `plugins` component;
it does not add the optional Rust/GIS binary to the core build.
