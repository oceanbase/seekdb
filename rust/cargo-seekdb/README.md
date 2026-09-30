# cargo-seekdb

Rust developer tooling for seekdb plugins, in an independent workspace. Commands
are `new`, `schema` and `package`. The CLI depends on the host's Rust runtime crate
to reuse its read-only package-source parser (including that crate's platform
dependencies); this does not add a host dependency to public plugins or the SDK.
Automatic migration inference and server test orchestration remain future work.
This tool does not add a second runtime or catalog.

## Create a Rust plugin project

From the seekdb checkout, create a new external project (the output's parent
directory must already exist):

```bash
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- new my_text \
  --seekdb-root /path/to/seekdb --plugin-id org.example.my-text \
  --output /path/to/new/my_text
```

`NAME` accepts 1..48 lowercase ASCII letters, digits and underscores, starting
with a letter. It determines the Cargo/library/CMake names and initial SQL
function `NAME_chars`. The plugin ID is separately configurable; it defaults to
`local.NAME` and accepts up to 120 lowercase ASCII letters/digits/`._-`. Without
`--output`, a new `NAME` directory is reserved under the current directory.

The generated project includes Cargo.toml, CMakeLists.txt, plugin.toml, a scalar
plugin using the public SDK, a schema Cargo example, lifecycle/Unicode tests, README, .gitignore and the
matching checkout's pinned rust-toolchain.toml. It uses an independent Cargo
workspace and unwind-enabled release profile. No external command runs during
creation; it does not download dependencies, initialize Git, edit parent build
files or load anything into a server. SDK/CMake checkout paths are explicit and
must both be updated if the checkout moves. The entrypoint filename matches the
creation host's platform; cross-compilation is not configured by this command.

Existing directories/files/symlinks are rejected. Failed writes retain the new
directory and `.seekdb-project-incomplete`; inspect it and retry into a new
directory rather than overwriting. Marker removal is not atomic publication or
a power-loss durability guarantee. Paths are encoded as TOML/CMake literals,
never shell commands. Source checkouts and build inputs remain trusted.

Follow the generated README to run `cargo test`, `cargo clippy`, then configure
the standalone CMake project and build its `seekdb_NAME_plugin` target. External
projects explicitly opt into `STANDALONE` and use the same resolved Cargo and
binary export/core-import checks as in-tree plugins. The opt-in is rejected for
host source directories and non-root external callsites. It is not a server-dev
profile or permission to link private host crates. In-tree project creation is
limited to plugins/; add its subdirectory to the parent CMakeLists.txt explicitly.

## Generate an SQL Extension package

```bash
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- schema \
  --manifest-path plugins/rust_text/Cargo.toml --output /tmp/new-rust-text-ops
```

The command reserves a new output directory, marks it `.seekdb-schema-incomplete`,
then runs `cargo run --offline --example seekdb_schema -- ABSOLUTE_OUTPUT` in the
plugin project. `--example NAME` selects another Cargo example; it defaults to
`seekdb_schema`. This is trusted developer code, including Cargo build scripts;
it is not sandboxed. It does not load a DSO or invoke plugin lifecycle callbacks.
Successful generation must produce exactly one primary control file, optionally
accompanied by `name--version.control` files for the same package. SQL-source packages require
a nonblank base; native-source packages permit no SQL files or only explicit
updates, and reject base SQL files. The CLI validates regular files, UTF-8/NUL and
host size bounds, then calls the same Rust reader as the server to validate
control metadata and the default installation path before removing the marker.
Missing/default-unreachable versions, unknown/duplicate control keys and invalid
native declarations now fail generation. All secondary control files are validated,
even outside the default path; default_version/directory overrides are rejected.
The CLI does not parse SQL, check module
availability, authorize objects or prove that every update is installable.
Existing outputs (including dangling symlinks) are never overwritten. Errors,
panics or an empty successful generator retain the incomplete directory. Trusted
inputs/output must not be modified concurrently; this is not atomic publication.

Generated projects include this example and compile an `rlib` alongside the
audited `cdylib`, so generation shares the actual `FunctionDefinition` without
loading the native library. `seekdb_extension::schema::scalar_wrapper` checks
arity/type mapping and derives determinism; authors explicitly select SQL types
and SQL data-access characteristics. The wrapper uses INVOKER security and a
different name from its native callee. Only BIGINT/TEXT/LONGBLOB mappings are
currently provided; custom types are rejected rather than silently erased.

`schema::Package` renders default-version/native-module control metadata, base
SQL and explicit update edges. Authors can mix generated wrappers and handwritten
SQL, including complete compound routines; the SDK does not split/reorder SQL or
infer data migrations. The example emits `rust_text_ops` with a typed wrapper,
handwritten custom-type composition and the explicit 1.0→1.1 update. Its generated
files are byte-compared with the CMake-installed package in the kernel regression,
which resolves these routines against the real loaded Rust native catalog.

For a native-source package, use `Package::write_to_with_source(directory,
InstallSource::Native)` (or `render_with_source`). A native module is required;
zero scripts yields only control, and supplied scripts must be explicit updates.
Existing `write_to`/`render` retain SQL-source behavior. For example:

