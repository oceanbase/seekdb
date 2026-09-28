// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use embedding_response_ffi::{seekdb_embedding_request_build, EmbeddingBytes};
use std::{ffi::c_void, ptr};

unsafe extern "C" fn copy(context: *mut c_void, data: *const u8, length: usize) -> i32 {
    let out = unsafe { &mut *context.cast::<Vec<u8>>() };
    out.extend_from_slice(unsafe { std::slice::from_raw_parts(data, length) });
    0
}
unsafe extern "C" fn fail(_: *mut c_void, _: *const u8, _: usize) -> i32 {
    -4013
}
fn bytes(value: &[u8]) -> EmbeddingBytes {
    EmbeddingBytes {
        data: value.as_ptr(),
        length: value.len(),
    }
}
const EMPTY: EmbeddingBytes = EmbeddingBytes {
    data: ptr::null(),
    length: 0,
};

#[test]
fn copied_body_outlives_call_and_input() {
    let mut out = Vec::<u8>::new();
    {
        let input = b"a\0\"".to_vec();
        let descriptors = [bytes(&input), EMPTY];
        assert_eq!(
            unsafe {
                seekdb_embedding_request_build(
                    descriptors.as_ptr(),
                    2,
                    bytes(b"m"),
                    2,
                    1,
                    (&mut out as *mut Vec<u8>).cast(),
                    Some(copy),
                )
            },
            0
        );
    }
    assert_eq!(
        out,
        br#"{"input":["a\u0000\"",""],"model":"m","encoding_format":"base64","dimensions":2}"#
    );
}

#[test]
fn validation_and_sink_errors() {
    let bad = EmbeddingBytes {
        data: ptr::null(),
        length: 1,
    };
    let huge = EmbeddingBytes {
        data: b"x".as_ptr(),
        length: usize::MAX,
    };
    // fail returns a different error, proving invalid requests never reach it.
    unsafe {
        for model in [bad, huge] {
            assert_eq!(
                seekdb_embedding_request_build(
                    ptr::null(),
                    0,
                    model,
                    1,
                    0,
                    ptr::null_mut(),
                    Some(fail)
                ),
                -4002
            );
        }
        for count in [1, usize::MAX] {
            assert_eq!(
                seekdb_embedding_request_build(
                    ptr::null(),
                    count,
                    EMPTY,
                    1,
                    0,
                    ptr::null_mut(),
                    Some(fail)
                ),
                -4002
            );
        }
        assert_eq!(
            seekdb_embedding_request_build(&bad, 1, EMPTY, 1, 0, ptr::null_mut(), Some(fail)),
            -4002
        );
        assert_eq!(
            seekdb_embedding_request_build(
                ptr::null(),
                0,
                EMPTY,
                1,
                9,
                ptr::null_mut(),
                Some(fail)
            ),
            -4002
        );
        assert_eq!(
            seekdb_embedding_request_build(ptr::null(), 0, EMPTY, 1, 0, ptr::null_mut(), None),
            -4002
        );
        assert_eq!(
            seekdb_embedding_request_build(
                ptr::null(),
                0,
                EMPTY,
                1,
                0,
                ptr::null_mut(),
                Some(fail)
            ),
            -4013
        );
    }
}
