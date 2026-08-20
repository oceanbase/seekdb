# Get the code, build, and run seekdb

## Prerequisites

Install the supported compiler and dependencies described in [Install the toolchain](toolchain.md). A full source build requires substantial disk space and memory.

## Clone the repository

```bash
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
```

## Build the production binary

The compatibility build supports Release (`RelWithDebInfo`, `-O2`). It includes debug information while retaining production optimization. Debug is not a supported `build.sh` mode.

```bash
source ~/.bashrc
./build.sh release --init --make
```

`--init` prepares the repository's platform-specific dependencies. It is normally required on the first build or after dependency metadata changes. The resulting binary is:

```text
build_release/src/observer/seekdb
```

For incremental builds after initialization, run:

```bash
./build.sh release --make
```

Bazel is the authoritative modular build graph. Use `./bazel.py` for modular builds, unit tests, architecture checks, and non-release configurations.

## Run a local instance

Prepare an isolated deployment and start seekdb through the repository's `obd.sh` wrapper:

```bash
./tools/deploy/obd.sh prepare -p /tmp/obtest
./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml
```

Read `mysql_port` from `tools/deploy/single.yaml`. When the generated port is `10000`, connect with either client:

```bash
mysql -h127.0.0.1 -P10000 -uroot
./deps/3rd/u01/obclient/bin/obclient -h127.0.0.1 -P10000 -uroot -Doceanbase -A
```

Use the actual generated port if it differs.

## Stop and remove the local deployment

```bash
./tools/deploy/obd.sh destroy --rm -n single
```

This command stops the local instance and removes the deployment data created by this example. Do not point it at data that must be retained.
