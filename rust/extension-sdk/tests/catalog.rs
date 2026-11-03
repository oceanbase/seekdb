// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    catalog::{
        CatalogContext, Installer, RoutineKind, Service, TransactionContext, TransactionalInstaller,
    },
    sys, Result,
};
use std::{
    ffi::{c_char, c_void},
    mem::size_of,
    ptr,
};

struct Sink {
    sql: Vec<String>,
    fail: bool,
}
unsafe extern "C" fn emit(opaque: *mut c_void, data: *const c_char, size: u64) -> sys::Status {
    let sink = unsafe { &mut *opaque.cast::<Sink>() };
    if sink.fail {
        return sys::UNAVAILABLE;
    }
    let text = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), size as usize) };
    sink.sql.push(std::str::from_utf8(text).unwrap().to_owned());
    sys::OK
}
fn raw(sink: &mut Sink) -> sys::CatalogContext {
    sys::CatalogContext {
        struct_size: size_of::<sys::CatalogContext>() as u32,
        reserved_word: 0,
        tenant_id: 1,
        database_id: 10,
        owner_id: 20,
        extension_name: c"test_ops".as_ptr(),
        extension_version: c"1.0".as_ptr(),
        host_context: ptr::from_mut(sink).cast(),
        emit_sql: Some(emit),
        reserved: [0; 4],
    }
}
struct Good;
impl Installer for Good {
    fn prepare(_: *mut sys::Handle, context: &mut CatalogContext<'_>) -> Result<()> {
        assert_eq!(
            (
                context.tenant_id(),
                context.database_id(),
                context.owner_id()
            ),
            (1, 10, 20)
        );
        assert_eq!(
            (context.extension_name(), context.extension_version()),
            ("test_ops", "1.0")
        );
        context.declare_sql("CREATE FUNCTION f() RETURNS INT RETURN 1; -- trailing")?;
        context.declare_sql("CREATE PROCEDURE p() BEGIN SELECT 'a;b'; END;")
    }
}
struct Ignore;
impl Installer for Ignore {
    fn prepare(_: *mut sys::Handle, context: &mut CatalogContext<'_>) -> Result<()> {
        let _ = context.declare_sql("SELECT 1;");
        let _ = context.declare_sql("SELECT 2;");
        Ok(())
    }
}
struct Invalid;
impl Installer for Invalid {
    fn prepare(_: *mut sys::Handle, context: &mut CatalogContext<'_>) -> Result<()> {
        let _ = context.declare_sql("bad\0SQL");
        let _ = context.declare_sql("SELECT 1;");
        Ok(())
    }
}
struct Panic;
impl Installer for Panic {
    fn prepare(_: *mut sys::Handle, _: &mut CatalogContext<'_>) -> Result<()> {
        panic!("fixture");
    }
}
#[test]
fn copies_separate_fragments_and_keeps_errors_sticky() {
    let mut sink = Sink {
        sql: Vec::new(),
        fail: false,
    };
    assert_eq!(
        unsafe { Service::<Good>::V1.prepare.unwrap()(ptr::dangling_mut(), &raw(&mut sink)) },
        sys::OK
    );
    assert_eq!(sink.sql.len(), 2);
    assert!(sink.sql[0].ends_with("-- trailing"));
    sink.sql.clear();
    sink.fail = true;
    assert_eq!(
        unsafe { Service::<Ignore>::V1.prepare.unwrap()(ptr::dangling_mut(), &raw(&mut sink)) },
        sys::UNAVAILABLE
    );
    sink.fail = false;
    assert_eq!(
        unsafe { Service::<Invalid>::V1.prepare.unwrap()(ptr::dangling_mut(), &raw(&mut sink)) },
        sys::INVALID
    );
    assert_eq!(
        unsafe { Service::<Panic>::V1.prepare.unwrap()(ptr::dangling_mut(), &raw(&mut sink)) },
        sys::INTERNAL
    );
    assert!(sink.sql.is_empty());
}
#[test]
fn rejects_short_context_and_invalid_identity_before_callback() {
    let mut sink = Sink {
        sql: Vec::new(),
        fail: false,
    };
    let callback = Service::<Good>::V1.prepare.unwrap();
    let mut context = raw(&mut sink);
    assert_eq!(unsafe { callback(ptr::null_mut(), &context) }, sys::INVALID);
    context.struct_size = 0;
    assert_eq!(
        unsafe { callback(ptr::dangling_mut(), &context) },
        sys::UNSUPPORTED_ABI
    );
    context = raw(&mut sink);
    context.reserved[0] = 1;
    assert_eq!(
        unsafe { callback(ptr::dangling_mut(), &context) },
        sys::INVALID
    );
    context = raw(&mut sink);
    context.owner_id = 0;
    assert_eq!(
        unsafe { callback(ptr::dangling_mut(), &context) },
        sys::INVALID
    );
    assert!(sink.sql.is_empty());
}

