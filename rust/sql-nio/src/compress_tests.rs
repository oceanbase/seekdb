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

const PLAIN: &[u8] = b"hello world\n";
const C_ZLIB: &[u8] = &[
    0x78, 0x9c, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x28, 0xcf, 0x2f, 0xca, 0x49, 0xe1, 0x02, 0x00,
    0x1e, 0x72, 0x04, 0x67,
];
const GZIP: &[u8] = &[
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57,
    0x28, 0xcf, 0x2f, 0xca, 0x49, 0xe1, 0x02, 0x00, 0x2d, 0x3b, 0x08, 0xaf, 0x0c, 0x00, 0x00, 0x00,
];
const DICTIONARY_ZLIB: &[u8] = &[
    0x78, 0xbb, 0x1e, 0x72, 0x04, 0x67, 0xcb, 0x40, 0x62, 0x03, 0x00, 0x1e, 0x72, 0x04, 0x67,
];

fn wire_frame(payload: &[u8], plain_len: usize, seq: u8) -> Vec<u8> {
    let size = payload.len();
    assert!(size <= MAX_COMPRESS_BODY_LEN && plain_len <= MAX_COMPRESS_BODY_LEN);
    let mut frame = vec![
        size as u8,
        (size >> 8) as u8,
        (size >> 16) as u8,
        seq,
        plain_len as u8,
        (plain_len >> 8) as u8,
        (plain_len >> 16) as u8,
    ];
    frame.extend_from_slice(payload);
    frame
}

fn packet(frame: &[u8], at: usize, max_plain: usize) -> (usize, u8, Vec<u8>) {
    match deframe_step_limited(frame, at, max_plain) {
        DeframeStep::Packet {
            consumed,
            seq,
            plain,
        } => (consumed, seq, plain),
        DeframeStep::NeedMore => panic!("complete frame unexpectedly needs more data"),
        DeframeStep::Bad => panic!("valid frame rejected"),
    }
}

fn reserve_failure() -> TryReserveError {
    Vec::<u8>::new().try_reserve_exact(usize::MAX).unwrap_err()
}

#[test]
fn decodes_standard_c_zlib_frame() {
    let frame = wire_frame(C_ZLIB, PLAIN.len(), 7);
    assert_eq!([20, 0, 0, 7, 12, 0, 0], frame[..COMPRESS_HEADER_SIZE]);
    let (consumed, seq, plain) = packet(&frame, 0, PLAIN.len());
    assert_eq!(frame.len(), consumed);
    assert_eq!(7, seq);
    assert_eq!(PLAIN, plain);
}

#[test]
fn compressed_and_plain_frames_preserve_prefix_offsets_and_sequence() {
    let prefix = b"prefix";
    let input = vec![b'x'; 8192];
    let mut frames = prefix.to_vec();
    let mut seq = 254;
    frame_into(&mut frames, &input, &mut seq).unwrap();
    assert_eq!(255, seq);
    assert!(frames.len() - prefix.len() < input.len());
    let first_end = frames.len();
    frame_into(&mut frames, b"small", &mut seq).unwrap();
    assert_eq!(0, seq);
    assert_eq!(prefix, &frames[..prefix.len()]);

    let (consumed, seen_seq, plain) = packet(&frames, prefix.len(), input.len());
    assert_eq!(first_end - prefix.len(), consumed);
    assert_eq!(254, seen_seq);
    assert_eq!(input, plain);
    let (consumed, seen_seq, plain) = packet(&frames, first_end, 5);
    assert_eq!(COMPRESS_HEADER_SIZE + 5, consumed);
    assert_eq!(255, seen_seq);
    assert_eq!(b"small", plain.as_slice());
    assert!(matches!(
        deframe_step_limited(&frames, frames.len(), 8192),
        DeframeStep::NeedMore
    ));
}

#[test]
fn incomplete_wire_headers_and_payloads_need_more_data() {
    let frame = wire_frame(C_ZLIB, PLAIN.len(), 1);
    for end in 0..frame.len() {
        assert!(
            matches!(
                deframe_step_limited(&frame[..end], 0, PLAIN.len()),
                DeframeStep::NeedMore
            ),
            "prefix length {end}"
        );
    }
}

#[test]
fn rejects_invalid_offsets_zero_payloads_and_plaintext_budgets() {
    assert!(matches!(
        deframe_step_limited(&[], 1, 100),
        DeframeStep::Bad
    ));
    assert!(matches!(
        deframe_step_limited(&[0; COMPRESS_HEADER_SIZE], 0, 100),
        DeframeStep::Bad
    ));
    for frame in [wire_frame(C_ZLIB, PLAIN.len(), 1), wire_frame(PLAIN, 0, 1)] {
        assert!(matches!(
            deframe_step_limited(&frame, 0, PLAIN.len() - 1),
            DeframeStep::Bad
        ));
    }
    let oversized = wire_frame(C_ZLIB, MAX_COMPRESS_BODY_LEN, 1);
    assert!(matches!(
        deframe_step_limited(&oversized[..COMPRESS_HEADER_SIZE], 0, 1024),
        DeframeStep::Bad
    ));
}

