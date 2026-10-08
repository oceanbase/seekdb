// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Expand a left spine of correlated INNER JOIN owners into an SJD1 schedule.
//! Whole right consumers and the base relation remain native input subtrees.
use super::*;

fn source<'a>(
    c: &mut Context<'a>,
    inputs: &[PlanId<'a>],
    expr: ExpressionId<'a>,
) -> Result<Option<usize>> {
    let mut found = None;
    for (index, &input) in inputs.iter().enumerate() {
        match c.dependency_scope(input, expr)? {
            DependencyScope::Contained if found.is_none() => found = Some(index),
            DependencyScope::Outside => (),
            _ => return Ok(None),
        }
    }
    Ok(found)
}
pub(super) fn contribute<'a>(
    c: &mut Context<'a>,
    index: u32,
    root: PlanId<'a>,
    subproblem: bool,
) -> Result<bool> {
    let mut owners = Vec::new();
    owners.try_reserve_exact(63).map_err(|_| sys::NO_MEMORY)?;
    let mut base = root;
    loop {
        let semantic = c.plan_semantics(base)?;
        if semantic.relation != RelationKind::InnerJoin || !semantic.local_serial {
            break;
        }
        let info = c.plan(base)?;
        if info.child_count != 2
            || [
                Role::JoinCondition,
                Role::Filter,
                Role::Startup,
                Role::JoinFilter,
            ]
            .iter()
            .any(|&role| info.expression_count(role) != 0)
        {
            break;
        }
        if c.binding_count(base, BindingRole::LeftPushDown)? != 0
            || c.binding_count(base, BindingRole::RightPushDown)? != 0
        {
            break;
        }
        let count = c.binding_count(base, BindingRole::NestedLoop)?;
        if count == 0 || count > 1024 || owners.len() == 63 {
            break;
        }
        let left = c.child(base, 0)?;
        let right = c.child(base, 1)?;
        if left == right
            || left == base
            || right == base
            || owners.iter().any(|&(owner, _, _)| owner == left)
        {
            return Ok(false);
        }
        if !c.plan_semantics(left)?.local_serial || !c.plan_semantics(right)?.local_serial {
            return Ok(false);
        }
        owners.push((base, right, count));
        base = left;
    }
    if owners.len() < 2 {
        return Ok(false);
    } // Keep the established two-input SJC1 path.
    let mut plans = Vec::new();
    plans
        .try_reserve_exact(owners.len() + 1)
        .map_err(|_| sys::NO_MEMORY)?;
    plans.push(base);
    for &(_, right, _) in owners.iter().rev() {
        if plans.contains(&right) {
            return Ok(false);
        }
        plans.push(right);
    }
    let mut inputs = Vec::new();
    inputs
        .try_reserve_exact(plans.len())
        .map_err(|_| sys::NO_MEMORY)?;
    for _ in &plans {
        let mut values = Vec::new();
        values.try_reserve_exact(1024).map_err(|_| sys::NO_MEMORY)?;
        inputs.push(values);
    }
    let mut bindings: Vec<InputBinding<'a>> = Vec::new();
    bindings
        .try_reserve_exact(1024)
        .map_err(|_| sys::NO_MEMORY)?;
    for (level, &(owner, _, count)) in owners.iter().rev().enumerate() {
        let target = level + 1;
        for ordinal in 0..count {
            let binding = c.binding(owner, BindingRole::NestedLoop, ordinal)?;
            let Some(input) = source(c, &plans, binding.source)? else {
                return Ok(false);
            };
            if input >= target
                || !c.expression_semantics(binding.source)?.scalar_deterministic
                || bindings.len() == 1024
                || bindings.iter().any(|b| b.parameter == binding.parameter)
            {
                return Ok(false);
            }
            let slot = if let Some(slot) = inputs[input].iter().position(|&v| v == binding.source) {
                slot
            } else {
                if inputs[input].len() == 1024 {
                    return Ok(false);
                }
                inputs[input].push(binding.source);
                inputs[input].len() - 1
            };
            bindings.push(InputBinding {
                parameter: binding.parameter,
                source_input: input as u32,
                source_column: slot as u32,
                target_input: target as u32,
            });
        }
    }
    // Decline known retained JOIN owners that carry a transferred identity.
    // The host additionally checks actual parameter-slot aliases and other
    // owner kinds, which the public graph does not expose as mutable frames.
    let mut work = Vec::new();
    let mut seen = Vec::new();
    work.try_reserve_exact(4096).map_err(|_| sys::NO_MEMORY)?;
    seen.try_reserve_exact(4096).map_err(|_| sys::NO_MEMORY)?;
    work.extend_from_slice(&plans);
    while let Some(node) = work.pop() {
        if seen.contains(&node) || seen.len() == 4096 {
            return Ok(false);
        }
        seen.push(node);
        for role in [
            BindingRole::NestedLoop,
            BindingRole::LeftPushDown,
            BindingRole::RightPushDown,
        ] {
            let count = c.binding_count(node, role)?;
            if count > 1024 {
                return Ok(false);
            }
            for ordinal in 0..count {
                let binding = c.binding(node, role, ordinal)?;
                if bindings.iter().any(|b| b.parameter == binding.parameter) {
                    return Ok(false);
                }
            }
        }
        let info = c.plan(node)?;
        if work.len() + seen.len() + info.child_count as usize > 4096 {
            return Ok(false);
        }
        for ordinal in 0..info.child_count {
            work.push(c.child(node, ordinal)?);
        }
    }
    let columns = c.column_count()? as usize;
    if columns > 1024 {
        return Ok(false);
    }
    let mut outputs = Vec::new();
    let mut mapping = Vec::new();
    outputs
        .try_reserve_exact(columns)
        .map_err(|_| sys::NO_MEMORY)?;
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
        let Some(input) = source(c, &plans, expr)? else {
            if subproblem && outside_relation(c, root, &plans, expr)? {
                continue;
            }
            return Ok(false);
        };
        let slot = if let Some(slot) = inputs[input].iter().position(|&v| v == expr) {
            slot
        } else {
            if inputs[input].len() == 1024 {
                return Ok(false);
            }
            inputs[input].push(expr);
            inputs[input].len() - 1
        };
        outputs.push(expr);
        mapping.push((input as u32, slot as u32));
    }
    // Reference estimate only; no upper limit/cardinality inherited from input 0.
    let mut combinations = 1.0;
    let mut cost = 1.0;
    for (i, &plan) in plans.iter().enumerate() {
        let rows = c.plan(plan)?.rows;
        if rows < 0.0 || !rows.is_finite() {
            return Ok(false);
        }
        combinations *= rows;
        if i != 0 {
            cost += combinations * 0.02;
        }
    }
    if !cost.is_finite() {
        return Ok(false);
    }
    let mut bytes = Vec::new();
    bytes
        .try_reserve_exact(12 + plans.len() * 8 + mapping.len() * 8)
        .map_err(|_| sys::NO_MEMORY)?;
    bytes.extend_from_slice(b"SJD1");
    bytes.extend_from_slice(&(plans.len() as u32).to_le_bytes());
    for i in 0..plans.len() {
        bytes.extend_from_slice(&(i as u32).to_le_bytes());
        bytes.extend_from_slice(&u32::from(i != 0).to_le_bytes());
    }
    bytes.extend_from_slice(&(mapping.len() as u32).to_le_bytes());
    for (input, column) in mapping {
        bytes.extend_from_slice(&input.to_le_bytes());
        bytes.extend_from_slice(&column.to_le_bytes());
    }
    let mut fragments = Vec::new();
    fragments
        .try_reserve_exact(plans.len())
        .map_err(|_| sys::NO_MEMORY)?;
    for (&plan, expressions) in plans.iter().zip(&inputs) {
        fragments.push(FragmentInput { plan, expressions });
    }
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
        &fragments,
        &outputs,
        &bindings,
    )?;
    Ok(true)
}
