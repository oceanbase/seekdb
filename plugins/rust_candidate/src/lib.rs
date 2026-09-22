// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Demonstrates construction and selection, NOT a production cost policy.
#![deny(unsafe_op_in_unsafe_fn)]
use seekdb_extension::{
    candidate, optimizer, server_dev, sys, ImplementationReference, Registration, Result,
};
use std::{
    mem::size_of,
    ptr,
    sync::atomic::{AtomicBool, Ordering},
};

// CMake binds to the final linked host. Cargo tracks both env! and include!
// inputs; switching hosts rebuilds this cdylib, not just its package metadata.
mod contract {
    include!(env!("SEEKDB_SERVER_DEV_CONTRACT"));
}
mod executor;
mod planner;
mod sort_plan;
mod sorter;
static ACTIVE: AtomicBool = AtomicBool::new(false);
fn handle() -> *mut sys::Handle {
    (&ACTIVE as *const AtomicBool).cast_mut().cast()
}
fn validate(instance: *mut sys::Handle) -> Result<()> {
    if instance == handle() {
        Ok(())
    } else {
        Err(sys::INVALID)
    }
}
struct CustomSpool;
impl candidate::Hook for CustomSpool {
    const MODE: candidate::Mode = candidate::Mode::Around;
    const BUILDERS: bool = true;
    const INSPECT: bool = true;
    const QUERY_TARGETS: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        validate(instance)?;
        if ACTIVE.load(Ordering::Acquire) {
            Ok(())
        } else {
            Err(sys::FAILED_PRECONDITION)
        }
    }
    fn invoke(_: *mut sys::Handle, context: &mut candidate::Context<'_>) -> Result<()> {
        let query = context.query()?;
        let mut first_target = None;
        for ordinal in 0..query.target_count.unwrap_or(0) {
            let target = context.target(ordinal)?;
            let _ = context.describe_expression(target)?;
            if ordinal == 0 {
                first_target = Some(target);
            }
        }
        let root = context.root(0)?;
        let input = context.plan(root)?;
        // Inspect actual already-present predicates/order expressions without
        // allocating SQL expressions or treating them as a final output schema.
        for role in [
            candidate::Role::Filter,
            candidate::Role::Startup,
            candidate::Role::Ordering,
            candidate::Role::JoinCondition,
            candidate::Role::JoinFilter,
        ] {
            for index in 0..input.expression_count(role) {
                let (id, _) = context.expression(root, role, index)?;
                let expr = context.describe_expression(id)?;
                for index in 0..expr.argument_count() {
                    let arg = context.argument(id, index)?;
                    let _ = context.describe_expression(arg)?;
                }
            }
        }
        let added = context.custom(&candidate::CustomPath {
            input: 0,
            service_id: c"org.seekdb.rust-candidate.spool",
            service_major: 1,
            minimum_minor: 0,
            plan: &[],
            operator_cost: 1.0 + input.rows * input.width * 0.00001,
            preserves_order: true,
            blocking: true,
        })?;
        context.call_next()?;
        if context.query()?.target_count != query.target_count {
            return Err(sys::FAILED_PRECONDITION);
        }
        if let Some(first) = first_target {
            if context.target(0)? != first {
                return Err(sys::FAILED_PRECONDITION);
            }
        }
        // A new custom path owns the original candidate as child; IDs must
        // survive construction and downstream hooks within this invocation.
        let constructed = context.root(added)?;
        if context.child(constructed, 0)? != root {
            return Err(sys::FAILED_PRECONDITION);
        }
        // Force the new candidate to prove the plugin controls the final
        // choice. The spool algorithm/state live in Rust, not host MATERIAL.
        context.select(added)
    }
}
static SERVICE: sys::CandidateService = candidate::Service::<CustomSpool>::ABI;
// Additive upper policy: replace supported SORTs with the Rust implementation;
// otherwise retain the existing native-result spool reference. No replacement
// aggregation/window algorithm or calibrated cost improvement is implied.
struct UpperSpool;
impl candidate::Hook for UpperSpool {
    const MODE: candidate::Mode = candidate::Mode::Around;
    const VALUES: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        <CustomSpool as candidate::Hook>::validate_instance(instance)
    }
    fn invoke(_: *mut sys::Handle, context: &mut candidate::Context<'_>) -> Result<()> {
        let mut selected = None;
        for index in 0..context.count() {
            let root = context.root(index)?;
            if context.plan_semantics(root)?.local_serial {
                // Special/unsupported sorts retain their native implementation
                // in the fallback spool. Supported sorts consume the child.
                if let Some(sort) = context.sort(root)? {
                    if sorter::contribute(context, index, root, &sort)? {
                        return context.call_next();
                    }
                    for ordinal in 0..sort.key_count {
                        let key = context.sort_key(root, ordinal)?;
                        context.describe_expression(key.expression)?;
                    }
                    for expression in [sort.topn, sort.topk_limit, sort.topk_offset, sort.hash]
                        .into_iter()
                        .flatten()
                    {
                        context.describe_expression(expression)?;
                    }
                }
                selected = Some(index);
                break;
            }
        }
        let Some(index) = selected else {
            return context.call_next();
        };
        let input = context.get(index)?;
        context.custom(&candidate::CustomPath {
            input: index,
            service_id: c"org.seekdb.rust-candidate.spool",
            service_major: 1,
            minimum_minor: 0,
            plan: &[],
            operator_cost: 1.0 + input.rows * input.width * 0.00001,
            preserves_order: true,
            blocking: true,
        })?;
        context.call_next()
    }
}
static UPPER_SERVICE: sys::CandidateService = candidate::Service::<UpperSpool>::ABI;
static JOIN_SERVICE: sys::CandidateService = candidate::Service::<planner::IntegerJoin>::ABI;
static SUBPROBLEM_SERVICE: sys::CandidateService =
    candidate::Service::<planner::JoinSubproblem>::ABI;
