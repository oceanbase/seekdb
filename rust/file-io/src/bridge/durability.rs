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

use super::{error_number, ffi};
use crate::{DirectoryDurability, DirectorySyncError, DirectorySyncSummary};
use std::io;

pub fn application_durability() -> DirectoryDurability {
    if cfg!(windows) {
        DirectoryDurability::WindowsCompatible
    } else {
        DirectoryDurability::Required
    }
}
pub fn no_error() -> ffi::FsError {
    ffi::FsError {
        domain: ffi::ErrorDomain::None,
        native_code: 0,
        error: 0,
        message: String::new(),
    }
}
pub fn fs_error(error: &io::Error) -> ffi::FsError {
    let domain = match error.raw_os_error() {
        None => ffi::ErrorDomain::Library,
        Some(_) if cfg!(windows) => ffi::ErrorDomain::Windows,
        Some(_) => ffi::ErrorDomain::Posix,
    };
    let equivalent = error
        .raw_os_error()
        .map(io::Error::from_raw_os_error)
        .unwrap_or_else(|| error.kind().into());
    ffi::FsError {
        domain,
        native_code: error.raw_os_error().unwrap_or(0),
        error: error_number(equivalent),
        message: error.to_string(),
    }
}
pub fn sync_result(error: Option<&DirectorySyncError>) -> ffi::DirectorySyncResult {
    let mut result = ffi::DirectorySyncResult {
        stage: ffi::SyncStage::Complete,
        primary: no_error(),
        close: no_error(),
    };
    if let Some(error) = error {
        result.primary = fs_error(error.primary());
        result.stage = match error {
            DirectorySyncError::Open(_) => ffi::SyncStage::OpenFailed,
            DirectorySyncError::Close(_) => ffi::SyncStage::CloseFailed,
            DirectorySyncError::Flush { close_error, .. } => {
                if let Some(error) = close_error {
                    result.close = fs_error(error);
                }
                ffi::SyncStage::FlushFailed
            }
        };
    }
    result
}
pub fn path_bytes(path: &std::path::Path) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        path.as_os_str().as_bytes().to_vec()
    }
    #[cfg(windows)]
    {
        path.to_string_lossy().as_bytes().to_vec()
    }
}
pub fn directory_summary(summary: &DirectorySyncSummary) -> ffi::DirectorySyncSummary {
    let mut value = ffi::DirectorySyncSummary {
        count: summary.count(),
        first_path: Vec::new(),
        first_error: no_error(),
    };
    if let Some(first) = summary.first() {
        value.first_path = path_bytes(&first.path);
        value.first_error = fs_error(&first.source);
    }
    value
}
pub fn sync_directory_path(path: &[u8]) -> ffi::DirectorySyncResult {
    let result = super::legacy_path(path)
        .map_err(DirectorySyncError::Open)
        .and_then(|path| crate::sync_directory(&path));
    sync_result(result.as_ref().err())
}
