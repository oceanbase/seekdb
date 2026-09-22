use seekdb_extension::{
    custom_executor::{Context, Executor, Service, Step},
    sys,
    table::Cell,
    Result,
};
use std::{
    ffi::c_void,
    mem::size_of,
    ptr,
    sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};
struct State {
    drops: Arc<AtomicUsize>,
}
impl Drop for State {
    fn drop(&mut self) {
        self.drops.fetch_add(1, Ordering::Relaxed);
    }
}
struct Policy<const MODE: u8>;
impl<const MODE: u8> Executor for Policy<MODE> {
    type State = State;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::INVALID)
        } else {
            Ok(())
        }
    }
    fn open(instance: *mut sys::Handle, _: &[u8]) -> Result<State> {
        if MODE == 9 {
            return Err(sys::INVALID);
        }
        if MODE == 10 {
            panic!("open failure");
        }
        Ok(State {
            drops: unsafe { &*instance.cast::<Arc<AtomicUsize>>() }.clone(),
        })
    }
    fn next(_: &mut State, context: &mut Context<'_>) -> Result<Step> {
        if MODE == 1 {
            return Ok(Step::Row);
        }
        if MODE == 4 {
            panic!("next failure");
        }
        if MODE == 5 {
            let _ = context.next_input(0);
            return Ok(Step::End);
        }
        if MODE == 8 {
            return Err(sys::END_OF_STREAM);
        }
        let value = {
            let Some(row) = context.next_input(0)? else {
                return Ok(Step::End);
            };
            assert_eq!(row.len(), 1);
            row.cell(0)?.bytes.map(|v| v.to_vec())
        };
        context.emit(&[Cell {
            type_id: c"core.type.bytes",
            bytes: value.as_deref(),
        }])?;
        if MODE == 2 {
            let _ = context.emit(&[Cell {
                type_id: c"core.type.bytes",
                bytes: Some(b"second"),
            }]);
        }
        Ok(if MODE == 3 { Step::End } else { Step::Row })
    }
    fn rescan(_: &mut State) -> Result<()> {
        if MODE == 7 {
            Err(sys::INTERNAL)
        } else {
            Ok(())
        }
    }
    fn close(state: State) -> Result<()> {
        drop(state);
        if MODE == 6 {
            Err(sys::INTERNAL)
        } else {
            Ok(())
        }
    }
}
struct Host {
    calls: usize,
    emitted: Vec<Option<Vec<u8>>>,
    mode: u8,
    value: sys::Value,
}
impl Host {
    fn new() -> Self {
        Self {
            calls: 0,
            emitted: Vec::new(),
            mode: 0,
            value: sys::Value {
                struct_size: size_of::<sys::Value>() as u32,
                type_id: c"core.type.bytes".as_ptr(),
                data: b"hello".as_ptr(),
                data_size: 5,
                is_null: 0,
                reserved_bytes: [0; 7],
                reserved: [0; 4],
            },
        }
    }
    fn raw(&mut self) -> sys::CustomContext {
        sys::CustomContext {
            struct_size: size_of::<sys::CustomContext>() as u32,
            input_count: 1,
            output_column_count: 1,
            reserved_word: 0,
            host_context: (self as *mut Self).cast(),
            next_input: Some(input),
            emit: Some(emit),
            check_interrupt: Some(poll),
            reserved: [0; 4],
        }
    }
}
unsafe extern "C" fn input(
    host: *mut c_void,
    _: u32,
    row: *mut sys::CustomRow,
    db: *mut i32,
) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    host.calls += 1;
    unsafe {
        *db = if host.mode == 1 || host.mode == 5 {
            -4012
        } else {
            0
        };
    }
    if host.mode == 1 {
        return sys::INTERNAL;
    }
    if host.mode == 5 {
        return sys::END_OF_STREAM;
    }
    if host.calls > 1 {
        return sys::END_OF_STREAM;
    }
    unsafe {
        (*row).values = &host.value;
        (*row).column_count = 1;
        if host.mode == 2 {
            (*row).reserved[0] = 1;
        }
    }
    sys::OK
}
unsafe extern "C" fn emit(
    host: *mut c_void,
    cells: *const sys::Value,
    count: u32,
    db: *mut i32,
) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    unsafe {
        *db = 0;
    }
    assert_eq!(count, 1);
    let cell = unsafe { &*cells };
    if host.mode == 3 {
        unsafe {
            *db = -4013;
        }
        return sys::INTERNAL;
    }
    host.emitted.push(if cell.is_null != 0 {
        None
    } else {
        Some(unsafe { std::slice::from_raw_parts(cell.data, cell.data_size as usize) }.to_vec())
    });
    sys::OK
}
unsafe extern "C" fn poll(host: *mut c_void, db: *mut i32) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    unsafe {
        *db = if host.mode == 4 { -4012 } else { 0 };
    }
    if host.mode == 4 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
