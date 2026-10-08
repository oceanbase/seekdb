// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sys,
    table::{Arguments, Cell, Cursor, QueryContext, Rows, Service},
    Result,
};
use std::{mem::size_of, ptr};

#[derive(Default)]
struct Host {
    opens: usize,
    wanted: Vec<bool>,
    rows: Vec<Vec<Vec<u8>>>,
}
struct Stream {
    position: usize,
}
impl Cursor for Stream {
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn open(_: *mut sys::Handle, _: &Arguments<'_>) -> Result<Self> {
        Ok(Self { position: 0 })
    }
    fn open_with_context(
        instance: *mut sys::Handle,
        args: &Arguments<'_>,
        query: &mut QueryContext<'_>,
    ) -> Result<Self> {
        let host = unsafe { &mut *instance.cast::<Host>() };
        host.opens += 1;
        host.wanted = vec![query.column_requested(0)?, query.column_requested(1)?];
        if let Some(count) = query.projection_column_count() {
            assert_eq!(count, 2);
            assert_eq!(query.column_requested(count), Err(sys::INVALID));
        }
        assert_eq!(query.column_requested(4096), Err(sys::INVALID));
        Self::open(instance, args)
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        if self.position == 3 {
            return Ok(());
        }
        let first: &[u8] = if rows.column_requested(0)? {
            b"one"
        } else {
            b""
        };
        let second: &[u8] = if rows.column_requested(1)? {
            b"two"
        } else {
            b""
        };
        rows.emit(&[
            Cell {
                type_id: c"core.type.bytes",
                bytes: Some(first),
            },
            Cell {
                type_id: c"core.type.bytes",
                bytes: Some(second),
            },
        ])?;
        self.position += 1;
        Ok(())
    }
}
unsafe extern "C" fn emit(raw: *mut sys::Handle, row: *const sys::TableRow) -> sys::Status {
    let host = unsafe { &mut *raw.cast::<Host>() };
    let row = unsafe { &*row };
    let values = unsafe { std::slice::from_raw_parts(row.columns, row.column_count as usize) };
    host.rows.push(
        values
            .iter()
            .map(|value| {
                assert_eq!(value.is_null, 0);
                if value.data_size == 0 {
                    Vec::new()
                } else {
                    unsafe { std::slice::from_raw_parts(value.data, value.data_size as usize) }
                        .to_vec()
                }
            })
            .collect(),
    );
    sys::OK
}
fn context(host: &mut Host) -> sys::TableContextV4 {
    sys::TableContextV4 {
        v3: sys::TableContextV3 {
            v2: sys::TableContextV2 {
                v1: sys::TableContext {
                    struct_size: size_of::<sys::TableContextV4>() as u32,
                    host: ptr::from_mut(host).cast(),
                    emit_row: Some(emit),
                    reserved: [0; 6],
                },
                query_context: ptr::null_mut(),
                poll_query: None,
                reserved: [0; 4],
            },
            sql_api: ptr::null(),
            reserved: [0; 4],
        },
        column_count: 0,
        reserved_word: 0,
        requested_columns: ptr::null(),
        reserved: [0; 4],
    }
}
#[test]
fn optional_projection_changes_without_changing_the_stream_or_legacy_contract() {
    let service = &Service::<Stream>::WITH_PROJECTION.v1;
    assert_eq!(service.spi_minor, 4);
    assert!(Service::<Stream>::WITH_PROJECTION.estimate.is_none());
    let mut host = Host::default();
    let instance = ptr::from_mut(&mut host).cast();
    let mut context = context(&mut host);
    let mut cursor = ptr::null_mut();
    let first = [1u8, 0];
    context.column_count = 2;
    context.requested_columns = first.as_ptr();
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &context.v3.v2.v1, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    assert_eq!(host.wanted, [true, false]);
    let mut emitted = 99;
    for wanted in [[1u8, 0], [0, 1], [0, 0]] {
        context.requested_columns = wanted.as_ptr();
        assert_eq!(
            unsafe { service.next.unwrap()(instance, cursor, &context.v3.v2.v1, 4, &mut emitted) },
            sys::OK
        );
        assert_eq!(emitted, 1);
    }
    context.requested_columns = first.as_ptr();
    assert_eq!(
        host.rows,
        vec![
            vec![b"one".to_vec(), vec![]],
            vec![vec![], b"two".to_vec()],
            vec![vec![], vec![]]
        ]
    );
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v3.v2.v1, 4, &mut emitted) },
        sys::END_OF_STREAM
    );
    assert_eq!(emitted, 0);
    assert_eq!(
        unsafe { service.rescan.unwrap()(instance, cursor, ptr::null(), 0) },
        sys::OK
    );
    // Unavailable projection is different from a present all-zero projection.
    context.column_count = 0;
    context.requested_columns = ptr::null();
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v3.v2.v1, 1, &mut emitted) },
        sys::OK
    );
    assert_eq!(
        host.rows.last().unwrap(),
        &vec![b"one".to_vec(), b"two".to_vec()]
    );
    assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
    // An actual v1 allocation, not an undersized advertisement over v4 memory.
    let legacy = sys::TableContext {
        struct_size: size_of::<sys::TableContext>() as u32,
        host: instance,
        emit_row: Some(emit),
        reserved: [0; 6],
    };
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &legacy, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    assert_eq!(host.wanted, [true, true]);
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &legacy, 1, &mut emitted) },
        sys::OK
    );
    assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
    assert_eq!(
        unsafe {
            Service::<Stream>::WITH_SQL_AND_PROJECTION.v1.open.unwrap()(
                instance,
                &context.v3.v2.v1,
                ptr::null(),
                0,
                &mut cursor,
            )
        },
        sys::UNAVAILABLE
    );
    assert!(cursor.is_null());
}
#[test]
fn malformed_projection_is_rejected_before_open_and_rows_keep_declared_arity() {
    let service = &Service::<Stream>::WITH_PROJECTION.v1;
    let mut host = Host::default();
    let instance = ptr::from_mut(&mut host).cast();
    for fault in 0..6 {
        let mut context = context(&mut host);
        let columns = [1u8, 2];
        match fault {
            0 => context.column_count = 2,
            1 => context.requested_columns = columns.as_ptr(),
            2 => {
                context.column_count = 4097;
                context.requested_columns = columns.as_ptr();
            }
            3 => {
                context.column_count = 2;
                context.requested_columns = columns.as_ptr();
            }
            4 => context.reserved[0] = 1,
            _ => context.reserved_word = 1,
        }
        let mut cursor = ptr::dangling_mut();
        assert_eq!(
            unsafe {
                service.open.unwrap()(instance, &context.v3.v2.v1, ptr::null(), 0, &mut cursor)
            },
            sys::INVALID
        );
        assert!(cursor.is_null());
        assert_eq!(host.opens, 0);
    }
    let mut context = context(&mut host);
    let mut cursor = ptr::null_mut();
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &context.v3.v2.v1, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    let columns = [1u8, 1, 0];
    context.column_count = 3;
    context.requested_columns = columns.as_ptr();
    let mut emitted = 99;
    // Handler emits two cells; known declared arity is three, so no host row.
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v3.v2.v1, 4, &mut emitted) },
        sys::INVALID
    );
    assert_eq!(emitted, 0);
    assert!(host.rows.is_empty());
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v3.v2.v1, 4, &mut emitted) },
        sys::FAILED_PRECONDITION
    );
    assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
}
