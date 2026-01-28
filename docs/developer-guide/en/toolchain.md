# Install toolchain

To build OceanBase seekdb from source code, you need to install the C++ toolchain in your development environment first. If the C++ toolchain is not installed yet, you can follow the instructions in this document for installation.

## Supported OS

OceanBase makes strong assumption on the underlying operating systems. Not all the operating systems are supported; especially, Windows is not supported yet.

Below is the OS compatibility list:

| Operating System    | Version               | Architecture     | Compatibility | Installer Deployment | Binary Deployment          | MySQLTest Test   |
| ------------------- | --------------------- | ---------------- | ------------- | -------------------- | -------------------------- | ---------------- |
| Alibaba Cloud Linux | 3                     | x86_64 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| CentOS              | 7 / 8 / 9             | x86_64 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| Debian              | 11 / 12 / 13          | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| Fedora              | 33                    | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| Kylin               | V10                   | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| openSUSE            | 15.2                  | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| OpenAnolis          | 8  / 23               | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| OpenEuler           | 22.03  / 24.03        | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| Rocky Linux         | 8  / 9                | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| StreamOS            | 3.4.8                 | x86_84 / aarch64 | Unknown       | Yes                  | Yes                        | Unknown          |
| SUSE                | 15.2                  | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| Ubuntu              | 20.04 / 22.04 / 24.04 | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| UOS                 | 20                    | x86_84 / aarch64 | Yes           | Yes                  | Yes                        | Yes              |
| macOS                | 12.0+ (Monterey+)     | x86_64 / arm64        | Yes*        | Yes                  | Yes                        | Limited          |

> **Note**:
>
> - Other Linux distributions _may_ work. If you verify that OceanBase seekdb can compile and deploy on a distribution except ones listed above, feel free to submit a pull request to add it.
> - macOS support is experimental and tested on both Intel (x86_64) and Apple Silicon (arm64) architectures. Some features may have limitations compared to Linux builds.

## Installation

The installation instructions vary among the operating systems and package managers you develop with. Below are the instructions for some popular environments:

### Fedora based

This includes CentOS, Fedora, OpenAnolis, RedHat, UOS, etc.

```shell
yum install git wget rpm* cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

### Debian based

This includes Debian, Ubuntu, etc.

```shell
apt-get install git wget rpm rpm2cpio cpio make build-essential binutils m4 file python3
```

> **Note**: If you are using Ubuntu 24.04 or later, or Debian 13 or later, you also need to install `libaio1t64`:
>
> ```shell
> apt-get install libaio1t64
> ```

### SUSE based

This includes SUSE, openSUSE, etc.

```shell
zypper install git wget rpm cpio make glibc-devel binutils m4 python3
```

### macOS

For macOS development, you need to install Homebrew first if it's not already installed:

```shell
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Then install the required dependencies:

```shell
# Install essential build tools and libraries
brew install zstd googletest utf8proc thrift re2 brotli bzip2

# Note: bzip2 is keg-only (not symlinked), but macOS usually provides a system version
# that should work for linking purposes.
```

**Required dependencies for macOS:**
- `zstd` - Compression library required by the build toolchain
- `googletest` - Google Test framework for unit tests (includes GTest and GMock)
- `utf8proc` - UTF-8 processing library (dependency of Apache Arrow on macOS)
- `thrift` - Apache Thrift (dependency of Apache Arrow on macOS)
- `re2` - Regular expression library (dependency of Apache Arrow on macOS)
- `brotli` - Compression library (dependency of Apache Arrow on macOS)
- `bzip2` - Compression library (dependency of Apache Arrow on macOS)

**Additional notes:**
- The build system will automatically download and use the OceanBase development toolchain (LLVM, Bison, Flex) from the dependency packages.
- Make sure you have Xcode Command Line Tools installed:
  ```shell
  xcode-select --install
  ```

**Common macOS build issues and solutions:**

If you encounter build errors on macOS, here are common issues and their solutions:

- **Error: `library 'utf8proc' not found`**: Make sure you've installed all required dependencies via Homebrew. Run `brew install utf8proc thrift re2 brotli` if you haven't already.

- **Error: `Could NOT find GTest`**: Install googletest via `brew install googletest`.

- **Error: `Library not loaded: libzstd.1.dylib`**: Install zstd via `brew install zstd`.

- **Error: `Cannot find source file: ftsblex_lex.c`**: This usually happens if parser files were deleted but the cache file (`_MD5`) still exists. Delete the cache files and rebuild:
  ```shell
  rm src/sql/parser/_MD5
  rm src/pl/parser/_MD5
  ```
  Then run the build again. The build system will automatically regenerate the parser files.
