use seekdb_extension::{
    custom_executor::{Context, Executor, Service, Step},
    sys, Result,
};
use std::{ffi::c_void, mem::size_of, ptr};

struct Policy;
impl Executor for Policy {
    type State = u8;
    const INPUT_RESCAN: bool = true;
    fn validate_instance(p: *mut sys::Handle) -> Result<()> {
        if p.is_null() {
            Err(sys::INVALID)
        } else {
            Ok(())
        }
    }
    fn open(p: *mut sys::Handle, _: &[u8]) -> Result<u8> {
        Ok(unsafe { *p.cast::<u8>() })
    }
    fn next(mode: &mut u8, c: &mut Context<'_>) -> Result<Step> {
        if *mode == 0 {
            assert!(c.has_input_rescan());
            c.rescan_input(1)?; // Before the first read is valid.
            assert!(c.next_input(0)?.is_some());
            assert!(c.next_input(1)?.is_some());
            assert!(c.next_input(1)?.is_some());
            assert!(c.next_input(1)?.is_none());
            c.rescan_input(1)?;
            c.rescan_input(1)?; // Repeated rewinds do not reset input zero.
            assert!(c.next_input(1)?.is_some());
            assert!(c.next_input(0)?.is_some());
            assert!(c.next_input(0)?.is_none());
            c.emit(&[])?;
            return Ok(Step::Row);
        }
        if *mode == 3 {
            c.emit(&[])?;
        }
        let _ = c.rescan_input(if *mode == 2 { 2 } else { 1 });
        // Deliberately ignore the result: no swallowed failure can become EOF.
        Ok(if *mode == 3 { Step::Row } else { Step::End })
    }
    fn rescan(_: &mut u8) -> Result<()> {
        Ok(())
    }
}
struct Host {
    positions: [u32; 2],
    rewinds: [u32; 2],
    calls: u32,
    polls: u32,
    cancel_at: u32,
    writes: u32,
    code: sys::Status,
    db: i32,
    schemas: [sys::CustomSchema; 2],
}
impl Host {
    fn new() -> Self {
        Self {
            positions: [0; 2],
            rewinds: [0; 2],
            calls: 0,
            polls: 0,
            cancel_at: 0,
            writes: 0,
            code: sys::OK,
            db: 0,
            schemas: std::array::from_fn(|_| sys::CustomSchema {
                struct_size: size_of::<sys::CustomSchema>() as u32,
                column_count: 0,
                columns: ptr::null(),
                reserved: [0; 4],
            }),
        }
    }
    fn raw(&mut self) -> sys::CustomContextV3 {
        sys::CustomContextV3 {
            v2: sys::CustomContextV2 {
                v1: sys::CustomContext {
                    struct_size: size_of::<sys::CustomContextV3>() as u32,
                    input_count: 2,
                    output_column_count: 0,
                    reserved_word: 0,
                    host_context: (self as *mut Self).cast(),
                    next_input: Some(input),
                    emit: Some(emit),
                    check_interrupt: Some(poll),
                    reserved: [0; 4],
                },
                inputs: self.schemas.as_ptr(),
                output: &self.schemas[0],
                reserved: [0; 4],
            },
            rescan_input: Some(rewind),
            reserved: [0; 4],
        }
    }
}
unsafe extern "C" fn input(
    p: *mut c_void,
    index: u32,
    row: *mut sys::CustomRow,
    db: *mut i32,
) -> sys::Status {
    let h = unsafe { &mut *p.cast::<Host>() };
    h.calls += 1;
    unsafe {
        *db = 0;
    }
    if h.positions[index as usize] == 2 {
        return sys::END_OF_STREAM;
    }
    h.positions[index as usize] += 1;
    unsafe {
        *row = sys::CustomRow {
            struct_size: size_of::<sys::CustomRow>() as u32,
            column_count: 0,
            values: ptr::null(),
            reserved: [0; 4],
        };
    }
    sys::OK
}
unsafe extern "C" fn rewind(p: *mut c_void, index: u32, db: *mut i32) -> sys::Status {
    let h = unsafe { &mut *p.cast::<Host>() };
    h.rewinds[index as usize] += 1;
    h.positions[index as usize] = 0;
    unsafe {
        *db = h.db;
    }
    h.code
}
unsafe extern "C" fn emit(
    p: *mut c_void,
    _: *const sys::Value,
    count: u32,
    db: *mut i32,
) -> sys::Status {
    assert_eq!(count, 0);
    let h = unsafe { &mut *p.cast::<Host>() };
    h.writes += 1;
    unsafe {
        *db = 0;
    }
    sys::OK
}
unsafe extern "C" fn poll(p: *mut c_void, db: *mut i32) -> sys::Status {
    let h = unsafe { &mut *p.cast::<Host>() };
    h.polls += 1;
    unsafe {
        *db = if h.polls == h.cancel_at { -4012 } else { 0 };
    }
    if h.polls == h.cancel_at {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
fn exercise(
    mode: u8,
    h: &mut Host,
    mutate: impl FnOnce(&mut sys::CustomContextV3),
    expected: sys::Status,
) {
    let mut mode = mode;
    let instance = (&mut mode as *mut u8).cast();
    let abi = Service::<Policy>::ABI;
    assert_eq!(abi.spi_minor, 1);
    let mut cursor = ptr::null_mut();
    assert_eq!(
        unsafe { abi.open.unwrap()(instance, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    let mut raw = h.raw();
    mutate(&mut raw);
    assert_eq!(
        unsafe { abi.next.unwrap()(instance, cursor, &raw.v2.v1) },
        expected
    );
    if expected != sys::OK && expected != sys::END_OF_STREAM {
        let calls = h.calls;
        let rewinds = h.rewinds;
        assert_eq!(
            unsafe { abi.next.unwrap()(instance, cursor, &raw.v2.v1) },
            sys::FAILED_PRECONDITION
        );
        assert_eq!(h.calls, calls);
        assert_eq!(h.rewinds, rewinds);
        // A complete operator rescan, not selective rewind, clears the poison.
        assert_eq!(unsafe { abi.rescan.unwrap()(instance, cursor) }, sys::OK);
    }
    assert_eq!(unsafe { abi.close.unwrap()(instance, cursor) }, sys::OK);
}
#[test]
fn independent_rewinds_preserve_sibling_position_and_zero_column_rows() {
    let mut h = Host::new();
    exercise(0, &mut h, |_| {}, sys::OK);
    assert_eq!(h.positions, [2, 1]);
    assert_eq!(h.rewinds, [0, 3]);
    assert_eq!(h.calls, 7);
    assert_eq!(h.writes, 1);
}
#[test]
fn ignored_control_errors_and_invalid_call_order_are_sticky() {
    for (code, db, expected) in [
        (sys::INTERNAL, -4012, sys::INTERNAL),
        (sys::OK, -4012, sys::FAILED_PRECONDITION),
        (sys::END_OF_STREAM, 0, sys::INVALID),
    ] {
        let mut h = Host::new();
        h.code = code;
        h.db = db;
        exercise(1, &mut h, |_| {}, expected);
        assert_eq!(h.rewinds, [0, 1]);
        assert_eq!(h.calls, 0);
    }
    for mode in [2, 3] {
        let mut h = Host::new();
        exercise(mode, &mut h, |_| {}, sys::INVALID);
        assert_eq!(h.rewinds, [0; 2]);
        assert_eq!(h.writes, u32::from(mode == 3));
    }
    for cancel in [2, 3] {
        let mut h = Host::new();
        h.cancel_at = cancel;
        exercise(1, &mut h, |_| {}, sys::INTERNAL);
        assert_eq!(h.rewinds[1], u32::from(cancel == 3));
    }
}
#[test]
fn incomplete_control_is_rejected_before_algorithm_or_host_callback() {
    for fault in 0..4 {
        let mut h = Host::new();
        exercise(
            0,
            &mut h,
            |c| match fault {
                0 => c.rescan_input = None,
                1 => c.reserved[3] = 1,
                2 => c.v2.v1.struct_size -= 1,
                _ => c.v2.reserved[0] = 1,
            },
            sys::INVALID,
        );
        assert_eq!(h.polls, 0);
        assert_eq!(h.calls, 0);
        assert_eq!(h.rewinds, [0; 2]);
    }
}
#[test]
fn older_contexts_do_not_claim_input_control() {
    for size in [
        size_of::<sys::CustomContext>(),
        size_of::<sys::CustomContextV2>(),
    ] {
        let mut h = Host::new();
        exercise(
            1,
            &mut h,
            |c| c.v2.v1.struct_size = size as u32,
            sys::UNSUPPORTED_ABI,
        );
        assert_eq!(h.rewinds, [0; 2]);
        assert_eq!(h.calls, 0);
    }
}

struct BindingPolicy;
impl Executor for BindingPolicy {
    type State = u8;
    // Binding support implies the independent-control prefix, without needing
    // the older flag. Host snapshots/parameter ownership are tested in kernel.
    const INPUT_BINDINGS: bool = true;
    fn validate_instance(p: *mut sys::Handle) -> Result<()> {
        Policy::validate_instance(p)
    }
    fn open(p: *mut sys::Handle, bytes: &[u8]) -> Result<u8> {
        Policy::open(p, bytes)
    }
    fn next(mode: &mut u8, c: &mut Context<'_>) -> Result<Step> {
        if *mode == 0 {
            assert!(c.has_input_bindings() && c.has_input_rescan());
            assert!(c.next_input(0)?.is_some());
            c.bind_rescan_input(1)?;
            assert!(c.next_input(1)?.is_some());
            c.bind_rescan_input(1)?; // Same source row can be rebound.
            c.rescan_input(1)?; // The older prefix remains callable.
            assert!(c.next_input(1)?.is_some());
            assert!(c.next_input(0)?.is_some());
            c.bind_rescan_input(1)?;
            c.emit(&[])?;
            return Ok(Step::Row);
        }
        if *mode == 3 {
            c.emit(&[])?;
        }
        if *mode == 4 {
            assert!(!c.has_input_bindings());
        }
        let _ = c.bind_rescan_input(if *mode == 2 { 2 } else { 1 });
        Ok(if *mode == 3 { Step::Row } else { Step::End })
    }
    fn rescan(_: &mut u8) -> Result<()> {
        Ok(())
    }
}
fn exercise_binding(
    mut mode: u8,
    h: &mut Host,
    mutate: impl FnOnce(&mut sys::CustomContextV4),
    expected: sys::Status,
) {
    let instance = (&mut mode as *mut u8).cast();
    let abi = Service::<BindingPolicy>::ABI;
    assert_eq!(abi.spi_minor, 2);
    let mut cursor = ptr::null_mut();
    assert_eq!(
        unsafe { abi.open.unwrap()(instance, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    let mut raw = sys::CustomContextV4 {
        v3: h.raw(),
        bind_rescan_input: Some(rewind),
        reserved: [0; 4],
    };
    raw.v3.v2.v1.struct_size = size_of::<sys::CustomContextV4>() as u32;
    mutate(&mut raw);
    assert_eq!(
        unsafe { abi.next.unwrap()(instance, cursor, &raw.v3.v2.v1) },
        expected
    );
    if expected != sys::OK && expected != sys::END_OF_STREAM {
        let before = (h.calls, h.rewinds, h.polls, h.writes);
        assert_eq!(
            unsafe { abi.next.unwrap()(instance, cursor, &raw.v3.v2.v1) },
            sys::FAILED_PRECONDITION
        );
        assert_eq!((h.calls, h.rewinds, h.polls, h.writes), before);
        assert_eq!(unsafe { abi.rescan.unwrap()(instance, cursor) }, sys::OK);
    }
    assert_eq!(unsafe { abi.close.unwrap()(instance, cursor) }, sys::OK);
}
#[test]
fn binding_control_transports_rebinds_and_preserves_older_control() {
    let mut h = Host::new();
    exercise_binding(0, &mut h, |_| {}, sys::OK);
    assert_eq!(h.positions, [2, 0]);
    assert_eq!(h.rewinds, [0, 4]);
    assert_eq!(h.calls, 4);
    assert_eq!(h.writes, 1);
}
#[test]
fn binding_errors_order_and_cancel_cannot_be_swallowed() {
    for (code, db, expected) in [
        (sys::INTERNAL, -4012, sys::INTERNAL),
        (sys::OK, -4012, sys::FAILED_PRECONDITION),
        (sys::END_OF_STREAM, 0, sys::INVALID),
    ] {
        let mut h = Host::new();
        h.code = code;
        h.db = db;
        exercise_binding(1, &mut h, |_| {}, expected);
        assert_eq!(h.rewinds, [0, 1]);
        assert_eq!(h.calls, 0);
    }
    for mode in [2, 3] {
        let mut h = Host::new();
        exercise_binding(mode, &mut h, |_| {}, sys::INVALID);
        assert_eq!(h.rewinds, [0; 2]);
        assert_eq!(h.writes, u32::from(mode == 3));
    }
    for cancel in [2, 3] {
        let mut h = Host::new();
        h.cancel_at = cancel;
        exercise_binding(1, &mut h, |_| {}, sys::INTERNAL);
        assert_eq!(h.rewinds[1], u32::from(cancel == 3));
    }
}
#[test]
fn malformed_binding_suffix_or_control_prefix_is_rejected_before_callbacks() {
    for fault in 0..6 {
        let mut h = Host::new();
        exercise_binding(
            0,
            &mut h,
            |c| match fault {
                0 => c.bind_rescan_input = None,
                1 => c.reserved[3] = 1,
                2 => c.v3.v2.v1.struct_size -= 1,
                3 => c.v3.rescan_input = None,
                4 => c.v3.reserved[0] = 1,
                _ => c.v3.v2.reserved[0] = 1,
            },
            sys::INVALID,
        );
        assert_eq!((h.polls, h.calls, h.rewinds, h.writes), (0, 0, [0; 2], 0));
    }
}
#[test]
fn older_contexts_do_not_claim_parameter_binding() {
    for size in [
        size_of::<sys::CustomContext>(),
        size_of::<sys::CustomContextV2>(),
        size_of::<sys::CustomContextV3>(),
    ] {
        let mut h = Host::new();
        exercise_binding(
            4,
            &mut h,
            |c| c.v3.v2.v1.struct_size = size as u32,
            sys::UNSUPPORTED_ABI,
        );
        assert_eq!(h.rewinds, [0; 2]);
        assert_eq!(h.calls, 0);
    }
}
