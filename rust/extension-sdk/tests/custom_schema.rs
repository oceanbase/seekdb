use seekdb_extension::{
    custom_executor::{Context, Encoding, Executor, Service, Step},
    sys,
    table::Cell,
    Result,
};
use std::{ffi::c_void, mem::size_of, ptr};
fn column(id: &std::ffi::CStr, encoding: u32, flags: u32, sql_type: u32) -> sys::CustomColumn {
    let mut column = sys::CustomColumn {
        struct_size: size_of::<sys::CustomColumn>() as u32,
        flags,
        encoding,
        sql_type,
        collation: 63,
        precision: -1,
        scale: -1,
        reserved_word: 0,
        type_id: [0; 256],
        reserved: [0; 4],
    };
    for (dest, byte) in column.type_id.iter_mut().zip(id.to_bytes_with_nul()) {
        *dest = *byte as _;
    }
    column
}
struct Probe;
impl Executor for Probe {
    type State = u8;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn open(_: *mut sys::Handle, plan: &[u8]) -> Result<u8> {
        Ok(plan[0])
    }
    fn next(mode: &mut u8, context: &mut Context<'_>) -> Result<Step> {
        if *mode == 6 {
            assert!(!context.has_schema());
            assert!(matches!(context.input_schema(0), Err(sys::UNSUPPORTED_ABI)));
            assert!(matches!(context.output_schema(), Err(sys::UNSUPPORTED_ABI)));
            return Ok(Step::End);
        }
        assert!(context.has_schema());
        let input = context.input_schema(0)?;
        let other = context.input_schema(1)?;
        let output = context.output_schema()?;
        assert!(other.is_empty());
        assert!(matches!(context.input_schema(2), Err(sys::INVALID)));
        assert!(matches!(other.column(0), Err(sys::INVALID)));
        if *mode == 5 {
            assert!(input.is_empty() && output.is_empty());
            context.emit(&[])?;
            return Ok(Step::Row);
        }
        assert_eq!(input.len(), 2);
        assert_eq!(output.len(), 2);
        let first = input.column(0)?;
        let second = input.column(1)?;
        assert_eq!(first.type_id(), c"core.type.bool");
        assert_eq!(first.encoding(), Encoding::Bool);
        assert!(!first.nullable());
        assert_eq!(first.sql_type(), 5);
        assert_eq!(first.collation(), 63);
        assert_eq!(first.precision(), -1);
        assert_eq!(first.scale(), -1);
        assert!(second.nullable() && second.stored());
        assert_eq!(second.encoding(), Encoding::Bytes);
        if *mode == 0 {
            assert!(context.next_input(0)?.is_none());
            return Ok(Step::End);
        }
        if *mode == 1 {
            let _ = context.next_input(0);
            return Ok(Step::End);
        }
        let boolean = [if *mode == 2 { 2 } else { 1 }];
        let _ = context.emit(&[
            Cell {
                type_id: c"core.type.bool",
                bytes: if *mode == 4 { None } else { Some(&boolean) },
            },
            Cell {
                type_id: c"core.type.bytes",
                bytes: Some(&[]),
            },
        ]);
        Ok(if *mode == 3 { Step::Row } else { Step::End })
    }
    fn rescan(_: &mut u8) -> Result<()> {
        Ok(())
    }
    fn close(_: u8) -> Result<()> {
        Ok(())
    }
}
struct Host {
    columns: [sys::CustomColumn; 2],
    schemas: [sys::CustomSchema; 2],
    values: [sys::Value; 2],
    reads: usize,
    writes: usize,
    mode: u8,
}
impl Host {
    fn new(mode: u8) -> Self {
        Self {
            columns: [
                column(c"core.type.bool", 2, 0, 5),
                column(c"core.type.bytes", 0, 3, 22),
            ],
            schemas: std::array::from_fn(|_| sys::CustomSchema {
                struct_size: size_of::<sys::CustomSchema>() as u32,
                column_count: 0,
                columns: ptr::null(),
                reserved: [0; 4],
            }),
            values: [
                sys::Value {
                    struct_size: size_of::<sys::Value>() as u32,
                    type_id: c"core.type.bool".as_ptr(),
                    data: b"\x02".as_ptr(),
                    data_size: 1,
                    is_null: 0,
                    reserved_bytes: [0; 7],
                    reserved: [0; 4],
                },
                sys::Value {
                    struct_size: size_of::<sys::Value>() as u32,
                    type_id: c"core.type.bytes".as_ptr(),
                    data: ptr::null(),
                    data_size: 0,
                    is_null: 1,
                    reserved_bytes: [0; 7],
                    reserved: [0; 4],
                },
            ],
            reads: 0,
            writes: 0,
            mode,
        }
    }
    fn raw(&mut self) -> sys::CustomContextV2 {
        self.schemas[0].column_count = if self.mode == 5 { 0 } else { 2 };
        self.schemas[0].columns = self.columns.as_ptr();
        sys::CustomContextV2 {
            v1: sys::CustomContext {
                struct_size: size_of::<sys::CustomContextV2>() as u32,
                input_count: 2,
                output_column_count: self.schemas[0].column_count,
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
    host.reads += 1;
    unsafe {
        *db = 0;
    }
    if host.mode == 0 {
        return sys::END_OF_STREAM;
    }
    unsafe {
        (*row).values = host.values.as_ptr();
        (*row).column_count = 2;
    }
    sys::OK
}
unsafe extern "C" fn emit(
    host: *mut c_void,
    _: *const sys::Value,
    _: u32,
    db: *mut i32,
) -> sys::Status {
    unsafe {
        (*host.cast::<Host>()).writes += 1;
        *db = 0;
    }
    sys::OK
}
unsafe extern "C" fn poll(_: *mut c_void, db: *mut i32) -> sys::Status {
    unsafe {
        *db = 0;
    }
    sys::OK
}
fn execute(
    mode: u8,
    mutate: impl FnOnce(&mut Host, &mut sys::CustomContextV2),
    expected: sys::Status,
) -> Host {
    let service = Service::<Probe>::ABI;
    let mut cursor = ptr::null_mut();
    let instance = ptr::dangling_mut();
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &mode, 1, &mut cursor) },
        sys::OK
    );
    let mut host = Host::new(mode);
    let mut context = host.raw();
    mutate(&mut host, &mut context);
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v1) },
        expected
    );
    if expected != sys::OK && expected != sys::END_OF_STREAM {
        assert_eq!(
            unsafe { service.next.unwrap()(instance, cursor, &context.v1) },
            sys::FAILED_PRECONDITION
        );
        assert_eq!(
            unsafe { service.rescan.unwrap()(instance, cursor) },
            sys::OK
        );
    }
    assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
    host
}
#[test]
fn metadata_precedes_empty_input_zero_columns_and_v1_absence() {
    let empty = execute(0, |_, _| {}, sys::END_OF_STREAM);
    assert_eq!((empty.reads, empty.writes), (1, 0));
    let zero = execute(5, |_, _| {}, sys::OK);
    assert_eq!((zero.reads, zero.writes), (0, 1));
    let legacy = execute(
        6,
        |_, c| c.v1.struct_size = size_of::<sys::CustomContext>() as u32,
        sys::END_OF_STREAM,
    );
    assert_eq!((legacy.reads, legacy.writes), (0, 0));
    let valid = execute(3, |_, _| {}, sys::OK);
    assert_eq!(valid.writes, 1);
}
#[test]
fn invalid_metadata_never_reaches_executor_or_host_io() {
    for mode in 0..15 {
        let host = execute(
            0,
            |host, c| match mode {
                0 => c.v1.struct_size = size_of::<sys::CustomContext>() as u32 + 1,
                1 => c.reserved[0] = 1,
                2 => c.inputs = ptr::null(),
                3 => c.output = ptr::null(),
                4 => host.schemas[0].struct_size -= 1,
                5 => host.schemas[0].column_count = 1025,
                6 => host.schemas[0].columns = ptr::null(),
                7 => host.schemas[1].reserved[0] = 1,
                8 => host.columns[0].struct_size -= 1,
                9 => host.columns[0].flags = 8,
                10 => host.columns[0].type_id.fill(b'x' as _),
                11 => host.columns[0].reserved[0] = 1,
                12 => host.columns[0].encoding = 99,
                13 => host.columns[0].encoding = 0,
                14 => c.v1.output_column_count = 1,
                _ => unreachable!(),
            },
            sys::INVALID,
        );
        assert_eq!((host.reads, host.writes), (0, 0));
    }
}
#[test]
fn schema_mismatches_are_sticky_even_if_handler_ignores_them() {
    for mode in [1, 2, 4] {
        let host = execute(mode, |_, _| {}, sys::INVALID);
        assert_eq!(host.writes, 0);
    }
    for field in 0..3 {
        let host = execute(
            1,
            |host, _| {
                host.values[0].data = b"\x01".as_ptr();
                match field {
                    0 => host.values[0].type_id = c"org.example.bool".as_ptr(),
                    1 => host.values[0].data_size = 0,
                    _ => {
                        host.values[0].is_null = 1;
                        host.values[0].data = ptr::null();
                        host.values[0].data_size = 0;
                    }
                }
            },
            sys::INVALID,
        );
        assert_eq!(host.reads, 1);
        assert_eq!(host.writes, 0);
    }
}
