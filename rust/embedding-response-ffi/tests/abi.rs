// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use embedding_response_ffi::seekdb_embedding_response_parse;
use std::ffi::c_void;

#[derive(Default)]
struct Output {
    vectors: Vec<Vec<f32>>,
    calls: usize,
    fail_at: Option<usize>,
}

unsafe extern "C" fn emit(context: *mut c_void, values: *const f32, count: usize) -> i32 {
    // SAFETY: run() lends its live Output for the duration of the parse. Rust
    // guarantees that values is a readable float slice during this callback.
    let output = unsafe { &mut *context.cast::<Output>() };
    output.calls += 1;
    if output.fail_at == Some(output.calls) {
        return -4013;
    }
    let vector = unsafe { std::slice::from_raw_parts(values, count) };
    output.vectors.push(vector.to_vec());
    0
}

fn run(data: &[u8], dimension: i64, encoding: u32, output: &mut Output) -> i32 {
    // SAFETY: input and output live through the call; emit copies borrowed data.
    unsafe {
        seekdb_embedding_response_parse(
            data.as_ptr(),
            data.len(),
            dimension,
            encoding,
            (output as *mut Output).cast(),
            Some(emit),
        )
    }
}

#[test]
fn copied_vectors_outlive_input_and_parser() {
    let mut output = Output::default();
    {
        let input = String::from(r#"{"data":[{"embedding":[1,2]},{"embedding":[3,4]}]}"#);
        assert_eq!(run(input.as_bytes(), 2, 0, &mut output), 0);
    }
    assert_eq!(output.vectors, vec![vec![1.0, 2.0], vec![3.0, 4.0]]);
}

#[test]
fn callback_failure_stops_before_later_semantic_error() {
    let input = br#"{"data":[{"embedding":[1]},{"embedding":[2]},{}]}"#;
    let mut output = Output {
        fail_at: Some(2),
        ..Output::default()
    };
    assert_eq!(run(input, 1, 0, &mut output), -4013);
    assert_eq!(output.calls, 2);
    assert_eq!(output.vectors, vec![vec![1.0]]);
}

#[test]
fn later_parse_error_retains_prior_callbacks() {
    let mut output = Output::default();
    assert_eq!(
        run(br#"{"data":[{"embedding":[1]},{}]}"#, 1, 0, &mut output),
        -4182
    );
    assert_eq!(output.calls, 1);
    assert_eq!(output.vectors, vec![vec![1.0]]);
}

#[test]
fn invalid_json_never_calls_sink() {
    let mut output = Output::default();
    assert_eq!(
        run(
            br#"{"data":[{"embedding":[1]}],"bad":1e999}"#,
            1,
            0,
            &mut output
        ),
        -5411
    );
    assert_eq!(output.calls, 0);
}

#[test]
fn base64_uses_the_same_callback_contract() {
    let mut output = Output::default();
    assert_eq!(
        run(br#"{"data":[{"embedding":"AAAAAA=="}]}"#, 1, 1, &mut output),
        0
    );
    assert_eq!(output.vectors, vec![vec![0.0]]);
}

#[test]
fn validates_abi_arguments_before_dereferencing() {
    let input = b"{}";
    let mut output = Output::default();
    assert_eq!(run(input, 1, 2, &mut output), -4002);
    assert_eq!(run(b"", 1, 0, &mut output), -4002);
    // SAFETY: deliberately invalid argument combinations are rejected before
    // pointer access. The oversize length is never used to construct a slice.
    unsafe {
        assert_eq!(
            seekdb_embedding_response_parse(
                std::ptr::null(),
                1,
                1,
                0,
                std::ptr::null_mut(),
                Some(emit)
            ),
            -4002
        );
        assert_eq!(
            seekdb_embedding_response_parse(
                input.as_ptr(),
                input.len(),
                1,
                0,
                std::ptr::null_mut(),
                None
            ),
            -4002
        );
        assert_eq!(
            seekdb_embedding_response_parse(
                input.as_ptr(),
                usize::MAX,
                1,
                0,
                std::ptr::null_mut(),
                Some(emit)
            ),
            -4002
        );
    }
    assert_eq!(output.calls, 0);
}

#[test]
fn independent_calls_can_run_concurrently() {
    let handles: Vec<_> = (0..8)
        .map(|_| {
            std::thread::spawn(|| {
                let mut output = Output::default();
                for _ in 0..20 {
                    assert_eq!(
                        run(br#"{"data":[{"embedding":[1,-2.5]}]}"#, 2, 0, &mut output),
                        0
                    );
                }
                assert_eq!(output.vectors, vec![vec![1.0, -2.5]; 20]);
            })
        })
        .collect();
    for handle in handles {
        handle.join().unwrap();
    }
}
