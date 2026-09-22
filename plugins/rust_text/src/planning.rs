// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{optimizer, sys, Call, FunctionDefinition, Result};
use std::{
    mem::size_of,
    sync::atomic::{AtomicI64, Ordering},
};

static CALLS: AtomicI64 = AtomicI64::new(0);
pub fn reset() {
    CALLS.store(0, Ordering::Relaxed);
}
pub fn definition() -> optimizer::Definition<'static> {
    optimizer::Definition {
        object_id: c"org.seekdb.rust-text.planning",
        priority: 0,
        flags: 0,
        implementation: super::implementation(c"org.seekdb.rust-text.planning"),
    }
}
struct Planning;
impl optimizer::Hook for Planning {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        super::validate_instance(instance)
    }
    fn invoke(_: *mut sys::Handle, context: &mut optimizer::Context<'_>) -> Result<()> {
        CALLS.fetch_add(1, Ordering::Relaxed);
        context.call_next()
    }
}
pub const fn provide() -> sys::ServiceProvide {
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.planning".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&optimizer::Service::<Planning>::ABI as *const sys::OptimizerService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    }
}
fn count(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
    let value = CALLS.load(Ordering::Relaxed).to_ne_bytes();
    call.emit_bytes(c"core.type.int64", Some(&value))
}
seekdb_extension::scalar_function! {
    counter {
        definition: FunctionDefinition {
            object_id: c"org.seekdb.rust-text.planning-count",
            sql_name: c"seekdb_rust_optimizer_calls",
            argument_types: &[], result_type: c"core.type.int64",
            service_id: c"org.seekdb.rust-text.planning-count",
            minimum_version: sys::Version { major: 1, minor: 0, patch: 0 },
            maximum_version_exclusive: sys::Version { major: 2, minor: 0, patch: 0 },
            required_capabilities: sys::THREAD_SAFE, flags: 0,
        },
        sql: false,
        validate: crate::validate_instance,
        execute: count,
    }
}
pub const COUNTER: FunctionDefinition<'static> = counter::DEFINITION;
pub const fn counter_provide() -> sys::ServiceProvide {
    counter::provide(
        sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        sys::THREAD_SAFE,
    )
}
