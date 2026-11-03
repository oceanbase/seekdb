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

use std::os::raw::c_long;
use std::time::Duration;

use curl::easy::{Easy2, Handler, List, SslOpt, WriteError};
use curl::multi::{Easy2Handle, Multi};

#[path = "tls.rs"]
mod tls;

use crate::{BatchProgress, ErrorKind, HttpError, Protocols, RequestOptions, Response};

struct Collector {
    body: Vec<u8>,
    discard: bool,
    host_check: Option<Box<tls::HostCheck>>,
}

impl Handler for Collector {
    fn write(&mut self, data: &[u8]) -> Result<usize, WriteError> {
        if !self.discard {
            if self.body.try_reserve(data.len()).is_err() {
                return Ok(0);
            }
            self.body.extend_from_slice(data);
        }
        Ok(data.len())
    }
}

#[derive(Clone)]
pub struct Client {
    url: String,
    options: RequestOptions,
}

impl Client {
    pub fn new(url: String, options: RequestOptions) -> Self {
        curl::init();
        Self { url, options }
    }

    pub fn add_header(&mut self, header: &str) -> Result<(), HttpError> {
        if header.as_bytes().contains(&0) {
            return Err(HttpError::invalid("HTTP header contains a NUL byte"));
        }
        self.options.headers.push(header.to_owned());
        Ok(())
    }

    pub fn post(&self, body: &[u8]) -> Result<Response, HttpError> {
        let mut easy = self.build_easy(body)?;
        easy.perform()
            .map_err(|error| transfer_error(error, easy.get_ref()))?;
        response(&mut easy)
    }

    pub fn batch(&self) -> Batch {
        Batch {
            client: self.clone(),
            multi: Multi::new(),
            handles: Vec::new(),
            replies: Vec::new(),
        }
    }

    fn build_easy(&self, body: &[u8]) -> Result<Easy2<Collector>, HttpError> {
        if self.url.is_empty() {
            return Err(HttpError::invalid("HTTP URL is empty"));
        }
        let mut easy = Easy2::new(Collector {
            body: Vec::new(),
            discard: self.options.discard_body,
            host_check: None,
        });
        easy.url(&self.url).map_err(HttpError::from_easy)?;
        easy.signal(false).map_err(HttpError::from_easy)?;
        easy.tcp_nodelay(true).map_err(HttpError::from_easy)?;
        easy.timeout(self.options.timeout)
            .map_err(HttpError::from_easy)?;
        easy.connect_timeout(self.options.connect_timeout)
            .map_err(HttpError::from_easy)?;
        easy.ssl_verify_peer(self.options.verify_peer)
            .map_err(HttpError::from_easy)?;
        easy.ssl_verify_host(self.options.verify_host)
            .map_err(HttpError::from_easy)?;
        if self.options.verify_peer {
            easy.ssl_options(SslOpt::new().native_ca(true))
                .map_err(HttpError::from_easy)?;
        }
        easy.proxy_ssl_options(SslOpt::new().native_ca(true))
            .map_err(HttpError::from_easy)?;
        easy.follow_location(self.options.follow_redirects)
            .map_err(HttpError::from_easy)?;
        if self.options.follow_redirects {
            easy.max_redirections(self.options.max_redirects)
                .map_err(HttpError::from_easy)?;
        }
        if let Some(encoding) = &self.options.accept_encoding {
            easy.accept_encoding(encoding)
                .map_err(HttpError::from_easy)?;
        }
        let mut headers = List::new();
        for header in &self.options.headers {
            headers.append(header).map_err(HttpError::from_easy)?;
        }
        easy.http_headers(headers).map_err(HttpError::from_easy)?;
        easy.post(true).map_err(HttpError::from_easy)?;
        easy.post_fields_copy(body).map_err(HttpError::from_easy)?;
        set_protocols(&mut easy, self.options.protocols)?;
        if !self.options.verify_peer && self.options.verify_host {
            easy.get_mut().host_check = Some(tls::HostCheck::new(easy.raw()));
            // The boxed state stays at one address as Easy2 moves into a Multi.
            // Easy2 cleans up libcurl before dropping its Collector and state.
            let state = easy.get_mut().host_check.as_mut().unwrap().as_mut() as *mut tls::HostCheck;
            tls::install(easy.raw(), state).map_err(HttpError::from_easy)?;
        }
        Ok(easy)
    }
}

