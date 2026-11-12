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

const NATIVE_ZLIB_LEVEL6: &[u8] = &[
    0x78, 0x9c, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x28, 0xcf, 0x2f, 0xca, 0x49, 0xe1, 0x02, 0x00,
    0x1e, 0x72, 0x04, 0x67,
];

fn data(size: usize) -> Vec<u8> {
    (0..size)
        .map(|index| ((index * 37 + index / 11) % 251) as u8)
        .collect()
}

#[test]
fn reads_native_zlib_and_accepts_trailing_bytes() {
    let mut output = [0u8; 32];
    assert_eq!(
        12,
        zlib_decompress(NATIVE_ZLIB_LEVEL6, &mut output).unwrap()
    );
    assert_eq!(b"hello world\n", &output[..12]);

    let mut trailing = NATIVE_ZLIB_LEVEL6.to_vec();
    trailing.extend_from_slice(b"trailing");
    assert_eq!(12, zlib_decompress(&trailing, &mut output).unwrap());
    assert_eq!(b"hello world\n", &output[..12]);
}

#[test]
fn zlib_round_trips_all_storage_levels() {
    for level in [-1, 0, 1, 6, 9] {
        for size in [1, 8, 4096, 65536, 100_000] {
            let input = data(size);
            let mut compressed = vec![0; zlib_compress_bound(input.len())];
            let compressed_size = zlib_compress(&input, &mut compressed, level).unwrap();
            let mut output = vec![0; input.len()];
            assert_eq!(
                input.len(),
                zlib_decompress(&compressed[..compressed_size], &mut output).unwrap()
            );
            assert_eq!(input, output);
        }
    }
    assert_eq!(
        Error::InvalidCompressionLevel,
        zlib_compress(b"x", &mut [0; 32], 10).unwrap_err()
    );
}

#[test]
fn rejects_truncated_corrupt_and_small_buffers() {
    let mut output = [0u8; 32];
    assert_eq!(
        Error::InvalidData,
        zlib_decompress(&NATIVE_ZLIB_LEVEL6[..8], &mut output).unwrap_err()
    );
    let mut corrupt = NATIVE_ZLIB_LEVEL6.to_vec();
    corrupt[5] ^= 0xff;
    assert_eq!(
        Error::InvalidData,
        zlib_decompress(&corrupt, &mut output).unwrap_err()
    );
    assert_eq!(
        Error::OutputTooSmall,
        zlib_decompress(NATIVE_ZLIB_LEVEL6, &mut [0; 4]).unwrap_err()
    );
    assert_eq!(
        Error::OutputTooSmall,
        zlib_compress(b"hello world\n", &mut [0; 2], 6).unwrap_err()
    );
}

#[test]
fn checksum_matches_standard_vector() {
    assert_eq!(0xcbf4_3926, crc32(b"123456789"));
    assert_eq!(0, crc32(b""));
}

#[test]
fn gzip_streaming_handles_small_buffers_and_reuse() {
    let input = data(200_000);
    let mut encoder = GzipEncoder::new(GZIP_COMPRESSION_LEVEL).unwrap();
    let mut encoded = Vec::new();

    for chunk in input.chunks(997) {
        let mut offset = 0;
        loop {
            let mut output = [0u8; 113];
            let progress = encoder
                .process(&chunk[offset..], &mut output, false)
                .unwrap();
            offset += progress.consumed;
            encoded.extend_from_slice(&output[..progress.produced]);
            if progress.drained {
                assert_eq!(chunk.len(), offset);
                break;
            }
            assert!(progress.consumed > 0 || progress.produced > 0);
        }
    }
    loop {
        let mut output = [0u8; 113];
        let progress = encoder.process(&[], &mut output, true).unwrap();
        encoded.extend_from_slice(&output[..progress.produced]);
        if progress.stream_end {
            break;
        }
        assert!(progress.produced > 0);
    }

    let mut decoder = StreamDecoder::new();
    let mut decoded = vec![0u8; input.len() + 1];
    let mut input_offset = 0;
    let mut output_offset = 0;
    while input_offset < encoded.len() {
        let progress = decoder
            .process(&encoded[input_offset..], &mut decoded[output_offset..])
            .unwrap();
        input_offset += progress.consumed;
        output_offset += progress.produced;
        if progress.stream_end {
            break;
        }
        assert!(progress.consumed > 0 || progress.produced > 0);
    }
    assert_eq!(input.len(), output_offset);
    assert_eq!(input, decoded[..output_offset]);

    encoder.reset();
    let mut second = vec![0u8; 128];
    let mut offset = 0;
    loop {
        let mut output = [0u8; 32];
        let progress = encoder.process(&[], &mut output, true).unwrap();
        if offset + progress.produced > second.len() {
            second.resize(offset + progress.produced, 0);
        }
        second[offset..offset + progress.produced].copy_from_slice(&output[..progress.produced]);
        offset += progress.produced;
        if progress.stream_end {
            break;
        }
    }
    second.truncate(offset);
    assert_eq!([0x1f, 0x8b], second[..2]);
    assert_eq!(
        Error::InvalidData,
        zlib_decompress(&encoded[..2], &mut [0; 1]).unwrap_err()
    );
}

