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

use std::{collections::HashSet, error::Error, fs, path::PathBuf};

fn raw_strings(
    messages: &mut [prost_types::DescriptorProto],
    prefix: &str,
    fields: &mut HashSet<String>,
) -> Result<(), Box<dyn Error>> {
    for message in messages {
        let name = format!(
            "{prefix}.{}",
            message.name.as_deref().ok_or("missing message name")?
        );
        for field in &mut message.field {
            let path = format!(
                "{name}.{}",
                field.name.as_deref().ok_or("missing field name")?
            );
            if fields.remove(&path) {
                if field.r#type != Some(prost_types::field_descriptor_proto::Type::String as i32) {
                    return Err(
                        format!("raw string override requires a string field: {path}").into(),
                    );
                }
                // SQL strings may contain non-UTF-8 bytes. Both representations
                // use the same length-delimited wire encoding and field number.
                field.r#type = Some(prost_types::field_descriptor_proto::Type::Bytes as i32);
            }
        }
        raw_strings(&mut message.nested_type, &name, fields)?;
    }
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut args = std::env::args_os().skip(1);
    let proto = PathBuf::from(args.next().ok_or("missing proto input")?);
    let output = PathBuf::from(args.next().ok_or("missing Rust output")?);
    let mut services = true;
    let mut fields = HashSet::new();
    while let Some(arg) = args.next() {
        if arg == "--messages-only" {
            services = false;
        } else if arg == "--raw-string" {
            fields.insert(
                args.next()
                    .ok_or("missing raw string field")?
                    .into_string()
                    .map_err(|_| "invalid field path")?,
            );
        } else {
            return Err("unexpected generator argument".into());
        }
    }
    let include = proto
        .parent()
        .ok_or("proto input has no parent")?
        .to_path_buf();
    let directory = output.parent().ok_or("Rust output has no parent")?;
    fs::create_dir_all(directory)?;
    let mut config = prost_build::Config::new();
    let mut descriptors = protox::compile(std::slice::from_ref(&proto), &[include])?;
    if services {
        if !fields.is_empty() {
            return Err("raw string storage is supported only for messages".into());
        }
        tonic_prost_build::configure()
            .out_dir(directory)
            .compile_fds_with_config(descriptors, config)?;
    } else {
        for file in &mut descriptors.file {
            let prefix = format!(".{}", file.package.as_deref().unwrap_or_default());
            raw_strings(&mut file.message_type, &prefix, &mut fields)?;
        }
        if !fields.is_empty() {
            return Err(format!("unknown raw string fields: {fields:?}").into());
        }
        config.out_dir(directory).compile_fds(descriptors)?;
    }
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
