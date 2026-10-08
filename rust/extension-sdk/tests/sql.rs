// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Controlled public-SPI host: validates SDK marshalling/lifetimes/errors, not
//! database parsing, permissions, cancellation, or transaction behavior.
use seekdb_extension::{
    boundary,
    sql::{Error, Value},
    sys, Call,
};
use std::ffi::c_void;
use std::mem::size_of;
use std::ptr;

#[derive(Default)]
struct Host {
    rows: Vec<Vec<sys::SqlValue>>,
    calls: usize,
    sql: String,
    parameters: Vec<(u32, Vec<u8>)>,
    status: sys::Status,
    database_error: i64,
    affected_rows: i64,
    ignore_consumer_error: bool,
    count_override: Option<u32>,
    returned_override: Option<u64>,
    short_result: bool,
    consumer_statuses: Vec<sys::Status>,
}

unsafe extern "C" fn emit(_: *mut c_void, _: *const sys::Value) -> sys::Status {
    sys::OK
}
unsafe extern "C" fn execute(
    context: *mut c_void,
    sql: *const std::ffi::c_char,
    sql_size: u64,
    parameters: *const sys::SqlValue,
    count: u32,
    _: u64,
    consume: Option<sys::ConsumeRow>,
    consumer: *mut c_void,
    result: *mut sys::SqlResult,
) -> sys::Status {
    boundary(|| {
        let host = unsafe { &mut *context.cast::<Host>() };
        let result = unsafe { &mut *result };
        host.calls += 1;
        host.sql = std::str::from_utf8(unsafe {
            std::slice::from_raw_parts(sql.cast(), sql_size as usize)
        })
        .map_err(|_| sys::INVALID)?
        .to_owned();
        let parameters = unsafe { std::slice::from_raw_parts(parameters, count as usize) };
        host.parameters = parameters
            .iter()
            .map(|value| {
                (
                    value.kind,
                    if value.data_size == 0 {
                        Vec::new()
                    } else {
                        unsafe {
                            std::slice::from_raw_parts(
                                value.data.cast::<u8>(),
                                value.data_size as usize,
                            )
                        }
                        .to_vec()
                    },
                )
            })
            .collect();
        result.database_error = host.database_error;
        result.affected_rows = host.affected_rows;
        if host.status != sys::OK {
            return Err(host.status);
        }
        for row in &host.rows {
            let status = unsafe {
                consume.unwrap()(
                    consumer,
                    row.as_ptr(),
                    host.count_override.unwrap_or(row.len() as u32),
                )
            };
            host.consumer_statuses.push(status);
            if status != sys::OK && !host.ignore_consumer_error {
                result.database_error = -4999; // controlled host cancellation code
                return Err(sys::FAILED_PRECONDITION);
            }
            result.returned_rows += 1;
        }
        if let Some(rows) = host.returned_override {
            result.returned_rows = rows;
        }
        if host.short_result {
            result.struct_size = 4;
        }
        Ok(())
    })
}

fn api() -> sys::SqlApi {
    sys::SqlApi {
        struct_size: size_of::<sys::SqlApi>() as u32,
        spi_major: 1,
        spi_minor: 0,
        reserved_word: 0,
        execute: Some(execute),
        reserved: [0; 6],
    }
}
fn context(host: &mut Host, api: &sys::SqlApi) -> sys::ContextV2 {
    sys::ContextV2 {
        v1: sys::ContextV1 {
            struct_size: size_of::<sys::ContextV2>() as u32,
            host: ptr::null_mut(),
            emit_result: Some(emit),
            reserved: [0; 6],
        },
        sql_api: api,
        sql_context: (host as *mut Host).cast(),
        reserved: [0; 4],
    }
}
fn raw(kind: u32, bytes: &[u8]) -> sys::SqlValue {
    sys::SqlValue {
        struct_size: size_of::<sys::SqlValue>() as u32,
        kind,
        data: bytes.as_ptr().cast(),
        data_size: bytes.len() as u64,
        reserved: [0; 2],
    }
}