struct BuildSink {
    calls: u64,
    failure: bool,
    invalid_id: bool,
}
unsafe extern "C" fn create(
    opaque: *mut c_void,
    data: *const c_char,
    size: u64,
    id: *mut u64,
) -> sys::Status {
    let sink = unsafe { &mut *opaque.cast::<BuildSink>() };
    let sql = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), size as usize) };
    assert!(std::str::from_utf8(sql)
        .unwrap()
        .starts_with("CREATE FUNCTION"));
    sink.calls += 1;
    unsafe {
        *id = if sink.invalid_id { 0 } else { 100 + sink.calls };
    }
    if sink.failure {
        sys::UNAVAILABLE
    } else {
        sys::OK
    }
}
fn build_raw(sink: &mut BuildSink) -> sys::CatalogBuildContext {
    sys::CatalogBuildContext {
        struct_size: size_of::<sys::CatalogBuildContext>() as u32,
        reserved_word: 0,
        tenant_id: 1,
        database_id: 10,
        owner_id: 20,
        extension_name: c"built".as_ptr(),
        extension_version: c"1".as_ptr(),
        host_context: ptr::from_mut(sink).cast(),
        create_routine: Some(create),
        reserved: [0; 4],
    }
}
struct BuildGood;
struct BuildIgnore;
struct BuildPanic;
macro_rules! empty_prepare {
    ($t:ty) => {
        impl Installer for $t {
            fn prepare(_: *mut sys::Handle, _: &mut CatalogContext<'_>) -> Result<()> {
                Ok(())
            }
        }
    };
}
empty_prepare!(BuildGood);
empty_prepare!(BuildIgnore);
empty_prepare!(BuildPanic);
impl TransactionalInstaller for BuildGood {
    fn build(_: *mut sys::Handle, context: &mut TransactionContext<'_>) -> Result<()> {
        assert_eq!(
            (
                context.tenant_id(),
                context.database_id(),
                context.owner_id()
            ),
            (1, 10, 20)
        );
        assert_eq!(
            (context.extension_name(), context.extension_version()),
            ("built", "1")
        );
        let first = context.create_routine("CREATE FUNCTION f() RETURNS INT RETURN 1;")?;
        let second = context.create_routine("CREATE FUNCTION g() RETURNS INT RETURN f();")?;
        assert_eq!((first.value(), second.value()), (101, 102));
        Ok(())
    }
}
impl TransactionalInstaller for BuildIgnore {
    fn build(_: *mut sys::Handle, context: &mut TransactionContext<'_>) -> Result<()> {
        let _ = context.create_routine("CREATE FUNCTION f() RETURNS INT RETURN 1;");
        let _ = context.create_routine("CREATE FUNCTION g() RETURNS INT RETURN f();");
        Ok(())
    }
}
impl TransactionalInstaller for BuildPanic {
    fn build(_: *mut sys::Handle, _: &mut TransactionContext<'_>) -> Result<()> {
        panic!("build fixture");
    }
}
#[test]
fn transaction_build_ids_failures_and_panic_stay_in_the_callback() {
    let mut sink = BuildSink {
        calls: 0,
        failure: false,
        invalid_id: false,
    };
    assert_eq!(Service::<BuildGood>::V1.spi_minor, 0);
    assert_eq!(Service::<BuildGood>::V2.v1.spi_minor, 1);
    assert_eq!(
        Service::<BuildGood>::V2.v1.struct_size as usize,
        size_of::<sys::CatalogServiceV2>()
    );
    assert_eq!(
        unsafe {
            Service::<BuildGood>::V2.build.unwrap()(ptr::dangling_mut(), &build_raw(&mut sink))
        },
        sys::OK
    );
    assert_eq!(sink.calls, 2);
    for invalid_id in [false, true] {
        sink = BuildSink {
            calls: 0,
            failure: !invalid_id,
            invalid_id,
        };
        assert_eq!(
            unsafe {
                Service::<BuildIgnore>::V2.build.unwrap()(
                    ptr::dangling_mut(),
                    &build_raw(&mut sink),
                )
            },
            if invalid_id {
                sys::INVALID
            } else {
                sys::UNAVAILABLE
            }
        );
        assert_eq!(sink.calls, 1); // A swallowed error cannot make another host call.
    }
    assert_eq!(
        unsafe {
            Service::<BuildPanic>::V2.build.unwrap()(ptr::dangling_mut(), &build_raw(&mut sink))
        },
        sys::INTERNAL
    );
}
#[test]
fn transaction_build_rejects_malformed_context_before_host_calls() {
    let mut sink = BuildSink {
        calls: 0,
        failure: false,
        invalid_id: false,
    };
    let callback = Service::<BuildGood>::V2.build.unwrap();
    for fault in 0..9 {
        let mut context = build_raw(&mut sink);
        match fault {
            0 => context.struct_size = 0,
            1 => context.reserved[0] = 1,
            2 => context.owner_id = 0,
            3 => context.database_id = u64::MAX,
            4 => context.create_routine = None,
            5 => context.host_context = ptr::null_mut(),
            6 => context.extension_name = ptr::null(),
            7 => context.extension_version = c"".as_ptr(),
            _ => context.tenant_id = 2,
        }
        assert_ne!(unsafe { callback(ptr::dangling_mut(), &context) }, sys::OK);
    }
    assert_eq!(sink.calls, 0);
}

