// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::{error::Error, fs, path::PathBuf};

fn main() -> Result<(), Box<dyn Error>> {
    let mut args = std::env::args_os().skip(1);
    let proto = PathBuf::from(args.next().ok_or("missing proto input")?);
    let protoc = PathBuf::from(args.next().ok_or("missing protoc executable")?);
    let output = PathBuf::from(args.next().ok_or("missing Rust output")?);
    if args.next().is_some() {
        return Err("unexpected generator argument".into());
    }
    let include = proto
        .parent()
        .ok_or("proto input has no parent")?
        .to_path_buf();
    let directory = output.parent().ok_or("Rust output has no parent")?;
    fs::create_dir_all(directory)?;
    let mut config = prost_build::Config::new();
    config.protoc_executable(protoc);
    tonic_prost_build::configure()
        .out_dir(directory)
        .compile_with_config(config, std::slice::from_ref(&proto), &[include])?;
    // Carry the schema's license header into the declared generated output.
    let source = fs::read_to_string(proto)?;
    let header = source
        .lines()
        .take_while(|line| line.starts_with("//") || line.trim().is_empty())
        .collect::<Vec<_>>()
        .join("\n");
    let generated = fs::read_to_string(&output)?;
    fs::write(output, format!("{header}\n{generated}"))?;
    Ok(())
}
