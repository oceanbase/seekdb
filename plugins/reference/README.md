# seekdb reference plugin

This module is a build- and ABI-conformance fixture, not a production plugin.
It publishes the thread-safe `org.seekdb.reference.echo` service from its
binary manifest and registers `org.seekdb.reference.dynamic-echo` during init,
both at version `1.0.0`.  It has no catalog objects, persistent data,
migrations, external permissions, or unload dependencies.

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
