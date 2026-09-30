# SQL Extension packages

`text_ops` is a source package with a control file, an installation script and an update script,
without a native module. It demonstrates the new Rust package reader; it is not
yet an executable `CREATE EXTENSION` integration test.

With experimental plugins enabled, the CMake `plugins` install component copies
these files directly to `<prefix>/<datadir>/seekdb/extension`, using the PG-style
flat control/SQL layout. That directory is the package reader's root. Source
examples remain grouped in subdirectories; the reader also accepts this legacy
layout. Do not deploy both layouts for the same name: ambiguous controls fail
instead of silently selecting an old or new package. An in-place upgrade from
the old install layout requires the administrator to move the old package aside.
No library is loaded and no SQL is run by installing or reading these files.

The primary control may set `directory` for scripts and secondary controls
inside the trusted root. `module_pathname` performs PG-style `MODULE_PATHNAME`
source substitution; it does not load a library or implement `LANGUAGE C`.
`native_module` remains the separately validated logical module dependency.
The [`native_math`](native_math/README.md) reference package now exercises native
routine declarations with `AS 'MODULE_PATHNAME', 'implementation-id' LANGUAGE C`.
It binds implementation-only entries in the optional C reference module directly,
without SQL RETURN wrappers. Its 1.0 installation and 1.0-to-1.1 update are shipped
by the same flat install component. The same component also ships
[`gis.control` and `gis--1.0.sql`](../gis/sql/README.md), with 106 declarations for
82 GIS function names backed by implementation-only entries in the GIS DSO.
Native package server installation and recovery remain unverified. See the
[PG migration plan](../../docs/developer-guide/zh/plugin-pg-extension-roadmap.md).

The `text_ops` sample declares two independent invoker-security SQL functions:
`seekdb_char_count(TEXT)` using `CHAR_LENGTH`, and `seekdb_byte_count(TEXT)` using
`LENGTH`. These independent functions are a minimal example; the installation
bridge now also stages earlier routines for later semantic resolution. Its SQL still needs the normal kernel parser,
resolver, permissions and transaction-aware schema installer. Do not simulate
atomic Extension installation by executing its statements with autocommit.

The default remains 1.0. Selecting version 1.1 uses `text_ops--1.0.sql` followed
by `text_ops--1.0--1.1.sql`, which adds `seekdb_is_empty(LONGTEXT)` (NULL input
returns NULL). No duplicated 1.1 base script is necessary. The reader chooses
the shortest available directed script path, treating version labels as opaque.
Files remain separate SQL inputs; they are not concatenated. This is support
for a fresh installation path; an update from an installed 1.0 uses only the update file.

The core source API now also provides `read_extension_update`: starting from
installed version 1.0 and targeting 1.1 returns only the update file, never the
base script. The kernel `ExtensionScript::load_update` parser consumes that plan.
Equal-version plans have no scripts; explicit empty update files are allowed.
Source reading alone does not change durable versions. The SQL command
`ALTER EXTENSION text_ops UPDATE TO '1.1'` now connects the authenticated source
observation, Rust-selected version path and Root's sequential routine updater.
It preserves the installation ID and old members. Omitting `TO` targets the
control file's default (currently 1.0), not the highest-looking version label.
Same-version updates still check ownership and return zero affected rows.
Actual server commit/rollback and concurrent updates remain unverified.

The real kernel-parser regression in `rust/plugin-runtime/tests/kernel_script.py`
uses the actual top-level CMake install component in a temporary prefix, then
parses both the default package and the two-file 1.1 plan. It does not install
the functions in a database. Pure SQL delivery does not force optional native
plugin targets to be built or installed.

The current SQL command wiring includes `CREATE EXTENSION text_ops` and
`DROP EXTENSION text_ops [RESTRICT]` for this pure-routine package. DROP uses the
stored membership; it does not require the package files or `--extension-dir`.
Explicit CASCADE is parsed but rejected until cascading dependency removal is
implemented. CREATE, UPDATE and DROP reject active caller transactions without
implicitly committing them. Prepared and nested update statements are unsupported.
The opt-in `rust/plugin-runtime/tests/extension_install_server.py` exercises these
commands on a disposable server; this is a separate, not-yet-executed database
regression, not evidence supplied by the kernel-parser or runtime-model tests.

See [package format and current limits](../../docs/developer-guide/zh/plugin-extension-package.md)
and [installation integration status](../../docs/developer-guide/zh/plugin-extension-install.md).

## Composing Extensions

[`text_composed`](text_composed/README.md) declares `requires = 'text_ops'` and
calls both provider routines from its own SQL bodies. It demonstrates that
Extension composition does not require a new native service or core function
factory entry. Install `text_ops` in the same database first, then install
`text_composed`; updating the latter to 1.1 adds a second composed routine.
The three files are delivered by the same CMake component, with no native DSO.

The kernel fixture resolves the actual installed provider and consumer scripts,
checks their routine-ID dependencies and schema-version fences, and reads the
same `requires` declaration on update/no-op paths. This uses staged schemas, not
an actual database commit. The opt-in server regression additionally checks
function results (ASCII, Unicode, empty and NULL), stable Extension-ID edges,
RESTRICT, cross-database isolation and edge cleanup on consumer/database drop.
It must be executed on a disposable current-schema server before claiming those
live database behaviors are verified.
