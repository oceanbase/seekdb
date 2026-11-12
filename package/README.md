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
| Jobs | 0 selects one job on macOS and two elsewhere; 1..64 overrides this |

The workflow builds O2 with line tables and the package runtime configuration.
Each target compiles seekdb once. Download the `binary-*` artifacts for portable
archives and checksums. Linux `system-packages-*` artifacts contain RPM/DEB
packages made from those archives and the SDK's obshell, without another SDK
initialization or engine build. Other targets produce portable archives.
Artifacts are retained for 14 days; the workflow does not publish a release.
When building both glibc variants, keep their system-package artifact directories
separate: native RPM/DEB filenames do not include the glibc variant.

## Compatibility

- Linux builds run tools in pinned manylinux 2.28 containers. The 2.17 variant
  uses the EL7 dependency SDK and an isolated glibc 2.17 sysroot; Bazel 9.3's
  own runtime requirements do not set the engine's glibc floor. The 2.28
  variant uses the EL8 dependency SDK. The exporter checks actual ELF symbol
  requirements, and system packaging also checks obshell.
- macOS builds target 13.0 and validate the executable and every bundled dylib.
- Windows builds retain `/MD` and the Windows 10 compatibility target. The
  bundled non-system DLLs and CRT are checked during export. A future VSAG
  integration can switch seekdb and its dependencies to `/MT` together.
- Android builds target API 26, use NDK 27.3.13750724, and require Android ELF
  API notes and at least 16 KiB load alignment. Non-system dependencies must
  be linked statically for the current exporter.

The timezone resource and build/sysroot images are pinned in
`.github/packaging.json`. A pinned timezone SHA-256 is checked before use.
The workflow uses the public upstream Bazel download URLs with their existing
integrity checks instead of the developer proxy configuration.

## Caches

| Cache | Contents and invalidation |
| --- | --- |
| SDK | Extracted `deps/3rd`, Android dependencies, and the Linux sysroot; keyed by target, dependency manifests, initializer, and pinned image metadata. `init` rechecks the manifest identity of an older same-target SDK snapshot; archives are reused from the separate download cache. |
| Rust | Installed toolchain and rustup shims; keyed by runner OS/architecture, native/Android target, and `rust-toolchain.toml`. |
| Downloads | SDK archives, Bazel launcher and repository downloads, Cargo registry/Git sources, Homebrew downloads, and timezone data. Completed SDK downloads survive a later initialization failure. |
| Android NDK | Separate cache for the pinned NDK. |
| Compilation | Bazel's disk action cache for C++ and Rust; isolated by target, ABI, SDK, Rust/Bazel versions, and the actual Windows toolset/SDK or Xcode version. |

The compilation cache restores the latest compatible snapshot, then saves a
new run-specific key so an immutable cache entry does not stop incremental
updates. Completed compilation actions are saved after failed builds too.
The initialized SDK and Rust caches are saved before compilation. Bazel's
action cache is limited to 2 GiB per target; the entire Bazel output base is
not archived. Allocate sufficient repository Actions cache storage for the
selected platform matrix to avoid eviction of frequently used caches.

For machines outside Actions, `build.py init --linux-profile el7` can select
the EL7 dependency SDK on a newer Linux host. Set `SEEKDB_SYSROOT` to a complete
matching sysroot before building; selecting older dependency archives alone
does not lower the compiler's system headers or link libraries.

Full engine builds need substantial RAM and disk. The runner labels are in
`.github/scripts/package.py`; use suitable larger/self-hosted runners there
if standard hosted runners are too small. Runtime certification on the oldest
supported systems is separate from the workflow's build and packaging checks.