```bash
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- schema \
  --manifest-path plugins/rust_text/Cargo.toml --example seekdb_native_schema \
  --output /tmp/new-rust-text-native
```

This generates `rust_text_native.control` plus the explicit 1.0→1.1 migration,
not a placeholder base SQL. The native callback supplies initial SQL only during
server installation. The kernel runner byte-compares these generated files with
installed artifacts before exercising their real Rust callback/PL resolver path.

For version-specific requirements, `PackageOptions::version_controls` accepts
`schema::VersionControl` entries. Omitted requirements inherit primary metadata;
`Some(&[])` explicitly clears them. The host collects intermediate prerequisites
separately from final dependencies, without auto-installing either group.

```bash
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- schema \
  --manifest-path plugins/rust_text/Cargo.toml --example seekdb_versioned_schema \
  --output /tmp/new-versioned-consumer
```

This emits the reference `consumer` package with two explicit migration scripts
and separate middle/target control files. Its provider names (`alpha`, `zulu`,
`migration`, `gamma`) illustrate version changes; they are not installed by the
generator. Kernel tests consume these generated files with controlled catalog rows.

This is ordinary SQL routine packaging, not native-symbol `LANGUAGE C` DDL,
automatic overload/custom type DDL or runtime-session catalog builders. The
associated native module must already be active during installation. Unsupported
installer statements are still rejected by the host even if generation succeeds.
SQL/control and the native DSO/manifest remain separately staged artifacts;
`package` does not silently include a possibly stale schema directory. Deployment,
signature/manifest reconciliation and live database testing remain separate work.

## Package an existing Rust plugin

Configure a seekdb CMake build with experimental plugins enabled first. Reconfigure
an older build to generate the new per-target package recipes. From `rust/`, using
the repository's pinned toolchain:

```bash
cargo run --offline --manifest-path cargo-seekdb/Cargo.toml -- package \
  --build-dir ../build_release \
  --target seekdb_rust_text_plugin \
  --output /tmp/seekdb-rust-text-package \
  --jobs 2
```

The output must not exist; its parent must already exist. The result contains the
plugin dynamic library and `plugin.toml`, using paths declared by
`seekdb_add_rust_plugin`. It is a deployable directory layout, not an archive,
signature, package-manager installation or a claim of server compatibility.
The command never loads the library or sends SQL to a database.

The same binary can be installed as a Cargo subcommand with `cargo install
--path cargo-seekdb --offline`, after which use `cargo seekdb package ...`.
Installing the CLI is optional; the `cargo run` form needs no global installation.

The command:

1. Validates the configured build and target-specific package recipe.
2. Reserves a new output directory and writes `.seekdb-package-incomplete`.
3. Runs the CMake target, which invokes Cargo's resolved local-dependency gate,
   compilation, and dynamic export/core-dependency audit.
4. Runs CMake's generated copy recipe, with `DESTDIR` cleared, to collect the
   declared library and manifest without guessing Cargo's output path.
5. Checks for a nonempty regular manifest and exactly one nonempty regular
   plugin library, then removes the incomplete marker on success.

Existing files, directories and symlinks are rejected, including an output
reserved by another packager. Paths with spaces or shell metacharacters are passed
as literal arguments, never through a shell. On failure, the reserved directory
and marker remain for inspection; retry into a new output directory. An interrupted
package is not atomically published, and marker removal is not a power-loss
durability guarantee. Do not deploy a directory containing the marker.

Build directories and generated recipes are trusted developer inputs: CMake,
Cargo and plugin build scripts execute code. This command is not a sandbox,
signature verifier or replacement for server artifact verification. It copies
the declared manifest, not a manifest reconstructed from runtime registration;
full binary/manifest/catalog reconciliation remains a separate requirement.
Keep build outputs unchanged by other processes until packaging finishes.

## Verification

```bash
cargo test --manifest-path cargo-seekdb/Cargo.toml --offline
cargo clippy --manifest-path cargo-seekdb/Cargo.toml --offline --all-targets -- -D warnings
```

The standalone runtime CTest suite includes `cargo_seekdb`, `rust_plugin_scaffold`
and `rust_plugin_package`. Scaffolding creates a fresh external project, runs its
tests and Clippy, configures/builds/audits/packages it, then invokes the actual
generated DSO through the native loader. It checks no-overwrite, symlinks, an
illegal private path dependency even after a valid cached binary exists, and
rejection of core/nested/unopted-in standalone CMake callsites. The dependency
rejection uses a test-owned registry-free crate so network availability cannot
mask the Cargo boundary. These gates are not a sandbox for build scripts.

The package test packages the real Rust text plugin, compares library/manifest bytes,
loads it through the production loader, and checks no-overwrite, dangling symlinks,
missing targets, `DESTDIR` handling and retained build failures. It runs serially
because it invokes the shared plugin build target. Catalog authorization/commit
in the loader test remain protocol fixtures, not real database transactions.
Linux is verified; other host platforms and Bazel packaging remain unverified.
