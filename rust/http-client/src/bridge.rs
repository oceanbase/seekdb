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

use crate::native::{collect, Collector};
use crate::{
    Batch as HttpBatch, Client as HttpClient, ErrorKind, HttpError, Protocols, RequestOptions,
    Response,
};

#[cxx::bridge(namespace = "oceanbase::http")]
pub(crate) mod ffi {
    struct SdkOptions {
        timeout_ms: u64,
        connect_timeout_ms: u64,
        https_only: bool,
        follow_redirects: bool,
        max_redirects: u32,
        verify_peer: bool,
        verify_host: bool,
    }

    #[derive(Debug, PartialEq, Eq)]
    enum EasyErrorKind {
        Transport,
        UrlMalformed,
        Timeout,
        PeerVerification,
        Connect,
        UnsupportedEncoding,
    }

    struct Completion {
        available: bool,
        index: usize,
        code: i32,
        multi_code: i32,
    }

    unsafe extern "C++" {
        include!("rust/http-client/include/sdk.h");

        type Easy;
        type Multi;
        fn initialize() -> i32;
        fn new_easy(collector: Box<Collector>) -> UniquePtr<Easy>;
        fn configure(self: Pin<&mut Easy>, options: &SdkOptions) -> i32;
        fn set_url(self: Pin<&mut Easy>, url: &str) -> i32;
        fn accept_encoding(self: Pin<&mut Easy>, encoding: &str) -> i32;
        fn add_header(self: Pin<&mut Easy>, header: &str) -> i32;
        fn set_body(self: Pin<&mut Easy>, body: &[u8]) -> i32;
        fn perform(self: Pin<&mut Easy>) -> i32;
        fn status(self: &Easy, status: &mut u32) -> i32;
        fn collector(self: Pin<&mut Easy>) -> &mut Collector;
        fn detach(self: Pin<&mut Easy>) -> i32;
        #[allow(dead_code)]
        fn trust_pem_file(self: Pin<&mut Easy>, path: &str) -> i32;

        fn new_multi() -> UniquePtr<Multi>;
        // The Rust Batch owns both handles and destroys Easy before Multi.
        unsafe fn add(self: Pin<&mut Multi>, easy: Pin<&mut Easy>, index: usize) -> i32;
        fn drive(self: Pin<&mut Multi>, running: &mut u32) -> i32;
        fn wait(self: &Multi, timeout_ms: i32) -> i32;
        fn next_completion(self: Pin<&mut Multi>) -> Completion;

        fn easy_error_kind(code: i32) -> EasyErrorKind;
        fn easy_error_message(code: i32) -> String;
        fn multi_error_message(code: i32) -> String;
        #[allow(dead_code)]
        fn gzip_supported() -> bool;
    }

    extern "Rust" {
        type Collector;
        fn collect(collector: &mut Collector, data: &[u8]) -> usize;
    }

    enum Code {
        Ok,
        InvalidArgument,
        NotReady,
        UrlMalformed,
        Timeout,
        Transport,
        Multi,
        NotSupported,
    }

    struct ClientConfig {
        timeout_ms: u64,
        connect_timeout_ms: u64,
        https_only: bool,
        follow_redirects: bool,
        max_redirects: u32,
        verify_peer: bool,
        verify_host: bool,
        discard_body: bool,
        accept_gzip: bool,
    }

    struct Result {
        code: Code,
        http_status: u32,
        transport_code: i32,
        body_size: u64,
    }

    struct Progress {
        code: Code,
        transport_code: i32,
        running: u32,
    }

    extern "Rust" {
        type HttpClient;
        type HttpBatch;
        type HttpReply;

        fn new_http_client(url: &str, config: ClientConfig) -> Box<HttpClient>;
        fn http_client_add_header(client: &mut HttpClient, header: &str) -> Code;
        fn http_post(client: &HttpClient, body: &[u8]) -> Box<HttpReply>;
        fn http_reply_result(reply: &HttpReply) -> Result;
        fn http_reply_body(reply: &HttpReply) -> &[u8];

        fn new_http_batch(client: &HttpClient) -> Box<HttpBatch>;
        fn http_batch_add(batch: &mut HttpBatch, body: &[u8]) -> Result;
        fn http_batch_perform(batch: &mut HttpBatch) -> Progress;
        fn http_batch_wait(batch: &HttpBatch, timeout_ms: u64) -> Result;
        fn http_batch_size(batch: &HttpBatch) -> usize;
        fn http_batch_result(batch: &HttpBatch, index: usize) -> Result;
        fn http_batch_body(batch: &HttpBatch, index: usize) -> &[u8];
    }
}

