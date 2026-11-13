# Redistribution Guide

This guide applies to redistribution of the supplied seekdb artifacts **unchanged**, using the inventory for source revision `8b2e34801b6024f37717630aa6182dae9b2c82ce`. Modifying third-party source can add notice obligations; changing a license alternative can change the obligations as well. An entry marked “unresolved” must not be treated as notices-only.

## Required bundle and conventions

The simplest retention rule is to preserve the complete installed `licenses/` directory, including:

- `LICENSE` and `NOTICE`;
- `THIRD_PARTY_COMPONENTS.md` and this `REDISTRIBUTION.md`;
- every component-specific `.txt` file, including files that contain several upstream license/notice sections.

In the tables below, **“notices only” has this exact meaning:** for redistribution of the supplied artifact unchanged, **only retaining the supplied notices is required**; no corresponding-source, relink, or replacement facility is required by the supplied license text. The upstream source location is still provided for reference and for downstream modifications. For dual- or multi-licensed works, the notices-only route uses any one granted permissive alternative; retaining the complete supplied file preserves the alternatives and all copyright notices.

“Optional source” is not a corresponding-source offer. Where source or relink obligations are unresolved, the table says so explicitly rather than treating an upstream homepage as a substitute for a required distribution mechanism.

## Native development dependencies

| Component | Files/notices to retain | Corresponding source, relink, or replacement facilities | Where to obtain source or related material |
| --- | --- | --- | --- |
| OpenSSL (`devdeps-openssl-static` / `devdeps-openssl`) | Retain the complete common bundle. No OpenSSL-specific license text is supplied. | **Unresolved.** Do not assume that notices alone are sufficient until the exact license is collected and reviewed. | Upstream: <https://github.com/openssl/openssl>. The exact package versions/revisions and OceanBase repository URLs are in `deps/init/*.deps`; obtain the matching source package and license from the package provider or corresponding upstream release. |
| Boost / Boost.Geometry development package | `licenses/boost-geometry-1.74-LICENSE_1_0.txt` and `licenses/boost-geometry-notices.txt`; also retain notices in the listed in-tree source files. | For the covered Geometry material, notices only; no source/relink/replacement facility is required by BSL-1.0. Notice coverage for other compiled Boost libraries is unresolved. | Boost 1.74 source: <https://github.com/boostorg/boost>. Geometry source: <https://github.com/boostorg/geometry>. Adapted source is already in `src/share/geo/ob_geo_area_geographic.h`, `src/share/geo/ob_geo_buffer_join_round.h`, and `src/share/geo/ob_geo_buffer_point_circle.h`. |
| Abseil (`devdeps-abseil-cpp` / `devdeps-abseil`) | `licenses/abseil-LICENSE.txt` and any applicable attribution notices in the common bundle. | Notices only under Apache-2.0; no corresponding-source, relink, or replacement facility is required for the unchanged artifact. | <https://github.com/abseil/abseil-cpp>; use release 20211102.0 where that version is pinned. The dated Windows archive does not establish its upstream version. |
| CRoaring (`devdeps-roaringbitmap-croaring` / `devdeps-roaring`) | `licenses/croaring-LICENSE.txt`, including both Apache-2.0 and MIT alternatives. | Notices only under either granted alternative; no source/relink/replacement facility is required. | <https://github.com/RoaringBitmap/CRoaring>; use release 3.0.0 where pinned. The dated Windows archive does not establish its upstream version. |
| VSAG (`devdeps-vsag-abiv1` / `devdeps-vsag`) | `licenses/vsag-LICENSE.txt`, plus the OpenBLAS, nlohmann/json, and robin-map notices listed below. | Notices only for VSAG under Apache-2.0 and for the listed dependencies under their supplied permissive texts. | <https://github.com/oceanbase/vsag>; use 1.1.0 on Linux/macOS and 0.18.0 on Android where pinned. The dated Windows archive does not establish its upstream version. |
| curl (`devdeps-libcurl-static` / `devdeps-libcurl` / `devdeps-curl`) | Retain the complete common bundle. No curl-specific license text is supplied. | **Unresolved.** Do not assume that notices alone are sufficient until the exact license is collected and reviewed. | <https://github.com/curl/curl>; exact pinned versions and archive revisions are in `deps/init/*.deps`. Obtain the corresponding source archive and license for the platform being redistributed. |
| libaio (`devdeps-libaio`) | Retain the complete common bundle. No libaio-specific license text is supplied. Keep `libaio.so`, `libaio.so.1`, and `libaio.so.1.0.1` together so the supplied runtime linkage remains usable. | **Unresolved.** The applicable corresponding-source and relink/replacement obligations, and a usable distribution mechanism for any required material, must be confirmed before redistribution. This guide does not claim that retaining notices alone is sufficient. | Upstream: <https://pagure.io/libaio>. Version 0.3.112 and the platform package revisions/repository are pinned in the Linux `deps/init/*.deps` manifests; obtain the exact corresponding source and build/relink instructions from the package provider or matching upstream release. |
| obshell | Retain the complete common bundle. No obshell-specific license text is supplied. | **Unresolved.** Determine the license and any source obligations before redistribution. | <https://github.com/oceanbase/obshell>; version 4.4.1.1 and package/archive revisions are pinned in `deps/init/*.deps`. |
| XZ (`devdeps-xz`) | Retain the complete common bundle. No XZ-specific license text is supplied. | **Unresolved.** Final inclusion, license, and source/relink obligations are not established by the manifest. | <https://tukaani.org/xz/>; macOS 15/27 pin version 5.2.2 and source-archive revision 20251208. |
| Windows dirent input (`devdeps-dirent`) | Retain the complete common bundle. No component-specific license text is supplied. | **Unresolved.** Identify the exact project and its license before redistribution. | The only pinned identity is source archive 20260506 in `deps/init/oceanbase.windows.x86_64.deps`; the upstream project and version are unspecified. |
| Windows pthreads input (`devdeps-pthreads`) | Retain the complete common bundle. No component-specific license text is supplied. | **Unresolved.** Identify the exact project and confirm corresponding-source and relink/replacement obligations before redistribution. | The only pinned identity is source archive 20260506 in `deps/init/oceanbase.windows.x86_64.deps`; the upstream project and version are unspecified. |

