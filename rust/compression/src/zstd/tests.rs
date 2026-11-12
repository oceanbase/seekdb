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

fn sample(len: usize) -> Vec<u8> {
    let mut state = 0x1234_5678u32;
    (0..len)
        .map(|index| {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            if index % 7 == 0 {
                state as u8
            } else {
                (index % 31) as u8
            }
        })
        .collect()
}

fn encode_stream(input: &[u8], input_chunk: usize, output_chunk: usize) -> Vec<u8> {
    let mut encoder = Encoder::default();
    let mut encoded = Vec::new();
    let mut output = vec![0; output_chunk];
    for chunk in input.chunks(input_chunk) {
        let mut consumed = 0;
        while consumed < chunk.len() {
            let progress = encoder
                .process(&chunk[consumed..], &mut output[..], Directive::Continue)
                .unwrap();
            assert!(!progress.finished);
            assert!(progress.consumed > 0 || progress.produced > 0);
            consumed += progress.consumed;
            encoded.extend_from_slice(&output[..progress.produced]);
        }
    }
    loop {
        let progress = encoder
            .process(&[], &mut output[..], Directive::Finish)
            .unwrap();
        encoded.extend_from_slice(&output[..progress.produced]);
        if progress.finished {
            return encoded;
        }
        assert!(progress.produced > 0);
    }
}

fn decode_stream(input: &[u8], input_chunk: usize, output_chunk: usize) -> Result<Vec<u8>, Error> {
    let mut decoder = Decoder::default();
    let mut decoded = Vec::new();
    let mut output = vec![0; output_chunk];
    for chunk in input.chunks(input_chunk) {
        let mut consumed = 0;
        while consumed < chunk.len() {
            let progress = decoder.process(&chunk[consumed..], &mut output[..])?;
            consumed += progress.consumed;
            decoded.extend_from_slice(&output[..progress.produced]);
            assert!(progress.consumed > 0 || progress.produced > 0);
        }
    }
    loop {
        let progress = decoder.process(&[], &mut output[..])?;
        decoded.extend_from_slice(&output[..progress.produced]);
        if progress.finished || progress.produced == 0 {
            break;
        }
    }
    decoder.finish()?;
    Ok(decoded)
}

#[test]
fn batch_round_trip_and_empty_frame() {
    for source in [Vec::new(), b"hello zstd".to_vec(), sample(131_072)] {
        let mut compressed = vec![0; compress_bound(source.len())];
        let size = compress(&source, &mut compressed).unwrap();
        compressed.truncate(size);
        assert_eq!(&compressed[..4], &[0x28, 0xb5, 0x2f, 0xfd]);
        let mut decoded = vec![0; source.len()];
        assert_eq!(decompress(&compressed, &mut decoded).unwrap(), source.len());
        assert_eq!(decoded, source);
        assert_eq!(decode_stream(&compressed, 7, 11).unwrap(), source);
    }
}

#[test]
fn vec_capacity_commits_only_written_bytes() {
    let source = sample(4096);
    let mut encoded = Vec::with_capacity(compress_bound(source.len()));
    assert_eq!(encoded.len(), 0);
    let size = compress(&source, &mut encoded).unwrap();
    assert_eq!(encoded.len(), size);
    let mut decoded = Vec::with_capacity(source.len());
    assert_eq!(decompress(&encoded, &mut decoded).unwrap(), source.len());
    assert_eq!(decoded, source);

    let mut encoder = Encoder::default();
    let mut encoded = Vec::new();
    let mut chunk = Vec::with_capacity(7);
    let mut consumed = 0;
    loop {
        let progress = encoder
            .process(&source[consumed..], &mut chunk, Directive::Finish)
            .unwrap();
        assert_eq!(chunk.len(), progress.produced);
        encoded.extend_from_slice(&chunk);
        consumed += progress.consumed;
        if progress.finished {
            break;
        }
        assert!(progress.consumed > 0 || progress.produced > 0);
    }
    assert_eq!(consumed, source.len());
    let mut decoder = Decoder::default();
    let mut decoded = Vec::new();
    consumed = 0;
    loop {
        let progress = decoder.process(&encoded[consumed..], &mut chunk).unwrap();
        assert_eq!(chunk.len(), progress.produced);
        decoded.extend_from_slice(&chunk);
        consumed += progress.consumed;
        if progress.finished {
            break;
        }
        assert!(progress.consumed > 0 || progress.produced > 0);
    }
    decoder.finish().unwrap();
    assert_eq!(consumed, encoded.len());
    assert_eq!(decoded, source);
}

