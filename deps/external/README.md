# External source dependencies

This standalone Cargo workspace only resolves source packages from the registry.
It is independent of the server's Rust workspace and is never compiled.
Commit Cargo.lock to pin both the version and registry checksum.

The cmake/Jemalloc.cmake file runs cargo metadata --locked (which downloads
missing packages), finds vendor/jemalloc through manifest_path, and builds outside
the registry cache in <build>/third-party/jemalloc/build. It links the resulting
libjemalloc_pic.a through the seekdb_jemalloc imported target.

Use the usual CARGO_HOME / registry configuration for downloads. A registry mirror
must contain seekdb-jemalloc-sys 0.1.0+5.3.1. After fetching, CARGO_NET_OFFLINE=true
can be used for offline configuration. CMake selects the toolchain pinned by
rust/rust-toolchain.toml, so rustup does not require a default toolchain.

The local seekdb-jemalloc-sys publishing directory is not a source dependency and
must not be committed into seekdb. The server explicitly selects je_ symbols and
stats at configure time; the source package itself imposes no allocator settings.
