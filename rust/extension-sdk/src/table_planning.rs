// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Optional metadata-only table-function planning. No cursor or query SQL runs.
use crate::{boundary, result_type, sys, table, Result};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, rc::Rc};

/// Metadata borrowed for one synchronous planning callback, not runtime values.
/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::table_planning::Context<'static>>();
/// ```
pub struct Context<'a> {
    raw: &'a sys::TablePlanningInfo,
    _thread: PhantomData<Rc<()>>,
}
impl Context<'_> {
    pub fn object_id(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.raw.object_id) }
    }
    pub fn argument_count(&self) -> usize {
        self.raw.argument_count as usize
    }
    pub fn argument_type(&self, index: usize) -> Result<&CStr> {
        if index >= self.argument_count() {
            return Err(sys::INVALID);
        }
        Ok(unsafe { CStr::from_ptr(*self.raw.argument_type_ids.add(index)) })
    }
    pub fn column_count(&self) -> u32 {
        self.raw.column_count
    }
}
/// Relative optimizer cost and average output width in bytes. These estimates
/// influence plan choice only; they never truncate execution to `rows` rows.
pub struct Estimate {
    pub rows: f64,
    pub row_width: f64,
    pub total_cost: f64,
}
pub trait Planner: table::Cursor {
    fn estimate(instance: *mut sys::Handle, context: &Context<'_>) -> Result<Estimate>;
}
pub struct Service<C>(PhantomData<C>);
impl<C: Planner> Service<C> {
    pub const WITH_PROJECTION: sys::TableFunctionServiceV2 = {
        let mut service = table::Service::<C>::WITH_PROJECTION;
        service.estimate = Some(estimate::<C>);
        service
    };
    pub const WITH_SQL_AND_PROJECTION: sys::TableFunctionServiceV2 = {
        let mut service = table::Service::<C>::WITH_SQL_AND_PROJECTION;
        service.estimate = Some(estimate::<C>);
        service
    };
    /// SQL-dependent cursor with custom planning estimates.
    pub const WITH_SQL: sys::TableFunctionServiceV2 = {
        let mut service = table::Service::<C>::WITH_SQL;
        service.estimate = Some(estimate::<C>);
        service
    };
    /// Keep the custom estimate and request optional query-control contexts.
    pub const WITH_QUERY_CONTROL: sys::TableFunctionServiceV2 = {
        let mut service = Self::ABI;
        service.v1.spi_minor = 2;
        service
    };
    /// Unsafe callbacks require live, correctly sized host allocations, valid
    /// NUL-terminated identifiers, a disjoint writable output, and synchronous
    /// execution on the callback thread. No foreign exception may enter Rust.
    pub const ABI: sys::TableFunctionServiceV2 = {
        let mut v1 = table::Service::<C>::ABI;
        v1.struct_size = size_of::<sys::TableFunctionServiceV2>() as u32;
        v1.spi_minor = 1;
        sys::TableFunctionServiceV2 {
            v1,
            estimate: Some(estimate::<C>),
            reserved: [0; 4],
        }
    };
}
unsafe extern "C" fn estimate<C: Planner>(
    instance: *mut sys::Handle,
    raw: *const sys::TablePlanningInfo,
    out: *mut sys::TableEstimate,
) -> sys::Status {
    boundary(|| {
        if out.is_null() || unsafe { (*out).struct_size } < size_of::<sys::TableEstimate>() as u32 {
            return Err(sys::INVALID);
        }
        unsafe {
            out.write(sys::TableEstimate {
                struct_size: size_of::<sys::TableEstimate>() as u32,
                reserved_word: 0,
                rows: 0.0,
                row_width: 0.0,
                total_cost: 0.0,
                reserved: [0; 4],
            });
        }
        C::validate_instance(instance)?;
        if raw.is_null() {
            return Err(sys::INVALID);
        }
        let raw = unsafe { &*raw };
        if raw.struct_size < size_of::<sys::TablePlanningInfo>() as u32
            || raw.argument_count > 1024
            || raw.column_count == 0
            || raw.column_count > 4096
            || raw.object_id.is_null()
            || (raw.argument_count > 0 && raw.argument_type_ids.is_null())
            || raw.reserved_word != 0
            || raw.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        result_type::validate_type(unsafe { CStr::from_ptr(raw.object_id) })?;
        for i in 0..raw.argument_count as usize {
            let id = unsafe { *raw.argument_type_ids.add(i) };
            if id.is_null() {
                return Err(sys::INVALID);
            }
            result_type::validate_type(unsafe { CStr::from_ptr(id) })?;
        }
        let result = C::estimate(
            instance,
            &Context {
                raw,
                _thread: PhantomData,
            },
        )?;
        for value in [result.rows, result.row_width, result.total_cost] {
            if !value.is_finite() || value < 0.0 {
                return Err(sys::INVALID);
            }
        }
        unsafe {
            (*out).rows = result.rows;
            (*out).row_width = result.row_width;
            (*out).total_cost = result.total_cost;
        }
        Ok(())
    })
}