#[derive(Default)]
struct LookupSink {
    calls: usize,
    creates: usize,
    failure: bool,
    invalid_id: bool,
}
unsafe extern "C" fn lookup(
    opaque: *mut c_void,
    kind: u32,
    name: *const c_char,
    len: u64,
    id: *mut u64,
) -> sys::Status {
    let sink = unsafe { &mut *opaque.cast::<LookupSink>() };
    sink.calls += 1;
    let name = unsafe { std::slice::from_raw_parts(name.cast::<u8>(), len as usize) };
    assert!(kind == sys::CATALOG_ROUTINE_FUNCTION || kind == sys::CATALOG_ROUTINE_PROCEDURE);
    unsafe {
        *id = if sink.invalid_id {
            u64::MAX
        } else if name == b"missing" {
            0
        } else {
            42
        };
    }
    if sink.failure {
        sys::UNAVAILABLE
    } else {
        sys::OK
    }
}
unsafe extern "C" fn lookup_create(
    opaque: *mut c_void,
    _: *const c_char,
    _: u64,
    id: *mut u64,
) -> sys::Status {
    let sink = unsafe { &mut *opaque.cast::<LookupSink>() };
    sink.creates += 1;
    unsafe {
        *id = 42;
    }
    sys::OK
}
fn lookup_raw(sink: &mut LookupSink) -> sys::CatalogBuildContextV2 {
    sys::CatalogBuildContextV2 {
        v1: sys::CatalogBuildContext {
            struct_size: size_of::<sys::CatalogBuildContextV2>() as u32,
            reserved_word: 0,
            tenant_id: 1,
            database_id: 10,
            owner_id: 20,
            extension_name: c"built".as_ptr(),
            extension_version: c"1".as_ptr(),
            host_context: ptr::from_mut(sink).cast(),
            create_routine: Some(lookup_create),
            reserved: [0; 4],
        },
        lookup_routine: Some(lookup),
        reserved: [0; 4],
    }
}
struct LookupGood;
struct LookupIgnore;
struct LookupInvalid;
empty_prepare!(LookupGood);
empty_prepare!(LookupIgnore);
empty_prepare!(LookupInvalid);
impl TransactionalInstaller for LookupGood {
    fn build(_: *mut sys::Handle, context: &mut TransactionContext<'_>) -> Result<()> {
        assert!(context.supports_lookup());
        assert_eq!(
            context.lookup_routine(RoutineKind::Function, "missing")?,
            None
        );
        let created = context.create_routine("CREATE FUNCTION f() RETURNS INT RETURN 1;")?;
        assert_eq!(
            context.lookup_routine(RoutineKind::Function, "f")?,
            Some(created)
        );
        assert_eq!(
            context
                .lookup_routine(RoutineKind::Procedure, "过程")?
                .unwrap()
                .value(),
            42
        );
        Ok(())
    }
}
impl TransactionalInstaller for LookupIgnore {
    fn build(_: *mut sys::Handle, context: &mut TransactionContext<'_>) -> Result<()> {
        let _ = context.lookup_routine(RoutineKind::Function, "f");
        let _ = context.lookup_routine(RoutineKind::Procedure, "p");
        let _ = context.create_routine("CREATE FUNCTION f() RETURNS INT RETURN 1;");
        Ok(())
    }
}
impl TransactionalInstaller for LookupInvalid {
    fn build(_: *mut sys::Handle, context: &mut TransactionContext<'_>) -> Result<()> {
        assert_eq!(
            context.lookup_routine(RoutineKind::Function, "f\0g"),
            Err(sys::INVALID)
        );
        let _ = context.lookup_routine(RoutineKind::Function, "f");
        let _ = context.create_routine("CREATE FUNCTION f() RETURNS INT RETURN 1;");
        Ok(())
    }
}
#[test]
fn lookup_absence_identity_and_cross_operation_sticky_errors() {
    let mut sink = LookupSink::default();
    let raw = lookup_raw(&mut sink);
    assert_eq!(
        unsafe {
            Service::<LookupGood>::V2.build.unwrap()(
                ptr::dangling_mut(),
                ptr::from_ref(&raw).cast(),
            )
        },
        sys::OK
    );
    assert_eq!((sink.calls, sink.creates), (3, 1));
    for invalid_id in [false, true] {
        sink = LookupSink {
            failure: !invalid_id,
            invalid_id,
            ..Default::default()
        };
        let raw = lookup_raw(&mut sink);
        assert_eq!(
            unsafe {
                Service::<LookupIgnore>::V2.build.unwrap()(
                    ptr::dangling_mut(),
                    ptr::from_ref(&raw).cast(),
                )
            },
            if invalid_id {
                sys::INVALID
            } else {
                sys::UNAVAILABLE
            }
        );
        assert_eq!((sink.calls, sink.creates), (1, 0));
    }
    sink = LookupSink::default();
    let raw = lookup_raw(&mut sink);
    assert_eq!(
        unsafe {
            Service::<LookupInvalid>::V2.build.unwrap()(
                ptr::dangling_mut(),
                ptr::from_ref(&raw).cast(),
            )
        },
        sys::INVALID
    );
    assert_eq!((sink.calls, sink.creates), (0, 0));
}
#[test]
fn lookup_suffix_is_size_negotiated_without_changing_old_build_context() {
    let mut sink = BuildSink {
        calls: 0,
        failure: false,
        invalid_id: false,
    };
    // These really are short allocations: the new SDK must not read a suffix.
    for extra in [0, 1] {
        let mut raw = build_raw(&mut sink);
        raw.struct_size += extra;
        assert_eq!(
            unsafe { Service::<LookupIgnore>::V2.build.unwrap()(ptr::dangling_mut(), &raw) },
            sys::UNSUPPORTED_ABI
        );
    }
    assert_eq!(sink.calls, 0);
    let mut sink = LookupSink::default();
    let mut raw = lookup_raw(&mut sink);
    raw.reserved[0] = 1;
    assert_eq!(
        unsafe {
            Service::<LookupGood>::V2.build.unwrap()(
                ptr::dangling_mut(),
                ptr::from_ref(&raw).cast(),
            )
        },
        sys::INVALID
    );
    raw.reserved[0] = 0;
    raw.lookup_routine = None;
    assert_eq!(
        unsafe {
            Service::<LookupIgnore>::V2.build.unwrap()(
                ptr::dangling_mut(),
                ptr::from_ref(&raw).cast(),
            )
        },
        sys::UNSUPPORTED_ABI
    );
    assert_eq!((sink.calls, sink.creates), (0, 0));
}