pub struct HttpReply {
    result: Result<Response, HttpError>,
}

fn options(config: ffi::ClientConfig) -> RequestOptions {
    RequestOptions {
        timeout: Duration::from_millis(config.timeout_ms),
        connect_timeout: Duration::from_millis(config.connect_timeout_ms),
        protocols: if config.https_only {
            Protocols::HttpsOnly
        } else {
            Protocols::HttpAndHttps
        },
        follow_redirects: config.follow_redirects,
        max_redirects: config.max_redirects,
        verify_peer: config.verify_peer,
        verify_host: config.verify_host,
        discard_body: config.discard_body,
        accept_encoding: if config.accept_gzip {
            Some("gzip".to_owned())
        } else {
            None
        },
        ..RequestOptions::default()
    }
}

fn code(kind: ErrorKind) -> ffi::Code {
    match kind {
        ErrorKind::InvalidArgument => ffi::Code::InvalidArgument,
        ErrorKind::NotReady => ffi::Code::NotReady,
        ErrorKind::UrlMalformed => ffi::Code::UrlMalformed,
        ErrorKind::Timeout => ffi::Code::Timeout,
        ErrorKind::Transport => ffi::Code::Transport,
        ErrorKind::Multi => ffi::Code::Multi,
        ErrorKind::NotSupported => ffi::Code::NotSupported,
    }
}

fn error_result(error: &HttpError) -> ffi::Result {
    ffi::Result {
        code: code(error.kind),
        http_status: 0,
        transport_code: error.transport_code,
        body_size: 0,
    }
}

fn result(result: &Result<Response, HttpError>) -> ffi::Result {
    match result {
        Ok(response) => ffi::Result {
            code: ffi::Code::Ok,
            http_status: response.status,
            transport_code: 0,
            body_size: response.body.len() as u64,
        },
        Err(error) => error_result(error),
    }
}

fn new_http_client(url: &str, config: ffi::ClientConfig) -> Box<HttpClient> {
    Box::new(HttpClient::new(url.to_owned(), options(config)))
}

fn http_client_add_header(client: &mut HttpClient, header: &str) -> ffi::Code {
    match client.add_header(header) {
        Ok(()) => ffi::Code::Ok,
        Err(error) => code(error.kind),
    }
}

fn http_post(client: &HttpClient, body: &[u8]) -> Box<HttpReply> {
    Box::new(HttpReply {
        result: client.post(body),
    })
}

fn http_reply_result(reply: &HttpReply) -> ffi::Result {
    result(&reply.result)
}

fn http_reply_body(reply: &HttpReply) -> &[u8] {
    match &reply.result {
        Ok(response) => &response.body,
        Err(_) => &[],
    }
}

fn new_http_batch(client: &HttpClient) -> Box<HttpBatch> {
    Box::new(client.batch())
}

fn http_batch_add(batch: &mut HttpBatch, body: &[u8]) -> ffi::Result {
    match batch.add_post(body) {
        Ok(_) => ffi::Result {
            code: ffi::Code::Ok,
            http_status: 0,
            transport_code: 0,
            body_size: 0,
        },
        Err(error) => error_result(&error),
    }
}

fn http_batch_perform(batch: &mut HttpBatch) -> ffi::Progress {
    match batch.perform() {
        Ok(progress) => ffi::Progress {
            code: ffi::Code::Ok,
            transport_code: 0,
            running: progress.running,
        },
        Err(error) => ffi::Progress {
            code: code(error.kind),
            transport_code: error.transport_code,
            running: 0,
        },
    }
}

fn http_batch_wait(batch: &HttpBatch, timeout_ms: u64) -> ffi::Result {
    match batch.wait(Duration::from_millis(timeout_ms)) {
        Ok(()) => ffi::Result {
            code: ffi::Code::Ok,
            http_status: 0,
            transport_code: 0,
            body_size: 0,
        },
        Err(error) => error_result(&error),
    }
}

fn http_batch_size(batch: &HttpBatch) -> usize {
    batch.result_count()
}

fn http_batch_result(batch: &HttpBatch, index: usize) -> ffi::Result {
    match batch.result(index) {
        Some(reply) => result(reply),
        None if index < batch.result_count() => error_result(&HttpError::new(
            ErrorKind::NotReady,
            0,
            "HTTP request has not completed",
        )),
        None => error_result(&HttpError::invalid("HTTP request index is out of range")),
    }
}

fn http_batch_body(batch: &HttpBatch, index: usize) -> &[u8] {
    match batch.result(index) {
        Some(Ok(response)) => &response.body,
        _ => &[],
    }
}
