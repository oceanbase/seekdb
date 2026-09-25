// Copyright (c) 2025 OceanBase.
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

use std::{collections::HashSet, env, fs, path::PathBuf};

use serde::Deserialize;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Parameter {
    name: String,
    #[serde(rename = "type")]
    kind: String,
    #[serde(rename = "default")]
    default_text: String,
    #[serde(default)]
    range: String,
    section: String,
    edit_level: String,
    description: String,
    #[serde(default)]
    checker: String,
    #[serde(default)]
    options: String,
}

fn rust_type(kind: &str) -> Option<&'static str> {
    match kind {
        "INT" | "INT_WITH_CHECKER" | "CAP" | "CAP_WITH_CHECKER" | "TIME" | "TIME_WITH_CHECKER" => {
            Some("i64")
        }
        "BOOL" => Some("bool"),
        "DBL" => Some("f64"),
        "STR" | "STR_WITH_CHECKER" | "LOG_LEVEL" | "WORK_AREA_POLICY" | "MOMENT"
        | "MODE_WITH_PARSER" => Some("String"),
        _ => panic!("unknown parameter type: {kind}"),
    }
}

fn scaled_default(text: &str, units: &[(&str, i64)]) -> i64 {
    let lower = text.to_ascii_lowercase();
    for (suffix, multiplier) in units {
        if let Some(number) = lower.strip_suffix(suffix) {
            return number
                .parse::<i64>()
                .expect("invalid scaled parameter default")
                .checked_mul(*multiplier)
                .expect("scaled parameter default overflows i64");
        }
    }
    panic!("unsupported scaled parameter default: {text}")
}

fn numeric_default(parameter: &Parameter) -> i64 {
    let text = parameter.default_text.as_str();
    match parameter.kind.as_str() {
        "INT" | "INT_WITH_CHECKER" => text.parse().expect("invalid integer default"),
        "CAP" | "CAP_WITH_CHECKER" => scaled_default(
            text,
            &[
                ("pb", 1_i64 << 50),
                ("tb", 1_i64 << 40),
                ("gb", 1_i64 << 30),
                ("mb", 1_i64 << 20),
                ("kb", 1_i64 << 10),
                ("p", 1_i64 << 50),
                ("t", 1_i64 << 40),
                ("g", 1_i64 << 30),
                ("m", 1_i64 << 20),
                ("k", 1_i64 << 10),
                ("b", 1),
            ],
        ),
        "TIME" | "TIME_WITH_CHECKER" => scaled_default(
            text,
            &[
                ("us", 1),
                ("ms", 1_000),
                ("s", 1_000_000),
                ("m", 60_000_000),
                ("h", 3_600_000_000),
                ("d", 86_400_000_000),
                ("", 1_000_000),
            ],
        ),
        _ => panic!("parameter is not numeric: {}", parameter.name),
    }
}

fn log_default() -> &'static str {
    match env::var("SEEKDB_DEFAULT_LOG_LEVEL")
        .unwrap_or_else(|_| "OB_LOG_LEVEL_ERROR".to_owned())
        .as_str()
    {
        "OB_LOG_LEVEL_ERROR" => "EDIAG",
        "OB_LOG_LEVEL_DBA_WARN" => "WARN",
        unknown => panic!("unsupported DEFAULT_LOG_LEVEL: {unknown}"),
    }
}

fn parameters(source: &str) -> Vec<Parameter> {
    let mut parameters: Vec<Parameter> =
        serde_yaml::from_str(source).expect("invalid YAML parameter declarations");
    for parameter in &mut parameters {
        let name = parameter.name.as_str();
        assert!(
            name.starts_with(|ch: char| ch.is_ascii_lowercase() || ch == '_')
                && name
                    .chars()
                    .all(|ch| ch.is_ascii_lowercase() || ch.is_ascii_digit() || ch == '_'),
            "invalid parameter name: {name}"
        );
        if parameter.default_text == "@DEFAULT_LOG_LEVEL@" {
            parameter.default_text = log_default().to_owned();
        }
    }
    let mut names = HashSet::new();
    for parameter in &parameters {
        rust_type(&parameter.kind);
        if parameter.kind == "MODE_WITH_PARSER" {
            let option_count = parameter.options.split(',').count();
            assert!(
                !parameter.options.is_empty() && option_count <= 32,
                "mode parameter needs one to 32 declared operations: {}",
                parameter.name
            );
        }
        assert!(
            names.insert(parameter.name.clone()),
            "duplicate parameter: {}",
            parameter.name
        );
    }
    parameters
}