#[test]
fn typed_parameters_and_multiple_rows_preserve_bytes_and_numeric_values() {
    let integer = (-42i64).to_ne_bytes();
    let unsigned = u64::MAX.to_ne_bytes();
    let floating = 1.25f64.to_ne_bytes();
    let mut host = Host {
        rows: vec![
            vec![raw(0, &[]), raw(1, &integer), raw(2, &unsigned)],
            vec![
                raw(3, &floating),
                raw(4, "中\0🙂".as_bytes()),
                raw(5, &[0xff, 0]),
            ],
        ],
        ..Host::default()
    };
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    let parameters = [
        Value::Null,
        Value::I64(-42),
        Value::U64(u64::MAX),
        Value::F64(1.25),
        Value::Text("中\0🙂"),
        Value::Bytes(&[0xff, 0]),
    ];
    let mut index = 0;
    let mut copied = Vec::new();
    let outcome = call
        .execute_sql("SELECT ?, ?, ?, ?, ?, ?", &parameters, 2, |row| {
            assert_eq!(row.len(), 3);
            assert!(!row.is_empty());
            assert_eq!(row.get(3), Err(sys::INVALID));
            for i in 0..3 {
                assert_eq!(row.get(i)?, parameters[index * 3 + i]);
            }
            if let Value::Text(text) = row.get(1)? {
                copied.push(text.to_owned());
            }
            index += 1;
            Ok(())
        })
        .unwrap();
    assert_eq!((outcome.returned_rows, index), (2, 2));
    assert_eq!(copied, ["中\0🙂"]);
    assert_eq!(host.sql, "SELECT ?, ?, ?, ?, ?, ?");
    assert_eq!(
        host.parameters,
        vec![
            (0, vec![]),
            (1, integer.to_vec()),
            (2, unsigned.to_vec()),
            (3, floating.to_vec()),
            (4, "中\0🙂".as_bytes().to_vec()),
            (5, vec![0xff, 0])
        ]
    );
}

#[test]
fn zero_parameter_dml_returns_affected_rows_without_consuming() {
    let mut host = Host {
        affected_rows: 7,
        ..Host::default()
    };
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    let outcome = call
        .execute_sql("UPDATE t SET x = 1", &[], 0, |_| panic!("no rows expected"))
        .unwrap();
    assert_eq!((outcome.affected_rows, outcome.returned_rows), (7, 0));
    assert!(host.parameters.is_empty());
}

#[test]
fn preserves_host_status_and_database_diagnostic() {
    let mut host = Host {
        status: sys::FAILED_PRECONDITION,
        database_error: -12345,
        ..Host::default()
    };
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    assert_eq!(
        call.execute_sql("SELECT denied", &[], 1, |_| Ok(())),
        Err(Error {
            status: sys::FAILED_PRECONDITION,
            database_error: -12345,
            consumer_status: None
        })
    );
    host.status = sys::OK;
    assert_eq!(
        call.execute_sql("SELECT denied", &[], 1, |_| Ok(()))
            .unwrap_err()
            .status,
        sys::INTERNAL
    );
}

#[test]
fn consumer_errors_and_panics_stay_inside_callback_and_are_sticky() {
    for panic in [false, true] {
        let mut host = Host {
            rows: vec![vec![], vec![]],
            ignore_consumer_error: true,
            ..Host::default()
        };
        let api = api();
        let context = context(&mut host, &api);
        let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
        let mut called = 0;
        let error = call
            .execute_sql("SELECT fixture", &[], 2, |_| {
                called += 1;
                if panic {
                    panic!("consumer panic")
                }
                Err(sys::INVALID)
            })
            .unwrap_err();
        let expected = if panic { sys::INTERNAL } else { sys::INVALID };
        assert_eq!(called, 1);
        assert_eq!(error.consumer_status, Some(expected));
        assert_eq!(error.status, expected);
        assert_eq!(host.consumer_statuses, [expected, expected]);
    }
}

#[test]
fn host_cancellation_keeps_original_consumer_error() {
    let mut host = Host {
        rows: vec![vec![]],
        ..Host::default()
    };
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    assert_eq!(
        call.execute_sql("SELECT fixture", &[], 1, |_| Err(sys::NO_MEMORY)),
        Err(Error {
            status: sys::FAILED_PRECONDITION,
            database_error: -4999,
            consumer_status: Some(sys::NO_MEMORY)
        })
    );
}

#[test]
fn row_limit_fails_instead_of_silently_truncating() {
    for max_rows in [0, 1] {
        let mut host = Host {
            rows: vec![vec![], vec![]],
            ..Host::default()
        };
        let api = api();
        let context = context(&mut host, &api);
        let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
        let mut rows = 0;
        let error = call
            .execute_sql("SELECT fixture", &[], max_rows, |_| {
                rows += 1;
                Ok(())
            })
            .unwrap_err();
        assert_eq!(rows, max_rows);
        assert_eq!(error.consumer_status, Some(sys::FAILED_PRECONDITION));
    }
}

#[test]
fn rejects_malformed_cells_even_when_consumer_would_ignore_them() {
    let mut short = raw(0, &[]);
    short.struct_size = 4;
    let mut null_data = raw(1, &[0; 8]);
    null_data.data = ptr::null();
    let mut large = raw(5, &[]);
    large.data_size = 16 * 1024 * 1024 + 1; // rejected before accessing the pointer
    for cell in [
        short,
        null_data,
        large,
        raw(27, &[]),
        raw(1, &[0; 7]),
        raw(0, &[0]),
        raw(4, &[0xff]),
    ] {
        let mut host = Host {
            rows: vec![vec![cell]],
            ..Host::default()
        };
        let api = api();
        let context = context(&mut host, &api);
        let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
        let error = call
            .execute_sql("SELECT fixture", &[], 1, |_| panic!("must validate first"))
            .unwrap_err();
        assert_eq!(error.consumer_status, Some(sys::INVALID));
    }
}

