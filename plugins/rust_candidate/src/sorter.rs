// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Actual SORT replacement. Policy limits are not builder or executor limits.
use crate::sort_plan::{Key, Plan as SortPlan};
#[cfg(test)]
mod tests;
use seekdb_extension::{candidate::*, sys, Result};

pub fn contribute<'a>(
    c: &mut Context<'a>,
    index: u32,
    root: PlanId<'a>,
    sort: &Sort<'a>,
) -> Result<bool> {
    if sort.key_count == 0
        || sort.key_count > 1024
        || sort.prefix_key_count != 0
        || sort.partition_key_count != 0
        || sort.local_merge
        || sort.with_ties
        || sort.runtime_filter
        || sort.topn.is_some()
        || sort.topk_limit.is_some()
        || sort.topk_offset.is_some()
        || sort.hash.is_some()
    {
        return Ok(false);
    }
    // Encoded keys are a native representation optimization. We consume the
    // unencoded SQL keys; no encoder/hash/Top-N implementation is retained.
    let info = c.plan(root)?;
    if info.child_count != 1
        || [Role::Filter, Role::Startup]
            .iter()
            .any(|&r| info.expression_count(r) != 0)
    {
        return Ok(false);
    }
    let child = c.child(root, 0)?;
    if !c.plan_semantics(child)?.local_serial {
        return Ok(false);
    }
    let query = c.query()?;
    let Some(target_count) = query.target_count else {
        return Ok(false);
    };
    if query.set_operation || target_count > 1024 {
        return Ok(false);
    }
    let mut inputs = Vec::new();
    let mut outputs = Vec::new();
    inputs.try_reserve_exact(1024).map_err(|_| sys::NO_MEMORY)?;
    outputs
        .try_reserve_exact(1024)
        .map_err(|_| sys::NO_MEMORY)?;
    let mut keys = Vec::new();
    keys.try_reserve_exact(sort.key_count as usize)
        .map_err(|_| sys::NO_MEMORY)?;
    for ordinal in 0..sort.key_count {
        let key = c.sort_key(root, ordinal)?;
        let semantics = c.expression_semantics(key.expression)?;
        // Native SORT already evaluates this exact key below its boundary.
        // Preserve that evaluation, including existing aggregate/window values;
        // do not require a materialized result to be a movable scalar.
        if semantics.value == ValueKind::Other
            || c.required_values(child, key.expression)?.is_none()
        {
            return Ok(false);
        }
        let slot = match inputs.iter().position(|&e| e == key.expression) {
            Some(slot) => slot,
            None => {
                inputs.push(key.expression);
                inputs.len() - 1
            }
        };
        keys.push(Key {
            column: slot,
            descending: key.descending,
            nulls_first: key.nulls_first,
        });
        if !c.describe_expression(key.expression)?.constant() && !outputs.contains(&key.expression)
        {
            outputs.push(key.expression);
        }
    }
    for i in 0..target_count {
        let expr = c.target(i)?;
        if outputs.contains(&expr) {
            continue;
        }
        // Carry a cut of existing values, not the SELECT computation. This
        // keeps errors/volatile calls above SORT/LIMIT and preserves completed
        // aggregate/window results instead of asking for their input columns.
        let Some(required) = c.required_values(child, expr)? else {
            return Ok(false);
        };
        for value in required {
            if outputs.contains(&value) {
                continue;
            }
            if inputs.len() == 1024 || outputs.len() == 1024 {
                return Ok(false);
            }
            if !inputs.contains(&value) {
                inputs.push(value);
            }
            outputs.push(value);
        }
    }
    let child_info = c.plan(child)?;
    let rows = child_info.rows;
    let cost =
        1.0 + rows * (rows.max(2.0).log2() * keys.len() as f64 * 0.02 + child_info.width * 0.00001);
    if !cost.is_finite() || rows < 0.0 {
        return Ok(false);
    }
    let mut mapping = Vec::new();
    mapping
        .try_reserve_exact(outputs.len())
        .map_err(|_| sys::NO_MEMORY)?;
    for expr in &outputs {
        mapping.push(inputs.iter().position(|v| v == expr).unwrap());
    }
    let bytes = SortPlan {
        keys,
        outputs: mapping,
    }
    .encode()?;
    c.custom_fragment(
        &CustomPath {
            input: index,
            service_id: c"org.seekdb.rust-candidate.spool",
            service_major: 1,
            minimum_minor: 0,
            plan: &bytes,
            operator_cost: cost,
            preserves_order: true,
            blocking: true,
        },
        &[FragmentInput {
            plan: child,
            expressions: &inputs,
        }],
        &outputs,
    )?;
    Ok(true)
}
