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

use crate::GlobError;
use std::path::Path;

#[cxx::bridge(namespace = "oceanbase::path_utils")]
pub mod ffi {
    enum Code {
        Ok,
        InvalidPattern,
        Io,
    }

    struct PathBytes {
        bytes: Vec<u8>,
    }

    struct GlobResult {
        code: Code,
        paths: Vec<PathBytes>,
    }

    extern "Rust" {
        fn expand_glob(pattern: &[u8]) -> GlobResult;
    }
}

fn expand_glob(pattern: &[u8]) -> ffi::GlobResult {
    let result = std::str::from_utf8(pattern)
        .map_err(|_| GlobError::InvalidPattern)
        .and_then(crate::expand);
    match result {
        Ok(paths) => ffi::GlobResult {
            code: ffi::Code::Ok,
            paths: paths
                .iter()
                .map(|path| ffi::PathBytes {
                    bytes: path_bytes(path),
                })
                .collect(),
        },
        Err(GlobError::InvalidPattern) => ffi::GlobResult {
            code: ffi::Code::InvalidPattern,
            paths: Vec::new(),
        },
        Err(GlobError::Io) => ffi::GlobResult {
            code: ffi::Code::Io,
            paths: Vec::new(),
        },
    }
}

#[cfg(unix)]
fn path_bytes(path: &Path) -> Vec<u8> {
    use std::os::unix::ffi::OsStrExt;
    path.as_os_str().as_bytes().to_vec()
}

#[cfg(windows)]
fn path_bytes(path: &Path) -> Vec<u8> {
    path.to_string_lossy().into_owned().into_bytes()
}
