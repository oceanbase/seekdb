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

use std::cell::RefCell;
use std::ffi::{c_char, c_int, c_uint, c_void, CStr, CString};
use std::fs;
use std::mem;
use std::os::unix::fs::symlink;
use std::ptr;

use crate::engine::{self, StartError};
use crate::parameters;
use crate::platform;

use SeekdbReturnCode::{SEEKDB_INTERNAL_ERROR, SEEKDB_INVALID_ARGUMENT, SEEKDB_SUCCESS};

const ALIAS_SOCKET: &str = "/r/sql.sock";

pub type SeekdbHandle = *mut c_void;

#[repr(C)]
#[allow(non_camel_case_types)]
pub enum SeekdbReturnCode {
    SEEKDB_SUCCESS = 0,
    SEEKDB_INTERNAL_ERROR = -1,
    SEEKDB_INVALID_ARGUMENT = -2,
    SEEKDB_NO_MORE_ROWS = -3,
}

#[repr(C)]
pub struct SeekdbConnectionOptions {
    pub transport: *const c_char,
    pub port: c_uint,
    pub host: *const c_char,
    pub unix_socket: *const c_char,
    pub named_pipe: *const c_char,
    pub user: *const c_char,
}

struct Handle {
    socket_path: CString,
    alias_directory: Option<String>,
}

impl Handle {
    fn new(engine_socket: &str, alias_patterns: &[String]) -> Option<Handle> {
        if engine_socket.len() < sun_path_capacity() {
            return Some(Handle {
                socket_path: CString::new(engine_socket).ok()?,
                alias_directory: None,
            });
        }
        let alias_directory = alias_patterns
            .iter()
            .filter(|pattern| pattern.len() + ALIAS_SOCKET.len() < sun_path_capacity())
            .find_map(|pattern| make_temporary_directory(pattern))?;
        let mut handle = Handle {
            socket_path: CString::default(),
            alias_directory: Some(alias_directory.clone()),
        };
        let run_directory = engine_socket
            .rsplit_once('/')
            .map_or(engine_socket, |(parent, _)| parent);
        symlink(run_directory, format!("{alias_directory}/r")).ok()?;
        handle.socket_path = CString::new(format!("{alias_directory}{ALIAS_SOCKET}")).ok()?;
        Some(handle)
    }
}

impl Drop for Handle {
    fn drop(&mut self) {
        if let Some(directory) = &self.alias_directory {
            let _ = fs::remove_file(format!("{directory}/r"));
            let _ = fs::remove_dir(directory);
        }
    }
}

fn sun_path_capacity() -> usize {
    mem::size_of::<libc::sockaddr_un>() - mem::offset_of!(libc::sockaddr_un, sun_path)
}

fn make_temporary_directory(pattern: &str) -> Option<String> {
    let mut template = CString::new(pattern).ok()?.into_bytes_with_nul();
    if unsafe { libc::mkdtemp(template.as_mut_ptr().cast()) }.is_null() {
        return None;
    }
    template.pop();
    String::from_utf8(template).ok()
}

thread_local! {
    static LAST_OPEN_ERROR: RefCell<CString> = RefCell::new(CString::default());
}

fn set_last_open_error(message: &str) {
    LAST_OPEN_ERROR.with(|error| *error.borrow_mut() = CString::new(message).unwrap_or_default());
}

fn open_failed(code: SeekdbReturnCode, message: &str) -> c_int {
    set_last_open_error(message);
    code as c_int
}

unsafe fn read_parameters(
    parameters: *const *const c_char,
) -> Result<Vec<(String, String)>, &'static str> {
    let mut pairs = Vec::new();
    if parameters.is_null() {
        return Ok(pairs);
    }
    let mut index = 0;
    while !(*parameters.add(index)).is_null() {
        let key = CStr::from_ptr(*parameters.add(index));
        let value = *parameters.add(index + 1);
        if key.to_bytes().is_empty() || value.is_null() {
            return Err("parameters must be key/value pairs with non-empty keys");
        }
        let (Ok(key), Ok(value)) = (key.to_str(), CStr::from_ptr(value).to_str()) else {
            return Err("parameters must be valid UTF-8");
        };
        parameters::check(key, value)?;
        pairs.push((key.to_owned(), value.to_owned()));
        index += 2;
    }
    Ok(pairs)
}

#[no_mangle]
pub unsafe extern "C" fn seekdb_open(
    db_dir: *const c_char,
    parameters: *mut *const c_char,
    out_handle: *mut SeekdbHandle,
) -> c_int {
    set_last_open_error("");
    if out_handle.is_null() {
        return open_failed(SEEKDB_INVALID_ARGUMENT, "out_handle must not be NULL");
    }
    *out_handle = ptr::null_mut();
    let pairs = match read_parameters(parameters) {
        Ok(pairs) => pairs,
        Err(message) => return open_failed(SEEKDB_INVALID_ARGUMENT, message),
    };
    let directory = if db_dir.is_null() {
        None
    } else {
        match CStr::from_ptr(db_dir).to_str() {
            Ok(directory) => Some(directory),
            Err(_) => {
                return open_failed(SEEKDB_INVALID_ARGUMENT, "the directory must be valid UTF-8")
            }
        }
    };
    let startup = parameters::startup();
    let initial = parameters::initial(&pairs, &startup);
    match engine::start(directory, startup, initial) {
        Err(StartError::InvalidArgument(message)) => open_failed(SEEKDB_INVALID_ARGUMENT, message),
        Err(StartError::Internal(message)) => open_failed(SEEKDB_INTERNAL_ERROR, message),
        Ok(socket_path) => match Handle::new(&socket_path, &platform::socket_alias_patterns()) {
            Some(handle) => {
                *out_handle = Box::into_raw(Box::new(handle)).cast();
                SEEKDB_SUCCESS as c_int
            }
            None => open_failed(SEEKDB_INTERNAL_ERROR, "cannot prepare the Unix socket path"),
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn seekdb_connection_options(
    handle: SeekdbHandle,
    out_options: *mut SeekdbConnectionOptions,
) -> c_int {
    if handle.is_null() || out_options.is_null() {
        return SEEKDB_INVALID_ARGUMENT as c_int;
    }
    let handle = &*handle.cast::<Handle>();
    out_options.write(SeekdbConnectionOptions {
        transport: c"unix_socket".as_ptr(),
        port: 0,
        host: ptr::null(),
        unix_socket: handle.socket_path.as_ptr(),
        named_pipe: ptr::null(),
        user: c"root".as_ptr(),
    });
    SEEKDB_SUCCESS as c_int
}

#[no_mangle]
pub extern "C" fn seekdb_last_open_error() -> *const c_char {
    LAST_OPEN_ERROR.with(|error| error.borrow().as_ptr())
}

#[no_mangle]
pub unsafe extern "C" fn seekdb_close(handle: SeekdbHandle) -> c_int {
    if handle.is_null() {
        return SEEKDB_INVALID_ARGUMENT as c_int;
    }
    drop(Box::from_raw(handle.cast::<Handle>()));
    SEEKDB_SUCCESS as c_int
}
