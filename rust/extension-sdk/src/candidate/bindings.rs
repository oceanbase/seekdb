// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum BindingRole {
    NestedLoop = 1,
    LeftPushDown = 2,
    RightPushDown = 3,
}
/// Invocation-local expression identities, not a mutable parameter slot or a
/// prepared-statement value. Inspect `source` with the existing graph APIs.
pub struct ParameterBinding<'a> {
    pub parameter: ExpressionId<'a>,
    pub source: ExpressionId<'a>,
}
impl<'a> Context<'a> {
    fn bindings_api(&mut self) -> Result<&'a sys::CandidateContextV6> {
        status(self.error)?;
        self.bindings.ok_or_else(|| {
            self.error = sys::UNSUPPORTED_ABI;
            self.error
        })
    }
    /// Bindings owned/carried by this JOIN. Zero for other node kinds is not
    /// proof that their subtrees are independent; this does not recurse.
    pub fn binding_count(&mut self, plan: PlanId<'a>, role: BindingRole) -> Result<u32> {
        let api = self.bindings_api()?;
        let mut count = 0;
        let result = unsafe {
            api.binding_count.unwrap()(self.raw.host_context, plan.raw(), role as u32, &mut count)
        };
        self.inspected(result)?;
        Ok(count)
    }
    pub fn binding(
        &mut self,
        plan: PlanId<'a>,
        role: BindingRole,
        index: u32,
    ) -> Result<ParameterBinding<'a>> {
        let api = self.bindings_api()?;
        let (mut parameter, mut source) = (u32::MAX, u32::MAX);
        let result = unsafe {
            api.binding.unwrap()(
                self.raw.host_context,
                plan.raw(),
                role as u32,
                index,
                &mut parameter,
                &mut source,
            )
        };
        self.inspected(result)?;
        Ok(ParameterBinding {
            parameter: ExpressionId::from_raw(self.valid_handle(parameter)?),
            source: ExpressionId::from_raw(self.valid_handle(source)?),
        })
    }
}
