// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum RelationKind {
    Other,
    InnerJoin,
    LeftJoin,
    RightJoin,
    FullJoin,
    LeftSemi,
    RightSemi,
    LeftAnti,
    RightAnti,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum ComparisonKind {
    Other,
    Equal,
    NullSafeEqual,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum ValueKind {
    Other,
    SignedInteger,
    UnsignedInteger,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum DependencyScope {
    Independent,
    Outside,
    Contained,
}
pub struct PlanSemantics {
    pub relation: RelationKind,
    pub local_serial: bool,
}
pub struct ExpressionSemantics {
    pub comparison: ComparisonKind,
    pub value: ValueKind,
    pub scalar_deterministic: bool,
}
impl<'a> Context<'a> {
    fn semantics_api(&mut self) -> Result<&'a sys::CandidateContextV5> {
        status(self.error)?;
        self.semantics.ok_or_else(|| {
            self.error = sys::UNSUPPORTED_ABI;
            self.error
        })
    }
    fn semantic_invalid<T>(&mut self) -> Result<T> {
        self.error = sys::INVALID;
        Err(self.error)
    }
    pub fn plan_semantics(&mut self, plan: PlanId<'a>) -> Result<PlanSemantics> {
        let api = self.semantics_api()?;
        let mut out = sys::PlanSemantics {
            struct_size: size_of::<sys::PlanSemantics>() as u32,
            ..Default::default()
        };
        let result =
            unsafe { api.plan_semantics.unwrap()(self.raw.host_context, plan.raw(), &mut out) };
        self.inspected(result)?;
        if out.struct_size != size_of::<sys::PlanSemantics>() as u32
            || out.flags & !1 != 0
            || out.reserved_word != 0
            || out.reserved != [0; 4]
        {
            return self.semantic_invalid();
        }
        let kinds = [
            RelationKind::Other,
            RelationKind::InnerJoin,
            RelationKind::LeftJoin,
            RelationKind::RightJoin,
            RelationKind::FullJoin,
            RelationKind::LeftSemi,
            RelationKind::RightSemi,
            RelationKind::LeftAnti,
            RelationKind::RightAnti,
        ];
        let Some(&relation) = kinds.get(out.relation_kind as usize) else {
            return self.semantic_invalid();
        };
        Ok(PlanSemantics {
            relation,
            local_serial: out.flags & 1 != 0,
        })
    }
    pub fn expression_semantics(&mut self, expr: ExpressionId<'a>) -> Result<ExpressionSemantics> {
        let api = self.semantics_api()?;
        let mut out = sys::ExprSemantics {
            struct_size: size_of::<sys::ExprSemantics>() as u32,
            ..Default::default()
        };
        let result = unsafe {
            api.expression_semantics.unwrap()(self.raw.host_context, expr.raw(), &mut out)
        };
        self.inspected(result)?;
        if out.struct_size != size_of::<sys::ExprSemantics>() as u32
            || out.flags & !1 != 0
            || out.reserved != [0; 4]
        {
            return self.semantic_invalid();
        }
        let comparison = match out.comparison_kind {
            0 => ComparisonKind::Other,
            1 => ComparisonKind::Equal,
            2 => ComparisonKind::NullSafeEqual,
            _ => return self.semantic_invalid(),
        };
        let value = match out.value_kind {
            0 => ValueKind::Other,
            1 => ValueKind::SignedInteger,
            2 => ValueKind::UnsignedInteger,
            _ => return self.semantic_invalid(),
        };
        Ok(ExpressionSemantics {
            comparison,
            value,
            scalar_deterministic: out.flags & 1 != 0,
        })
    }
    /// Relation-ID containment, not proof of physical availability or safe
    /// expression movement. Independent is distinct from nonempty containment.
    pub fn dependency_scope(
        &mut self,
        plan: PlanId<'a>,
        expr: ExpressionId<'a>,
    ) -> Result<DependencyScope> {
        let api = self.semantics_api()?;
        let mut out = u32::MAX;
        let result =
            unsafe { api.scope.unwrap()(self.raw.host_context, plan.raw(), expr.raw(), &mut out) };
        self.inspected(result)?;
        match out {
            0 => Ok(DependencyScope::Independent),
            1 => Ok(DependencyScope::Outside),
            2 => Ok(DependencyScope::Contained),
            _ => self.semantic_invalid(),
        }
    }
    /// Resolved column-reference inventory, including non-SELECT uses. Not a
    /// minimal projection, subtree schema, or enumeration across query blocks.
    pub fn column_count(&mut self) -> Result<u32> {
        let api = self.semantics_api()?;
        let mut count = 0;
        let result = unsafe { api.column_count.unwrap()(self.raw.host_context, &mut count) };
        self.inspected(result)?;
        Ok(count)
    }
    pub fn column(&mut self, ordinal: u32) -> Result<ExpressionId<'a>> {
        let api = self.semantics_api()?;
        let mut out = u32::MAX;
        let result = unsafe { api.column.unwrap()(self.raw.host_context, ordinal, &mut out) };
        self.inspected(result)?;
        Ok(ExpressionId::from_raw(self.valid_handle(out)?))
    }
}
