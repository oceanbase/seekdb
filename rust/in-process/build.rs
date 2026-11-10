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

use std::env;
use std::path::PathBuf;

fn main() {
    let crate_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    cbindgen::generate(&crate_dir)
        .expect("cbindgen failed")
        .write_to_file(crate_dir.join("include").join("seekdb.h"));
    let unix = env::var_os("CARGO_CFG_UNIX").is_some();
    let emscripten = env::var("CARGO_CFG_TARGET_OS").is_ok_and(|os| os == "emscripten");
    if unix && !emscripten {
        cxx_build::bridge("src/bridge.rs")
            .include("include")
            .std("c++17")
            .compile("in_process_cxx");
    }
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=cbindgen.toml");
    println!("cargo:rerun-if-changed=include/in_process_server.h");
}
