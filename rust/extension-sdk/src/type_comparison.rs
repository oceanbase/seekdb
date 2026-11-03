// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Optional deterministic ordering for decoded values of one logical type.
use crate::{boundary, result_type, sys, Result};
use std::{cmp::Ordering, ffi::CStr, marker::PhantomData, mem::size_of, slice};

/// Opt-in total ordering, independent of physical encoding and SQL collation.
/// Implementations must be antisymmetric, transitive, generation-stable and
/// thread-safe. They must check their instance/lifecycle as for codec callbacks.
/// Borrowed values cannot be retained. No SQL/query context is supplied.
pub trait Comparator {
    fn compare(
        instance: *mut sys::Handle,
        logical_type: &CStr,
        left: &[u8],
        right: &[u8],
    ) -> Result<Ordering>;
}

pub struct Service<C>(PhantomData<C>);
impl<C: Comparator> Service<C> {
    /// Extend an existing codec without changing its decode/encode callbacks.
    /// The resulting table is codec SPI 1.1, not scalar service SPI 1.1.
    pub const fn with_codec(mut codec: sys::TypeCodecService) -> sys::TypeCodecServiceV2 {
        codec.struct_size = size_of::<sys::TypeCodecServiceV2>() as u32;
        codec.spi_major = 1;
        codec.spi_minor = 1;
        sys::TypeCodecServiceV2 {
            v1: codec,
            compare: Some(compare::<C>),
            comparison_reserved: [0; 4],
        }
    }
}

// SAFETY: The host lends aligned readable values, advertised input spans and
// NUL-terminated identifiers, plus a disjoint writable result allocation, for
// this call only. Invalid length/null metadata is checked before reading bytes.
unsafe fn value<'a>(raw: *const sys::Value) -> Result<(&'a CStr, &'a [u8])> {
    if raw.is_null() {
        return Err(sys::INVALID);
    }
    let value = unsafe { &*raw };
    if value.struct_size < size_of::<sys::Value>() as u32
        || value.is_null != 0
        || value.type_id.is_null()
        || value.reserved_bytes != [0; 7]
        || value.reserved != [0; 4]
        || value.data_size > 16 * 1024 * 1024
        || (value.data_size != 0 && value.data.is_null())
    {
        return Err(sys::INVALID);
    }
    let id = unsafe { CStr::from_ptr(value.type_id) };
    result_type::validate_type(id)?;
    let bytes = if value.data_size == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(value.data, value.data_size as usize) }
    };
    Ok((id, bytes))
}
unsafe extern "C" fn compare<C: Comparator>(
    instance: *mut sys::Handle,
    left: *const sys::Value,
    right: *const sys::Value,
    output: *mut sys::TypeComparison,
) -> sys::Status {
    boundary(|| {
        if output.is_null()
            || unsafe { (*output).struct_size } < size_of::<sys::TypeComparison>() as u32
        {
            return Err(sys::INVALID);
        }
        unsafe {
            *output = sys::TypeComparison {
                struct_size: size_of::<sys::TypeComparison>() as u32,
                ordering: 0,
                reserved: [0; 4],
            };
        }
        let (left_id, left) = unsafe { value(left) }?;
        let (right_id, right) = unsafe { value(right) }?;
        if left_id != right_id {
            return Err(sys::INVALID);
        }
        let ordering = C::compare(instance, left_id, left, right)?;
        unsafe {
            (*output).ordering = match ordering {
                Ordering::Less => -1,
                Ordering::Equal => 0,
                Ordering::Greater => 1,
            };
        }
        Ok(())
    })
}
