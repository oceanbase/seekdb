# NativeLink single-node worker

This directory contains the server-side files for a trusted internal Bazel
Remote Execution service. The client-facing REAPI endpoint provides CAS,
Action Cache, and execution on one gRPC port. The worker API remains bound to
loopback.

The `local` worker type means that NativeLink starts action processes on the
NativeLink host. It is remote from Bazel's point of view. Linux user, PID, and
mount namespaces isolate each remote action; Bazel's `sandboxed` strategy is a
separate local fallback and never wraps a remote action.

## Install a new worker

The server is Linux x86-64. It must support unprivileged user and mount
namespaces. Check before installing:

```bash
unshare --user --map-root-user true
unshare --user --map-root-user --mount true
```

Choose an absolute persistent directory. The service is generic to Bazel and
is not tied to a seekdb checkout:

```bash
RBE_ROOT="$HOME/nijia.nj/bazel-rbe"
mkdir -p \
  "$RBE_ROOT/bin" \
  "$RBE_ROOT/config" \
  "$RBE_ROOT/data" \
  "$RBE_ROOT/logs" \
  "$RBE_ROOT/run" \
  "$RBE_ROOT/work"
```

Install the pinned NativeLink binary as `bin/nativelink-1.6.2`, verify its
published SHA-256, and create the stable symlink:

```bash
chmod 0755 "$RBE_ROOT/bin/nativelink-1.6.2"
ln -sfn nativelink-1.6.2 "$RBE_ROOT/bin/nativelink"
```

Copy `nativelink-service.sh` to `$RBE_ROOT`, then render the configuration on
the machine where this repository is checked out:

```bash
sed "s|@ROOT@|$RBE_ROOT|g" \
  tools/bazel_remote/nativelink.json5.tpl \
  >"$RBE_ROOT/config/nativelink.json5"
cp tools/bazel_remote/nativelink-service.sh "$RBE_ROOT/"
chmod 0755 "$RBE_ROOT/nativelink-service.sh"
```

Start and inspect the worker:

```bash
"$RBE_ROOT/nativelink-service.sh" start
"$RBE_ROOT/nativelink-service.sh" status
```

Port 50051 intentionally has no TLS or authentication. Bind it only to a
trusted internal interface or restrict it with the host firewall. Port 50061
must not be exposed to clients.

## Use from a client

One argument enables both remote execution and the remote cache because
`bazel.py` automatically points both REAPI settings at the executor:

```bash
./bazel.py \
  --remote-executor=grpc://worker.example.internal:50051 \
  build //src/oblib:oblib_objects_build
```

The current launcher sends `CppCompile` actions to NativeLink. Other spawn
actions use Bazel's local sandbox. If the executor is unavailable, compilation
falls back to the local sandbox.
