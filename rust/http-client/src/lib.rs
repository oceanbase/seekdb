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

//! Rust-owned HTTP POST clients with synchronous and non-blocking interfaces.

use std::time::Duration;

#[cfg(not(target_os = "emscripten"))]
mod bridge;
#[cfg(not(target_os = "emscripten"))]
mod native;
#[cfg(all(test, not(target_os = "emscripten")))]
mod tests;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Protocols {
    HttpAndHttps,
    HttpsOnly,
}

#[derive(Clone, Debug)]
pub struct RequestOptions {
    pub headers: Vec<String>,
    pub timeout: Duration,
    pub connect_timeout: Duration,
    pub protocols: Protocols,
    pub follow_redirects: bool,
    pub max_redirects: u32,
    pub verify_peer: bool,
    pub verify_host: bool,
    pub discard_body: bool,
    pub accept_encoding: Option<String>,
}

impl Default for RequestOptions {
    fn default() -> Self {
        Self {
            headers: Vec::new(),
            timeout: Duration::from_secs(60),
            connect_timeout: Duration::from_secs(10),
            protocols: Protocols::HttpAndHttps,
            follow_redirects: false,
            max_redirects: 0,
            verify_peer: true,
            verify_host: true,
            discard_body: false,
            accept_encoding: None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ErrorKind {
    InvalidArgument,
    NotReady,
    UrlMalformed,
    Timeout,
    Transport,
    Multi,
    NotSupported,
}

#[derive(Clone, Debug)]
pub struct HttpError {
    pub kind: ErrorKind,
    pub transport_code: i32,
    pub message: String,
}

impl HttpError {
    pub(crate) fn new(kind: ErrorKind, transport_code: i32, message: impl Into<String>) -> Self {
        Self {
            kind,
            transport_code,
            message: message.into(),
        }
    }

    pub(crate) fn invalid(message: impl Into<String>) -> Self {
        Self::new(ErrorKind::InvalidArgument, 0, message)
    }

    #[cfg(target_os = "emscripten")]
    pub(crate) fn unsupported() -> Self {
        Self::new(
            ErrorKind::NotSupported,
            0,
            "native HTTP is unavailable on Emscripten",
        )
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Response {
    pub status: u32,
    pub body: Vec<u8>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BatchProgress {
    pub running: u32,
}

#[cfg(not(target_os = "emscripten"))]
pub use native::{Batch, Client};

#[cfg(target_os = "emscripten")]
pub struct Client {
    _url: String,
    _options: RequestOptions,
}

#[cfg(target_os = "emscripten")]
impl Client {
    pub fn new(url: String, options: RequestOptions) -> Self {
        Self {
            _url: url,
            _options: options,
        }
    }

    pub fn add_header(&mut self, header: &str) -> Result<(), HttpError> {
        self._options.headers.push(header.to_owned());
        Ok(())
    }

    pub fn post(&self, _body: &[u8]) -> Result<Response, HttpError> {
        Err(HttpError::unsupported())
    }

    pub fn batch(&self) -> Batch {
        Batch {
            replies: Vec::new(),
        }
    }
}

#[cfg(target_os = "emscripten")]
pub struct Batch {
    replies: Vec<Result<Response, HttpError>>,
}

#[cfg(target_os = "emscripten")]
impl Batch {
    pub fn add_post(&mut self, _body: &[u8]) -> Result<usize, HttpError> {
        Err(HttpError::unsupported())
    }

    pub fn perform(&mut self) -> Result<BatchProgress, HttpError> {
        Err(HttpError::unsupported())
    }

    pub fn wait(&self, _timeout: Duration) -> Result<(), HttpError> {
        Err(HttpError::unsupported())
    }

    pub fn result(&self, index: usize) -> Option<&Result<Response, HttpError>> {
        self.replies.get(index)
    }

    pub fn result_count(&self) -> usize {
        self.replies.len()
    }
}
