#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
"""Generate configuration storage, CXX declarations and C++ checker dispatch."""

import argparse
from pathlib import Path
import re
import struct

import yaml

TYPES = {
    "INT": "i64", "INT_WITH_CHECKER": "i64", "CAP": "i64", "CAP_WITH_CHECKER": "i64",
    "TIME": "i64", "TIME_WITH_CHECKER": "i64", "BOOL": "bool", "DBL": "f64",
    "STR": "String", "STR_WITH_CHECKER": "String", "LOG_LEVEL": "String",
    "WORK_AREA_POLICY": "String", "MOMENT": "String", "MODE": "String",
}
PARSERS = {"INT": "parse_int", "CAP": "parse_capacity", "TIME": "parse_time",
           "BOOL": "parse_bool", "DBL": "parse_double"}
FIELDS = ("name", "type", "default", "range", "section", "edit_level", "description", "checker")


def quoted(value):
    escapes = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\r": "\\r", "\t": "\\t", "\0": "\\0"}
    return '"' + "".join(escapes.get(ch, ch if ch.isprintable()
                                  else "\\u{%x}" % ord(ch)) for ch in value) + '"'


def parameters(path, log_level):
    result = yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
    names = set()
    required = set(FIELDS) - {"range", "checker"}
    for p in result:
        if not required <= p.keys() or p.keys() - set(FIELDS):
            raise ValueError("invalid parameter fields: " + repr(p))
        name = p["name"]
        if not re.fullmatch(r"[a-z_][a-z_0-9]*", name) or name in names:
            raise ValueError("invalid or duplicate parameter: " + name)
        names.add(name)
        TYPES[p["type"]]
        p.setdefault("range", "")
        p.setdefault("checker", "")
        if p["default"] == "@DEFAULT_LOG_LEVEL@":
            p["default"] = {"OB_LOG_LEVEL_ERROR": "EDIAG", "OB_LOG_LEVEL_DBA_WARN": "WARN"}[log_level]
    return result


def numeric_default(p):
    kind, value = p["type"].replace("_WITH_CHECKER", ""), p["default"].lower()
    if kind == "INT":
        result = int(value)
    else:
        units = ([(unit, 1 << shift) for unit, shift in
                  (("pb", 50), ("tb", 40), ("gb", 30), ("mb", 20), ("kb", 10),
                   ("p", 50), ("t", 40), ("g", 30), ("m", 20), ("k", 10), ("b", 0))]
                 if kind == "CAP" else
                 [("us", 1), ("ms", 1000), ("s", 1000000), ("m", 60000000),
                  ("h", 3600000000), ("d", 86400000000), ("", 1000000)])
        for suffix, multiplier in units:
            if value.endswith(suffix):
                result = int(value[:-len(suffix)] if suffix else value) * multiplier
                break
        else:
            raise ValueError("unsupported default: " + value)
    if not -(1 << 63) <= result < (1 << 63):
        raise ValueError("default overflows i64: " + value)
    return result


def catalog(parameters):
    return "".join("    ParameterMeta { " + ", ".join(
        ("kind" if field == "type" else field) + ": " + quoted(p[field])
        for field in FIELDS) + " },\n" for p in parameters)


def generate_bridge(parameters, internal_state):
    code = [PRELUDE, FUNCTIONS, CALLBACKS]
    for p in parameters:
        name, kind = p["name"], p["type"]
        code.append(f"        fn {name}() -> {TYPES[kind]};\n")
        if kind == "MOMENT":
            code.append(f"        fn {name}_parts() -> MomentTime;\n")
    code.append("    }\n}\n")
    for p in parameters:
        name, kind = p["name"], p["type"]
        storage, ty = name.upper(), TYPES[kind]
        if ty == "String":
            code.append(f"static {storage}: OnceLock<RwLock<String>> = OnceLock::new();\n"
                        f"fn {name}_cell() -> &'static RwLock<String> {{ {storage}.get_or_init(|| RwLock::new({quoted(p['default'])}.to_owned())) }}\n"
                        f'pub fn {name}() -> String {{ {name}_cell().read().expect("config string lock poisoned").clone() }}\n')
        else:
            atomic = {"i64": "AtomicI64", "bool": "AtomicBool", "f64": "AtomicU64"}[ty]
            if ty == "i64":
                default = numeric_default(p)
            elif ty == "bool":
                default = p["default"].lower()
                if default not in ("true", "false"):
                    raise ValueError("invalid boolean default: " + name)
            else:
                default = struct.unpack("<Q", struct.pack("<d", float(p["default"])))[0]
            value = f"{storage}.load(Ordering::Acquire)"
            if ty == "f64":
                value = f"f64::from_bits({value})"
            code.append(f"static {storage}: {atomic} = {atomic}::new({default});\n"
                        f"pub fn {name}() -> {ty} {{ {value} }}\n")
        if kind == "MOMENT":
            code.append(f"pub fn {name}_parts() -> bridge::MomentTime {{ parse_moment_parts(&{name}()) }}\n")
    code += ["pub static CATALOG: &[ParameterMeta] = &[\n", catalog(parameters),
             "];\npub fn find(name: &str) -> Option<&'static ParameterMeta> {\n"
             "CATALOG.iter().find(|parameter| parameter.name.eq_ignore_ascii_case(name))\n}\n",
             "pub static INTERNAL_STATE: &[ParameterMeta] = &[\n", catalog(internal_state), "];\n",
             "pub(crate) fn apply(name: &str, value: &str) -> Result<(), crate::Error> {\n"
             "    validate_storable(name, value)?;\n    match name.to_ascii_lowercase().as_str() {\n"]
    for p in parameters:
        name, kind = p["name"], p["type"]
        storage, ty = name.upper(), TYPES[kind]
        if ty == "String":
            statement = f'*{name}_cell().write().expect("config string lock poisoned") = value.to_owned();'
        else:
            parser = PARSERS[kind.replace("_WITH_CHECKER", "")]
            if ty == "bool":
                statement = f"{storage}.store({parser}(name, value)?, Ordering::Release);"
            else:
                value = "parsed.to_bits()" if ty == "f64" else "parsed"
                statement = f"let parsed = {parser}(name, value)?; {storage}.store({value}, Ordering::Release);"
        code.append(f"        {quoted(name)} => {{ {statement} Ok(()) }},\n")
    code += ["        _ => Err(invalid_value(name)),\n    }\n}\n", QUERIES]
    return "".join(code)