fn exercise<const MODE: u8>(expected: sys::Status, host_mode: u8) {
    let drops = Arc::new(AtomicUsize::new(0));
    let instance = (&drops as *const Arc<AtomicUsize>).cast_mut().cast();
    let service = Service::<Policy<MODE>>::ABI;
    let mut cursor = ptr::null_mut();
    let opened = unsafe { service.open.unwrap()(instance, ptr::null(), 0, &mut cursor) };
    if MODE >= 9 {
        assert_ne!(opened, sys::OK);
        assert!(cursor.is_null());
        assert_eq!(drops.load(Ordering::Relaxed), 0);
        return;
    }
    assert_eq!(opened, sys::OK);
    assert!(!cursor.is_null());
    let mut host = Host::new();
    host.mode = host_mode;
    let code = unsafe { service.next.unwrap()(instance, cursor, &host.raw()) };
    assert_eq!(code, expected);
    if code != sys::OK && code != sys::END_OF_STREAM {
        assert_eq!(
            unsafe { service.next.unwrap()(instance, cursor, &host.raw()) },
            sys::FAILED_PRECONDITION
        );
    }
    let reset = unsafe { service.rescan.unwrap()(instance, cursor) };
    assert_eq!(reset, if MODE == 7 { sys::INTERNAL } else { sys::OK });
    if MODE == 0 && host_mode == 0 {
        assert_eq!(host.emitted, [Some(b"hello".to_vec())]);
        host.calls = 0;
        host.value.is_null = 1;
        host.value.data_size = 0;
        host.value.data = ptr::null();
        assert_eq!(
            unsafe { service.next.unwrap()(instance, cursor, &host.raw()) },
            sys::OK
        );
        assert_eq!(host.emitted.last(), Some(&None));
        assert_eq!(
            unsafe { service.next.unwrap()(instance, cursor, &host.raw()) },
            sys::END_OF_STREAM
        );
    }
    assert_eq!(
        unsafe { service.close.unwrap()(instance, cursor) },
        if MODE == 6 { sys::INTERNAL } else { sys::OK }
    );
    assert_eq!(drops.load(Ordering::Relaxed), 1);
}
#[test]
fn row_protocol_error_poisoning_rescan_and_consuming_close() {
    exercise::<0>(sys::OK, 0);
    exercise::<1>(sys::FAILED_PRECONDITION, 0);
    exercise::<2>(sys::INVALID, 0);
    exercise::<3>(sys::FAILED_PRECONDITION, 0);
    exercise::<4>(sys::INTERNAL, 0);
    exercise::<5>(sys::INTERNAL, 1);
    exercise::<6>(sys::OK, 0);
    exercise::<7>(sys::OK, 0);
    exercise::<8>(sys::INVALID, 0);
    exercise::<9>(sys::INVALID, 0);
    exercise::<10>(sys::INTERNAL, 0);
}
#[test]
fn host_errors_and_invalid_row_never_become_success_or_eof() {
    for mode in [1, 2, 3, 4, 5] {
        exercise::<0>(
            if mode == 2 || mode == 5 {
                sys::INVALID
            } else {
                sys::INTERNAL
            },
            mode,
        );
    }
}
#[test]
fn invalid_context_does_not_fetch_and_still_consumes_state() {
    for variant in 0..7 {
        let drops = Arc::new(AtomicUsize::new(0));
        let instance = (&drops as *const Arc<AtomicUsize>).cast_mut().cast();
        let service = Service::<Policy<0>>::ABI;
        let mut cursor = ptr::null_mut();
        assert_eq!(
            unsafe { service.open.unwrap()(instance, ptr::null(), 0, &mut cursor) },
            sys::OK
        );
        let mut host = Host::new();
        let mut raw = host.raw();
        match variant {
            0 => raw.struct_size -= 1,
            1 => raw.input_count = 65,
            2 => raw.output_column_count = 1025,
            3 => raw.next_input = None,
            4 => raw.emit = None,
            5 => raw.check_interrupt = None,
            _ => raw.reserved[0] = 1,
        }
        assert_eq!(
            unsafe { service.next.unwrap()(instance, cursor, &raw) },
            sys::INVALID
        );
        assert_eq!(host.calls, 0);
        assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
        assert_eq!(drops.load(Ordering::Relaxed), 1);
    }
}