struct Provides([sys::ServiceProvide; 5]);
// SAFETY: all pointers refer to immutable module-lifetime data.
unsafe impl Sync for Provides {}
static PROVIDES: Provides = Provides([
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-candidate.upper-policy".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&UPPER_SERVICE as *const sys::CandidateService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-candidate.subproblem-policy".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&SUBPROBLEM_SERVICE as *const sys::CandidateService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-candidate.join-policy".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&JOIN_SERVICE as *const sys::CandidateService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-candidate.policy".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&SERVICE as *const sys::CandidateService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-candidate.spool".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&executor::SERVICE as *const sys::CustomExecutor).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
]);
unsafe extern "C" fn init(host: *const sys::HostApiV1, out: *mut *mut sys::Handle) -> sys::Status {
    seekdb_extension::boundary(|| {
        if out.is_null() {
            return Err(sys::INVALID);
        }
        unsafe {
            *out = ptr::null_mut();
        }
        let mut registration = unsafe { Registration::begin(host) }?;
        registration.candidate_hook(&optimizer::Definition {
            object_id: c"org.seekdb.rust-candidate.hook",
            priority: 0,
            flags: 0,
            implementation: ImplementationReference {
                service_id: c"org.seekdb.rust-candidate.policy",
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
                required_capabilities: sys::THREAD_SAFE,
            },
        })?;
        registration.relation_paths_hook(&optimizer::Definition {
            object_id: c"org.seekdb.rust-candidate.join-hook",
            priority: 0,
            flags: 0,
            implementation: ImplementationReference {
                service_id: c"org.seekdb.rust-candidate.join-policy",
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
                required_capabilities: sys::THREAD_SAFE,
            },
        })?;
        registration.join_paths_hook(&optimizer::Definition {
            object_id: c"org.seekdb.rust-candidate.subproblem-hook",
            priority: 0,
            flags: 0,
            implementation: ImplementationReference {
                service_id: c"org.seekdb.rust-candidate.subproblem-policy",
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
                required_capabilities: sys::THREAD_SAFE,
            },
        })?;
        for (stage, object_id) in [
            (
                candidate::UpperStage::Group,
                c"org.seekdb.rust-candidate.upper-group",
            ),
            (
                candidate::UpperStage::Window,
                c"org.seekdb.rust-candidate.upper-window",
            ),
            (
                candidate::UpperStage::Distinct,
                c"org.seekdb.rust-candidate.upper-distinct",
            ),
            (
                candidate::UpperStage::Ordered,
                c"org.seekdb.rust-candidate.upper-ordered",
            ),
        ] {
            registration.upper_paths_hook(
                stage,
                &optimizer::Definition {
                    object_id,
                    priority: 0,
                    flags: 0,
                    implementation: ImplementationReference {
                        service_id: c"org.seekdb.rust-candidate.upper-policy",
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
                        required_capabilities: sys::THREAD_SAFE,
                    },
                },
            )?;
        }
        registration.commit()?;
        unsafe {
            *out = handle();
        }
        Ok(())
    })
}
unsafe extern "C" fn start(instance: *mut sys::Handle) -> sys::Status {
    seekdb_extension::boundary(|| {
        validate(instance)?;
        ACTIVE.store(true, Ordering::Release);
        Ok(())
    })
}
unsafe extern "C" fn stop(instance: *mut sys::Handle) -> sys::Status {
    seekdb_extension::boundary(|| {
        validate(instance)?;
        ACTIVE.store(false, Ordering::Release);
        Ok(())
    })
}
unsafe extern "C" fn deinit(instance: *mut sys::Handle) {
    if instance == handle() {
        ACTIVE.store(false, Ordering::Release);
    }
}
struct Manifest(sys::ServerDevManifest);
// SAFETY: immutable manifest, service and strings. Mutable state is atomic,
// accessed only by callbacks; no Rust references or C++ layout cross the ABI.
unsafe impl Sync for Manifest {}
static MANIFEST: Manifest = Manifest(server_dev::bind(
    sys::Manifest {
        struct_size: size_of::<sys::Manifest>() as u32,
        abi_major: 1,
        abi_minor: 0,
        plugin_id: c"org.seekdb.rust-candidate".as_ptr(),
        vendor: c"seekdb".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        build_id: c"rust-candidate-v18".as_ptr(),
        catalog_version: 1,
        data_format_version: 0,
        capabilities: sys::THREAD_SAFE,
        provides: PROVIDES.0.as_ptr(),
        provides_count: PROVIDES.0.len() as u32,
        required_services: ptr::null(),
        required_services_count: 0,
        init: Some(init),
        start: Some(start),
        stop: Some(stop),
        deinit: Some(deinit),
        reserved: [0; 8],
    },
    &contract::HOST_BUILD_ID,
));

#[no_mangle]
pub extern "C" fn seekdb_plugin_entry_v1() -> *const sys::Manifest {
    &MANIFEST.0.v1
}
