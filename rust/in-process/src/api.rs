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

use std::ffi::CString;
use std::fs;
use std::mem;
use std::os::unix::fs::symlink;
use std::str;

use crate::bridge::ffi::{OpenCode, Parameter};
use crate::engine::{self, StartError};
use crate::parameters;
use crate::platform;

const ALIAS_SOCKET: &str = "/r/sql.sock";

pub struct OpenResult(Result<Connection, StartError>);

impl OpenResult {
    pub fn status(&self) -> OpenCode {
        match &self.0 {
            Ok(_) => OpenCode::Success,
            Err(StartError::InvalidArgument(_)) => OpenCode::InvalidArgument,
            Err(StartError::Internal(_)) => OpenCode::InternalError,
        }
    }

    pub fn error(&self) -> &str {
        match &self.0 {
            Ok(_) => "",
            Err(StartError::InvalidArgument(message) | StartError::Internal(message)) => message,
        }
    }

    pub fn socket_path(&self) -> &[u8] {
        match &self.0 {
            Ok(connection) => connection.socket_path.as_bytes_with_nul(),
            Err(_) => &[],
        }
    }
}

struct Connection {
    socket_path: CString,
    alias_directory: Option<String>,
}

impl Connection {
    fn new(engine_socket: &str, alias_patterns: &[String]) -> Option<Connection> {
        if engine_socket.len() < sun_path_capacity() {
            return Some(Connection {
                socket_path: CString::new(engine_socket).ok()?,
                alias_directory: None,
            });
        }
        let alias_directory = alias_patterns
            .iter()
            .filter(|pattern| pattern.len() + ALIAS_SOCKET.len() < sun_path_capacity())
            .find_map(|pattern| make_temporary_directory(pattern))?;
        let mut handle = Connection {
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

impl Drop for Connection {
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

fn read_parameters(parameters: &[Parameter]) -> Result<Vec<(String, String)>, &'static str> {
    let mut pairs = Vec::new();
    for parameter in parameters {
        if parameter.key.is_empty() {
            return Err("parameters must be key/value pairs with non-empty keys");
        }
        let (Ok(key), Ok(value)) = (
            str::from_utf8(&parameter.key),
            str::from_utf8(&parameter.value),
        ) else {
            return Err("parameters must be valid UTF-8");
        };
        parameters::check(key, value)?;
        pairs.push((key.to_owned(), value.to_owned()));
    }
    Ok(pairs)
}

pub fn open(directory: &[u8], parameters: &[Parameter]) -> Box<OpenResult> {
    Box::new(OpenResult(open_connection(directory, parameters)))
}

fn open_connection(directory: &[u8], parameters: &[Parameter]) -> Result<Connection, StartError> {
    let pairs = read_parameters(parameters).map_err(StartError::InvalidArgument)?;
    let directory = str::from_utf8(directory)
        .map_err(|_| StartError::InvalidArgument("the directory must be valid UTF-8"))?;
    let startup = parameters::startup();
    let initial = parameters::initial(&pairs, &startup);
    let socket_path = engine::start(Some(directory), startup, initial)?;
    Connection::new(&socket_path, &platform::socket_alias_patterns())
        .ok_or(StartError::Internal("cannot prepare the Unix socket path"))
}
