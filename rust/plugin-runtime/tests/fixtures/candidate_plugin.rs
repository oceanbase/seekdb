// Real SDK-based Rust callback linked into the version-bound test DSO.
use seekdb_extension::{
    candidate::{Context, Hook, Mode, Service},
    sys, Result,
};
#[no_mangle]
pub unsafe extern "C" fn candidate_register(host: *const sys::HostApiV1) -> sys::Status {
    unsafe { register_at(host, false) }
}
#[no_mangle]
pub unsafe extern "C" fn candidate_register_relation(host: *const sys::HostApiV1) -> sys::Status {
    unsafe { register_at(host, true) }
}
unsafe fn register_at(host: *const sys::HostApiV1, relation: bool) -> sys::Status {
    seekdb_extension::boundary(|| {
        let mut registration = unsafe { seekdb_extension::Registration::begin(host) }?;
        let definition = seekdb_extension::optimizer::Definition {
            object_id: c"org.seekdb.candidate.hook",
            priority: 0,
            flags: 0,
            implementation: seekdb_extension::ImplementationReference {
                service_id: c"org.seekdb.candidate.policy",
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
            },
        };
        if relation {
            registration.relation_paths_hook(&definition)?;
        } else {
            registration.candidate_hook(&definition)?;
        }
        registration.commit()
    })
}
struct Policy<const AROUND: bool>;
struct Construct;
struct Contribute;
impl Hook for Contribute {
    const MODE: Mode = Mode::Around;
    const BUILDERS: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        Construct::validate_instance(instance)
    }
    fn invoke(instance: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        let variant = unsafe { *instance.cast::<u32>() };
        let before = context.count();
        let built = context.materialize(0);
        if variant == 18 {
            return Ok(());
        } // A failed build stays sticky.
        let built = built?;
        assert_eq!(built, before);
        if variant == 17 {
            return Ok(());
        } // Missing next must fail.
        if variant == 19 {
            let _ = context.select(built); // Selection is illegal in this phase.
            return Ok(());
        }
        context.call_next()?;
        assert_eq!(context.count(), before + 1);
        Ok(()) // No winner: host retains originals and this contribution.
    }
}
#[no_mangle]
pub unsafe extern "C" fn candidate_contribute(
    instance: *mut sys::Handle,
    context: *const sys::CandidateContext,
) -> sys::Status {
    unsafe { Service::<Contribute>::ABI.invoke.unwrap()(instance, context) }
}
impl Hook for Construct {
    const MODE: Mode = Mode::Around;
    const BUILDERS: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::INVALID)
        } else {
            Ok(())
        }
    }
    fn invoke(instance: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        let variant = unsafe { *instance.cast::<u32>() };
        let before = context.count();
        if variant == 11 {
            let _ = context.materialize(before);
            return Ok(());
        }
        let constructed = context.materialize(0);
        if variant == 12 {
            return Ok(());
        } // Host error must remain exact.
        let constructed = constructed?;
        assert_eq!(constructed, before);
        assert_eq!(context.count(), before + 1);
        let _ = context.get(constructed)?;
        if variant == 10 {
            return Ok(());
        }
        context.call_next()?;
        context.select(constructed)
    }
}
#[no_mangle]
pub unsafe extern "C" fn candidate_construct(
    instance: *mut sys::Handle,
    context: *const sys::CandidateContext,
) -> sys::Status {
    unsafe { Service::<Construct>::ABI.invoke.unwrap()(instance, context) }
}
impl<const AROUND: bool> Hook for Policy<AROUND> {
    const MODE: Mode = if AROUND { Mode::Around } else { Mode::Replace };
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::INVALID)
        } else {
            Ok(())
        }
    }
    fn invoke(instance: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        let variant = unsafe { *instance.cast::<u32>() };
        if variant == 2 || variant == 5 {
            return Ok(());
        }
        if variant == 3 {
            let _ = context.get(context.count());
            return Ok(());
        }
        if variant == 4 {
            let _ = context.select(context.count());
            return Ok(());
        }
        if AROUND {
            let result = context.call_next();
            if variant == 7 {
                return Ok(());
            } // Must not swallow the host error.
            result?;
            if variant == 6 {
                let _ = context.call_next();
                return Ok(());
            }
        }
        // Deliberately chooses a DIFFERENT plan than the host cost minimum.
        // A deterministic test policy, not a recommended production optimizer.
        let mut selected = 0;
        let mut highest = context.get(0)?.cost;
        for index in 1..context.count() {
            let candidate = context.get(index)?;
            if candidate.cost > highest {
                highest = candidate.cost;
                selected = index;
            }
        }
        context.select(selected)
    }
}
#[no_mangle]
pub unsafe extern "C" fn candidate_around(
    instance: *mut sys::Handle,
    context: *const sys::CandidateContext,
) -> sys::Status {
    unsafe { Service::<Policy<true>>::ABI.invoke.unwrap()(instance, context) }
}
#[no_mangle]
pub unsafe extern "C" fn candidate_replace(
    instance: *mut sys::Handle,
    context: *const sys::CandidateContext,
) -> sys::Status {
    unsafe { Service::<Policy<false>>::ABI.invoke.unwrap()(instance, context) }
}
