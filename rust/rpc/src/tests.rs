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

use super::*;
use std::{
    convert::Infallible,
    future::{ready, Ready},
    task::{Context, Poll},
};
use tonic::codegen::{
    http::{Request, Response},
    Service,
};

#[derive(Clone)]
struct TestService;
impl tonic::server::NamedService for TestService {
    const NAME: &'static str = "test.Transport";
}
impl Service<Request<tonic::body::Body>> for TestService {
    type Response = Response<tonic::body::Body>;
    type Error = Infallible;
    type Future = Ready<Result<Self::Response, Self::Error>>;
    fn poll_ready(&mut self, _: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }
    fn call(&mut self, _: Request<tonic::body::Body>) -> Self::Future {
        ready(Ok(Response::new(tonic::body::Body::empty())))
    }
}

#[test]
fn shutdown_waits_for_registered_blocking_work() {
    let active = Arc::new(Dispatches::default());
    let work = active.enter();
    let mut server = start_server(0, false, TestService, active).unwrap();
    assert_eq!(server.cert_expire_time(), 0);
    let (finished, receiver) = std::sync::mpsc::channel();
    let thread = std::thread::spawn(move || {
        server.stop();
        finished.send(()).unwrap();
    });
    assert!(receiver.recv_timeout(Duration::from_millis(50)).is_err());
    drop(work);
    receiver.recv_timeout(Duration::from_secs(5)).unwrap();
    thread.join().unwrap();
}

#[test]
fn deadline_covers_a_pending_operation() {
    runtime().block_on(async {
        let result: Result<(), tonic::Status> =
            with_timeout(Duration::from_millis(5), std::future::pending()).await;
        assert_eq!(result.unwrap_err().code(), tonic::Code::DeadlineExceeded);
        assert_eq!(
            with_timeout(Duration::from_secs(1), async { Ok(42) })
                .await
                .unwrap(),
            42
        );
    });
}
