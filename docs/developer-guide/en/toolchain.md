# Install the build toolchain

The maintained source build is Linux x86-64, Release, and Unity-only.

## System prerequisites

Install the basic tools for your distribution.

Fedora-family systems:

```shell
yum install git wget curl make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

Debian-family systems:

```shell
apt-get install git wget curl make build-essential binutils m4 file python3 libaio1
```

On distributions that renamed `libaio1`, install the compatible runtime
package such as `libaio1t64`.

## Initialize and build

The repository dependency initializer provides the compiler and third-party
libraries. Install the Bazel version recorded in `.bazelversion`; the
repository launcher uses that installed binary and never downloads Bazel or
Bazelisk.

```bash
source ~/.bashrc
./build.sh release --init
cd build_release
make -j"$(nproc)"
```

Each module owns its source, header, and Unity inventories. Cross-module
compile inputs come from declared semantic targets rather than a generated
depfile compatibility closure. Local C++ actions use Bazel's sandbox by
default; remote execution can be enabled explicitly through `bazel.py`.

The resulting executable is:

```text
build_release/src/observer/seekdb
```

macOS, Windows, Android, debug, coverage, and non-Unity source builds are not
currently maintained. Their previous entry points must not be used as evidence
that those configurations still work.