#[test]
fn stream_decoder_accepts_zlib_and_concatenated_gzip_members() {
    let mut output = [0u8; 32];
    let mut decoder = StreamDecoder::new();
    let progress = decoder.process(NATIVE_ZLIB_LEVEL6, &mut output).unwrap();
    assert!(progress.stream_end);
    assert_eq!(b"hello world\n", &output[..progress.produced]);

    let mut encoder = GzipEncoder::new(GZIP_COMPRESSION_LEVEL).unwrap();
    let mut member = Vec::new();
    let mut offset = 0;
    loop {
        let mut buffer = [0u8; 64];
        let progress = encoder.process(b"hello", &mut buffer, true).unwrap();
        member.extend_from_slice(&buffer[..progress.produced]);
        offset += progress.consumed;
        if progress.stream_end {
            break;
        }
        assert!(progress.consumed > 0 || progress.produced > 0);
    }
    assert_eq!(5, offset);
    let mut concatenated = member.clone();
    concatenated.extend_from_slice(&member);

    let mut decoded = [0u8; 16];
    let first = decoder.process(&concatenated, &mut decoded).unwrap();
    assert!(first.stream_end);
    assert_eq!(b"hello", &decoded[..first.produced]);
    let second = decoder
        .process(&concatenated[first.consumed..], &mut decoded)
        .unwrap();
    assert!(second.stream_end);
    assert_eq!(b"hello", &decoded[..second.produced]);
    let final_poll = decoder.process(&[], &mut decoded).unwrap();
    assert!(final_poll.stream_end);
    assert_eq!(0, final_poll.consumed);
    assert_eq!(0, final_poll.produced);

    let third = decoder.process(NATIVE_ZLIB_LEVEL6, &mut decoded).unwrap();
    assert!(third.stream_end);
    assert_eq!(b"hello world\n", &decoded[..third.produced]);
}

#[test]
fn strict_zlib_decoder_accepts_exact_and_larger_output_buffers() {
    let mut exact = [0; 12];
    assert_eq!(
        exact.len(),
        zlib_decompress_complete(NATIVE_ZLIB_LEVEL6, &mut exact).unwrap()
    );
    assert_eq!(b"hello world\n", &exact);

    let mut larger = [0xab; 32];
    let produced = zlib_decompress_complete(NATIVE_ZLIB_LEVEL6, &mut larger).unwrap();
    assert_eq!(12, produced);
    assert_eq!(b"hello world\n", &larger[..produced]);
    assert!(larger[produced..].iter().all(|&byte| byte == 0xab));
}

#[test]
fn strict_zlib_decoder_rejects_every_truncation_and_trailing_input() {
    for end in 0..NATIVE_ZLIB_LEVEL6.len() {
        assert!(zlib_decompress_complete(&NATIVE_ZLIB_LEVEL6[..end], &mut [0; 32]).is_err());
    }
    for suffix in [b"trailing".as_slice(), NATIVE_ZLIB_LEVEL6] {
        let mut input = NATIVE_ZLIB_LEVEL6.to_vec();
        input.extend_from_slice(suffix);
        assert_eq!(
            Error::InvalidData,
            zlib_decompress_complete(&input, &mut [0; 64]).unwrap_err()
        );
    }
}

