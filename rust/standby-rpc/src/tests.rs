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

#[test]
fn simple_responses_use_typed_protobuf_fields() {
    use prost::Message;
    let frame = count_frame(i64::MAX).unwrap();
    assert!(frame.buf.is_empty());
    let wire = count_response(frame).unwrap().encode_to_vec();
    let decoded = GetLsViewTabletCountRes::decode(wire.as_slice()).unwrap();
    assert_eq!(decoded.tablet_count, i64::MAX);
    assert!(count_frame(0).is_err());
    assert!(count_frame(-1).is_err());

    let expected = CheckRestorePreconditionRes {
        required_disk_size: i64::MAX,
        total_tablet_size: 1024,
        data_version: u64::MAX,
    };
    let frame = precondition_frame(expected).unwrap();
    assert!(frame.buf.is_empty());
    let wire = precondition_response(frame).unwrap().encode_to_vec();
    assert_eq!(
        CheckRestorePreconditionRes::decode(wire.as_slice()).unwrap(),
        expected
    );
    assert!(precondition_frame(CheckRestorePreconditionRes {
        required_disk_size: -1,
        ..expected
    })
    .is_err());
}

fn node(ip: &str, port: u32) -> NodeAddress {
    NodeAddress {
        ip: ip.into(),
        port,
    }
}

#[test]
fn promotion_boundary_round_trips_ipv4_ipv6_and_large_scn() {
    use prost::Message;
    let expected = GetPromotionBoundaryRes {
        origin: Some(node("127.0.0.1", 42000)),
        cutover_scn: (1_u64 << 60) + 123,
        source_chain: vec![SourceHop {
            relay: Some(node("::1", 42002)),
            source: Some(node("127.0.0.1", 42000)),
        }],
    };
    let frame = boundary_frame(expected.clone()).unwrap();
    assert!(frame.buf.is_empty());
    let wire = boundary_response(frame).unwrap().encode_to_vec();
    assert_eq!(
        GetPromotionBoundaryRes::decode(wire.as_slice()).unwrap(),
        expected
    );
    let request = GetPromotionBoundaryReq {
        visited: vec![node("127.0.0.1", 42000), node("::1", 42001)],
    };
    let frame = boundary_request_frame(request.clone()).unwrap();
    assert!(frame.buf.is_empty());
    assert_eq!(boundary_request(frame).unwrap(), request);
}

#[test]
fn invalid_promotion_metadata_is_rejected() {
    assert!(boundary_request_frame(GetPromotionBoundaryReq {
        visited: vec![node("::1", 1), node("0:0:0:0:0:0:0:1", 1)]
    })
    .is_err());
    assert!(boundary_request_frame(GetPromotionBoundaryReq {
        visited: (1..=17).map(|port| node("127.0.0.1", port)).collect()
    })
    .is_err());
    for value in [
        node("not-an-ip", 1),
        node("127.0.0.1", 0),
        node("::1", 65536),
    ] {
        assert!(boundary_request_frame(GetPromotionBoundaryReq {
            visited: vec![value]
        })
        .is_err());
    }
    assert!(boundary_frame(GetPromotionBoundaryRes {
        origin: None,
        cutover_scn: 1,
        source_chain: vec![]
    })
    .is_err());
    assert!(boundary_frame(GetPromotionBoundaryRes {
        origin: Some(node("::1", 1)),
        cutover_scn: 0,
        source_chain: vec![]
    })
    .is_err());
    assert!(boundary_frame(GetPromotionBoundaryRes {
        origin: Some(node("::1", 1)),
        cutover_scn: 1,
        source_chain: vec![SourceHop {
            relay: Some(node("::1", 1)),
            source: None
        }]
    })
    .is_err());
    assert!(boundary_frame(GetPromotionBoundaryRes {
        origin: Some(node("::1", 1)),
        cutover_scn: 1,
        source_chain: vec![SourceHop {
            relay: Some(node("::1", 1)),
            source: Some(node("::1", 1))
        }]
    })
    .is_err());
}
