// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sys, CastContext, CastDefinition, DynamicFunctionDefinition, FunctionDefinition,
    ImplementationReference, Registration, TypeDefinition,
};
use std::ffi::{c_void, CStr, CString};
use std::mem::size_of;
use std::ptr;

#[derive(Default)]
struct Host {
    begun: u32,
    aborted: u32,
    committed: u32,
    registered: u32,
    fail_commit: bool,
    fail_register: bool,
    name: String,
    version: u32,
    capabilities: u64,
    copied: Vec<String>,
    properties: Vec<u64>,
    dynamic: bool,
    signature: Vec<String>,
    arity: (u32, u32, u32),
}
unsafe extern "C" fn begin(host: *mut c_void, token: *mut *mut c_void) -> sys::Status {
    unsafe {
        (*host.cast::<Host>()).begun += 1;
        *token = host;
    }
    sys::OK
}
unsafe extern "C" fn abort(host: *mut c_void, _token: *mut c_void) {
    unsafe { (*host.cast::<Host>()).aborted += 1 };
}
unsafe extern "C" fn commit(host: *mut c_void, _token: *mut c_void) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    host.committed += 1;
    if host.fail_commit {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
unsafe extern "C" fn register(
    host: *mut c_void,
    _token: *mut c_void,
    kind: i32,
    descriptor: *const c_void,
    bytes: u32,
) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    if host.fail_register {
        host.fail_register = false;
        return sys::INVALID;
    }
    if kind == 5 {
        return seekdb_extension::boundary(|| {
            assert_eq!(bytes, size_of::<sys::OptimizerHookDescriptor>() as u32);
            let d = unsafe { &*descriptor.cast::<sys::OptimizerHookDescriptor>() };
            assert_eq!(d.struct_size, bytes);
            assert_eq!(d.reserved_word, 0);
            assert_eq!(d.reserved, [0; 4]);
            assert_eq!(d.priority, -3);
            for s in [d.object_id, d.hook_point, d.implementation.service_id] {
                host.copied
                    .push(unsafe { CStr::from_ptr(s) }.to_str().unwrap().into());
            }
            host.registered += 1;
            Ok(())
        });
    }
    if kind == sys::TABLE_FUNCTION {
        return seekdb_extension::boundary(|| {
            assert_eq!(bytes, size_of::<sys::TableFunction>() as u32);
            let d = unsafe { &*descriptor.cast::<sys::TableFunction>() };
            assert_eq!(d.struct_size, bytes);
            assert_eq!(d.reserved, [0; 4]);
            assert_eq!(d.reserved_word, 0);
            assert_eq!(d.signature_flags, 0);
            assert_eq!(d.minimum_arity, d.maximum_arity);
            assert_eq!(d.maximum_arity, d.argument_type_count);
            host.arity = (d.minimum_arity, d.maximum_arity, d.signature_flags);
            host.name = unsafe { CStr::from_ptr(d.sql_name) }
                .to_str()
                .unwrap()
                .into();
            host.capabilities = d.implementation.required_capabilities;
            for i in 0..d.argument_type_count as usize {
                host.signature.push(
                    unsafe { CStr::from_ptr(*d.argument_type_ids.add(i)) }
                        .to_str()
                        .unwrap()
                        .into(),
                );
            }
            if d.argument_type_count == 0 {
                assert!(d.argument_type_ids.is_null());
            }
            for column in unsafe { std::slice::from_raw_parts(d.columns, d.column_count as usize) }
            {
                assert_eq!(column.struct_size, size_of::<sys::TableColumn>() as u32);
                assert_eq!(column.reserved, [0; 4]);
                assert_eq!(column.reserved_bytes, [0; 7]);
                for name in [column.sql_name, column.type_id] {
                    host.copied
                        .push(unsafe { CStr::from_ptr(name) }.to_str().unwrap().into());
                }
                host.properties.push(column.nullable as u64);
            }
            host.registered += 1;
            Ok(())
        });
    }
    if kind == sys::TYPE || kind == sys::CAST {
        return seekdb_extension::boundary(|| {
            let (strings, properties, implementation) = if kind == sys::TYPE {
                if bytes != size_of::<sys::TypeDescriptor>() as u32 {
                    return Err(sys::INVALID);
                }
                let d = unsafe { &*descriptor.cast::<sys::TypeDescriptor>() };
                if d.struct_size != bytes || d.reserved_word != 0 || d.reserved != [0; 4] {
                    return Err(sys::INVALID);
                }
                (
                    vec![d.object_id, d.sql_name, d.physical_format_id],
                    vec![d.physical_format_version as u64, d.flags],
                    &d.codec_service,
                )
            } else {
                if bytes != size_of::<sys::CastDescriptor>() as u32 {
                    return Err(sys::INVALID);
                }
                let d = unsafe { &*descriptor.cast::<sys::CastDescriptor>() };
                if d.struct_size != bytes || d.reserved != [0; 4] {
                    return Err(sys::INVALID);
                }
                (
                    vec![d.object_id, d.source_type_id, d.target_type_id],
                    vec![d.context as u64, d.cost as u64, d.flags],
                    &d.implementation,
                )
            };
            let range = &implementation.version_range;
            if implementation.struct_size != size_of::<sys::Implementation>() as u32
                || implementation.reserved != [0; 4]
                || range.struct_size != size_of::<sys::VersionRange>() as u32
                || range.reserved != [0; 2]
            {
                return Err(sys::INVALID);
            }
            for s in strings.into_iter().chain([implementation.service_id]) {
                host.copied.push(
                    unsafe { CStr::from_ptr(s) }
                        .to_str()
                        .map_err(|_| sys::INVALID)?
                        .to_owned(),
                );
            }
            host.properties.extend(properties);
            host.properties.extend([
                range.minimum_inclusive.major as u64,
                range.minimum_inclusive.minor as u64,
                range.minimum_inclusive.patch as u64,
                range.maximum_exclusive.major as u64,
                range.maximum_exclusive.minor as u64,
                range.maximum_exclusive.patch as u64,
                implementation.required_capabilities,
            ]);
            host.registered += 1;
            Ok(())
        });
    }
    if kind != sys::FUNCTION || bytes != size_of::<sys::FunctionV2>() as u32 {
        return sys::INVALID;
    }
    let descriptor = unsafe { &*descriptor.cast::<sys::FunctionV2>() };
    host.dynamic = descriptor.descriptor.static_result_type_id.is_null();
    host.arity = (
        descriptor.descriptor.minimum_arity,
        descriptor.descriptor.maximum_arity,
        descriptor.signature_flags,
    );
    host.signature.clear();
    for index in 0..descriptor.argument_type_count as usize {
        host.signature.push(
            unsafe { CStr::from_ptr(*descriptor.argument_type_ids.add(index)) }
                .to_str()
                .unwrap()
                .to_owned(),
        );
    }
    host.registered += 1;
    host.name = unsafe { CStr::from_ptr(descriptor.descriptor.sql_name) }
        .to_string_lossy()
        .into_owned();
    host.version = descriptor
        .descriptor
        .implementation
        .version_range
        .minimum_inclusive
        .major;
    host.capabilities = descriptor.descriptor.implementation.required_capabilities;
    sys::OK
}
fn api(host: &mut Host) -> sys::HostApiV2 {
    sys::HostApiV2 {
        host: sys::HostApiV1 {
            struct_size: size_of::<sys::HostApiV2>() as u32,
            abi_major: 1,
            abi_minor: 0,
            host_handle: (host as *mut Host).cast(),
            alloc: None,
            free: None,
            log: None,
            acquire_service: None,
            release_service: None,
            begin_registration: Some(begin),
            register_service: None,
            commit_registration: Some(commit),
            abort_registration: Some(abort),
            reserved: [0; 8],
        },
        registration_spi_major: 1,
        registration_spi_minor: 0,
        register_extension: Some(register),
        registration_reserved: [0; 4],
    }
}

