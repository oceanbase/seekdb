// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! A Rust generator can author a pure SQL package without binding this DSO.
use seekdb_extension::schema::{Package, PackageOptions, Script};

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 1 {
        return Err("expected a CLI-owned staging directory".into());
    }
    Package {
        name: "text_composed",
        default_version: "1.0",
        native_module: None,
        scripts: &[
            Script {
                from: None,
                to: "1.0",
                sql: include_str!("../../sql_packages/text_composed/text_composed--1.0.sql"),
            },
            Script {
                from: Some("1.0"),
                to: "1.1",
                sql: include_str!("../../sql_packages/text_composed/text_composed--1.0--1.1.sql"),
            },
        ],
    }
    .write_to_with_options(
        std::path::Path::new(&args[0]),
        PackageOptions {
            requires: &["text_ops"],
            superuser: Some(false),
            ..PackageOptions::default()
        },
    )
}