#[test]
fn batch_capacity_errors_require_reset() {
    let source = sample(4096);
    let mut encoder = Encoder::default();
    assert_eq!(
        encoder.encode_frame(&source, &mut [][..]).unwrap_err().kind,
        ErrorKind::OutputTooSmall
    );
    let mut compressed = vec![0; compress_bound(source.len())];
    assert_eq!(
        encoder
            .encode_frame(&source, &mut compressed)
            .unwrap_err()
            .kind,
        ErrorKind::InvalidState
    );
    encoder.reset().unwrap();
    let size = encoder.encode_frame(&source, &mut compressed).unwrap();
    compressed.truncate(size);

    let mut decoder = Decoder::default();
    assert_eq!(
        decoder
            .decode_frame(&compressed, &mut [0; 1][..])
            .unwrap_err()
            .kind,
        ErrorKind::OutputTooSmall
    );
    let mut decoded = vec![0; source.len()];
    assert_eq!(
        decoder
            .decode_frame(&compressed, &mut decoded)
            .unwrap_err()
            .kind,
        ErrorKind::InvalidState
    );
    decoder.reset().unwrap();
    assert_eq!(
        decoder.decode_frame(&compressed, &mut decoded).unwrap(),
        source.len()
    );
    assert_eq!(decoded, source);
}

#[test]
fn streams_progress_with_one_byte_output() {
    let source = sample(8192);
    for input_chunk in [1, 97, source.len()] {
        let encoded = encode_stream(&source, input_chunk, 1);
        let mut batch_decoded = vec![0; source.len()];
        assert_eq!(
            decompress(&encoded, &mut batch_decoded).unwrap(),
            source.len()
        );
        assert_eq!(batch_decoded, source);
        for compressed_chunk in [1, 17, encoded.len()] {
            assert_eq!(
                decode_stream(&encoded, compressed_chunk, 1).unwrap(),
                source
            );
        }
    }
    assert_eq!(decode_stream(&encode_stream(&[], 1, 1), 1, 1).unwrap(), []);
}

#[test]
fn finish_consumes_final_input_and_is_sticky() {
    let source = sample(8192);
    let mut encoder = Encoder::default();
    let mut output = [0; 1];
    let mut encoded = Vec::new();
    let mut consumed = 0;
    let first = encoder
        .process(&source, &mut [][..], Directive::Finish)
        .unwrap();
    assert_eq!(first, Progress::default());
    assert_eq!(
        encoder
            .process(&source, &mut output[..], Directive::Continue)
            .unwrap_err()
            .kind,
        ErrorKind::InvalidState
    );
    loop {
        let progress = encoder
            .process(&source[consumed..], &mut output[..], Directive::Finish)
            .unwrap();
        consumed += progress.consumed;
        encoded.extend_from_slice(&output[..progress.produced]);
        if progress.finished {
            break;
        }
        assert!(progress.consumed > 0 || progress.produced > 0);
    }
    assert_eq!(consumed, source.len());
    assert_eq!(decode_stream(&encoded, 3, 7).unwrap(), source);
    assert_eq!(
        encoder
            .process(&[], &mut output[..], Directive::Finish)
            .unwrap_err()
            .kind,
        ErrorKind::InvalidState
    );
    encoder.reset().unwrap();
    assert!(
        encoder
            .process(&[], &mut [0; 32][..], Directive::Finish)
            .unwrap()
            .finished
    );
}

#[test]
fn concatenated_and_skippable_frames() {
    let first = sample(1024);
    let second = sample(2031);
    let mut encoded = encode_stream(&first, 11, 7);
    // A standard skippable frame with a three-byte payload.
    encoded.extend_from_slice(&[0x50, 0x2a, 0x4d, 0x18, 3, 0, 0, 0, 1, 2, 3]);
    encoded.extend_from_slice(&encode_stream(&[], 1, 1));
    encoded.extend_from_slice(&encode_stream(&second, 31, 19));
    let mut expected = first;
    expected.extend_from_slice(&second);
    for input_chunk in [1, 5, encoded.len()] {
        assert_eq!(decode_stream(&encoded, input_chunk, 3).unwrap(), expected);
    }
    let mut decoded = vec![0; expected.len()];
    assert_eq!(decompress(&encoded, &mut decoded).unwrap(), expected.len());
    assert_eq!(decoded, expected);
}

#[test]
fn truncation_at_every_offset_is_rejected() {
    let encoded = encode_stream(&sample(137), 7, 11);
    for end in 0..encoded.len() {
        assert!(
            decode_stream(&encoded[..end], 3, 5).is_err(),
            "accepted prefix {end}"
        );
    }
}

