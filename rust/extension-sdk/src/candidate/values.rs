// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
/// A value at a retained plan boundary, not a final physical output slot.
#[derive(Clone, Copy, Debug)]
pub struct Value {
    pub available: bool,
    /// Ordinary scalar evaluation consumes the graph arguments. This does
    /// not imply purity or permission to move evaluation across operators.
    pub scalar_arguments: bool,
}
impl<'a> Context<'a> {
    pub fn value(&mut self, plan: PlanId<'a>, expression: ExpressionId<'a>) -> Result<Value> {
        status(self.error)?;
        let Some(api) = self.values else {
            self.error = sys::UNSUPPORTED_ABI;
            return Err(self.error);
        };
        let mut out = sys::ValueInfo {
            struct_size: size_of::<sys::ValueInfo>() as u32,
            ..Default::default()
        };
        let result = unsafe {
            api.value_info.unwrap()(
                self.raw.host_context,
                plan.raw(),
                expression.raw(),
                &mut out,
            )
        };
        self.inspected(result)?;
        if out.struct_size != size_of::<sys::ValueInfo>() as u32
            || out.flags & !3 != 0
            || out.reserved != [0; 4]
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok(Value {
            available: out.flags & 1 != 0,
            scalar_arguments: out.flags & 2 != 0,
        })
    }
    /// Find a cut of existing row values sufficient for later evaluation of
    /// `expression`. Preserves aggregate/window results instead of traversing
    /// into their inputs. None means unproven, not impossible. The caller must
    /// keep evaluation at its original stage and preserve each returned value.
    /// Constants need no writable slot. Cycles, limits and host errors fail.
    /// ```compile_fail
    /// use seekdb_extension::candidate::{Context, ExpressionId, PlanId};
    /// fn escape<'a>(c: &mut Context<'a>, p: PlanId<'a>, e: ExpressionId<'a>) -> Vec<ExpressionId<'static>> {
    ///     c.required_values(p, e).unwrap().unwrap()
    /// }
    /// ```
    pub fn required_values(
        &mut self,
        plan: PlanId<'a>,
        expression: ExpressionId<'a>,
    ) -> Result<Option<Vec<ExpressionId<'a>>>> {
        // An explicit DFS keeps stack use bounded; active/finished states
        // distinguish expression DAG sharing from malformed cycles.
        let result = (|| {
            status(self.error)?;
            if self.values.is_none() {
                return Err(sys::UNSUPPORTED_ABI);
            }
            let mut states = std::collections::HashMap::new();
            let mut pending = Vec::new();
            let mut result = Vec::new();
            pending.try_reserve(1).map_err(|_| sys::NO_MEMORY)?;
            pending.push((expression, false));
            while let Some((expr, finish)) = pending.pop() {
                if finish {
                    states.insert(expr.raw(), 2u8);
                    continue;
                }
                match states.get(&expr.raw()) {
                    Some(1) => return Err(sys::INVALID),
                    Some(2) => continue,
                    _ => (),
                }
                if states.len() == 16384 {
                    return Err(sys::INVALID);
                }
                states.try_reserve(1).map_err(|_| sys::NO_MEMORY)?;
                let descriptor = self.describe_expression(expr)?;
                if descriptor.constant() {
                    states.insert(expr.raw(), 2);
                    continue;
                }
                let value = self.value(plan, expr)?;
                if value.available {
                    result.try_reserve(1).map_err(|_| sys::NO_MEMORY)?;
                    result.push(expr);
                    states.insert(expr.raw(), 2);
                } else if value.scalar_arguments {
                    let count = descriptor.argument_count() as usize;
                    if count > 16384 || pending.len() + count + 1 > 32768 {
                        return Err(sys::INVALID);
                    }
                    states.insert(expr.raw(), 1);
                    pending.try_reserve(count + 1).map_err(|_| sys::NO_MEMORY)?;
                    pending.push((expr, true));
                    for i in (0..count).rev() {
                        pending.push((self.argument(expr, i as u32)?, false));
                    }
                } else {
                    return Ok(None);
                }
            }
            Ok(Some(result))
        })();
        if let Err(error) = result {
            self.error = error;
        }
        result
    }
}
