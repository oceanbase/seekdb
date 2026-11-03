// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Reference package: change durable providers across a two-step migration.
//! Providers are symbolic examples; generation does not install them.
use seekdb_extension::schema::{Package, PackageOptions, Script, VersionControl};

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 1 {
        return Err("expected a CLI-owned staging directory".into());
    }
    Package {
        name: "consumer",
        default_version: "1.1",
        native_module: None,
        scripts: &[
            Script {
                from: None,
                to: "1.0",
                sql: "CREATE FUNCTION consumer_value() RETURNS INT RETURN 1;\n",
            },
            Script {
                from: Some("1.0"),
                to: "middle",
                sql: "-- Intermediate migration needs a temporary provider.\n",
            },
            Script {
                from: Some("middle"),
                to: "1.1",
                sql: "-- Explicit metadata-only migration; no schema statements.\n",
            },
        ],
    }
    .write_to_with_options(
        std::path::Path::new(&args[0]),
        PackageOptions {
            requires: &["alpha", "zulu"],
            version_controls: &[
                VersionControl {
                    requires: Some(&["migration"]),
                    ..VersionControl::new("middle")
                },
                VersionControl {
                    requires: Some(&["gamma", "zulu"]),
                    ..VersionControl::new("1.1")
                },
            ],
            ..PackageOptions::default()
        },
    )
}
