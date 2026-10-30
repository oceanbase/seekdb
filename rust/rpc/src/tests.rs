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

#[test]
fn database_error_codes_survive_transport() {
    for error in [-4002, -4007, -4012, -4018, -4072, -4121, -4016, -12345] {
        assert_eq!(status_error(&error_status(error)), error);
    }
}

#[test]
fn transport_errors_map_to_database_errors() {
    for (status, error) in [
        (Status::ok(""), 0),
        (Status::deadline_exceeded(""), -4012),
        (Status::cancelled(""), -4072),
        (Status::unimplemented(""), -4007),
        (Status::invalid_argument(""), -4002),
        (Status::not_found(""), -4018),
        (Status::unavailable("connection closed"), RPC_ERROR),
    ] {
        assert_eq!(status_error(&status), error);
    }
    for message in [
        "OB_ERROR:0",
        "OB_ERROR:1",
        "OB_ERROR:-4012garbage",
        "OB_ERROR:999999999999",
    ] {
        assert_eq!(status_error(&Status::internal(message)), RPC_ERROR);
    }
}

#[test]
fn response_queue_transfers_buffer_ownership() {
    let (sender, mut receiver) = mpsc::channel(2);
    let writer = Writer { sender };
    let mut frame = Frame::empty();
    frame.buf = vec![42; 4096];
    let data = frame.buf.as_ptr();
    assert!(writer.write(frame));
    let received = receiver.blocking_recv().unwrap().unwrap();
    assert_eq!(received.buf.as_ptr(), data);
    assert_eq!(received.buf.len(), 4096);
    drop(receiver);
    assert!(writer.cancelled());
    assert!(!writer.write(Frame::empty()));
}
