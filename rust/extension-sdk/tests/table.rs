// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    boundary, sys,
    table::{Arguments, Cell, Cursor, Rows, Service},
    Result,
};
use std::{
    mem::size_of,
    ptr,
    sync::atomic::{AtomicUsize, Ordering},
};

static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Stream {
    bytes: Vec<u8>,
    position: usize,
}
impl Drop for Stream {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}
impl Cursor for Stream {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::UNAVAILABLE)
        } else {
            Ok(())
        }
    }
    fn open(_: *mut sys::Handle, args: &Arguments<'_>) -> Result<Self> {
        if args.len() != 1 {
            return Err(sys::INVALID);
        }
        let bytes = args.bytes(0, c"example.bytes")?.unwrap_or(&[]);
        if bytes == b"open-panic" {
            panic!("open");
        }
        Ok(Self {
            bytes: bytes.to_vec(),
            position: 0,
        })
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        if self.bytes == b"panic" {
            panic!("next");
        }
        if self.bytes == b"overshoot" {
            let cell = [Cell {
                type_id: c"example.bytes",
                bytes: Some(b"x"),
            }];
            rows.emit(&cell)?;
            let _ = rows.emit(&cell); // Ignored budget failure must stay sticky.
            return Ok(());
        }
        if self.bytes == b"large" {
            let bytes = vec![0; 8 * 1024 * 1024 + 1];
            let _ = rows.emit(&[
                Cell {
                    type_id: c"example.bytes",
                    bytes: Some(&bytes),
                },
                Cell {
                    type_id: c"example.bytes",
                    bytes: Some(&bytes),
                },
            ]);
            return Ok(());
        }
        if self.bytes == b"ignore" {
            let _ = rows.emit(&[Cell {
                type_id: c"example.bytes",
                bytes: Some(&self.bytes),
            }]);
            return Ok(());
        }
        while self.position < self.bytes.len() && rows.remaining() > 0 {
            let value = &self.bytes[self.position..self.position + 1];
            rows.emit(&[Cell {
                type_id: c"example.bytes",
                bytes: Some(value),
            }])?;
            self.position += 1;
        }
        Ok(())
    }
}
#[derive(Default)]
struct Host {
    bytes: Vec<u8>,
    calls: usize,
    fail: bool,
}
unsafe extern "C" fn emit(host: *mut sys::Handle, row: *const sys::TableRow) -> sys::Status {
    boundary(|| {
        let host = unsafe { &mut *host.cast::<Host>() };
        host.calls += 1;
        if host.fail {
            return Err(sys::UNAVAILABLE);
        }
        let row = unsafe { &*row };
        assert_eq!(row.column_count, 1);
        assert_eq!(row.reserved_word, 0);
        assert_eq!(row.reserved, [0; 4]);
        let value = unsafe { &*row.columns };
        host.bytes.extend_from_slice(unsafe {
            std::slice::from_raw_parts(value.data, value.data_size as usize)
        });
        Ok(())
    })
}
fn value(bytes: &[u8]) -> sys::Value {
    sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"example.bytes".as_ptr(),
        data: bytes.as_ptr(),
        data_size: bytes.len() as u64,
        is_null: 0,
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    }
}
#[test]
fn owned_cursor_batches_eof_rescan_errors_panic_and_cleanup() {
    let api = Service::<Stream>::ABI;
    let mut host = Host::default();
    let instance = (&mut host as *mut Host).cast();
    let context = sys::TableContext {
        struct_size: size_of::<sys::TableContext>() as u32,
        host: instance,
        emit_row: Some(emit),
        reserved: [0; 6],
    };
    let mut cursor = ptr::null_mut();
    let mut bytes = b"abc".to_vec();
    unsafe {
        assert_eq!(
            api.open.unwrap()(instance, &context, &value(&bytes), 1, &mut cursor),
            sys::OK
        );
        bytes.fill(b'x'); // Input storage is not retained by the cursor.
        let mut count = 99;
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 2, &mut count),
            sys::OK
        );
        assert_eq!(count, 2);
        assert_eq!(host.bytes, b"ab");
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 2, &mut count),
            sys::OK
        );
        assert_eq!(count, 1);
        assert_eq!(host.bytes, b"abc");
        for _ in 0..2 {
            assert_eq!(
                api.next.unwrap()(instance, cursor, &context, 2, &mut count),
                sys::END_OF_STREAM
            );
            assert_eq!(count, 0);
        }
        assert_eq!(
            api.rescan.unwrap()(instance, cursor, &value(b"z"), 1),
            sys::OK
        );
        assert_eq!(DROPS.load(Ordering::SeqCst), 1);
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 1, &mut count),
            sys::OK
        );
        assert_eq!(host.bytes, b"abcz");
        // Host failure cannot be hidden by a handler that ignores emit status.
        assert_eq!(
            api.rescan.unwrap()(instance, cursor, &value(b"ignore"), 1),
            sys::OK
        );
        host.fail = true;
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 1, &mut count),
            sys::UNAVAILABLE
        );
        assert_eq!(count, 0);
        let calls = host.calls;
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 1, &mut count),
            sys::FAILED_PRECONDITION
        );
        assert_eq!(host.calls, calls);
        assert_eq!(
            api.rescan.unwrap()(instance, cursor, &value(b"panic"), 1),
            sys::OK
        );
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 1, &mut count),
            sys::INTERNAL
        );
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 1, &mut count),
            sys::FAILED_PRECONDITION
        );
        assert_eq!(
            api.rescan.unwrap()(instance, cursor, &value(b"open-panic"), 1),
            sys::INTERNAL
        );
        assert_eq!(
            api.next.unwrap()(instance, cursor, &context, 1, &mut count),
            sys::FAILED_PRECONDITION
        );
        assert_eq!(
            api.close.unwrap()(ptr::null_mut(), cursor),
            sys::FAILED_PRECONDITION
        );
        assert_eq!(api.close.unwrap()(instance, cursor), sys::OK);
        assert_eq!(DROPS.load(Ordering::SeqCst), 4);
        host.fail = false;
        for (mode, emitted) in [(b"overshoot".as_slice(), 1), (b"large".as_slice(), 0)] {
            assert_eq!(
                api.open.unwrap()(instance, &context, &value(mode), 1, &mut cursor),
                sys::OK
            );
            let calls = host.calls;
            assert_eq!(
                api.next.unwrap()(instance, cursor, &context, 1, &mut count),
                sys::INVALID
            );
            assert_eq!(count, emitted);
            assert_eq!(host.calls, calls + emitted as usize);
            assert_eq!(
                api.next.unwrap()(instance, cursor, &context, 1, &mut count),
                sys::FAILED_PRECONDITION
            );
            assert_eq!(api.close.unwrap()(instance, cursor), sys::OK);
        }
        assert_eq!(DROPS.load(Ordering::SeqCst), 6);
        // Failed open clears output and never transfers a cursor.
        assert_eq!(
            api.open.unwrap()(instance, &context, &value(b"open-panic"), 1, &mut cursor),
            sys::INTERNAL
        );
        assert!(cursor.is_null());
        let mut invalid = value(b"a");
        invalid.reserved[0] = 1;
        assert_eq!(
            api.open.unwrap()(instance, &context, &invalid, 1, &mut cursor),
            sys::INVALID
        );
        assert!(cursor.is_null());
        assert_eq!(
            api.open.unwrap()(instance, ptr::null(), &value(b"a"), 1, &mut cursor),
            sys::INVALID
        );
        assert_eq!(
            api.open.unwrap()(instance, &context, ptr::null(), 1, &mut cursor),
            sys::INVALID
        );
        assert_eq!(
            api.open.unwrap()(ptr::null_mut(), &context, &value(b"a"), 1, &mut cursor),
            sys::UNAVAILABLE
        );
    }
}
