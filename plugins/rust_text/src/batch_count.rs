// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    batch::{Batch, Handler, Service},
    sys, Result,
};

struct CountBatch;
impl Handler for CountBatch {
    fn validate(instance: *mut sys::Handle) -> Result<()> {
        super::validate_instance(instance)
    }
    fn execute(_: *mut sys::Handle, batch: &mut Batch<'_>) -> Result<()> {
        let control = batch.supports_query_control();
        let mut results = Vec::with_capacity(batch.row_count());
        // One callback sees all inputs. More expensive providers can replace
        // this loop with one batched model/library operation, without changing
        // the SQL function or adding a native service for each individual row.
        for index in 0..batch.row_count() {
            if control {
                batch.poll_query().map_err(|e| e.status)?;
            }
            let row = batch.row(index)?;
            if row.argument_count() != 1 {
                return Err(sys::INVALID);
            }
            let bytes = row
                .bytes(0, c"core.type.bytes")
                .or_else(|_| row.bytes(0, super::TEXT_TYPE))?;
            let value = match bytes {
                None => None,
                Some(bytes) => {
                    let text = std::str::from_utf8(bytes).map_err(|_| sys::INVALID)?;
                    let mut length = 0i64;
                    for _ in text.chars() {
                        length += 1;
                        if control && length % 4096 == 0 {
                            batch.poll_query().map_err(|e| e.status)?;
                        }
                    }
                    Some(length)
                }
            };
            results.push(value);
        }
        // Indexed results need not be emitted in input order.
        for index in (0..results.len()).rev() {
            batch.emit_i64(index, results[index])?;
        }
        Ok(())
    }
}
unsafe extern "C" fn scalar(
    instance: *mut sys::Handle,
    context: *const sys::ContextV1,
    arguments: *const sys::Value,
    count: u32,
) -> sys::Status {
    unsafe { super::count_function::SERVICE.execute.unwrap()(instance, context, arguments, count) }
}
static SERVICE: sys::FunctionServiceV3 = Service::<CountBatch>::with_scalar(scalar, None);
pub const fn provide() -> sys::ServiceProvide {
    let mut result = super::count_function::provide(
        sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        sys::THREAD_SAFE,
    );
    result.service = (&SERVICE as *const sys::FunctionServiceV3).cast();
    result
}