fn set_protocols<H>(easy: &mut Easy2<H>, protocols: Protocols) -> Result<(), HttpError> {
    let allowed = match protocols {
        Protocols::HttpAndHttps => curl_sys::CURLPROTO_HTTP | curl_sys::CURLPROTO_HTTPS,
        Protocols::HttpsOnly => curl_sys::CURLPROTO_HTTPS,
    } as c_long;
    // SAFETY: `easy` is a live Easy2 handle and both options take a C long.
    // The numeric bitmask is the stable CURLOPT_PROTOCOLS ABI also used by
    // libcurl's newer string-based option for the HTTP/HTTPS-only case.
    let protocol_result =
        unsafe { curl_sys::curl_easy_setopt(easy.raw(), curl_sys::CURLOPT_PROTOCOLS, allowed) };
    if protocol_result != curl_sys::CURLE_OK {
        return Err(HttpError::from_easy(curl::Error::new(protocol_result)));
    }
    // SAFETY: same handle and option contract as above.
    let redirect_result = unsafe {
        curl_sys::curl_easy_setopt(easy.raw(), curl_sys::CURLOPT_REDIR_PROTOCOLS, allowed)
    };
    if redirect_result != curl_sys::CURLE_OK {
        return Err(HttpError::from_easy(curl::Error::new(redirect_result)));
    }
    Ok(())
}

fn response<H>(easy: &mut Easy2<H>) -> Result<Response, HttpError>
where
    H: ResponseBody,
{
    let status = easy.response_code().map_err(HttpError::from_easy)?;
    Ok(Response {
        status,
        body: easy.get_mut().take_body(),
    })
}

fn handle_response(handle: &mut Easy2Handle<Collector>) -> Result<Response, HttpError> {
    let status = handle.response_code().map_err(HttpError::from_easy)?;
    Ok(Response {
        status,
        body: std::mem::take(&mut handle.get_mut().body),
    })
}

trait ResponseBody {
    fn take_body(&mut self) -> Vec<u8>;
}

impl ResponseBody for Collector {
    fn take_body(&mut self) -> Vec<u8> {
        std::mem::take(&mut self.body)
    }
}

pub struct Batch {
    client: Client,
    multi: Multi,
    handles: Vec<Option<Easy2Handle<Collector>>>,
    replies: Vec<Option<Result<Response, HttpError>>>,
}

impl Batch {
    pub fn add_post(&mut self, body: &[u8]) -> Result<usize, HttpError> {
        let easy = self.client.build_easy(body)?;
        let token = self.handles.len();
        let mut handle = self.multi.add2(easy).map_err(HttpError::from_multi)?;
        handle.set_token(token).map_err(HttpError::from_easy)?;
        self.handles.push(Some(handle));
        self.replies.push(None);
        Ok(token)
    }

    pub fn perform(&mut self) -> Result<BatchProgress, HttpError> {
        let running = self.multi.perform().map_err(HttpError::from_multi)?;
        let multi = &self.multi;
        let handles = &mut self.handles;
        let replies = &mut self.replies;
        let mut collection_error = None;
        multi.messages(|message| {
            let token = match message.token() {
                Ok(token) => token,
                Err(error) => {
                    collection_error = Some(HttpError::from_easy(error));
                    return;
                }
            };
            let Some(handle) = handles.get_mut(token).and_then(Option::as_mut) else {
                collection_error = Some(HttpError::new(
                    ErrorKind::Multi,
                    0,
                    "libcurl returned an unknown request token",
                ));
                return;
            };
            let reply = match message.result_for2(handle) {
                Some(Ok(())) => handle_response(handle),
                Some(Err(error)) => Err(transfer_error(error, handle.get_ref())),
                None => Err(HttpError::new(
                    ErrorKind::Multi,
                    0,
                    "libcurl completion message does not match the request",
                )),
            };
            replies[token] = Some(reply);
        });
        if let Some(error) = collection_error {
            return Err(error);
        }
        Ok(BatchProgress { running })
    }

    pub fn wait(&self, timeout: Duration) -> Result<(), HttpError> {
        self.multi
            .wait(&mut [], timeout)
            .map_err(HttpError::from_multi)?;
        Ok(())
    }

    pub fn result(&self, index: usize) -> Option<&Result<Response, HttpError>> {
        self.replies.get(index).and_then(Option::as_ref)
    }

    pub fn result_count(&self) -> usize {
        self.replies.len()
    }
}

fn transfer_error(error: curl::Error, collector: &Collector) -> HttpError {
    if let Some(reason) = collector
        .host_check
        .as_ref()
        .and_then(|check| check.failure())
    {
        HttpError::new(
            ErrorKind::Transport,
            curl_sys::CURLE_PEER_FAILED_VERIFICATION as i32,
            reason,
        )
    } else {
        HttpError::from_easy(error)
    }
}

impl HttpError {
    fn from_easy(error: curl::Error) -> Self {
        let kind = if error.is_url_malformed() {
            ErrorKind::UrlMalformed
        } else if error.is_operation_timedout() {
            ErrorKind::Timeout
        } else {
            ErrorKind::Transport
        };
        Self::new(kind, error.code() as i32, error.to_string())
    }

    fn from_multi(error: curl::MultiError) -> Self {
        Self::new(ErrorKind::Multi, error.code(), error.to_string())
    }
}