## Native code and resources

| Component | Files/notices to retain | Corresponding source, relink, or replacement facilities | Where to obtain source or related material |
| --- | --- | --- | --- |
| Boost.Geometry adapted strategies | `licenses/boost-geometry-1.74-LICENSE_1_0.txt`, `licenses/boost-geometry-notices.txt`, and the copyright/modification comments in the three source files named in that notice file. | Notices only for unchanged redistribution; no additional source/relink/replacement facility is required by BSL-1.0. | Original: <https://github.com/boostorg/geometry> at Boost 1.74. Modified source: the three `src/share/geo/` files identified in `licenses/boost-geometry-notices.txt`. |
| libev | `licenses/libev.txt`, including the complete BSD/GPL alternative text. | The notices-only route uses BSD-2-Clause. No source/relink/replacement facility is required under that alternative. Choosing GPL-2.0-or-later instead invokes different obligations not described by this notices-only statement. | <http://software.schmorp.de/pkg/libev.html>; in-tree source under `src/oblib/easy/io/`, version 4.1 from `ev.h`. |
| http_parser | `licenses/http_parser.txt`. | Notices only under MIT; no source/relink/replacement facility is required. | <https://github.com/nodejs/http-parser>. The version and containing module are unspecified in the pinned evidence. |
| LZ4 1.7.1 | `licenses/lz4.171.txt`. | Notices only under BSD-2-Clause; no source/relink/replacement facility is required. | <https://github.com/lz4/lz4>, release 1.7.1. |
| LZ4 1.9.1 | `licenses/lz4.191.txt`; do not discard the separate 1.7.1 notice. | Notices only under BSD-2-Clause; no source/relink/replacement facility is required. | <https://github.com/lz4/lz4>, release 1.9.1. |
| Snappy | `licenses/snappy.txt`. | Notices only under BSD-3-Clause; no source/relink/replacement facility is required. | <https://github.com/google/snappy>. The pinned version is unspecified. |
| zlib | `licenses/zlib.txt`. | Notices only under Zlib; no source/relink/replacement facility is required for unchanged redistribution. Altered source versions must be plainly marked and must not be misrepresented as the original. | <https://zlib.net/>. The upstream version is unspecified; the Windows source archive revision is 20260506. |
| xxHash | `licenses/xxhash-LICENSE.txt` and the copyright notice in the in-tree source. | Notices only under BSD-2-Clause; no source/relink/replacement facility is required. | <https://github.com/Cyan4973/xxHash>, version 0.6.2; in-tree source is `src/oblib/lib/hash_func/xxhash.c` and `xxhash.h`. |
| IANA timezone source notices | `licenses/timezone-public-domain.txt` and the notices in `tzfile.h`, `private.h`, and `localtime.c` when distributing source. | Only retaining the supplied notices is required for this bundle; no corresponding-source, relink, or replacement facility is required by the public-domain notice. | <https://www.iana.org/time-zones>; the exact source revision is unspecified. Packaged data is `share/timezone/timezone_V1.log` relative to the selected layout. |
| OpenBLAS | `licenses/openblas-LICENSE.txt`. | Notices only under BSD-3-Clause; no source/relink/replacement facility is required. | <https://github.com/OpenMathLib/OpenBLAS>. The pinned version and exact containing module are unspecified. |
| nlohmann/json | `licenses/nlohmann-json-LICENSE.MIT.txt`. | Notices only under MIT; no source/relink/replacement facility is required. | <https://github.com/nlohmann/json>. The pinned version and exact containing module are unspecified. |
| tsl::robin_map | `licenses/robin-map-LICENSE.txt`. | Notices only under MIT; no source/relink/replacement facility is required. | <https://github.com/Tessil/robin-map>. The pinned version and exact containing module are unspecified. |

