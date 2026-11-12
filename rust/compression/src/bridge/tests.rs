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

// SAFETY: callers pass only the prefix reported as written by a successful
// codec operation and do not mutate storage while the returned slice lives.
unsafe fn written(storage: &[MaybeUninit<u8>], len: usize) -> &[u8] {
    assert!(len <= storage.len());
    unsafe { slice::from_raw_parts(storage.as_ptr().cast(), len) }
}

#[test]
fn batch_writes_uninitialized_cpp_style_allocations() {
    let source = b"an uninitialized destination is writable without reading its bytes";
    let mut encoded = [MaybeUninit::<u8>::uninit(); 256];
    let mut decoded = [MaybeUninit::<u8>::uninit(); 128];
    // SAFETY: each destination is a disjoint live array exclusively held for
    // this call. Only successful result prefixes are subsequently read.
    unsafe {
        for algorithm in [2, 3] {
            let compressed = compress(
                algorithm,
                source.as_ptr(),
                source.len() as i64,
                encoded.as_mut_ptr().cast(),
                encoded.len() as i64,
            );
            assert_eq!(compressed.code, ffi::Code::Ok);
            let input = written(&encoded, compressed.size);
            let decompressed = decompress(
                algorithm,
                input.as_ptr(),
                input.len() as i64,
                decoded.as_mut_ptr().cast(),
                decoded.len() as i64,
            );
            assert_eq!(decompressed.code, ffi::Code::Ok);
            assert_eq!(written(&decoded, decompressed.size), source);
        }
    }
}

#[test]
fn streams_write_small_uninitialized_outputs() {
    let source: Vec<_> = (0..4096).map(|index| (index % 197) as u8).collect();
    let mut encoder = new_zstd_encoder();
    let mut decoder = new_zstd_decoder();
    let mut output = [MaybeUninit::<u8>::uninit(); 3];
    let mut encoded = Vec::new();
    let mut consumed = 0;
    // SAFETY: the destination is exclusively held and does not overlap either
    // source. Only successful produced prefixes are read after each call.
    unsafe {
        loop {
            let result = zstd_encode(
                &mut encoder,
                &source[consumed..],
                output.as_mut_ptr().cast(),
                output.len(),
                true,
            );
            assert_eq!(result.code, ffi::Code::Ok);
            consumed += result.consumed;
            encoded.extend_from_slice(written(&output, result.produced));
            if result.finished {
                break;
            }
            assert!(result.consumed > 0 || result.produced > 0);
        }
        assert_eq!(consumed, source.len());
        consumed = 0;
        let mut decoded = Vec::new();
        loop {
            let result = zstd_decode(
                &mut decoder,
                &encoded[consumed..],
                output.as_mut_ptr().cast(),
                output.len(),
            );
            assert_eq!(result.code, ffi::Code::Ok);
            consumed += result.consumed;
            decoded.extend_from_slice(written(&output, result.produced));
            if result.finished {
                break;
            }
            assert!(result.consumed > 0 || result.produced > 0);
        }
        assert_eq!(consumed, encoded.len());
        assert_eq!(zstd_decoder_finish(&mut decoder), ffi::Code::Ok);
        assert_eq!(decoded, source);
    }
}

