// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Synchronous around-planning callbacks; custom plan replacement is not this
//! v1 interface. A callback may observe or veto, and cannot suppress next errors.
use crate::{boundary, status, sys, ImplementationReference, Registration, Result};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, rc::Rc};

pub struct Definition<'a> {
    pub object_id: &'a CStr,
    pub priority: i32,
    pub flags: u64,
    pub implementation: ImplementationReference<'a>,
}
impl Registration<'_> {
    pub fn optimizer_hook(&mut self, definition: &Definition<'_>) -> Result<()> {
        let descriptor = sys::OptimizerHookDescriptor {
            struct_size: size_of::<sys::OptimizerHookDescriptor>() as u32,
            object_id: definition.object_id.as_ptr(),
            hook_point: c"optimizer.plan.v1".as_ptr(),
            priority: definition.priority,
            reserved_word: 0,
            flags: definition.flags,
            implementation: definition.implementation.raw(),
            reserved: [0; 4],
        };
        self.register_descriptor(5, &descriptor)
    }
}

/// Borrowed exclusively for this synchronous callback. Neither the continuation
/// nor its context may escape, be used asynchronously, or sent to another thread.
/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::optimizer::Context<'static>>();
/// ```
pub struct Context<'a> {
    raw: &'a sys::OptimizerContext,
    info: &'a sys::OptimizerInfo,
    called: bool,
    error: sys::Status,
    database_error: i32,
    _thread: PhantomData<Rc<()>>,
}
impl Context<'_> {
    pub fn statement_kind(&self) -> u32 {
        self.info.statement_kind
    }
    pub fn database_id(&self) -> u64 {
        self.info.database_id
    }
    pub fn user_id(&self) -> u64 {
        self.info.user_id
    }
    /// Exact downstream host error after next (zero on success).
    pub fn database_error(&self) -> i32 {
        self.database_error
    }
    pub fn call_next(&mut self) -> Result<()> {
        if self.called {
            self.error = sys::FAILED_PRECONDITION;
            return Err(self.error);
        }
        self.called = true;
        self.error =
            unsafe { self.raw.next.unwrap()(self.raw.continuation, &mut self.database_error) };
        if self.error == sys::OK && self.database_error != 0 {
            self.error = sys::FAILED_PRECONDITION;
        }
        status(self.error)
    }
}
pub trait Hook {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()>;
    fn invoke(instance: *mut sys::Handle, context: &mut Context<'_>) -> Result<()>;
}
pub struct Service<H: Hook>(PhantomData<H>);
impl<H: Hook> Service<H> {
    pub const ABI: sys::OptimizerService = sys::OptimizerService {
        struct_size: size_of::<sys::OptimizerService>() as u32,
        spi_major: 1,
        spi_minor: 0,
        reserved_word: 0,
        invoke: Some(invoke::<H>),
        reserved: [0; 4],
    };
}
unsafe extern "C" fn invoke<H: Hook>(
    instance: *mut sys::Handle,
    raw: *const sys::OptimizerContext,
) -> sys::Status {
    boundary(|| {
        H::validate_instance(instance)?;
        if raw.is_null() {
            return Err(sys::INVALID);
        }
        let raw = unsafe { &*raw };
        if raw.struct_size < size_of::<sys::OptimizerContext>() as u32
            || raw.info.is_null()
            || raw.next.is_none()
            || raw.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        let info = unsafe { &*raw.info };
        if info.struct_size < size_of::<sys::OptimizerInfo>() as u32
            || info.statement_kind > 5
            || info.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        let mut context = Context {
            raw,
            info,
            called: false,
            error: sys::OK,
            database_error: 0,
            _thread: PhantomData,
        };
        let result = H::invoke(instance, &mut context);
        status(context.error)?;
        result?;
        if !context.called {
            return Err(sys::FAILED_PRECONDITION);
        }
        Ok(())
    })
}