#[test]
fn relation_and_selection_hooks_register_distinct_points_in_one_transaction() {
    let mut host = Host::default();
    let api = api(&mut host);
    let mut registration = unsafe { Registration::begin(&api.host) }.unwrap();
    let definition = seekdb_extension::optimizer::Definition {
        object_id: c"test.policy",
        priority: -3,
        flags: 0,
        implementation: implementation(c"test.service"),
    };
    registration.relation_paths_hook(&definition).unwrap();
    registration.join_paths_hook(&definition).unwrap();
    registration.candidate_hook(&definition).unwrap();
    for stage in [
        seekdb_extension::candidate::UpperStage::Group,
        seekdb_extension::candidate::UpperStage::Window,
        seekdb_extension::candidate::UpperStage::Distinct,
        seekdb_extension::candidate::UpperStage::Ordered,
    ] {
        registration.upper_paths_hook(stage, &definition).unwrap();
    }
    registration.commit().unwrap();
    assert_eq!(
        (host.begun, host.committed, host.aborted, host.registered),
        (1, 1, 0, 7)
    );
    assert_eq!(
        host.copied,
        [
            "test.policy",
            "optimizer.relation.paths.v1",
            "test.service",
            "test.policy",
            "optimizer.join.paths.v1",
            "test.service",
            "test.policy",
            "optimizer.candidate.select.v1",
            "test.service",
            "test.policy",
            "optimizer.upper.group.paths.v1",
            "test.service",
            "test.policy",
            "optimizer.upper.window.paths.v1",
            "test.service",
            "test.policy",
            "optimizer.upper.distinct.paths.v1",
            "test.service",
            "test.policy",
            "optimizer.upper.ordered.paths.v1",
            "test.service"
        ]
    );
}

