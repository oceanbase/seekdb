# Get the code, build, and run seekdb

## Prerequisites

Install the supported compiler and dependencies described in [Install the toolchain](toolchain.md). A full source build requires substantial disk space and memory.

## Clone the repository

```bash
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
```

## Build the production binary

The repository's `build.py` downloads the Bazel version pinned in `.bazelversion` and runs the requested command. The default build uses `-O2` and includes debug information.

```bash
./build.py init
./build.py build seekdb
```

`init` prepares Rust and platform dependencies; run it on the first build or after dependency metadata or `rust/rust-toolchain.toml` changes. For incremental builds, repeat only the `build` command. The resulting binary is `build/bin/src/observer/seekdb`.

Put all options after the command, including Bazel startup options such as `--batch` and `--bazelrc`. `build.py` reads the installed Bazel's option definitions on each invocation and moves startup options before the command internally. Keep Bazel's original option names, including underscores.

On Windows, use `python build.py` with the same commands. For C++ test targets, see [Write and run unit tests](unittest.md). When you change Rust dependencies, commit the updated `rust/Cargo.lock`.

## Set build concurrency

Bazel defaults to the available host CPU count. When you explicitly set local build concurrency, `build.py` sets both the job limit and the local CPU scheduling budget:

```bash
./build.py build -j32 seekdb
```

`-j32`, `-j 32`, and `--jobs=32` are equivalent. Only explicit numeric command-line values are synchronized; omitting the option preserves Bazel's automatic defaults. An explicit `--local_resources=cpu=N` overrides this budget. Memory settings are unaffected.

To pass a job option to another tool, use an equals sign, such as `--test_arg=-j32`. Program arguments after `run --` are forwarded unchanged.

## Use remote C++ compilation

Specify your REAPI service address and concurrency:

```bash
./build.py build --remote_executor=grpc://HOST:PORT --jobs=100 seekdb
```

The default strategies in `.bazelrc` prefer remote execution for C++ compilation and use local execution when no remote executor is configured. Set `--remote_executor` without adding `--config=remote`. Cargo, generators, and linking run locally. The default local strategy order is `worker,sandboxed,local` on Linux and macOS, and `worker,local` on Windows, where Bazel does not support sandboxing. Explicit execution strategies on the command line override these defaults. If the service requires an SSH tunnel, establish the tunnel separately.

With `--config=remote` or a nonempty `--remote_executor` on the command line, `--jobs` only controls total concurrency. Bazel still detects the local CPU budget automatically unless you set it separately with `--local_resources=cpu=N`.

## Build a Sanity binary

On Linux, enable Sanity instrumentation with:

```bash
./build.py build --sanity seekdb
```

Sanity is disabled by default. `--sanity` is equivalent to `--sanity=true`; use `--sanity=false` to disable it explicitly.

To keep a separate Sanity build, specify a build directory:

```bash
./build.py build -o build_sanity --sanity seekdb
```

Its binary is accessible at `build_sanity/bin/src/observer/seekdb`.

## Build directories

Bazel's working directory defaults to `build/` inside the project. It contains intermediate files and final outputs. The `build/bin`, `build/out`, and other output links point into this directory; `build/bin/src/observer/seekdb` is the native Linux/macOS binary path.

Put `-o PATH` or `--output_base=PATH` after the command; `build.py` also sets the output link prefix to `PATH/`. For example, `./build.py build -o build3 seekdb` uses `build3/` as the working directory and `build3/bin/src/observer/seekdb` as the binary path. Relative paths are resolved from the project root; absolute paths can place builds on another disk. Different directories have independent build state and links and can build concurrently. Bazel serializes commands using the same directory.

Run `./build.py info output_base` to locate the working directory and `./build.py clean --expunge` to remove it. For a custom directory, use `./build.py info -o build3 output_base` or `./build.py clean -o build3 --expunge`. Exclude build directories when searching source files, for example: `find . -path './build' -prune -o -type f -name '*.cpp' -print`.

## Reuse build caches

Bazel binaries and the default action cache are shared between checkouts. Their base directory is `~/.cache/bazel` on Linux, `~/Library/Caches/bazel` on macOS, or `%LOCALAPPDATA%/bazel` on Windows. Linux honors `XDG_CACHE_HOME`. Bazel manages its shared repository download cache separately.

Use `--launcher_cache_dir PATH` for the Bazel binary cache, `--disk_cache=PATH` for build actions, or `--repository_cache=PATH` for downloaded dependencies. All go after the command. For example:

```bash
./build.py build --launcher_cache_dir=/data/bazel --disk_cache=/data/bazel-actions seekdb
```

To change dependency download settings, use Bazel's `--registry=URL` or `--downloader_config=PATH` after the command.

## Build for another platform

Use the `seekdb` target on every platform. Select a platform with `--config=android`, `--config=ios`, `--config=ios-simulator`, or `--config=wasm`; without a platform configuration, the target builds the native program. Follow the [Android](android.md), [iOS](ios.md), or [WebAssembly](webassembly.md) guide to prepare dependencies. Android builds a server program, iOS builds a framework, and WebAssembly builds an engine and SQL shell. The alias preserves each platform's output paths.

## Maintain build declarations

Choose the file by what you need to change:

| File | Responsibility |
| --- | --- |
| `build.py` | Launch the pinned Bazel binary and prepare platform dependencies with `init`. |
| `.bazelrc` | Default Bazel options and named platform configurations. |
| `MODULE.bazel` | Bazel module dependencies and repository/toolchain registration. |
| `bazel/defs.bzl` | Shared C++ and linking rule entry points for `BUILD.bazel` files. |
| `bazel/compile_config.bzl` | Common compiler options and Sanity instrumentation settings. |
| `bazel/build_settings.bzl`, `bazel/BUILD.bazel` | Custom setting types and shared flag declarations. |
| A package's `BUILD.bazel` | Targets, headers, explicit dependencies, and visibility. |

For C++ libraries, load `cc_library`, `unity_cc_library`, or `grouped_unity_cc_library` from `//bazel:defs.bzl`. `cc_library` compiles each source separately. `unity_cc_library` combines an ordered `unity_members` list into one translation unit. `grouped_unity_cc_library` creates one translation unit per named group in `unity_groups`.

- Add a normal source to its owning target's `srcs`. For an existing Unity group, add its repository-relative path to that group's `srcs` in the package's `*_source_inventory.bzl`; preserve the member order. Targets declared with `unity_members` list package-relative paths directly in `BUILD.bazel`.
- To keep a file out of a multi-file Unity group, add it to the standalone source list consumed by its target. These lists contain paths. Keep C and C++ lists separate; the rule call selects the language. A new list must be connected to a target in `BUILD.bazel`.
- Put public headers in `hdrs` or the module's public header list. Put private headers in `srcs` or the module's internal header target; modules that use a header `glob()` pick up matching new files automatically. The Unity helpers call these attributes `public_hdrs` and `private_hdrs`.
- Add dependencies used by public headers to `deps`. Put dependencies used only by implementation files in `implementation_deps`. Set `visibility` to the intended consumers. Dependencies are maintained explicitly; there is no include scanner that generates them.

Build the affected target with `./build.py build //package:target`. Existing module-direction and header-layering checks run as part of the build.

## Run a local instance

On Linux and macOS, start the compiled binary with the repository's instance helper:

```bash
python3 tools/deploy/sdb.py start \
  --binary build/bin/src/observer/seekdb \
  --base-dir "$PWD/build/dev-instance" --port 2881
python3 tools/deploy/sdb.py wait-ready \
  --client deps/3rd/u01/obclient/bin/obclient \
  --base-dir "$PWD/build/dev-instance" --port 2881
```

The helper starts seekdb directly. A new data directory initializes automatically. Pass repeated `--parameter key=value` options to `start` when you need server settings. `start` returns after spawning the process; `wait-ready` checks the managed process and waits for `SELECT 1` to succeed.

Connect with either client:

```bash
mysql -h127.0.0.1 -P2881 -uroot
./deps/3rd/u01/obclient/bin/obclient -h127.0.0.1 -P2881 -uroot -Doceanbase -A
```

To run in the foreground, including on Windows, invoke the binary directly with `--nodaemon --base-dir=<directory> --port=2881`. The Python instance helper currently supports Linux and macOS.

## Stop or remove the local instance

Stop the instance and keep its data:

```bash
python3 tools/deploy/sdb.py stop --base-dir "$PWD/build/dev-instance"
```

Run the same `start` and `wait-ready` commands to restart it. To stop it and delete this example's data instead:

```bash
python3 tools/deploy/sdb.py destroy --base-dir "$PWD/build/dev-instance"
```

`destroy` removes the entire managed instance directory. Only use it for disposable data. See [Running mysqltest](mysqltest.md) for SQL regression tests with a separate disposable instance.
