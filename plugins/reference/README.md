# seekdb reference plugin

This module is a build- and ABI-conformance fixture, not a production plugin.
It publishes the thread-safe `org.seekdb.reference.echo` service from its
binary manifest and registers `org.seekdb.reference.dynamic-echo` during init,
both at version `1.0.0`.  Its manifest also declares the
`org.seekdb.reference.extensions` extension-catalog service.  After start, the
catalog returns one normalized fixture for each v1 extension kind: type,
function, cast, index access method, optimizer hook, DAS hook, and declarative
catalog object, plus a second function descriptor used for stride validation.
Every executable fixture refers to the echo service instead of exposing a
function pointer through catalog metadata.

The catalog object is inert test metadata carrying a fixed, syntactically valid
digest assertion; no production verifier authenticates or binds that assertion
to payload content in this fixture.  The plugin does not execute catalog SQL,
run migrations, request external permissions, or own production persistent
data.  It still declares catalog schema version `1` and the persistent-data
capability in its binary manifest so the fixture exercises the loader's current
catalog-object consistency gate.  It does not stand in for the still-missing
authenticated package/binary full-field reconciliation.  The loader uses the
extension-catalog service only during discovery, then atomically publishes the
two runtime echo services and all eight descriptors as one registry generation.

Build it with:

```bash
cmake -S . -B build_release -DSEEKDB_ENABLE_EXPERIMENTAL_PLUGINS=ON
cmake --build build_release --target seekdb_reference_plugin
```

The target links only the public `seekdb_plugin_sdk` interface.  The
`plugin_boundary_check` target rejects dependencies on seekdb private headers
or libraries.  Runtime hot `dlclose` is outside the v1 contract; a loader must
logically disable and drain the provider first.

`seekdb_reference_blocked_plugin` and
`seekdb_reference_stop_blocked_plugin` are fault-injection fixtures.  They
exercise rollback-stop and runtime-stop failures respectively, proving that a
failed callback enters `BLOCKED`, keeps its identity reserved, and is retried
only by terminal process shutdown.

`seekdb_reference_invalid_extensions_plugin` truncates the declared byte span
of a later descriptor and proves that it cannot partially publish earlier
services or extensions.  The valid function array places a padded descriptor
before a compact descriptor, proving that the byte-stride walker crosses a
forward-compatible descriptor tail and reaches the next mixed-stride element.
`seekdb_reference_invalid_manifest_plugin` proves that the loader-only
extension-catalog capability is rejected when claimed at manifest scope.

`seekdb_reference_registration_conflict_plugin` opens the maximum number of
host registration transactions, verifies that the next begin is rejected, and
aborts them all.  Two further transactions then share the maximum aggregate
pending-service budget; the next registration is rejected and both transactions
remain abortable.  Finally, it stages the same service in two simultaneous
transactions: the first commit succeeds, the second reports `ALREADY_EXISTS`
and remains abortable, and a fresh transaction successfully commits afterward.
