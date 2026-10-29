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

#[cxx::bridge(namespace = "oceanbase::uuid_generator")]
pub mod ffi {
    #[derive(Debug, PartialEq, Eq)]
    enum Code {
        Ok,
        Entropy,
        Clock,
        Invalid,
    }
    extern "Rust" {
        fn init() -> Code;
        fn generate(output: &mut [u8]) -> Code;
    }
}

pub(crate) fn init() -> ffi::Code {
    match crate::generator() {
        Ok(_) => ffi::Code::Ok,
        Err(code) => code,
    }
}

pub(crate) fn generate(output: &mut [u8]) -> ffi::Code {
    if output.len() != 16 {
        return ffi::Code::Invalid;
    }
    match crate::generator().and_then(crate::Generator::generate) {
        Ok(uuid) => {
            output.copy_from_slice(uuid.as_bytes());
            ffi::Code::Ok
        }
        Err(code) => code,
    }
}
