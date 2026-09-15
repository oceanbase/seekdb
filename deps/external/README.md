# External source dependencies

This standalone Cargo workspace builds native dependencies distributed through
the registry. It is independent of the server's Rust workspace. Commit
Cargo.lock to pin both the version and registry checksum.

The CMakeLists.txt entry point enables external libraries for supported
configurations. cmake/CargoExternal.cmake selects Cargo, the pinned Rust
toolchain, and the active C compiler command. cmake/Jemalloc.cmake invokes the
crate's build.rs and exports libjemalloc_pic.a through the seekdb_jemalloc
imported target. The Bazel rule invokes the same Cargo package with the Bazel C
toolchain and consumes the same exported library/header layout.

Use the usual CARGO_HOME / registry configuration for downloads. A registry mirror
must contain seekdb-jemalloc-sys 0.2.0+5.3.1. After fetching, CARGO_NET_OFFLINE=true
can be used for offline configuration. CMake selects the toolchain pinned by
rust/rust-toolchain.toml, so rustup does not require a default toolchain.

The local seekdb-jemalloc-sys publishing directory is not a source dependency and
must not be committed into seekdb. The server explicitly selects je_ symbols
through JEMALLOC_SYS_CONFIGURE_ARGS and stats through a Cargo feature.