#[test]
fn unfinished_and_failed_commit_abort_exactly_once() {
    let mut host = Host::default();
    let api = api(&mut host);
    drop(unsafe { Registration::begin(&api.host) }.unwrap());
    assert_eq!((host.begun, host.aborted, host.committed), (1, 1, 0));
    host.fail_commit = true;
    assert_eq!(
        unsafe { Registration::begin(&api.host) }.unwrap().commit(),
        Err(sys::INTERNAL)
    );
    assert_eq!((host.begun, host.aborted, host.committed), (2, 2, 1));
    host.fail_commit = false;
    unsafe { Registration::begin(&api.host) }
        .unwrap()
        .commit()
        .unwrap();
    assert_eq!((host.begun, host.aborted, host.committed), (3, 2, 2));
}

#[test]
fn dynamic_names_and_explicit_implementation_contract_survive_staging_retry() {
    let mut host = Host {
        fail_register: true,
        ..Host::default()
    };
    let api = api(&mut host);
    let mut registration = unsafe { Registration::begin(&api.host) }.unwrap();
    {
        let name = CString::new(format!("generated_function_{}", 42)).unwrap();
        let args = [c"core.type.bytes"];
        let definition = FunctionDefinition {
            object_id: c"test.dynamic",
            sql_name: &name,
            argument_types: &args,
            result_type: c"core.type.int64",
            service_id: c"test.implementation",
            minimum_version: sys::Version {
                major: 7,
                minor: 2,
                patch: 1,
            },
            maximum_version_exclusive: sys::Version {
                major: 8,
                minor: 0,
                patch: 0,
            },
            required_capabilities: 0,
            flags: 0,
        };
        assert_eq!(registration.function(&definition), Err(sys::INVALID));
        registration.function(&definition).unwrap();
    }
    registration.commit().unwrap();
    assert_eq!(host.name, "generated_function_42");
    assert_eq!(
        (host.version, host.capabilities, host.registered),
        (7, 0, 1)
    );
    assert_eq!(host.aborted, 0);
}

