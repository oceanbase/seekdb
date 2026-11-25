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

use crate::bridge::ffi;
use crate::{Client, ErrorKind, Protocols, RequestOptions};

pub(crate) fn configure_trust(easy: std::pin::Pin<&mut ffi::Easy>) -> Result<(), crate::HttpError> {
    if std::env::var_os("HTTP_CLIENT_NATIVE_CA_TEST").is_some() {
        let path =
            std::env::var("SSL_CERT_FILE").expect("isolated TLS test requires its CA fixture");
        crate::native::check_easy(easy.trust_pem_file(&path))?;
    }
    Ok(())
}

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

type Request = (String, Vec<u8>);

fn server(mut replies: Vec<Reply>) -> (String, JoinHandle<Vec<Request>>) {
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
fn gzip_response_is_decoded_by_sdk_libcurl() {
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
    let reply = client.post(b"compression");
    if ffi::gzip_supported() {
        assert_eq!(b"compressed response", reply.unwrap().body.as_slice());
    } else {
        assert_eq!(
            ffi::EasyErrorKind::UnsupportedEncoding,
            ffi::easy_error_kind(reply.unwrap_err().transport_code)
        );
    }
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

    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    listener.set_nonblocking(true).unwrap();
    let url = format!("http://{}/timeout", listener.local_addr().unwrap());
    let stop = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
    let stopped = stop.clone();
    let server = thread::spawn(move || {
        while !stopped.load(std::sync::atomic::Ordering::Relaxed) {
            match listener.accept() {
                Ok((_socket, _)) => {
                    thread::sleep(Duration::from_millis(100));
                    return;
                }
                Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                    thread::sleep(Duration::from_millis(1));
                }
                Err(error) => panic!("timeout fixture accept failed: {error}"),
            }
        }
    });
    let mut request_options = options();
    request_options.timeout = Duration::from_millis(10);
    let client = Client::new(url, request_options);
    let result = client.post(b"timeout");
    stop.store(true, std::sync::atomic::Ordering::Relaxed);
    server.join().unwrap();
    assert_eq!(ErrorKind::Timeout, result.unwrap_err().kind);
}

struct TlsServer {
    url: String,
    stop: std::sync::Arc<std::sync::atomic::AtomicBool>,
    requests: std::sync::Arc<std::sync::Mutex<Vec<Vec<u8>>>>,
    connections: std::sync::Arc<std::sync::atomic::AtomicUsize>,
    thread: Option<JoinHandle<()>>,
}

impl TlsServer {
    fn new(cert: &'static [u8], key: &'static [u8], redirects: Vec<Option<String>>) -> Self {
        use rustls::pki_types::{CertificateDer, PrivateKeyDer, PrivatePkcs8KeyDer};
        use std::sync::{atomic::AtomicBool, atomic::AtomicUsize, Arc, Mutex};
        let provider = rustls::crypto::ring::default_provider();
        let config = rustls::ServerConfig::builder_with_provider(Arc::new(provider))
            .with_safe_default_protocol_versions()
            .unwrap()
            .with_no_client_auth()
            .with_single_cert(
                vec![CertificateDer::from(cert)],
                PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(key)),
            )
            .unwrap();
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        listener.set_nonblocking(true).unwrap();
        let url = format!(
            "https://localhost:{}/resource",
            listener.local_addr().unwrap().port()
        );
        let stop = Arc::new(AtomicBool::new(false));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let connections = Arc::new(AtomicUsize::new(0));
        let (done, received, accepted) = (stop.clone(), requests.clone(), connections.clone());
        let thread = thread::spawn(move || {
            use std::sync::atomic::Ordering;
            let config = Arc::new(config);
            while !done.load(Ordering::Relaxed) {
                let (socket, _) = match listener.accept() {
                    Ok(connection) => connection,
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                        thread::sleep(Duration::from_millis(2));
                        continue;
                    }
                    Err(error) => panic!("TLS fixture accept failed: {error}"),
                };
                accepted.fetch_add(1, Ordering::Relaxed);
                socket.set_nonblocking(false).unwrap();
                socket
                    .set_read_timeout(Some(Duration::from_millis(300)))
                    .unwrap();
                socket
                    .set_write_timeout(Some(Duration::from_secs(1)))
                    .unwrap();
                let connection = rustls::ServerConnection::new(config.clone()).unwrap();
                let mut stream = rustls::StreamOwned::new(connection, socket);
                while !done.load(Ordering::Relaxed) {
                    let mut request = Vec::new();
                    let mut byte = [0; 1];
                    let mut header_end = None;
                    let mut content_length = 0;
                    while let Ok(1) = stream.read(&mut byte) {
                        request.push(byte[0]);
                        if header_end.is_none() && request.ends_with(b"\r\n\r\n") {
                            header_end = Some(request.len());
                            content_length = String::from_utf8_lossy(&request)
                                .lines()
                                .filter_map(|line| line.split_once(':'))
                                .find(|(name, _)| name.eq_ignore_ascii_case("content-length"))
                                .map(|(_, value)| value.trim().parse::<usize>().unwrap())
                                .unwrap_or(0);
                        }
                        if header_end.is_some_and(|end| request.len() >= end + content_length) {
                            break;
                        }
                    }
                    if request.is_empty() {
                        break;
                    }
                    let mut requests = received.lock().unwrap();
                    let index = requests.len();
                    requests.push(request);
                    drop(requests);
                    let response = if let Some(Some(location)) = redirects.get(index) {
                        format!("HTTP/1.1 307 Temporary Redirect\r\nLocation: {location}\r\nContent-Length: 0\r\n\r\n")
                    } else {
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok".to_owned()
                    };
                    if stream.write_all(response.as_bytes()).is_err() || stream.flush().is_err() {
                        break;
                    }
                }
            }
        });
        Self {
            url,
            stop,
            requests,
            connections,
            thread: Some(thread),
        }
    }

    fn delivered(&self) -> Vec<Vec<u8>> {
        self.requests.lock().unwrap().clone()
    }
}