def generate_checkers(parameters):
    code = [CHECKERS]
    for p in parameters:
        checker = p["checker"]
        if checker:
            if "::" not in checker:
                checker = "common::" + checker
            code.append(f"  if (0 == key.case_compare({quoted(p['name'])})) {{ {checker} checker; return checker.check(value); }}\n")
    return "".join(code) + "  return true;\n}\n}} // namespace oceanbase::config\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("parameters", "internal-state", "rust-output", "checkers-output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--default-log-level", choices=("OB_LOG_LEVEL_ERROR", "OB_LOG_LEVEL_DBA_WARN"),
                        default="OB_LOG_LEVEL_ERROR")
    args = parser.parse_args()
    declared = parameters(args.parameters, args.default_log_level)
    state = parameters(args.internal_state, args.default_log_level)
    args.rust_output.write_text(generate_bridge(declared, state), encoding="utf-8")
    args.checkers_output.write_text(generate_checkers(declared), encoding="utf-8")


PRELUDE = '''use std::sync::{OnceLock, RwLock};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering};
pub struct ParameterMeta {
pub name: &'static str,
pub kind: &'static str,
pub default: &'static str,
pub range: &'static str,
pub section: &'static str,
pub edit_level: &'static str,
pub description: &'static str,
pub checker: &'static str,
}
#[cxx::bridge(namespace = "oceanbase::config")]
pub(crate) mod bridge {
struct MomentTime {
disabled: bool,
hour: u8,
minute: u8,
}
struct ParameterRow {
name: String,
data_type: String,
value: String,
info: String,
section: String,
scope: String,
source: String,
edit_level: String,
default_value: String,
}
struct Status {
ok: bool,
commit_uncertain: bool,
message: String,
}
unsafe extern "C++" {
include!("rust/config/include/check_callback.h");
include!("rust/local.h");
#[namespace = "oceanbase::local"]
type Store = local::Store;
type CheckCallback;
fn check(self: &CheckCallback) -> i32;
}
extern "Rust" {
'''

FUNCTIONS = '''        fn parameter_count() -> usize;
        fn parameter_row(index: usize) -> ParameterRow;
        fn parameter_exists(name: &str) -> bool;
        fn parameter_readonly(name: &str) -> bool;
        fn parameter_default(name: &str) -> String;
        fn parameter_valid(name: &str, value: &str) -> bool;
        fn generation() -> i64;
        fn server_create_time() -> i64;
        fn server_role_info() -> String;
'''

CALLBACKS = '''        #[cxx_name = "load_active"]
fn load_active_status(store: &Store, startup: bool) -> Status;
#[cxx_name = "save_bootstrap"]
fn save_bootstrap_status(store: &Store) -> Status;
#[cxx_name = "bootstrap_set"]
fn bootstrap_set_status(name: &CxxString, value: &CxxString) -> Status;
#[cxx_name = "update_internal_state"]
fn update_internal_state_status(store: &Store, name: &CxxString, value: &CxxString) -> Status;
#[cxx_name = "update_checked"]
fn update_checked_status(store: &Store, name: &CxxString, value: &CxxString, reset: bool, callback: &CheckCallback) -> Status;
'''

QUERIES = '''pub fn parameter_count() -> usize { CATALOG.len() }
pub fn parameter_exists(name: &str) -> bool { find(name).is_some() }
pub fn parameter_readonly(name: &str) -> bool { find(name).is_some_and(|meta| meta.edit_level == "READONLY") }
pub fn parameter_default(name: &str) -> String { find(name).map(|meta| meta.default.to_owned()).unwrap_or_default() }
pub fn parameter_valid(name: &str, value: &str) -> bool { validate(name, value).is_ok() }
pub fn parameter_row(index: usize) -> bridge::ParameterRow {
let meta = &CATALOG[index];
let data_type = match meta.kind {
"INT" | "INT_WITH_CHECKER" => "INT",
"CAP" | "CAP_WITH_CHECKER" => "CAPACITY",
"TIME" | "TIME_WITH_CHECKER" => "TIME",
"DBL" => "DOUBLE",
"BOOL" => "BOOL",
"MOMENT" => "MOMENT",
"MODE" => "MODE",
_ => "STRING",
};
bridge::ParameterRow {
name: meta.name.to_owned(),
data_type: data_type.to_owned(),
value: effective_value(meta.name).expect("catalogued parameter"),
info: meta.description.to_owned(),
section: meta.section.to_owned(),
scope: "CLUSTER".to_owned(),
source: "DEFAULT".to_owned(),
edit_level: meta.edit_level.to_owned(),
default_value: meta.default.to_owned(),
}
}
'''

CHECKERS = '''// Generated from the Rust instance parameter catalog.
#pragma once
#include "share/config/ob_config_helper.h"
#include "lib/string/ob_string.h"
namespace oceanbase { namespace config {
inline bool check_parameter(const char *name, const char *value) {
if (nullptr == name || nullptr == value) return false;
const common::ObString key = common::ObString::make_string(name);
'''

if __name__ == "__main__":
    main()