#[test]
fn missing_callbacks_or_short_api_never_begin_a_transaction() {
    let mut host = Host::default();
    let mut api = api(&mut host);
    api.host.abort_registration = None;
    assert!(matches!(
        unsafe { Registration::begin(&api.host) },
        Err(sys::UNSUPPORTED_ABI)
    ));
    api.host.abort_registration = Some(abort);
    api.host.struct_size = size_of::<sys::HostApiV1>() as u32;
    assert!(matches!(
        unsafe { Registration::begin(&api.host) },
        Err(sys::UNSUPPORTED_ABI)
    ));
    assert!(matches!(
        unsafe { Registration::begin(ptr::null()) },
        Err(sys::INVALID)
    ));
    assert_eq!(host.begun, 0);
}

#[test]
fn table_registration_copies_columns_in_the_shared_transaction() {
    use seekdb_extension::table::{Column, Definition};
    let mut host = Host::default();
    let api = api(&mut host);
    let mut registration = unsafe { Registration::begin(&api.host) }.unwrap();
    {
        let name = CString::new("generated_rows").unwrap();
        let column = CString::new("generated_column").unwrap();
        let columns = [Column {
            name: &column,
            type_id: c"example.type",
            nullable: true,
        }];
        let mut definition = Definition {
            object_id: c"example.rows",
            sql_name: &name,
            argument_types: &[c"example.type"],
            columns: &columns,
            flags: 0,
            implementation: implementation(c"example.service"),
        };
        registration.table_function(&definition).unwrap();
        definition.columns = &[];
        assert_eq!(registration.table_function(&definition), Err(sys::INVALID));
        definition.columns = &columns;
        definition.argument_types = &[];
        registration.table_function(&definition).unwrap();
    }
    registration.commit().unwrap();
    assert_eq!(host.name, "generated_rows");
    assert_eq!(
        host.copied,
        [
            "generated_column",
            "example.type",
            "generated_column",
            "example.type"
        ]
    );
    assert_eq!(host.properties, [1, 1]);
    assert_eq!(host.signature, ["example.type"]);
    assert_eq!(host.arity, (0, 0, 0));
    assert_eq!(host.capabilities, sys::THREAD_SAFE);
    assert_eq!(
        (host.begun, host.committed, host.aborted, host.registered),
        (1, 1, 0, 2)
    );
}

fn implementation(service_id: &CStr) -> ImplementationReference<'_> {
    ImplementationReference {
        service_id,
        minimum_version: sys::Version {
            major: 2,
            minor: 3,
            patch: 4,
        },
        maximum_version_exclusive: sys::Version {
            major: 5,
            minor: 6,
            patch: 7,
        },
        required_capabilities: sys::THREAD_SAFE,
    }
}

#[test]
fn types_and_casts_copy_dynamic_metadata_and_preserve_explicit_contracts() {
    for context in [
        CastContext::Explicit,
        CastContext::Assignment,
        CastContext::Implicit,
    ] {
        let mut host = Host::default();
        let api = api(&mut host);
        let mut registration = unsafe { Registration::begin(&api.host) }.unwrap();
        {
            let name = CString::new(format!("custom_type_{}", context as i32)).unwrap();
            let format = CString::new("test.format").unwrap();
            registration
                .data_type(&TypeDefinition {
                    object_id: c"test.type",
                    sql_name: &name,
                    physical_format_id: &format,
                    physical_format_version: 42,
                    flags: sys::IMMUTABLE,
                    codec: implementation(c"test.codec"),
                })
                .unwrap();
            registration
                .cast(&CastDefinition {
                    object_id: c"test.cast",
                    source_type_id: c"core.type.bytes",
                    target_type_id: c"test.type",
                    context,
                    cost: 23,
                    flags: sys::DETERMINISTIC,
                    implementation: implementation(c"test.cast-service"),
                })
                .unwrap();
        }
        registration.commit().unwrap();
        assert_eq!(
            host.copied,
            [
                "test.type".to_owned(),
                format!("custom_type_{}", context as i32),
                "test.format".to_owned(),
                "test.codec".to_owned(),
                "test.cast".to_owned(),
                "core.type.bytes".to_owned(),
                "test.type".to_owned(),
                "test.cast-service".to_owned()
            ]
        );
        assert_eq!(
            host.properties,
            [
                42,
                sys::IMMUTABLE,
                2,
                3,
                4,
                5,
                6,
                7,
                sys::THREAD_SAFE,
                context as u64,
                23,
                sys::DETERMINISTIC,
                2,
                3,
                4,
                5,
                6,
                7,
                sys::THREAD_SAFE
            ]
        );
        assert_eq!((host.registered, host.committed, host.aborted), (2, 1, 0));
    }
}