fn generate_bridge(parameters: &[Parameter], internal_state: &[Parameter]) -> String {
    let mut code = String::from(
        "use std::sync::{OnceLock, RwLock};\n\
         use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering};\n\
         pub struct ParameterMeta {\n\
             pub name: &'static str,\n\
             pub kind: &'static str,\n\
             pub default: &'static str,\n\
             pub range: &'static str,\n\
             pub section: &'static str,\n\
             pub edit_level: &'static str,\n\
             pub description: &'static str,\n\
             pub checker: &'static str,\n\
             pub options: &'static str,\n\
         }\n\
         #[cxx::bridge(namespace = \"oceanbase::config\")]\n\
         mod bridge {\n\
             struct MomentTime {\n\
                 disabled: bool,\n\
                 hour: u8,\n\
                 minute: u8,\n\
             }\n\
             struct ParameterRow {\n\
                 name: String,\n\
                 data_type: String,\n\
                 value: String,\n\
                 info: String,\n\
                 section: String,\n\
                 scope: String,\n\
                 source: String,\n\
                 edit_level: String,\n\
                 default_value: String,\n\
             }\n\
             extern \"Rust\" {\n",
    );
    code.push_str("        fn parameter_count() -> usize;\n        fn parameter_row(index: usize) -> ParameterRow;\n        fn parameter_exists(name: &str) -> bool;\n        fn parameter_readonly(name: &str) -> bool;\n        fn parameter_default(name: &str) -> String;\n        fn parameter_valid(name: &str, value: &str) -> bool;\n        fn generation() -> i64;\n        fn server_create_time() -> i64;\n        fn server_role_info() -> String;\n");
    for parameter in parameters {
        if let Some(ty) = rust_type(&parameter.kind) {
            code.push_str(&format!("        fn {}() -> {ty};\n", parameter.name));
        }
        match parameter.kind.as_str() {
            "MOMENT" => code.push_str(&format!(
                "        fn {}_parts() -> MomentTime;\n",
                parameter.name
            )),
            "MODE_WITH_PARSER" => {
                code.push_str(&format!("        fn {}_bits() -> u64;\n", parameter.name))
            }
            _ => {}
        }
    }
    code.push_str("    }\n}\n");
    for parameter in parameters {
        let storage = parameter.name.to_ascii_uppercase();
        match parameter.kind.as_str() {
            "INT" | "INT_WITH_CHECKER" | "CAP" | "CAP_WITH_CHECKER" | "TIME"
            | "TIME_WITH_CHECKER" => {
                code.push_str(&format!(
                    "static {storage}: AtomicI64 = AtomicI64::new({});\n\
                     pub fn {}() -> i64 {{ {storage}.load(Ordering::Acquire) }}\n",
                    numeric_default(parameter),
                    parameter.name,
                ));
            }
            "BOOL" => {
                let default = match parameter.default_text.to_ascii_lowercase().as_str() {
                    "true" => true,
                    "false" => false,
                    _ => panic!("invalid boolean default: {}", parameter.name),
                };
                code.push_str(&format!(
                    "static {storage}: AtomicBool = AtomicBool::new({default});\n\
                     pub fn {}() -> bool {{ {storage}.load(Ordering::Acquire) }}\n",
                    parameter.name,
                ));
            }
            "DBL" => {
                let bits = parameter
                    .default_text
                    .parse::<f64>()
                    .expect("invalid floating-point default")
                    .to_bits();
                code.push_str(&format!(
                    "static {storage}: AtomicU64 = AtomicU64::new({bits});\n\
                     pub fn {}() -> f64 {{ f64::from_bits({storage}.load(Ordering::Acquire)) }}\n",
                    parameter.name,
                ));
            }
            "STR" | "STR_WITH_CHECKER" | "LOG_LEVEL" | "WORK_AREA_POLICY" | "MOMENT"
            | "MODE_WITH_PARSER" => {
                code.push_str(&format!(
                    "static {storage}: OnceLock<RwLock<String>> = OnceLock::new();\n\
                     fn {}_cell() -> &'static RwLock<String> {{ {storage}.get_or_init(|| RwLock::new({:?}.to_owned())) }}\n\
                     pub fn {}() -> String {{ {}_cell().read().expect(\"config string lock poisoned\").clone() }}\n",
                    parameter.name, parameter.default_text, parameter.name,
                    parameter.name,
                ));
            }
            _ => unreachable!(),
        }
        match parameter.kind.as_str() {
            "MOMENT" => code.push_str(&format!(
                "pub fn {}_parts() -> bridge::MomentTime {{ parse_moment_parts(&{}()) }}\n",
                parameter.name, parameter.name
            )),
            "MODE_WITH_PARSER" => code.push_str(&format!(
                "pub fn {}_bits() -> u64 {{ parse_mode_bits(&{}(), {:?}) }}\n",
                parameter.name, parameter.name, parameter.options
            )),
            _ => {}
        }
    }
    code.push_str("pub static CATALOG: &[ParameterMeta] = &[\n");
    for parameter in parameters {
        code.push_str(&format!(
            "    ParameterMeta {{ name: {:?}, kind: {:?}, default: {:?}, range: {:?}, section: {:?}, edit_level: {:?}, description: {:?}, checker: {:?}, options: {:?} }},\n",
            parameter.name,
            parameter.kind,
            parameter.default_text,
            parameter.range,
            parameter.section,
            parameter.edit_level,
            parameter.description,
            parameter.checker,
            parameter.options,
        ));
    }
    code.push_str(
        "];\n\
         pub fn find(name: &str) -> Option<&'static ParameterMeta> {\n\
             CATALOG.iter().find(|parameter| parameter.name.eq_ignore_ascii_case(name))\n\
         }\n",
    );
    code.push_str("pub static INTERNAL_STATE: &[ParameterMeta] = &[\n");
    for parameter in internal_state {
        code.push_str(&format!(
            "    ParameterMeta {{ name: {:?}, kind: {:?}, default: {:?}, range: {:?}, section: {:?}, edit_level: {:?}, description: {:?}, checker: {:?}, options: {:?} }},\n",
            parameter.name, parameter.kind, parameter.default_text, parameter.range,
            parameter.section, parameter.edit_level, parameter.description,
            parameter.checker, parameter.options,
        ));
    }
    code.push_str("];\n");
    code.push_str("pub(crate) fn apply(name: &str, value: &str) -> Result<(), crate::Error> {\n    validate(name, value)?;\n    match name.to_ascii_lowercase().as_str() {\n");
    for parameter in parameters {
        let parser = match parameter.kind.as_str() {
            "INT" | "INT_WITH_CHECKER" => "parse_int",
            "CAP" | "CAP_WITH_CHECKER" => "parse_capacity",
            "TIME" | "TIME_WITH_CHECKER" => "parse_time",
            "BOOL" => "parse_bool",
            "DBL" => "parse_double",
            "STR" | "STR_WITH_CHECKER" | "LOG_LEVEL" | "WORK_AREA_POLICY" | "MOMENT"
            | "MODE_WITH_PARSER" => "",
            _ => unreachable!(),
        };
        let statement = match parameter.kind.as_str() {
            "STR" | "STR_WITH_CHECKER" | "LOG_LEVEL" | "WORK_AREA_POLICY"
            | "MOMENT" | "MODE_WITH_PARSER" => format!(
                "*{}_cell().write().expect(\"config string lock poisoned\") = value.to_owned();",
                parameter.name
            ),
            "DBL" => format!(
                "let parsed = {parser}(name, value)?; validate_f64_range(name, parsed, {:?})?; {}.store(parsed.to_bits(), Ordering::Release);",
                parameter.range, parameter.name.to_ascii_uppercase()
            ),
            "BOOL" => format!(
                "{}.store({parser}(name, value)?, Ordering::Release);",
                parameter.name.to_ascii_uppercase(),
            ),
            _ => format!(
                "let parsed = {parser}(name, value)?; validate_i64_range(name, parsed, {:?}, {parser})?; {}.store(parsed, Ordering::Release);",
                parameter.range, parameter.name.to_ascii_uppercase()
            ),
        };
        code.push_str(&format!(
            "        {:?} => {{ {statement} Ok(()) }},\n",
            parameter.name
        ));
    }
    code.push_str("        _ => Err(invalid_value(name)),\n    }\n}\n");
    code.push_str(
        "pub fn parameter_count() -> usize { CATALOG.len() }\n\
         pub fn parameter_exists(name: &str) -> bool { find(name).is_some() }\n\
         pub fn parameter_readonly(name: &str) -> bool { find(name).is_some_and(|meta| meta.edit_level == \"READONLY\") }\n\
         pub fn parameter_default(name: &str) -> String { find(name).map(|meta| meta.default.to_owned()).unwrap_or_default() }\n\
         pub fn parameter_valid(name: &str, value: &str) -> bool { validate(name, value).is_ok() }\n\
         pub fn parameter_row(index: usize) -> bridge::ParameterRow {\n\
             let meta = &CATALOG[index];\n\
             let data_type = match meta.kind {\n\
                 \"INT\" | \"INT_WITH_CHECKER\" => \"INT\",\n\
                 \"CAP\" | \"CAP_WITH_CHECKER\" => \"CAPACITY\",\n\
                 \"TIME\" | \"TIME_WITH_CHECKER\" => \"TIME\",\n\
                 \"DBL\" => \"DOUBLE\",\n\
                 \"BOOL\" => \"BOOL\",\n\
                 \"MOMENT\" => \"MOMENT\",\n\
                 \"MODE_WITH_PARSER\" => \"MODE\",\n\
                 _ => \"STRING\",\n\
             };\n\
             bridge::ParameterRow {\n\
                 name: meta.name.to_owned(),\n\
                 data_type: data_type.to_owned(),\n\
                 value: effective_value(meta.name).expect(\"catalogued parameter\"),\n\
                 info: meta.description.to_owned(),\n\
                 section: meta.section.to_owned(),\n\
                 scope: \"CLUSTER\".to_owned(),\n\
                 source: \"DEFAULT\".to_owned(),\n\
                 edit_level: meta.edit_level.to_owned(),\n\
                 default_value: meta.default.to_owned(),\n\
             }\n\
         }\n",
    );
    code
}

