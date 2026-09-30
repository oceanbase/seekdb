// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Adopt a native correlated inner JOIN whose predicates already execute in
//! its children. Rust drives each left row/bind/right rescan; the host evaluates
//! the retained SQL expressions. No SQL-text matching or independent-input guess.
use super::*;
pub(super) fn contribute<'a>(
    c: &mut Context<'a>,
    index: u32,
    root: PlanId<'a>,
    count: u32,
    subproblem: bool,
) -> Result<bool> {
    let info = c.plan(root)?;
    if info.child_count != 2
        || count > 1024
        || [
            Role::JoinCondition,
            Role::Filter,
            Role::Startup,
            Role::JoinFilter,
        ]
        .iter()
        .any(|&r| info.expression_count(r) != 0)
    {
        return Ok(false);
    }
    let plans = [c.child(root, 0)?, c.child(root, 1)?];
    if plans[0] == plans[1]
        || !c.plan_semantics(plans[0])?.local_serial
        || !c.plan_semantics(plans[1])?.local_serial
    {
        return Ok(false);
    }
    if super::multi::contribute(c, index, root, subproblem)? {
        return Ok(true);
    }
    let columns = c.column_count()? as usize;
    if columns > 1024 {
        return Ok(false);
    }
    let mut inputs: [Vec<ExpressionId<'a>>; 2] = [Vec::new(), Vec::new()];
    for input in &mut inputs {
        input.try_reserve_exact(1024).map_err(|_| sys::NO_MEMORY)?;
    }
    let mut bindings = Vec::new();
    bindings
        .try_reserve_exact(count as usize)
        .map_err(|_| sys::NO_MEMORY)?;
    for ordinal in 0..count {
        let binding = c.binding(root, BindingRole::NestedLoop, ordinal)?;
        if branch(c, plans, binding.source)? != Some(0)
            || !c.expression_semantics(binding.source)?.scalar_deterministic
        {
            return Ok(false);
        }
        let slot = if let Some(slot) = inputs[0].iter().position(|&id| id == binding.source) {
            slot
        } else {
            inputs[0].push(binding.source);
            inputs[0].len() - 1
        };
        bindings.push(InputBinding {
            parameter: binding.parameter,
            source_input: 0,
            source_column: slot as u32,
            target_input: 1,
        });
    }
    let mut outputs = Vec::new();
    outputs
        .try_reserve_exact(columns)
        .map_err(|_| sys::NO_MEMORY)?;
    let mut mapping = Vec::new();
    mapping
        .try_reserve_exact(columns)
        .map_err(|_| sys::NO_MEMORY)?;
    for ordinal in 0..columns {
        let expr = c.column(ordinal as u32)?;
        if outputs.contains(&expr) {
            continue;
        }
        if c.describe_expression(expr)?.column().is_none() {
            return Ok(false);
        }
        let Some(side) = branch(c, plans, expr)? else {
            if subproblem && outside_relation(c, root, &plans, expr)? {
                continue;
            }
            return Ok(false);
        };
        let slot = if let Some(slot) = inputs[side].iter().position(|&id| id == expr) {
            slot
        } else {
            if inputs[side].len() == 1024 {
                return Ok(false);
            }
            inputs[side].push(expr);
            inputs[side].len() - 1
        };
        outputs.push(expr);
        mapping.push((side as u32, slot as u32));
    }
    let left = c.plan(plans[0])?;
    let right = c.plan(plans[1])?;
    let cost = 1.0 + left.rows * right.rows * 0.02;
    if left.rows < 0.0 || right.rows < 0.0 || !cost.is_finite() {
        return Ok(false);
    }
    let mut bytes = encode(&mapping)?;
    bytes[..4].copy_from_slice(b"SJC1");
    c.custom_bound_fragment(
        &CustomPath {
            input: index,
            service_id: c"org.seekdb.rust-candidate.spool",
            service_major: 1,
            minimum_minor: 0,
            plan: &bytes,
            operator_cost: cost,
            preserves_order: false,
            blocking: false,
        },
        &[
            FragmentInput {
                plan: plans[0],
                expressions: &inputs[0],
            },
            FragmentInput {
                plan: plans[1],
                expressions: &inputs[1],
            },
        ],
        &outputs,
        &bindings,
    )?;
    Ok(true)
}