impl Drop for TlsServer {
    fn drop(&mut self) {
        self.stop.store(true, std::sync::atomic::Ordering::Relaxed);
        self.thread.take().unwrap().join().unwrap();
    }
}

fn hostname_options() -> RequestOptions {
    RequestOptions {
        verify_peer: false,
        verify_host: true,
        ..options()
    }
}

fn poll_batch(batch: &mut crate::Batch) {
    let deadline = Instant::now() + Duration::from_secs(3);
    while batch.perform().unwrap().running > 0 {
        assert!(Instant::now() < deadline, "TLS batch did not finish");
        batch.wait(Duration::from_millis(10)).unwrap();
    }
}

#[test]
fn hostname_only_tls_rejects_wrong_host_before_any_request_bytes() {
    let bad = TlsServer::new(
        include_bytes!("testdata/bad.der"),
        include_bytes!("testdata/bad-key.der"),
        vec![],
    );
    let error = Client::new(bad.url.clone(), hostname_options())
        .post(b"private POST body")
        .unwrap_err();
    assert_eq!(
        ffi::EasyErrorKind::PeerVerification,
        ffi::easy_error_kind(error.transport_code)
    );
    assert!(bad.delivered().is_empty());
    let good = TlsServer::new(
        include_bytes!("testdata/good.der"),
        include_bytes!("testdata/good-key.der"),
        vec![],
    );
    assert_eq!(
        200,
        Client::new(good.url.clone(), hostname_options())
            .post(b"positive control")
            .unwrap()
            .status
    );
    assert!(good.delivered()[0].ends_with(b"positive control"));
}

#[test]
fn hostname_only_tls_keeps_legacy_cn_san_ip_and_url_normalization() {
    let cases: &[(&[u8], &[u8], bool, bool)] = &[
        (
            include_bytes!("testdata/cn.der"),
            include_bytes!("testdata/cn-key.der"),
            false,
            true,
        ),
        (
            include_bytes!("testdata/ip.der"),
            include_bytes!("testdata/ip-key.der"),
            true,
            true,
        ),
        (
            include_bytes!("testdata/ipcn.der"),
            include_bytes!("testdata/ipcn-key.der"),
            true,
            true,
        ),
        (
            include_bytes!("testdata/precedence.der"),
            include_bytes!("testdata/precedence-key.der"),
            false,
            false,
        ),
        (
            include_bytes!("testdata/ipprecedence.der"),
            include_bytes!("testdata/ipprecedence-key.der"),
            false,
            false,
        ),
    ];
    for &(cert, key, ip, matches) in cases {
        let server = TlsServer::new(cert, key, vec![]);
        let host = if ip {
            "127.0.0.1"
        } else {
            "user:password@LOCALHOST."
        };
        let url = server.url.replace("localhost", host);
        let result = Client::new(url, hostname_options()).post(b"name policy");
        assert_eq!(matches, result.is_ok(), "{result:?}");
        assert_eq!(usize::from(matches), server.delivered().len());
    }
    let server = TlsServer::new(
        include_bytes!("testdata/good.der"),
        include_bytes!("testdata/good-key.der"),
        vec![],
    );
    let url = server.url.replace("localhost", "user:password@%6cocalhost");
    assert!(Client::new(url, hostname_options())
        .post(b"encoded host")
        .is_ok());
}

