# GitHub packaging

After `.github/workflows/package.yml` is merged into the default branch, open
**Actions → Package seekdb → Run workflow**. Select a source branch/tag that
contains the workflow, then choose:

| Input | Values |
| --- | --- |
| Platform | Linux x86_64/aarch64, macOS arm64, Windows x86_64, Android arm64, or all |
| Linux glibc | 2.17, 2.28, or both |
| Version | Empty uses `VERSION` from `bazel/compile_config.bzl` |
| Release | Package release, default 1 |
| Formats | Binary archive only, RPM, DEB, or both system formats |
| Jobs | 0 selects one job on macOS and two elsewhere; other values are passed to Bazel |

The workflow builds O2 with line tables and the package runtime configuration.
Each target compiles seekdb once. Download the `binary-*` artifacts for portable
archives and checksums. Linux `system-packages-*` artifacts contain RPM/DEB
packages made from those archives and the SDK's obshell, without another SDK
initialization or engine build. Other targets produce portable archives.
Artifacts use the repository's default retention period; the workflow does
not publish a release.
When building both glibc variants, keep their system-package artifact directories
separate: native RPM/DEB filenames do not include the glibc variant.

## Compatibility

- Linux builds run directly on the Ubuntu runners with Conda-provided glibc
  2.17 or 2.28 sysroots, using the EL7 or EL8 dependency SDK respectively.
  Conda supplies system headers and libraries; the environment is not activated
  and compilation still uses seekdb's bundled Clang/Bazel toolchain. The host
  tools' runtime requirements do not set the engine's glibc floor. The exporter
  checks actual ELF symbol requirements, and system packaging also checks obshell.
- macOS builds target 13.0 and validate the executable and every bundled dylib.
- Windows builds retain `/MD` and the Windows 10 compatibility target. The
  bundled non-system DLLs and CRT are checked during export. A future VSAG
  integration can switch seekdb and its dependencies to `/MT` together.
- Android builds target API 26, use NDK 27.3.13750724, and require Android ELF
  API notes and at least 16 KiB load alignment. Non-system dependencies must
  be linked statically for the current exporter.

The timezone resource, micromamba version, and Conda sysroot versions/builds
are pinned in `.github/packaging.json`. The timezone and installed sysroot
package records are checked against their pinned SHA-256 values before use.
The workflow uses the public upstream Bazel download URLs with their existing
integrity checks instead of the developer proxy configuration.

## Dependency cache

The workflow restores and saves one dependency cache containing `deps/3rd`
and `deps/android`. These directories hold the extracted SDKs and their
downloaded archives in `pkg/`. The cache key includes the target, dependency
manifests, and initializer. `init` rechecks the manifest identity when an older
same-target snapshot is restored, reusing its downloaded archives as needed.
The initialized SDK is saved before compilation.

Conda, Rust/Cargo, the Android NDK, Bazel downloads, Homebrew, timezone data,
and compilation outputs are not persisted between workflow runs. Additional
caches can be evaluated after measuring the actual workflow costs.

For machines outside Actions, `build.py init --linux-profile el7` can select
the EL7 dependency SDK on a newer Linux host. Set `SEEKDB_SYSROOT` to a complete
matching sysroot before building; selecting older dependency archives alone
does not lower the compiler's system headers or link libraries. For example,
after installing `sysroot_linux-64=2.17` into a Conda environment, point
`SEEKDB_SYSROOT` at `<environment>/x86_64-conda-linux-gnu/sysroot`.

Full engine builds need substantial RAM and disk. The runner labels are in
`.github/scripts/package.py`; use suitable larger/self-hosted runners there
if standard hosted runners are too small. Runtime certification on the oldest
supported systems is separate from the workflow's build and packaging checks.
