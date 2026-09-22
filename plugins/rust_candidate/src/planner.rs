// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Rust-owned relation strategy. No host enum ordinals, SQL text matching,
//! SELECT-position/left-right assumptions, or callback into the host join.
use seekdb_extension::{candidate::*, sys, Result};

mod correlated;
mod multi;
#[cfg(test)]
mod tests;

pub struct IntegerJoin;
pub struct JoinSubproblem;
impl Hook for JoinSubproblem {
    const MODE: Mode = Mode::Around;
    const PARAMETERS: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        <super::CustomSpool as Hook>::validate_instance(instance)
    }
    fn invoke(_: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        let query = context.query()?;
        if query.target_count.is_some() && !query.set_operation {
            for index in 0..context.count() {
                if contribute_scoped(context, index, true)? {
                    break;
                }
            }
        }
        context.call_next()
    }
}
impl Hook for IntegerJoin {
    const MODE: Mode = Mode::Around;
    const PARAMETERS: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        <super::CustomSpool as Hook>::validate_instance(instance)
    }
    fn invoke(_: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        // Contribute at most one equivalent algorithm per invocation. Leave all
        // native alternatives in place and let upper planning/costing decide.
        let query = context.query()?;
        if query.target_count.is_some() && !query.set_operation {
            let count = context.count();
            for index in 0..count {
                if contribute(context, index)? {
                    break;
                }
            }
        }
        context.call_next()
    }
}
fn branch<'a>(
    c: &mut Context<'a>,
    plans: [PlanId<'a>; 2],
    expr: ExpressionId<'a>,
) -> Result<Option<usize>> {
    let a = c.dependency_scope(plans[0], expr)?;
    let b = c.dependency_scope(plans[1], expr)?;
    Ok(match (a, b) {
        (DependencyScope::Contained, DependencyScope::Outside) => Some(0),
        (DependencyScope::Outside, DependencyScope::Contained) => Some(1),
        _ => None, // Constants, cross-input expressions, and ambiguous scopes.
    })
}
fn contribute<'a>(c: &mut Context<'a>, index: u32) -> Result<bool> {
    contribute_scoped(c, index, false)
}
// Only resolved columns call this helper. A dependency that merely fails to
// fit one input may span other retained inputs, so check the target as well.
fn outside_relation<'a>(
    c: &mut Context<'a>,
    root: PlanId<'a>,
    plans: &[PlanId<'a>],
    expr: ExpressionId<'a>,
) -> Result<bool> {
    if c.dependency_scope(root, expr)? != DependencyScope::Outside {
        return Ok(false);
    }
    for &plan in plans {
        if c.dependency_scope(plan, expr)? != DependencyScope::Outside {
            return Ok(false);
        }
    }
    Ok(true)
}
fn contribute_scoped<'a>(c: &mut Context<'a>, index: u32, subproblem: bool) -> Result<bool> {
    let root = c.root(index)?;
    let semantics = c.plan_semantics(root)?;
    if semantics.relation != RelationKind::InnerJoin || !semantics.local_serial {
        return Ok(false);
    }
    // Removing a parameter-binding JOIN while reading its children as two
    // independent streams would lose correlation/bind-before-rescan semantics.
    for role in [BindingRole::LeftPushDown, BindingRole::RightPushDown] {
        if c.binding_count(root, role)? != 0 {
            return Ok(false);
        }
    }
    let bindings = c.binding_count(root, BindingRole::NestedLoop)?;
    if bindings != 0 {
        return correlated::contribute(c, index, root, bindings, subproblem);
    }
    let info = c.plan(root)?;
    if info.child_count != 2
        || info.expression_count(Role::JoinCondition) != 1
        || [Role::Filter, Role::Startup, Role::JoinFilter]
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
    let (condition, _) = c.expression(root, Role::JoinCondition, 0)?;
    if c.expression_semantics(condition)?.comparison != ComparisonKind::Equal
        || c.describe_expression(condition)?.argument_count() != 2
    {
        return Ok(false);
    }
    let mut keys = [c.argument(condition, 0)?, c.argument(condition, 1)?];
    let a = c.expression_semantics(keys[0])?;
    let b = c.expression_semantics(keys[1])?;
    if a.value == ValueKind::Other
        || a.value != b.value
        || !a.scalar_deterministic
        || !b.scalar_deterministic
        || c.describe_expression(keys[0])?.sql_type() != c.describe_expression(keys[1])?.sql_type()
    {
        return Ok(false);
    }
    match (branch(c, plans, keys[0])?, branch(c, plans, keys[1])?) {
        (Some(0), Some(1)) => (),
        (Some(1), Some(0)) => keys.swap(0, 1),
        _ => return Ok(false),
    }
    let count = c.column_count()? as usize;
    if count > 1024 {
        return Ok(false);
    }
    let mut inputs: [Vec<ExpressionId<'a>>; 2] = [Vec::new(), Vec::new()];
    for side in 0..2 {
        inputs[side]
            .try_reserve_exact(count + 1)
            .map_err(|_| sys::NO_MEMORY)?;
        inputs[side].push(keys[side]);
    }
    let mut outputs = Vec::new();
    outputs
        .try_reserve_exact(count)
        .map_err(|_| sys::NO_MEMORY)?;
    let mut mapping = Vec::new();
    mapping
        .try_reserve_exact(count)
        .map_err(|_| sys::NO_MEMORY)?;
    // Preserve every resolved query column, not just SELECT targets. Upper
    // ORDER BY/group/filter expressions may need additional input columns.
    for ordinal in 0..count {
        let expr = c.column(ordinal as u32)?;
        if outputs.contains(&expr) {
            continue;
        }
        if c.describe_expression(expr)?.column().is_none() {
            return Ok(false);
        }
        let Some(side) = branch(c, plans, expr)? else {
            // Query metadata deliberately retains its query-block scope. Only
            // proven outside BOTH inputs can be omitted for this subproblem;
            // ambiguous, independent or unknown dependencies still decline.
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
    // Explicit reference nested-loop estimate, not a calibrated production
    // model. Runtime still enforces owned-input memory/row budgets and cancel.
    let cost = 1.0 + left.rows * right.rows * 0.02;
    if left.rows < 0.0 || right.rows < 0.0 || !cost.is_finite() {
        return Ok(false);
    }
    let bytes = encode(&mapping)?;
    c.custom_fragment(
        &CustomPath {
            input: index,
            service_id: c"org.seekdb.rust-candidate.spool",
            service_major: 1,
            minimum_minor: 0,
            plan: &bytes,
            operator_cost: cost,
            preserves_order: false,
            blocking: true,
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
    )?;
    Ok(true)
}
fn encode(mapping: &[(u32, u32)]) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    bytes
        .try_reserve_exact(16 + 8 * mapping.len())
        .map_err(|_| sys::NO_MEMORY)?;
    bytes.extend_from_slice(b"SJE1");
    for value in [0, 0, mapping.len() as u32] {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    for &(side, column) in mapping {
        bytes.extend_from_slice(&side.to_le_bytes());
        bytes.extend_from_slice(&column.to_le_bytes());
    }
    Ok(bytes)
}