#[test]
fn hostname_only_tls_checks_redirects_and_batch_result_tokens() {
    let bad = TlsServer::new(
        include_bytes!("testdata/bad.der"),
        include_bytes!("testdata/bad-key.der"),
        vec![],
    );
    let good = TlsServer::new(
        include_bytes!("testdata/good.der"),
        include_bytes!("testdata/good-key.der"),
        vec![None, Some(bad.url.clone())],
    );
    let mut settings = hostname_options();
    settings.follow_redirects = true;
    settings.max_redirects = 3;
    let mut batch = Client::new(good.url.clone(), settings).batch();
    assert_eq!(0, batch.add_post(b"successful token").unwrap());
    poll_batch(&mut batch);
    assert_eq!(1, batch.add_post(b"rejected token").unwrap());
    poll_batch(&mut batch);
    assert_eq!(200, batch.result(0).unwrap().as_ref().unwrap().status);
    let error = batch.result(1).unwrap().as_ref().unwrap_err();
    assert_eq!(
        ffi::EasyErrorKind::PeerVerification,
        ffi::easy_error_kind(error.transport_code)
    );
    assert!(bad.delivered().is_empty());
    assert_eq!(2, good.delivered().len());
    assert_eq!(
        1,
        good.connections.load(std::sync::atomic::Ordering::Relaxed)
    );
}

#[test]
fn disabled_host_verification_and_ordinary_tls_failures_keep_their_policy() {
    let bad = TlsServer::new(
        include_bytes!("testdata/bad.der"),
        include_bytes!("testdata/bad-key.der"),
        vec![],
    );
    let mut settings = hostname_options();
    settings.verify_host = false;
    assert!(Client::new(bad.url.clone(), settings)
        .post(b"explicit bypass")
        .is_ok());
    assert_eq!(1, bad.delivered().len());
    let good = TlsServer::new(
        include_bytes!("testdata/good.der"),
        include_bytes!("testdata/good-key.der"),
        vec![],
    );
    assert!(Client::new(good.url.clone(), options())
        .post(b"untrusted CA")
        .is_err());
    assert!(good.delivered().is_empty());
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let url = format!(
        "https://localhost:{}",
        listener.local_addr().unwrap().port()
    );
    drop(listener);
    let error = Client::new(url, hostname_options())
        .post(b"connect error")
        .unwrap_err();
    assert_eq!(
        ffi::EasyErrorKind::Connect,
        ffi::easy_error_kind(error.transport_code)
    );
}

#[cfg(target_os = "linux")]
fn native_ca_test_process(name: &str) -> std::process::Command {
    let root_file = std::env::temp_dir().join(format!(
        "http-client-native-ca-{}-{name}.pem",
        std::process::id()
    ));
    std::fs::write(&root_file, include_bytes!("testdata/native-ca.pem")).unwrap();
    let mut command = std::process::Command::new(std::env::current_exe().unwrap());
    command
        .arg("--exact")
        .arg(format!("tests::{name}"))
        .arg("--nocapture")
        .env("HTTP_CLIENT_NATIVE_CA_TEST", name)
        .env("SSL_CERT_FILE", root_file)
        .env_remove("SSL_CERT_DIR");
    for variable in [
        "http_proxy",
        "https_proxy",
        "all_proxy",
        "no_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
        "NO_PROXY",
    ] {
        command.env_remove(variable);
    }
    command
}

#[cfg(target_os = "linux")]
fn assert_child_succeeded(command: &mut std::process::Command) {
    let root_file = command
        .get_envs()
        .find(|(name, _)| *name == "SSL_CERT_FILE")
        .and_then(|(_, value)| value)
        .map(std::path::PathBuf::from)
        .unwrap();
    let output = command.output();
    std::fs::remove_file(root_file).unwrap();
    let output = output.unwrap();
    assert!(
        output.status.success(),
        "isolated native CA test failed ({:?})\n{}\n{}",
        output.status,
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr),
    );
}

