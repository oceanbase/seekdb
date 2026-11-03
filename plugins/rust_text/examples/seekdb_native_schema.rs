// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Build-time delivery only: never invokes the native installation callback.
use seekdb_extension::schema::{InstallSource, Package, PackageOptions, Script};

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 1 {
        return Err("expected a CLI-owned staging directory".into());
    }
    Package {
        name: "rust_text_native",
        default_version: "1.0",
        native_module: Some("org.seekdb.rust-text"),
        scripts: &[Script {
            from: Some("1.0"),
            to: "1.1",
            sql: include_str!("../../sql_packages/rust_text_native/rust_text_native--1.0--1.1.sql"),
        }],
    }
    .write_to_with_options(
        std::path::Path::new(&args[0]),
        PackageOptions {
            install_source: InstallSource::Native,
            superuser: Some(false),
            ..PackageOptions::default()
        },
    )
}