## Rust crates

Every row in this section is **notices only** for the unchanged artifact: only retaining the supplied notices is required, and no corresponding-source, relink, or replacement facility is required by the supplied license texts. Preserve the entire file named in each row because several files contain multiple licenses, copyright notices, or an upstream NOTICE. The crates.io download URL is an optional upstream source archive, not a statement that source must accompany the binary.

| Component and locked version | Files/notices to retain | Optional corresponding upstream source |
| --- | --- | --- |
| chrono 0.4.45 | `licenses/rust-chrono-0.4.45.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/chrono/0.4.45/download> |
| cxx 1.0.202 | `licenses/rust-cxx-1.0.202.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/cxx/1.0.202/download> |
| fancy-regex 0.20.0 | `licenses/rust-fancy-regex-0.20.0.txt` (MIT) | <https://crates.io/api/v1/crates/fancy-regex/0.20.0/download> |
| fast-float2 0.2.4 | `licenses/rust-fast-float2-0.2.4.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/fast-float2/0.2.4/download> |
| getrandom 0.4.3 | `licenses/rust-getrandom-0.4.3.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/getrandom/0.4.3/download> |
| globset 0.4.18 | `licenses/rust-globset-0.4.18.txt` (Unlicense OR MIT) | <https://crates.io/api/v1/crates/globset/0.4.18/download> |
| libc 0.2.189 | `licenses/rust-libc-0.2.189.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/libc/0.2.189/download> |
| libm 0.2.16 | `licenses/libm.txt` (MIT) | <https://crates.io/api/v1/crates/libm/0.2.16/download> |
| log 0.4.34 | `licenses/rust-log-0.4.34.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/log/0.4.34/download> |
| memchr 2.8.3 | `licenses/rust-memchr-2.8.3.txt` (Unlicense OR MIT) | <https://crates.io/api/v1/crates/memchr/2.8.3/download> |
| mio 1.2.3 | `licenses/rust-mio-1.2.3.txt` (MIT) | <https://crates.io/api/v1/crates/mio/1.2.3/download> |
| prost 0.14.3 | `licenses/rust-prost-0.14.3.txt` (Apache-2.0) | <https://crates.io/api/v1/crates/prost/0.14.3/download> |
| quick-xml 0.41.0 | `licenses/rust-quick-xml-0.41.0.txt` (MIT) | <https://crates.io/api/v1/crates/quick-xml/0.41.0/download> |
| ring 0.17.14 | `licenses/rust-ring-0.17.14.txt`; retain all sections: ring/ISC, BoringSSL/Apache-2.0, other-bits, once_cell alternatives, and fiat/Apache-2.0. | <https://crates.io/api/v1/crates/ring/0.17.14/download> |
| rustls 0.23.46 | `licenses/rust-rustls-0.23.46.txt` (Apache-2.0 OR ISC OR MIT) | <https://crates.io/api/v1/crates/rustls/0.23.46/download> |
| rustls-pki-types 1.15.1 | `licenses/rust-rustls-pki-types-1.15.1.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/rustls-pki-types/1.15.1/download> |
| ryu 1.0.23 | `licenses/rust-ryu-1.0.23.txt` (Apache-2.0 OR BSL-1.0) | <https://crates.io/api/v1/crates/ryu/1.0.23/download> |
| s2rst 0.5.1 | `licenses/rust-s2rst-0.5.1.txt`; this includes the Apache-2.0 license and upstream NOTICE. | <https://crates.io/api/v1/crates/s2rst/0.5.1/download> |
| serde 1.0.229 | `licenses/rust-serde-1.0.229.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/serde/1.0.229/download> |
| serde_json 1.0.151 | `licenses/serde_json.txt`; the supplied redistribution path uses MIT. | <https://crates.io/api/v1/crates/serde_json/1.0.151/download> |
| SimSIMD / simsimd 6.5.16 | `licenses/rust-simsimd-6.5.16.txt` (Apache-2.0) | <https://crates.io/api/v1/crates/simsimd/6.5.16/download> |
| slab 0.4.12 | `licenses/rust-slab-0.4.12.txt` (MIT) | <https://crates.io/api/v1/crates/slab/0.4.12/download> |
| socket2 0.6.5 | `licenses/rust-socket2-0.6.5.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/socket2/0.6.5/download> |
| spdlog-rs 0.5.3 | `licenses/rust-spdlog-rs-0.5.3.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/spdlog-rs/0.5.3/download> |
| tokio 1.52.3 | `licenses/rust-tokio-1.52.3.txt` (MIT) | <https://crates.io/api/v1/crates/tokio/1.52.3/download> |
| tokio-stream 0.1.18 | `licenses/rust-tokio-stream-0.1.18.txt` (MIT) | <https://crates.io/api/v1/crates/tokio-stream/0.1.18/download> |
| tonic 0.14.6 | `licenses/rust-tonic-0.14.6.txt` (MIT) | <https://crates.io/api/v1/crates/tonic/0.14.6/download> |
| tonic-prost 0.14.6 | `licenses/rust-tonic-prost-0.14.6.txt` (MIT) | <https://crates.io/api/v1/crates/tonic-prost/0.14.6/download> |
| uuid 1.29.0 | `licenses/rust-uuid-1.29.0.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/uuid/1.29.0/download> |
| walkdir 2.5.0 | `licenses/rust-walkdir-2.5.0.txt` (Unlicense OR MIT) | <https://crates.io/api/v1/crates/walkdir/2.5.0/download> |
| windows-sys 0.61.2 | `licenses/rust-windows-sys-0.61.2.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/windows-sys/0.61.2/download> |
| x509-parser 0.18.1 | `licenses/rust-x509-parser-0.18.1.txt` (MIT OR Apache-2.0) | <https://crates.io/api/v1/crates/x509-parser/0.18.1/download> |
| zlib-rs 0.6.8 | `licenses/rust-zlib-rs-0.6.8.txt` (Zlib); plainly mark any altered source version. | <https://crates.io/api/v1/crates/zlib-rs/0.6.8/download> |
| zmij 1.0.23 | `licenses/zmij.txt`; the supplied redistribution path uses MIT. | <https://crates.io/api/v1/crates/zmij/1.0.23/download> |
| zstd-safe 8.2.0 | `licenses/zstd-safe.txt` (BSD-3-Clause) | <https://crates.io/api/v1/crates/zstd-safe/8.2.0/download> |
| zstd-sys 2.1.2+zstd.1.5.7 | `licenses/zstd-sys.txt`; retain both bindings and bundled Zstandard BSD-3-Clause notices. | <https://crates.io/api/v1/crates/zstd-sys/2.1.2+zstd.1.5.7/download> |