#[test]
fn strict_zlib_decoder_rejects_corrupt_data_and_small_output() {
    for index in [5, NATIVE_ZLIB_LEVEL6.len() - 1] {
        let mut corrupt = NATIVE_ZLIB_LEVEL6.to_vec();
        corrupt[index] ^= 0xff;
        assert!(zlib_decompress_complete(&corrupt, &mut [0; 32]).is_err());
    }
    assert_eq!(
        Error::OutputTooSmall,
        zlib_decompress_complete(NATIVE_ZLIB_LEVEL6, &mut [0; 4]).unwrap_err()
    );
}

#[test]
fn strict_zlib_decoder_accepts_an_empty_stream() {
    const EMPTY: &[u8] = &[0x78, 0x9c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01];
    assert_eq!(0, zlib_decompress_complete(EMPTY, &mut []).unwrap());
    let mut output = [0xab];
    assert_eq!(0, zlib_decompress_complete(EMPTY, &mut output).unwrap());
    assert_eq!([0xab], output);
}

#[test]
fn strict_zlib_decoder_rejects_dictionaries_and_gzip() {
    // RFC1950 FDICT header with dictionary Adler32 1.
    const DICTIONARY: &[u8] = &[0x78, 0xbb, 0x00, 0x00, 0x00, 0x01];
    assert_eq!(
        Error::Inflate(InflateError::NeedDict { dict_id: 1 }),
        zlib_decompress_complete(DICTIONARY, &mut [0; 32]).unwrap_err()
    );

    let mut encoded = [0; 128];
    let progress = GzipEncoder::default()
        .process(b"hello", &mut encoded, true)
        .unwrap();
    assert!(progress.stream_end);
    assert!(zlib_decompress_complete(&encoded[..progress.produced], &mut [0; 32]).is_err());
    let decoded = StreamDecoder::new()
        .process(&encoded[..progress.produced], &mut [0; 32])
        .unwrap();
    assert!(decoded.stream_end);
    assert_eq!(5, decoded.produced);
}

#[test]
fn gzip_encoder_rejects_invalid_levels() {
    for level in [-2, 10] {
        assert!(matches!(
            GzipEncoder::new(level),
            Err(Error::InvalidCompressionLevel)
        ));
    }
}

#[test]
fn decoder_handles_empty_and_fragmented_gzip_members() {
    let mut encoder = GzipEncoder::default();
    let mut encoded = [0; 128];
    let empty = encoder.process(&[], &mut encoded, true).unwrap();
    assert!(empty.stream_end);

    let mut decoder = StreamDecoder::new();
    let decoded = decoder
        .process(&encoded[..empty.produced], &mut [0; 1])
        .unwrap();
    assert!(decoded.stream_end);
    assert_eq!(0, decoded.produced);

    encoder.reset();
    let full = encoder.process(b"fragmented", &mut encoded, true).unwrap();
    assert!(full.stream_end);
    let mut output = Vec::new();
    let mut ended = false;
    for chunk in encoded[..full.produced].chunks(3) {
        let mut offset = 0;
        loop {
            let mut buffer = [0; 2];
            let progress = decoder.process(&chunk[offset..], &mut buffer).unwrap();
            offset += progress.consumed;
            output.extend_from_slice(&buffer[..progress.produced]);
            ended |= progress.stream_end;
            if offset == chunk.len() && progress.produced < buffer.len() {
                break;
            }
            assert!(progress.consumed > 0 || progress.produced > 0);
        }
    }
    assert!(ended);
    assert_eq!(b"fragmented", output.as_slice());

    let mut output = [0; 32];
    let decoded = decoder.process(NATIVE_ZLIB_LEVEL6, &mut output).unwrap();
    assert!(decoded.stream_end);
    assert_eq!(b"hello world\n", &output[..decoded.produced]);
}