#[test]
fn rejects_incorrect_announced_output_lengths() {
    for announced in [1, 4, PLAIN.len() - 1, PLAIN.len() + 1, 100] {
        let frame = wire_frame(C_ZLIB, announced, 1);
        assert!(
            matches!(deframe_step_limited(&frame, 0, 100), DeframeStep::Bad),
            "announced length {announced}"
        );
    }
}

#[test]
fn rejects_truncated_corrupt_trailing_and_non_zlib_streams() {
    let mut corrupt = C_ZLIB.to_vec();
    *corrupt.last_mut().unwrap() ^= 1;
    let mut trailing = C_ZLIB.to_vec();
    trailing.extend_from_slice(b"trailing");
    let mut concatenated = C_ZLIB.to_vec();
    concatenated.extend_from_slice(C_ZLIB);
    for (name, payload) in [
        ("truncated", &C_ZLIB[..C_ZLIB.len() - 1]),
        ("checksum", corrupt.as_slice()),
        ("trailing", trailing.as_slice()),
        ("concatenated", concatenated.as_slice()),
        ("gzip", GZIP),
        ("dictionary", DICTIONARY_ZLIB),
    ] {
        let frame = wire_frame(payload, PLAIN.len(), 3);
        assert!(
            matches!(
                deframe_step_limited(&frame, 0, PLAIN.len()),
                DeframeStep::Bad
            ),
            "{name}"
        );
    }
}

#[test]
fn scratch_reservation_failure_falls_back_to_plaintext() {
    let input = vec![b'x'; 8192];
    assert!(deflate_if_smaller_with_reserve(&input, |_, _| Err(reserve_failure())).is_none());
    let mut frames = Vec::new();
    let mut seq = 12;
    frame_into_impl(
        &mut frames,
        &input,
        &mut seq,
        reserve_frame_output,
        |chunk| deflate_if_smaller_with_reserve(chunk, |_, _| Err(reserve_failure())),
    )
    .unwrap();
    assert_eq!(13, seq);
    assert_eq!([0, 0, 0], frames[4..COMPRESS_HEADER_SIZE]);
    assert_eq!(input, packet(&frames, 0, input.len()).2);
}

#[test]
fn short_and_incompressible_payloads_stay_uncompressed() {
    assert!(deflate_if_smaller(b"short").is_none());
    let mut state = 0x1234_5678u32;
    let input: Vec<u8> = (0..65536)
        .map(|_| {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            state as u8
        })
        .collect();
    let mut frames = Vec::new();
    let mut seq = 1;
    frame_into(&mut frames, &input, &mut seq).unwrap();
    assert_eq!([0, 0, 0], frames[4..COMPRESS_HEADER_SIZE]);
    assert_eq!(input, packet(&frames, 0, input.len()).2);
}

#[test]
fn framing_discards_a_compression_result_that_is_not_smaller() {
    let input = [b'x'; 100];
    let mut frames = Vec::new();
    let mut seq = 1;
    frame_into_impl(
        &mut frames,
        &input,
        &mut seq,
        reserve_frame_output,
        |chunk| Some(chunk.to_vec()),
    )
    .unwrap();
    assert_eq!([0, 0, 0], frames[4..COMPRESS_HEADER_SIZE]);
    assert_eq!(input.as_slice(), packet(&frames, 0, input.len()).2);
}

#[test]
fn maximum_payload_boundary_wraps_sequence_and_rolls_back_on_later_failure() {
    let input = vec![b'x'; MAX_COMPRESS_BODY_LEN + 1];
    let prefix = b"prefix";
    let mut frames = prefix.to_vec();
    let mut seq = 255;
    frame_into_impl(&mut frames, &input, &mut seq, reserve_frame_output, |_| {
        None
    })
    .unwrap();
    assert_eq!(1, seq);
    assert_eq!(
        [0xff, 0xff, 0xff, 255, 0, 0, 0],
        frames[prefix.len()..prefix.len() + COMPRESS_HEADER_SIZE]
    );
    let second = prefix.len() + COMPRESS_HEADER_SIZE + MAX_COMPRESS_BODY_LEN;
    assert_eq!(
        [1, 0, 0, 0, 0, 0, 0],
        frames[second..second + COMPRESS_HEADER_SIZE]
    );
    assert_eq!([b'x'], packet(&frames, second, 1).2.as_slice());

    frames.truncate(prefix.len());
    seq = 255;
    let mut calls = 0;
    let result = frame_into_impl(
        &mut frames,
        &input,
        &mut seq,
        |out, additional| {
            calls += 1;
            if calls == 2 {
                Err(reserve_failure())
            } else {
                reserve_frame_output(out, additional)
            }
        },
        |_| None,
    );
    assert!(result.is_err());
    assert_eq!(2, calls);
    assert_eq!(prefix, frames.as_slice());
    assert_eq!(255, seq);
}

#[test]
fn empty_input_preserves_output_and_sequence() {
    let mut frames = b"prefix".to_vec();
    let mut seq = 4;
    frame_into(&mut frames, &[], &mut seq).unwrap();
    assert_eq!(b"prefix", frames.as_slice());
    assert_eq!(4, seq);
}
