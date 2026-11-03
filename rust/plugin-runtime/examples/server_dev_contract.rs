// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_plugin_runtime::build_contract::read_build_id;
use std::{io::Write, path::PathBuf};
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if !(args.len() == 2 || (args.len() == 3 && args[2] == "--rust")) {
        return Err("usage: server_dev_contract <host ELF> <new file | -> [--rust]".into());
    }
    let id = read_build_id(&PathBuf::from(&args[0]))?;
    let bytes = id
        .iter()
        .map(|byte| format!("0x{byte:02x}"))
        .collect::<Vec<_>>()
        .join(", ");
    let body = if args.len() == 3 {
        format!("// SEEKDB_GENERATED_SERVER_DEV_CONTRACT_RS\n// Linked-host identity, not a signature or checksum.\npub const HOST_BUILD_ID: [u8; {}] = [{}];\n", id.len(), bytes)
    } else {
        format!("/* Linked-host identity, not a signature or checksum. */\n#ifndef SEEKDB_GENERATED_SERVER_DEV_CONTRACT_H_\n#define SEEKDB_GENERATED_SERVER_DEV_CONTRACT_H_\n#define SEEKDB_SERVER_DEV_HOST_BUILD_ID_SIZE {}u\n#define SEEKDB_SERVER_DEV_HOST_BUILD_ID_BYTES {{ {} }}\n#endif\n", id.len(), bytes)
    };
    if args[1] == "-" {
        std::io::stdout().write_all(body.as_bytes())?;
        return Ok(());
    }
    // Refuse to overwrite a contract or the input executable.
    let mut output = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&args[1])?;
    output.write_all(body.as_bytes())?;
    Ok(())
}
