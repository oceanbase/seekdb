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

#[cxx::bridge(namespace = "oceanbase::in_process")]
mod ffi {
    unsafe extern "C++" {
        include!("in_process_server.h");

        fn run_server(directory: &str, parameters: &[String], report_running: fn()) -> i32;
    }
}

pub(crate) use ffi::run_server;
