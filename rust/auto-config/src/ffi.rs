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

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::path::Path;

use crate::{self as store, Error};

#[repr(C)]
pub struct SeekdbAutoConfigError {
    pub line: u32,
    pub after_replace: u8,
    pub message: [c_char; 512],
}

pub type SeekdbAutoConfigEntryCallback = unsafe extern "C" fn(
    context: *mut c_void,
    name: *const c_char,
    value: *const c_char,
    line: u32,
) -> c_int;

fn write_error(output: *mut SeekdbAutoConfigError, error: &Error) {
    if output.is_null() {
        return;
    }
    let output = unsafe { &mut *output };
    output.line = error.line.min(u32::MAX as usize) as u32;
    output.after_replace = u8::from(error.after_replace);
    output.message.fill(0);
    let message = error.to_string();
    let capacity = output.message.len() - 1;
    for (slot, byte) in output
        .message
        .iter_mut()
        .take(capacity)
        .zip(message.bytes())
    {
        *slot = byte as c_char;
    }
}

fn argument(value: *const c_char, label: &str) -> Result<String, Error> {
    if value.is_null() {
        return Err(Error {
            line: 0,
            name: None,
            after_replace: false,
            message: format!("{label} is null"),
        });
    }
    unsafe { CStr::from_ptr(value) }
        .to_str()
        .map(str::to_owned)
        .map_err(|_| Error {
            line: 0,
            name: None,
            after_replace: false,
            message: format!("{label} is not UTF-8"),
        })
}

#[no_mangle]
pub extern "C" fn seekdb_auto_config_supported(
    path: *const c_char,
    error: *mut SeekdbAutoConfigError,
) -> c_int {
    let result =
        argument(path, "path").and_then(|path| store::check_storage_directory(Path::new(&path)));
    match result {
        Ok(()) => 0,
        Err(problem) => {
            write_error(error, &problem);
            1
        }
    }
}

#[no_mangle]
pub extern "C" fn seekdb_auto_config_load(
    path: *const c_char,
    callback: SeekdbAutoConfigEntryCallback,
    context: *mut c_void,
    error: *mut SeekdbAutoConfigError,
) -> c_int {
    let result = (|| {
        let path = argument(path, "path")?;
        for entry in store::load(Path::new(&path))? {
            let entry_name = entry.name.clone();
            let name = CString::new(entry.name).map_err(|_| Error {
                line: entry.line,
                name: None,
                after_replace: false,
                message: "parameter name contains NUL".to_owned(),
            })?;
            let value = CString::new(entry.value).map_err(|_| Error {
                line: entry.line,
                name: None,
                after_replace: false,
                message: "parameter value contains NUL".to_owned(),
            })?;
            let status =
                unsafe { callback(context, name.as_ptr(), value.as_ptr(), entry.line as u32) };
            if status != 0 {
                return Err(Error {
                    line: entry.line,
                    name: Some(entry_name),
                    after_replace: false,
                    message: format!("entry callback failed with status {status}"),
                });
            }
        }
        Ok(())
    })();
    match result {
        Ok(()) => 0,
        Err(problem) => {
            write_error(error, &problem);
            1
        }
    }
}

#[no_mangle]
pub extern "C" fn seekdb_auto_config_update(
    path: *const c_char,
    name: *const c_char,
    value: *const c_char,
    reset: u8,
    error: *mut SeekdbAutoConfigError,
) -> c_int {
    let result = (|| {
        let path = argument(path, "path")?;
        let name = argument(name, "name")?;
        let value = if reset == 0 {
            Some(argument(value, "value")?)
        } else {
            None
        };
        store::update(Path::new(&path), &name, value.as_deref())
    })();
    match result {
        Ok(()) => 0,
        Err(problem) => {
            write_error(error, &problem);
            1
        }
    }
}
