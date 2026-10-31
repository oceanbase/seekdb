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

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::{Client, ErrorKind, Protocols, RequestOptions};

struct Reply {
    status: u16,
    content_encoding: Option<&'static str>,
    body: Vec<u8>,
    delay: Duration,
}

fn read_request(stream: &mut TcpStream) -> (String, Vec<u8>) {
    stream
        .set_read_timeout(Some(Duration::from_secs(2)))
        .unwrap();
    let mut request = Vec::new();
    let mut buffer = [0; 4096];
    let header_end = loop {
        let count = stream.read(&mut buffer).unwrap();
        assert!(count > 0, "connection closed before request headers");
        request.extend_from_slice(&buffer[..count]);
        if let Some(index) = request.windows(4).position(|window| window == b"\r\n\r\n") {
            break index + 4;
        }
    };
    let headers = String::from_utf8(request[..header_end].to_vec()).unwrap();
    let content_length = headers
        .lines()
        .find_map(|line| line.strip_prefix("Content-Length: "))
        .map(|value| value.trim().parse::<usize>().unwrap())
        .unwrap_or(0);
    while request.len() < header_end + content_length {
        let count = stream.read(&mut buffer).unwrap();
        assert!(count > 0, "connection closed before request body");
        request.extend_from_slice(&buffer[..count]);
    }
    (
        headers,
        request[header_end..header_end + content_length].to_vec(),
    )
}

fn server(mut replies: Vec<Reply>) -> (String, JoinHandle<Vec<(String, Vec<u8>)>>) {
    replies.reverse();
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let url = format!("http://{}/resource", listener.local_addr().unwrap());
    let handle = thread::spawn(move || {
        let mut requests = Vec::new();
        for reply in replies {
            let (mut stream, _) = listener.accept().unwrap();
            let request = read_request(&mut stream);
            assert!(request.0.starts_with("POST /resource HTTP/1.1\r\n"));
            thread::sleep(reply.delay);
            let body = if reply.body.is_empty() {
                request.1.clone()
            } else {
                reply.body
            };
            let mut response = format!(
                "HTTP/1.1 {} Status\r\nContent-Length: {}\r\nConnection: close\r\n",
                reply.status,
                body.len(),
            );
            if let Some(encoding) = reply.content_encoding {
                response.push_str(&format!("Content-Encoding: {encoding}\r\n"));
            }
            response.push_str("\r\n");
            let _ = stream.write_all(response.as_bytes());
            let _ = stream.write_all(&body);
            requests.push(request);
        }
        requests
    });
    (url, handle)
}

fn options() -> RequestOptions {
    RequestOptions {
        timeout: Duration::from_secs(2),
        connect_timeout: Duration::from_secs(1),
        protocols: Protocols::HttpAndHttps,
        ..RequestOptions::default()
    }
}

#[test]
fn synchronous_post_returns_status_headers_and_body() {
    let (url, server) = server(vec![Reply {
        status: 201,
        content_encoding: None,
        body: b"created".to_vec(),
        delay: Duration::ZERO,
    }]);
    let mut client = Client::new(url, options());
    client.add_header("Content-Type: application/json").unwrap();
    client.add_header("X-Test: native-rust").unwrap();
    let response = client.post(br#"{"input":1}"#).unwrap();
    assert_eq!(201, response.status);
    assert_eq!(b"created", response.body.as_slice());
    let requests = server.join().unwrap();
    assert!(requests[0].0.contains("X-Test: native-rust\r\n"));
    assert_eq!(br#"{"input":1}"#, requests[0].1.as_slice());
}

#[test]
fn batch_progress_preserves_request_order() {
    let (url, server) = server(vec![
        Reply {
            status: 200,
            content_encoding: None,
            body: Vec::new(),
            delay: Duration::from_millis(30),
        },
        Reply {
            status: 200,
            content_encoding: None,
            body: Vec::new(),
            delay: Duration::ZERO,
        },
    ]);
    let client = Client::new(url, options());
    let mut batch = client.batch();
    assert_eq!(0, batch.add_post(b"first").unwrap());
    assert_eq!(1, batch.add_post(b"second").unwrap());
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        let progress = batch.perform().unwrap();
        if progress.running == 0 {
            break;
        }
        assert!(Instant::now() < deadline, "batch did not complete");
        batch.wait(Duration::from_millis(10)).unwrap();
    }
    assert_eq!(2, batch.result_count());
    assert_eq!(
        b"first",
        batch.result(0).unwrap().as_ref().unwrap().body.as_slice()
    );
    assert_eq!(
        b"second",
        batch.result(1).unwrap().as_ref().unwrap().body.as_slice()
    );
    server.join().unwrap();
}

#[test]
fn gzip_response_is_decoded_by_rust_zlib() {
    const GZIP: &[u8] = &[
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x4b, 0xce, 0xcf, 0x2d, 0x28,
        0x4a, 0x2d, 0x2e, 0x4e, 0x4d, 0x51, 0x00, 0x52, 0x05, 0xf9, 0x79, 0xc5, 0xa9, 0x00, 0xb1,
        0xff, 0x32, 0x6f, 0x13, 0x00, 0x00, 0x00,
    ];
    let (url, server) = server(vec![Reply {
        status: 200,
        content_encoding: Some("gzip"),
        body: GZIP.to_vec(),
        delay: Duration::ZERO,
    }]);
    let mut request_options = options();
    request_options.accept_encoding = Some("gzip".to_owned());
    let client = Client::new(url, request_options);
    let response = client.post(b"compression").unwrap();
    assert_eq!(b"compressed response", response.body.as_slice());
    let requests = server.join().unwrap();
    assert!(requests[0]
        .0
        .to_ascii_lowercase()
        .contains("accept-encoding: gzip"));
}

#[test]
fn malformed_url_and_timeout_keep_distinct_errors() {
    let client = Client::new("not a URL".to_owned(), options());
    assert_eq!(ErrorKind::UrlMalformed, client.post(b"").unwrap_err().kind);

    let (url, server) = server(vec![Reply {
        status: 200,
        content_encoding: None,
        body: b"late".to_vec(),
        delay: Duration::from_millis(100),
    }]);
    let mut request_options = options();
    request_options.timeout = Duration::from_millis(10);
    let client = Client::new(url, request_options);
    assert_eq!(
        ErrorKind::Timeout,
        client.post(b"timeout").unwrap_err().kind
    );
    server.join().unwrap();
}