#[test]
fn mixed_registration_failure_retains_one_transaction_and_aborts_once() {
    let mut host = Host {
        fail_register: true,
        fail_commit: true,
        ..Host::default()
    };
    let api = api(&mut host);
    let mut registration = unsafe { Registration::begin(&api.host) }.unwrap();
    let definition = TypeDefinition {
        object_id: c"test.type",
        sql_name: c"test_type",
        physical_format_id: c"test.format",
        physical_format_version: 1,
        flags: 0,
        codec: implementation(c"test.codec"),
    };
    assert_eq!(registration.data_type(&definition), Err(sys::INVALID));
    registration.data_type(&definition).unwrap();
    registration
        .function(&FunctionDefinition {
            object_id: c"test.function",
            sql_name: c"test_function",
            argument_types: &[c"test.type"],
            result_type: c"test.type",
            service_id: c"test.function",
            minimum_version: sys::Version {
                major: 1,
                minor: 0,
                patch: 0,
            },
            maximum_version_exclusive: sys::Version {
                major: 2,
                minor: 0,
                patch: 0,
            },
            required_capabilities: 0,
            flags: 0,
        })
        .unwrap();
    assert_eq!(registration.commit(), Err(sys::INTERNAL));
    assert_eq!(
        (host.begun, host.registered, host.committed, host.aborted),
        (1, 2, 1, 1)
    );
}

#[test]
fn dynamic_registration_preserves_typed_and_untyped_envelopes() {
    for typed in [false, true] {
        let mut host = Host::default();
        let api = api(&mut host);
        let mut registration = unsafe { Registration::begin(&api.host) }.unwrap();
        let owned = CString::new("org.test.type").unwrap();
        let types = [owned.as_c_str()];
        let mut definition = DynamicFunctionDefinition {
            object_id: c"test.dynamic",
            sql_name: c"test_dynamic",
            argument_types: if typed { Some(&types) } else { None },
            minimum_arity: 1,
            maximum_arity: 3,
            variadic: typed,
            implementation: implementation(c"test.resolve"),
            flags: sys::IMMUTABLE,
        };
        registration.dynamic_function(&definition).unwrap();
        definition.maximum_arity = 0;
        assert_eq!(
            registration.dynamic_function(&definition),
            Err(sys::INVALID)
        );
        definition.maximum_arity = 3;
        definition.argument_types = Some(&[]);
        definition.variadic = false;
        assert_eq!(
            registration.dynamic_function(&definition),
            Err(sys::INVALID)
        );
        drop(owned);
        registration.commit().unwrap();
        assert!(host.dynamic);
        assert_eq!(host.arity, (1, 3, u32::from(typed)));
        assert_eq!(
            host.signature,
            if typed {
                vec!["org.test.type".to_owned()]
            } else {
                vec![]
            }
        );
        assert_eq!(
            (host.begun, host.registered, host.committed, host.aborted),
            (1, 1, 1, 0)
        );
    }
}
