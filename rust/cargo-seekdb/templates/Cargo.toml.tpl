[package]
name = "@@NAME@@"
version = "1.0.0"
edition = "2021"
publish = false

[lib]
name = "seekdb_@@NAME@@"
crate-type = ["cdylib", "rlib"]

[dependencies]
seekdb-extension = { path = @@SDK_TOML@@ }

# Independent from the host's panic=abort workspace.
[workspace]

[profile.release]
panic = "unwind"
lto = "thin"
codegen-units = 1
