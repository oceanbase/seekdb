// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Native metadata plus handwritten SQL, using the ordinary routine installer.
use seekdb_extension::schema::{scalar_wrapper, Package, Script, SqlAccess, SqlType};

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 1 {
        return Err("expected a CLI-owned staging directory".into());
    }
    let mut base = scalar_wrapper(
        "rust_text_length",
        &seekdb_rust_text::count_function::DEFINITION,
        &[("input_text", SqlType::Text)],
        SqlType::BigInt,
        SqlAccess::NoSql,
    )?;
    // Custom-type composition remains author SQL. Do not erase a custom type
    // signature to bytes or pretend the host supports CREATE TYPE yet.
    base.push_str("\nCREATE FUNCTION rust_unicode_length(input_text TEXT)\nRETURNS BIGINT\nDETERMINISTIC\nNO SQL\nSQL SECURITY INVOKER\nRETURN seekdb_rust_char_count(seekdb_rust_text(input_text));\n");
    Package {
        name: "rust_text_ops",
        default_version: "1.0",
        native_module: Some("org.seekdb.rust-text"),
        scripts: &[
            Script {
                from: None,
                to: "1.0",
                sql: &base,
            },
            Script {
                from: Some("1.0"),
                to: "1.1",
                sql: include_str!("../../sql_packages/rust_text_ops/rust_text_ops--1.0--1.1.sql"),
            },
        ],
    }
    .write_to(std::path::Path::new(&args[0]))
}