#[cfg(target_os = "linux")]
#[test]
fn native_ca_trust_accepts_matching_names_and_rejects_wrong_names() {
    let name = "native_ca_trust_accepts_matching_names_and_rejects_wrong_names";
    if std::env::var("HTTP_CLIENT_NATIVE_CA_TEST").as_deref() != Ok(name) {
        assert_child_succeeded(&mut native_ca_test_process(name));
        return;
    }
    let trusted = TlsServer::new(
        include_bytes!("testdata/native-ca-localhost.der"),
        include_bytes!("testdata/native-ca-localhost-key.der"),
        vec![],
    );
    assert_eq!(
        200,
        Client::new(trusted.url.clone(), options())
            .post(b"trusted native CA")
            .unwrap()
            .status,
    );
    assert_eq!(1, trusted.delivered().len());
    assert!(trusted.delivered()[0].ends_with(b"trusted native CA"));

    let wrong_name = TlsServer::new(
        include_bytes!("testdata/native-ca-localhost.der"),
        include_bytes!("testdata/native-ca-localhost-key.der"),
        vec![],
    );
    let error = Client::new(wrong_name.url.replace("localhost", "127.0.0.1"), options())
        .post(b"must not be delivered")
        .unwrap_err();
    assert_eq!(
        ffi::EasyErrorKind::PeerVerification,
        ffi::easy_error_kind(error.transport_code)
    );
    assert!(wrong_name.delivered().is_empty());

    let untrusted = TlsServer::new(
        include_bytes!("testdata/good.der"),
        include_bytes!("testdata/good-key.der"),
        vec![],
    );
    let error = Client::new(untrusted.url.clone(), options())
        .post(b"untrusted leaf")
        .unwrap_err();
    assert_eq!(
        ffi::EasyErrorKind::PeerVerification,
        ffi::easy_error_kind(error.transport_code)
    );
    assert!(untrusted.delivered().is_empty());
}

#[cfg(target_os = "linux")]
struct HttpsProxy {
    url: String,
    stop: std::sync::Arc<std::sync::atomic::AtomicBool>,
    connects: std::sync::Arc<std::sync::atomic::AtomicUsize>,
    thread: Option<JoinHandle<()>>,
}

#[cfg(target_os = "linux")]
impl HttpsProxy {
    fn new(cert: &'static [u8], key: &'static [u8], origin: &str) -> Self {
        use rustls::pki_types::{CertificateDer, PrivateKeyDer, PrivatePkcs8KeyDer};
        use std::sync::{atomic::AtomicBool, atomic::AtomicUsize, atomic::Ordering, Arc};
        let config = rustls::ServerConfig::builder_with_provider(Arc::new(
            rustls::crypto::ring::default_provider(),
        ))
        .with_safe_default_protocol_versions()
        .unwrap()
        .with_no_client_auth()
        .with_single_cert(
            vec![CertificateDer::from(cert)],
            PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(key)),
        )
        .unwrap();
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        listener.set_nonblocking(true).unwrap();
        let url = format!(
            "https://localhost:{}",
            listener.local_addr().unwrap().port()
        );
        let target = origin
            .strip_prefix("https://")
            .unwrap()
            .split('/')
            .next()
            .unwrap()
            .to_owned();
        let stop = Arc::new(AtomicBool::new(false));
        let connects = Arc::new(AtomicUsize::new(0));
        let (done, received) = (stop.clone(), connects.clone());
        let thread = thread::spawn(move || {
            let (socket, _) = loop {
                match listener.accept() {
                    Ok(connection) => break connection,
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                        if done.load(Ordering::Relaxed) {
                            return;
                        }
                        thread::sleep(Duration::from_millis(2));
                    }
                    Err(error) => panic!("HTTPS proxy accept failed: {error}"),
                }
            };
            socket
                .set_read_timeout(Some(Duration::from_secs(2)))
                .unwrap();
            socket
                .set_write_timeout(Some(Duration::from_secs(2)))
                .unwrap();
            let connection = rustls::ServerConnection::new(Arc::new(config)).unwrap();
            let mut stream = rustls::StreamOwned::new(connection, socket);
            let mut headers = Vec::new();
            let mut byte = [0; 1];
            while !headers.ends_with(b"\r\n\r\n") {
                if stream.read(&mut byte).ok() != Some(1) {
                    return;
                }
                headers.push(byte[0]);
                assert!(headers.len() < 8192, "oversized CONNECT request");
            }
            assert!(String::from_utf8_lossy(&headers)
                .starts_with(&format!("CONNECT {target} HTTP/1.1\r\n")));
            received.fetch_add(1, Ordering::Relaxed);
            let mut upstream = TcpStream::connect(&target).unwrap();
            stream
                .write_all(b"HTTP/1.1 200 Connection established\r\n\r\n")
                .unwrap();
            stream.flush().unwrap();
            stream.sock.set_nonblocking(true).unwrap();
            upstream.set_nonblocking(true).unwrap();
            let mut buffer = [0; 4096];
            while !done.load(Ordering::Relaxed) {
                match stream.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(count) => upstream.write_all(&buffer[..count]).unwrap(),
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {}
                    Err(_) => break,
                }
                match upstream.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(count) => {
                        if stream.write_all(&buffer[..count]).is_err() || stream.flush().is_err() {
                            break;
                        }
                    }
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {}
                    Err(_) => break,
                }
                thread::sleep(Duration::from_millis(1));
            }
        });
        Self {
            url,
            stop,
            connects,
            thread: Some(thread),
        }
    }
}