### Jemalloc text with unresolved package presence

`licenses/rust-seekdb-jemalloc-sys-0.2.3+5.3.1.txt` contains both the wrapper's Apache-2.0 text and jemalloc's BSD-2-Clause text. If this component is present in an artifact, only retaining the supplied notices is required by those texts; no source/relink/replacement facility is required for unchanged redistribution. However, the crate is absent from `Cargo.lock`, so its pinned version and actual inclusion are unresolved. Do not use the filename alone as proof of the shipped version. Upstream project page: <https://crates.io/crates/seekdb-jemalloc-sys>.

### Unresolved transitive-only Rust crates

The inventory covers direct external runtime dependencies and locked transitive `ring` and `zmij`. For any additional transitive-only crate included by the final feature/target resolution, license collection and retention requirements remain unresolved. Resolve the final crate set for the target, collect its exact registry license texts, and add it to the inventory and this guide before treating the bundle as a complete final-artifact audit. `Cargo.lock` by itself does not establish that every locked crate is linked.

## Build and test tools are not shipped

The following are build- or test-only inputs, not seekdb runtime components staged by `package/package.py`:

| Component | Redistribution treatment for seekdb recipients | Source/reference |
| --- | --- | --- |
| GoogleTest (`devdeps-gtest`) | No seekdb runtime notice/source/relink action is required because the test dependency is not staged. Its license is unspecified in the pinned manifest and must be reviewed if GoogleTest itself is redistributed. | <https://github.com/google/googletest>, version 1.8.0 in the Linux manifests. |
| GCC (`obdevtools-gcc`) | Not staged; no seekdb runtime action. Review toolchain runtime exceptions and licenses separately if distributing the compiler or its runtime libraries. | <https://gcc.gnu.org/> and the Linux manifests. |
| Bison/Flex (`obdevtools-bison`, `obdevtools-flex`, `obdevtools-win-flex-bison`) | Not staged; no seekdb runtime action. Review their licenses if distributing the tools. | <https://www.gnu.org/software/bison/>; <https://github.com/westes/flex>. |
| LLVM (`obdevtools-llvm`, `devdeps-llvm`) | Not staged as a runtime library by this packaging path; no seekdb runtime action here. Review separately if distributing LLVM or its runtime libraries. | <https://llvm.org/> and the platform manifests. |
| rpmbuild | External packaging executable; not staged. | <https://github.com/rpm-software-management/rpm>; version is not pinned in this repository. |
| auditwheel | External wheel-repair tool; not staged by the native packager. Any libraries it adds to a wheel are **not covered** by this table and must be inventoried from the repaired artifact. | <https://github.com/pypa/auditwheel>; version is not pinned in this repository. |
| obclient / libobclient | Test utilities; not staged by `package/package.py`. Review their licenses if distributing the clients themselves. | <https://github.com/oceanbase/obclient>; versions are pinned in the platform manifests. |

## Final-artifact limitation

No engine or installation package was built for this guide, and no final RPM, DEB, TGZ, MSI, or wheel was inspected. The native server and libs layouts are staged by `package/package.py`; the RPM libs file list is in `package/rpm/seekdb.spec.in`. Before release, inspect the actual artifact for its platform and architecture, repeat the inventory for any libraries added by final packaging/repair, and resolve every item marked `unspecified` or “unresolved” that applies to that artifact.