#[test]
fn aggregate_parameter_and_result_byte_limits_are_enforced() {
    let bytes = vec![0u8; 8 * 1024 * 1024 + 1];
    let mut host = Host::default();
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    assert_eq!(
        call.execute_sql(
            "SELECT ?, ?",
            &[Value::Bytes(&bytes), Value::Bytes(&bytes)],
            2,
            |_| Ok(())
        ),
        Err(sys::INVALID.into())
    );
    assert_eq!(host.calls, 0);
    host.rows = vec![vec![raw(5, &bytes)], vec![raw(5, &bytes)]];
    let mut seen = 0;
    let error = call
        .execute_sql("SELECT fixture", &[], 2, |_| {
            seen += 1;
            Ok(())
        })
        .unwrap_err();
    assert_eq!(seen, 1);
    assert_eq!(error.consumer_status, Some(sys::INVALID));
}

#[test]
fn rejects_bad_sql_parameters_and_result_metadata() {
    let mut host = Host::default();
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    for sql in ["", "SELECT\0 1"] {
        assert_eq!(
            call.execute_sql(sql, &[], 0, |_| Ok(())),
            Err(sys::INVALID.into())
        );
    }
    assert_eq!(
        call.execute_sql("SELECT ?", &[Value::Null; 1025], 0, |_| Ok(())),
        Err(sys::INVALID.into())
    );
    assert_eq!(host.calls, 0);
    host.returned_override = Some(1);
    assert_eq!(
        call.execute_sql("SELECT fixture", &[], 1, |_| Ok(())),
        Err(sys::INTERNAL.into())
    );
    host.returned_override = None;
    host.short_result = true;
    assert_eq!(
        call.execute_sql("SELECT fixture", &[], 1, |_| Ok(())),
        Err(sys::INTERNAL.into())
    );
    host.short_result = false;
    host.rows = vec![vec![]];
    host.count_override = Some(1025);
    assert_eq!(
        call.execute_sql("SELECT fixture", &[], 1, |_| Ok(()))
            .unwrap_err()
            .consumer_status,
        Some(sys::INVALID)
    );
}

#[test]
fn validates_sql_context_and_abi_before_entering_host() {
    let mut host = Host::default();
    for (api, expected) in [
        (
            sys::SqlApi {
                struct_size: 4,
                ..api()
            },
            sys::UNSUPPORTED_ABI,
        ),
        (
            sys::SqlApi {
                spi_major: 99,
                ..api()
            },
            sys::UNSUPPORTED_ABI,
        ),
        (
            sys::SqlApi {
                execute: None,
                ..api()
            },
            sys::UNAVAILABLE,
        ),
    ] {
        let context = context(&mut host, &api);
        let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
        assert_eq!(
            call.execute_sql("SELECT 1", &[], 1, |_| Ok(())),
            Err(expected.into())
        );
    }
    assert_eq!(host.calls, 0);
}

#[test]
fn empty_null_and_unaligned_numeric_results_are_distinct() {
    let mut number = [0u8; 9];
    number[1..].copy_from_slice(&(-19i64).to_ne_bytes());
    let mut empty = raw(4, &[]);
    empty.data = ptr::null();
    let mut host = Host {
        rows: vec![vec![empty, raw(5, &[]), raw(0, &[]), raw(1, &number[1..])]],
        ..Host::default()
    };
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    call.execute_sql("SELECT fixture", &[], 1, |row| {
        assert_eq!(row.get(0)?, Value::Text(""));
        assert_eq!(row.get(1)?, Value::Bytes(&[]));
        assert_eq!(row.get(2)?, Value::Null);
        assert_eq!(row.get(3)?, Value::I64(-19));
        Ok(())
    })
    .unwrap();
}

#[test]
fn scalar_convenience_checks_cardinality_and_unsigned_overflow() {
    let unsigned = u64::MAX.to_ne_bytes();
    let normal = 42u64.to_ne_bytes();
    let mut host = Host::default();
    let api = api();
    let context = context(&mut host, &api);
    let mut call = unsafe { Call::from_raw(&context.v1, ptr::null(), 0) }.unwrap();
    assert_eq!(call.query_i64("SELECT ?", None), Err(sys::INTERNAL));
    host.rows = vec![vec![raw(2, &unsigned)]];
    assert_eq!(
        call.query_i64("SELECT ?", None),
        Err(sys::FAILED_PRECONDITION)
    );
    host.rows = vec![vec![raw(2, &normal)]];
    assert_eq!(call.query_i64("SELECT ?", Some("x")), Ok(Some(42)));
    host.rows = vec![vec![raw(0, &[])]];
    assert_eq!(call.query_i64("SELECT ?", None), Ok(None));
    host.rows = vec![vec![raw(0, &[]), raw(0, &[])]];
    assert_eq!(
        call.query_i64("SELECT ?", None),
        Err(sys::FAILED_PRECONDITION)
    );
}
