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

use std::time::Duration;

use cxx::UniquePtr;

use crate::bridge::ffi;
use crate::{BatchProgress, ErrorKind, HttpError, Protocols, RequestOptions, Response};

pub(crate) struct Collector {
    body: Vec<u8>,
    discard: bool,
}

pub(crate) fn collect(collector: &mut Collector, data: &[u8]) -> usize {
    if !collector.discard {
        if collector.body.try_reserve(data.len()).is_err() {
            return 0;
        }
        collector.body.extend_from_slice(data);
    }
    data.len()
}

#[derive(Clone)]
pub struct Client {
    url: String,
    options: RequestOptions,
}

impl Client {
    pub fn new(url: String, options: RequestOptions) -> Self {
        Self { url, options }
    }

    pub fn add_header(&mut self, header: &str) -> Result<(), HttpError> {
        validate_text(header, "HTTP header")?;
        self.options.headers.push(header.to_owned());
        Ok(())
    }

    pub fn post(&self, body: &[u8]) -> Result<Response, HttpError> {
        let mut easy = self.build_easy(body)?;
        check_easy(easy.pin_mut().perform())?;
        response(&mut easy)
    }

    pub fn batch(&self) -> Batch {
        let multi = check_easy(ffi::initialize()).and_then(|()| {
            let multi = ffi::new_multi();
            if multi.is_null() {
                Err(HttpError::new(
                    ErrorKind::Multi,
                    0,
                    "cannot allocate libcurl multi handle",
                ))
            } else {
                Ok(multi)
            }
        });
        Batch {
            requests: Vec::new(),
            client: self.clone(),
            multi,
        }
    }

    fn build_easy(&self, body: &[u8]) -> Result<UniquePtr<ffi::Easy>, HttpError> {
        if self.url.is_empty() {
            return Err(HttpError::invalid("HTTP URL is empty"));
        }
        validate_text(&self.url, "HTTP URL")?;
        if let Some(encoding) = &self.options.accept_encoding {
            validate_text(encoding, "HTTP encoding")?;
        }
        for header in &self.options.headers {
            validate_text(header, "HTTP header")?;
        }
        check_easy(ffi::initialize())?;
        let mut easy = ffi::new_easy(Box::new(Collector {
            body: Vec::new(),
            discard: self.options.discard_body,
        }));
        if easy.is_null() {
            return Err(HttpError::new(
                ErrorKind::Transport,
                0,
                "cannot allocate libcurl easy handle",
            ));
        }
        let options = ffi::SdkOptions {
            timeout_ms: millis(self.options.timeout)?,
            connect_timeout_ms: millis(self.options.connect_timeout)?,
            https_only: self.options.protocols == Protocols::HttpsOnly,
            follow_redirects: self.options.follow_redirects,
            max_redirects: self.options.max_redirects,
            verify_peer: self.options.verify_peer,
            verify_host: self.options.verify_host,
        };
        check_easy(easy.pin_mut().configure(&options))?;
        check_easy(easy.pin_mut().set_url(&self.url))?;
        if let Some(encoding) = &self.options.accept_encoding {
            check_easy(easy.pin_mut().accept_encoding(encoding))?;
        }
        for header in &self.options.headers {
            check_easy(easy.pin_mut().add_header(header))?;
        }
        check_easy(easy.pin_mut().set_body(body))?;
        #[cfg(test)]
        crate::tests::configure_trust(easy.pin_mut())?;
        Ok(easy)
    }
}

fn validate_text(value: &str, name: &str) -> Result<(), HttpError> {
    if value.as_bytes().contains(&0) {
        Err(HttpError::invalid(format!("{name} contains a NUL byte")))
    } else {
        Ok(())
    }
}

fn millis(duration: Duration) -> Result<u64, HttpError> {
    let value = u64::try_from(duration.as_millis())
        .map_err(|_| HttpError::invalid("HTTP timeout is too large"))?;
    Ok(if duration.is_zero() { 0 } else { value.max(1) })
}

fn response(easy: &mut UniquePtr<ffi::Easy>) -> Result<Response, HttpError> {
    let mut status = 0;
    check_easy(easy.status(&mut status))?;
    Ok(Response {
        status,
        body: std::mem::take(&mut easy.pin_mut().collector().body),
    })
}

enum Request {
    Pending(UniquePtr<ffi::Easy>),
    Complete(Result<Response, HttpError>),
}

pub struct Batch {
    requests: Vec<Request>,
    client: Client,
    multi: Result<UniquePtr<ffi::Multi>, HttpError>,
}

impl Batch {
    pub fn add_post(&mut self, body: &[u8]) -> Result<usize, HttpError> {
        let multi = self.multi.as_mut().map_err(|error| error.clone())?;
        let mut easy = self.client.build_easy(body)?;
        self.requests.try_reserve(1).map_err(|_| {
            HttpError::new(ErrorKind::Multi, 0, "cannot allocate HTTP request slot")
        })?;
        let index = self.requests.len();
        // SAFETY: Batch owns both handles and drops requests before Multi.
        // Easy detaches on destruction. No callback runs here.
        check_multi(unsafe { multi.pin_mut().add(easy.pin_mut(), index) })?;
        self.requests.push(Request::Pending(easy));
        Ok(index)
    }

    pub fn perform(&mut self) -> Result<BatchProgress, HttpError> {
        let multi = self.multi.as_mut().map_err(|error| error.clone())?;
        let mut running = 0;
        check_multi(multi.pin_mut().drive(&mut running))?;
        loop {
            let completion = multi.pin_mut().next_completion();
            check_multi(completion.multi_code)?;
            if !completion.available {
                break;
            }
            let slot = self.requests.get_mut(completion.index).ok_or_else(|| {
                HttpError::new(
                    ErrorKind::Multi,
                    0,
                    "libcurl returned an unknown request token",
                )
            })?;
            let reply = match slot {
                Request::Pending(easy) => {
                    check_multi(easy.pin_mut().detach())?;
                    check_easy(completion.code).and_then(|()| response(easy))
                }
                Request::Complete(_) => {
                    return Err(HttpError::new(
                        ErrorKind::Multi,
                        0,
                        "libcurl completed a request twice",
                    ));
                }
            };
            *slot = Request::Complete(reply);
        }
        Ok(BatchProgress { running })
    }

    pub fn wait(&self, timeout: Duration) -> Result<(), HttpError> {
        let multi = self.multi.as_ref().map_err(|error| error.clone())?;
        let timeout_ms = millis(timeout)?.min(i32::MAX as u64) as i32;
        check_multi(multi.wait(timeout_ms))
    }

    pub fn result(&self, index: usize) -> Option<&Result<Response, HttpError>> {
        match self.requests.get(index) {
            Some(Request::Complete(reply)) => Some(reply),
            _ => None,
        }
    }

    pub fn result_count(&self) -> usize {
        self.requests.len()
    }
}

pub(crate) fn check_easy(code: i32) -> Result<(), HttpError> {
    if code == 0 {
        return Ok(());
    }
    let kind = match ffi::easy_error_kind(code) {
        ffi::EasyErrorKind::UrlMalformed => ErrorKind::UrlMalformed,
        ffi::EasyErrorKind::Timeout => ErrorKind::Timeout,
        _ => ErrorKind::Transport,
    };
    Err(HttpError::new(kind, code, ffi::easy_error_message(code)))
}

fn check_multi(code: i32) -> Result<(), HttpError> {
    if code == 0 {
        Ok(())
    } else {
        Err(HttpError::new(
            ErrorKind::Multi,
            code,
            ffi::multi_error_message(code),
        ))
    }
}