#[test]
fn invalid_frame_and_checksum_failure_recover_after_reset() {
    let source = sample(4096);
    let mut context = CCtx::try_create().unwrap();
    context
        .set_parameter(CParameter::ChecksumFlag(true))
        .unwrap();
    let mut encoded = vec![0; compress_bound(source.len())];
    let size = context.compress2(&mut encoded[..], &source).unwrap();
    encoded.truncate(size);
    *encoded.last_mut().unwrap() ^= 1;
    assert_eq!(
        decode_stream(&encoded, 17, 23).unwrap_err().kind,
        ErrorKind::InvalidData
    );

    let mut decoder = Decoder::default();
    assert_eq!(
        decoder
            .process(b"invalid zstd", &mut [0; 32][..])
            .unwrap_err()
            .kind,
        ErrorKind::InvalidData
    );
    assert_eq!(
        decoder.process(&[], &mut [0; 32][..]).unwrap_err().kind,
        ErrorKind::InvalidState
    );
    decoder.reset().unwrap();
    let valid = encode_stream(b"reset worked", 3, 3);
    let mut output = [0; 32];
    let progress = decoder.process(&valid, &mut output[..]).unwrap();
    assert!(progress.finished);
    assert_eq!(&output[..progress.produced], b"reset worked");
    decoder.finish().unwrap();
    decoder.finish().unwrap();
    assert_eq!(
        decoder.process(&valid, &mut output[..]).unwrap_err().kind,
        ErrorKind::InvalidState
    );
}

#[test]
fn zero_capacity_does_not_consume_or_finish_input() {
    let encoded = encode_stream(b"zero capacity", 2, 3);
    let mut decoder = Decoder::default();
    assert_eq!(
        decoder.process(&encoded, &mut [][..]).unwrap(),
        Progress::default()
    );
    assert_eq!(
        decoder.finish().unwrap_err().kind,
        ErrorKind::TruncatedInput
    );
    decoder.reset().unwrap();
    let mut output = [0; 32];
    assert!(decoder.process(&encoded, &mut output[..]).unwrap().finished);
    assert!(decoder.process(&[], &mut [][..]).unwrap().finished);
    decoder.finish().unwrap();
}

#[test]
fn window_limit_and_parameter_validation() {
    assert!(Encoder::new(zstd_safe::max_c_level() + 1).is_err());
    assert!(Encoder::new(zstd_safe::min_c_level() - 1).is_err());
    assert!(Decoder::new(9).is_err());
    assert!(Decoder::new(u32::MAX).is_err());
    assert!(Decoder::new(0).is_ok());
    let encoded = encode_stream(&sample(200_000), 8192, 4096);
    let mut decoder = Decoder::new(10).unwrap();
    assert_eq!(
        decoder
            .process(&encoded, &mut [0; 128][..])
            .unwrap_err()
            .kind,
        ErrorKind::ResourceLimit
    );
}

#[test]
fn contexts_own_workspace_and_reuse_after_reset() {
    let source = sample(1024);
    let mut encoder = Encoder::default();
    let mut decoder = Decoder::default();
    assert_eq!(encoder.memory_usage(), 0);
    assert_eq!(decoder.memory_usage(), 0);
    for _ in 0..3 {
        let mut encoded = vec![0; compress_bound(source.len())];
        let size = encoder.encode_frame(&source, &mut encoded).unwrap();
        let mut decoded = vec![0; source.len()];
        assert_eq!(
            decoder
                .decode_frame(&encoded[..size], &mut decoded)
                .unwrap(),
            source.len()
        );
        assert_eq!(decoded, source);
        assert!(encoder.memory_usage() > 0);
        assert!(decoder.memory_usage() > 0);
        encoder.reset().unwrap();
        decoder.reset().unwrap();
    }
}

#[test]
fn independent_contexts_work_on_concurrent_threads() {
    let workers: Vec<_> = (0..8)
        .map(|index| {
            std::thread::spawn(move || {
                let source = sample(4096 + index * 67);
                for _ in 0..8 {
                    let encoded = encode_stream(&source, 97, 13);
                    assert_eq!(decode_stream(&encoded, 17, 23).unwrap(), source);
                    let mut batch = vec![0; compress_bound(source.len())];
                    let size = compress(&source, &mut batch).unwrap();
                    let mut decoded = vec![0; source.len()];
                    assert_eq!(
                        decompress(&batch[..size], &mut decoded).unwrap(),
                        source.len()
                    );
                    assert_eq!(decoded, source);
                }
            })
        })
        .collect();
    for worker in workers {
        worker.join().unwrap();
    }
}
