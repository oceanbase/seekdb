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

//! Executed by cargo seekdb schema; no plugin lifecycle or server connection.
use seekdb_extension::schema::{scalar_wrapper, Package, Script, SqlAccess, SqlType};

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 1 { return Err("expected a CLI-owned staging directory".into()); }
    let sql = scalar_wrapper("@@NAME@@_length", &seekdb_@@NAME@@::chars::DEFINITION,
        &[("input_text", SqlType::Text)], SqlType::BigInt, SqlAccess::NoSql)?;
    Package {
        name: "@@NAME@@_ops", default_version: "1.0", native_module: Some("@@PLUGIN_ID@@"),
        scripts: &[Script { from: None, to: "1.0", sql: &sql }],
    }.write_to(std::path::Path::new(&args[0]))
}