fn generate_cpp_checkers(parameters: &[Parameter]) -> String {
    let mut code = String::from(
        "// Generated from the Rust instance parameter catalog.\n\
         #pragma once\n\
         #include \"share/config/ob_config_helper.h\"\n\
         #include \"lib/string/ob_string.h\"\n\
         namespace oceanbase { namespace config {\n\
         inline bool check_parameter(const char *name, const char *value) {\n\
           if (nullptr == name || nullptr == value) return false;\n\
           const common::ObString key = common::ObString::make_string(name);\n",
    );
    for parameter in parameters {
        if parameter.checker.is_empty() || parameter.checker.starts_with("parser:") {
            continue;
        }
        let checker = if parameter.checker.contains("::") {
            parameter.checker.clone()
        } else {
            format!("common::{}", parameter.checker)
        };
        code.push_str(&format!(
            "  if (0 == key.case_compare({:?})) {{ {} checker; return checker.check(value); }}\n",
            parameter.name, checker
        ));
    }
    code.push_str("  return true;\n}\n}} // namespace oceanbase::config\n");
    code
}

fn main() {
    let crate_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let catalog = fs::read_to_string(crate_dir.join("parameters.yaml"))
        .expect("cannot read Rust parameter declarations");
    let state_catalog = fs::read_to_string(crate_dir.join("internal_state.yaml"))
        .expect("cannot read Rust internal-state declarations");
    let output_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let generated = output_dir.join("generated_config.rs");
    let declared = parameters(&catalog);
    fs::write(
        &generated,
        generate_bridge(&declared, &parameters(&state_catalog)),
    )
    .expect("cannot write generated CXX bridge");
    env::set_current_dir(&output_dir).expect("cannot enter generated source directory");
    cxx_build::bridge("generated_config.rs")
        .flag_if_supported("-std=c++17")
        .compile("seekdb_config_cxx");
    env::set_current_dir(&crate_dir).expect("cannot restore crate directory");
    if let Ok(header_dir) = env::var("SEEKDB_CXX_HEADER_DIR") {
        let header_dir = PathBuf::from(header_dir);
        fs::create_dir_all(&header_dir).expect("cannot create CXX header directory");
        let source = output_dir.join("cxxbridge/include/auto-config/generated_config.rs.h");
        let target = header_dir.join("config_bridge.h");
        let bytes = fs::read(source).expect("cannot read generated CXX header");
        if fs::read(&target).ok().as_deref() != Some(bytes.as_slice()) {
            fs::write(target, bytes).expect("cannot publish generated CXX header");
        }
        let target = header_dir.join("config_checkers.h");
        let bytes = generate_cpp_checkers(&declared).into_bytes();
        if fs::read(&target).ok().as_deref() != Some(bytes.as_slice()) {
            fs::write(target, bytes).expect("cannot publish generated checker header");
        }
    }

    let config = cbindgen::Config::from_file(crate_dir.join("cbindgen.toml"))
        .expect("auto-config cbindgen config failed");
    cbindgen::Builder::new()
        .with_src(crate_dir.join("src").join("ffi.rs"))
        .with_config(config)
        .generate()
        .expect("auto-config cbindgen failed")
        .write_to_file(crate_dir.join("include").join("auto_config.h"));

    println!("cargo:rerun-if-changed=src/ffi.rs");
    println!("cargo:rerun-if-changed=cbindgen.toml");
    println!("cargo:rerun-if-changed=parameters.yaml");
    println!("cargo:rerun-if-changed=internal_state.yaml");
    println!("cargo:rerun-if-env-changed=SEEKDB_CXX_HEADER_DIR");
    println!("cargo:rerun-if-env-changed=SEEKDB_DEFAULT_LOG_LEVEL");
}