#[cfg(target_os = "linux")]
impl Drop for HttpsProxy {
    fn drop(&mut self) {
        self.stop.store(true, std::sync::atomic::Ordering::Relaxed);
        self.thread.take().unwrap().join().unwrap();
    }
}

#[cfg(target_os = "linux")]
#[test]
fn https_proxy_uses_native_ca_and_keeps_verification_with_unverified_origin() {
    let name = "https_proxy_uses_native_ca_and_keeps_verification_with_unverified_origin";
    if std::env::var("HTTP_CLIENT_NATIVE_CA_TEST").as_deref() == Ok(name) {
        let client = Client::new(
            std::env::var("HTTP_CLIENT_TEST_ORIGIN").unwrap(),
            hostname_options(),
        );
        let result = client.post(b"verified proxy, hostname-only origin");
        if std::env::var("HTTP_CLIENT_TEST_PROXY_ACCEPT").as_deref() == Ok("true") {
            assert_eq!(200, result.unwrap().status);
        } else {
            assert_eq!(
                ffi::EasyErrorKind::PeerVerification,
                ffi::easy_error_kind(result.unwrap_err().transport_code)
            );
        }
        return;
    }
    for (cert, key, wrong_name, accepted) in [
        (
            include_bytes!("testdata/native-ca-localhost.der").as_slice(),
            include_bytes!("testdata/native-ca-localhost-key.der").as_slice(),
            false,
            true,
        ),
        (
            include_bytes!("testdata/good.der").as_slice(),
            include_bytes!("testdata/good-key.der").as_slice(),
            false,
            false,
        ),
        (
            include_bytes!("testdata/native-ca-localhost.der").as_slice(),
            include_bytes!("testdata/native-ca-localhost-key.der").as_slice(),
            true,
            false,
        ),
    ] {
        let origin = TlsServer::new(
            include_bytes!("testdata/good.der"),
            include_bytes!("testdata/good-key.der"),
            vec![],
        );
        let proxy = HttpsProxy::new(cert, key, &origin.url);
        let proxy_url = if wrong_name {
            proxy.url.replace("localhost", "127.0.0.1")
        } else {
            proxy.url.clone()
        };
        let mut child = native_ca_test_process(name);
        child
            .env("https_proxy", proxy_url)
            .env("HTTP_CLIENT_TEST_ORIGIN", &origin.url)
            .env("HTTP_CLIENT_TEST_PROXY_ACCEPT", accepted.to_string());
        assert_child_succeeded(&mut child);
        assert_eq!(
            usize::from(accepted),
            proxy.connects.load(std::sync::atomic::Ordering::Relaxed)
        );
        let delivered = origin.delivered();
        assert_eq!(usize::from(accepted), delivered.len());
        if accepted {
            assert!(delivered[0].ends_with(b"verified proxy, hostname-only origin"));
        }
    }
}
