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

use super::{error_number, ffi, legacy_path};
use std::{io, path::Path};

pub fn inspect_path(bytes: &[u8], follow: bool) -> ffi::PathMetadata {
    let result = legacy_path(bytes).and_then(|path| {
        if follow {
            crate::metadata(&path)
        } else {
            crate::symlink_metadata(&path)
        }
    });
    match result {
        Ok(info) => ffi::PathMetadata {
            directory: info.is_dir(),
            symlink: info.is_symlink(),
            length: info.len(),
            error: 0,
        },
        Err(error) => ffi::PathMetadata {
            directory: false,
            symlink: false,
            length: 0,
            error: error_number(error),
        },
    }
}

fn status(result: io::Result<()>) -> i32 {
    result.err().map(error_number).unwrap_or(0)
}

pub fn create_directory_path(bytes: &[u8], mode: u32, recursive: bool) -> i32 {
    status(legacy_path(bytes).and_then(|path| {
        if recursive {
            crate::create_dir_all(&path, mode)
        } else {
            crate::create_dir(&path, mode)
        }
    }))
}

pub fn remove_file_path(bytes: &[u8]) -> i32 {
    status(legacy_path(bytes).and_then(|path| crate::remove_file(&path)))
}

pub fn remove_directory_path(bytes: &[u8]) -> i32 {
    status(legacy_path(bytes).and_then(|path| crate::remove_dir(&path)))
}

pub fn symlink_file_path(target: &[u8], link: &[u8]) -> i32 {
    status((|| {
        crate::symlink_file(&legacy_path(target)?, &legacy_path(link)?)
    })())
}

pub fn resolve_path(bytes: &[u8], lexical: bool) -> ffi::ResolvedPath {
    let result = legacy_path(bytes)
        .and_then(|path| {
            if lexical {
                crate::lexical_absolute(&path)
            } else {
                crate::canonical_existing(&path)
            }
        })
        .and_then(|path| legacy_bytes(&path));
    match result {
        Ok(path) => ffi::ResolvedPath { path, error: 0 },
        Err(error) => ffi::ResolvedPath {
            path: Vec::new(),
            error: error_number(error),
        },
    }
}

pub fn read_usable_space(bytes: &[u8]) -> ffi::UsableSpaceResult {
    match legacy_path(bytes).and_then(|path| crate::usable_space(&path)) {
        Ok(value) => ffi::UsableSpaceResult {
            total: value.total,
            available: value.available,
            error: 0,
        },
        Err(error) => ffi::UsableSpaceResult {
            total: 0,
            available: 0,
            error: error_number(error),
        },
    }
}

#[cfg(unix)]
pub(super) fn legacy_bytes(path: &Path) -> io::Result<Vec<u8>> {
    use std::os::unix::ffi::OsStrExt;
    Ok(path.as_os_str().as_bytes().to_vec())
}

#[cfg(windows)]
pub(super) fn legacy_bytes(path: &Path) -> io::Result<Vec<u8>> {
    let path = path.to_str().ok_or(io::ErrorKind::InvalidData)?;
    Ok(path.as_bytes().to_vec())
}