#[test]
fn rejected_pointer_ranges_do_not_touch_sessions() {
    let source = b"range validation precedes native state changes";
    let mut encoded = Vec::with_capacity(zstd::compress_bound(source.len()));
    zstd::compress(source, &mut encoded).unwrap();
    let mut encoder = new_zstd_encoder();
    let mut decoder = new_zstd_decoder();
    let null = std::ptr::null_mut();
    // SAFETY: invalid pointers and overlap are rejected before a reference is
    // constructed or native code is invoked; valid retry output is disjoint.
    unsafe {
        assert_eq!(
            compress(2, source.as_ptr(), source.len() as i64, null, 1).code,
            ffi::Code::InvalidArgument
        );
        assert_eq!(
            decompress(2, encoded.as_ptr(), encoded.len() as i64, null, 1).code,
            ffi::Code::InvalidArgument
        );
        let result = zstd_encode(&mut encoder, source, null, 1, true);
        assert_eq!(result.code, ffi::Code::InvalidArgument);
        assert_eq!(
            (result.consumed, result.produced, result.finished),
            (0, 0, false)
        );
        let result = zstd_decode(&mut decoder, &encoded, null, 1);
        assert_eq!(result.code, ffi::Code::InvalidArgument);
        assert_eq!(
            (result.consumed, result.produced, result.finished),
            (0, 0, false)
        );

        let overlapping = encoded.as_ptr().cast_mut();
        assert_eq!(
            compress(
                2,
                encoded.as_ptr(),
                encoded.len() as i64,
                overlapping,
                encoded.len() as i64
            )
            .code,
            ffi::Code::InvalidArgument
        );
        assert_eq!(
            decompress(
                2,
                encoded.as_ptr(),
                encoded.len() as i64,
                overlapping,
                encoded.len() as i64
            )
            .code,
            ffi::Code::InvalidArgument
        );
        assert_eq!(
            zstd_encode(&mut encoder, &encoded, overlapping, encoded.len(), true).code,
            ffi::Code::InvalidArgument
        );
        assert_eq!(
            zstd_decode(&mut decoder, &encoded, overlapping, encoded.len()).code,
            ffi::Code::InvalidArgument
        );
        assert_eq!(
            compress(
                2,
                source.as_ptr(),
                source.len() as i64,
                NonNull::<u8>::dangling().as_ptr(),
                -1
            )
            .code,
            ffi::Code::InvalidArgument
        );
        assert_eq!(
            compress(
                2,
                source.as_ptr(),
                source.len() as i64,
                usize::MAX as *mut u8,
                1
            )
            .code,
            ffi::Code::InvalidArgument
        );

        let mut output = [MaybeUninit::<u8>::uninit(); 256];
        assert!(
            zstd_encode(
                &mut encoder,
                source,
                output.as_mut_ptr().cast(),
                output.len(),
                true
            )
            .finished
        );
        let result = zstd_decode(
            &mut decoder,
            &encoded,
            output.as_mut_ptr().cast(),
            output.len(),
        );
        assert_eq!(result.code, ffi::Code::Ok);
        assert!(result.finished);
        assert_eq!(written(&output, result.produced), source);
    }
}

#[test]
fn null_zero_capacity_is_valid_and_does_not_consume_input() {
    let source = b"zero capacity";
    let mut encoded = Vec::with_capacity(zstd::compress_bound(source.len()));
    zstd::compress(source, &mut encoded).unwrap();
    let mut encoder = new_zstd_encoder();
    let mut decoder = new_zstd_decoder();
    // SAFETY: a null output pointer is permitted when capacity is zero, and
    // the later retry uses an exclusively held, disjoint destination.
    unsafe {
        let result = compress(
            2,
            source.as_ptr(),
            source.len() as i64,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(result.code, ffi::Code::OutputTooSmall);
        assert_eq!(result.size, 0);
        let result = decompress(
            2,
            encoded.as_ptr(),
            encoded.len() as i64,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(result.code, ffi::Code::OutputTooSmall);
        assert_eq!(result.size, 0);
        let result = zstd_encode(&mut encoder, source, std::ptr::null_mut(), 0, true);
        assert_eq!(result.code, ffi::Code::Ok);
        assert_eq!(
            (result.consumed, result.produced, result.finished),
            (0, 0, false)
        );
        let result = zstd_decode(&mut decoder, &encoded, std::ptr::null_mut(), 0);
        assert_eq!(result.code, ffi::Code::Ok);
        assert_eq!(
            (result.consumed, result.produced, result.finished),
            (0, 0, false)
        );
        let mut output = [MaybeUninit::<u8>::uninit(); 256];
        assert!(
            zstd_encode(
                &mut encoder,
                source,
                output.as_mut_ptr().cast(),
                output.len(),
                true
            )
            .finished
        );
        assert!(
            zstd_decode(
                &mut decoder,
                &encoded,
                output.as_mut_ptr().cast(),
                output.len()
            )
            .finished
        );
    }
}
