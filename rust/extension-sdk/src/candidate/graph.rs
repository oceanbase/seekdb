// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
/// An identity in this hook invocation, not a candidate index or cached binding.
/// ```compile_fail
/// use seekdb_extension::candidate::{Context, PlanId};
/// fn escape(context: &mut Context<'_>) -> PlanId<'static> { context.root(0).unwrap() }
/// ```
/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::candidate::PlanId<'static>>();
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct PlanId<'a>(u32, PhantomData<(&'a (), Rc<()>)>);
impl PlanId<'_> {
    pub(super) fn raw(self) -> u32 {
        self.0
    }
}
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct ExpressionId<'a>(u32, PhantomData<(&'a (), Rc<()>)>);
impl ExpressionId<'_> {
    pub(super) fn from_raw(id: u32) -> Self {
        Self(id, PhantomData)
    }
    pub(super) fn raw(self) -> u32 {
        self.0
    }
}
/// Already-present fields, NOT output columns or all required dependencies.
#[derive(Clone, Copy, Debug)]
#[repr(u32)]
pub enum Role {
    Filter = 1,
    Startup = 2,
    Ordering = 3,
    JoinCondition = 4,
    JoinFilter = 5,
}
pub struct Plan {
    pub operator_type: u32,
    pub child_count: u32,
    /// Matched-build join enum; absent for non-join nodes.
    pub join_type: Option<u32>,
    pub cost: f64,
    pub rows: f64,
    pub width: f64,
    counts: [u32; 5],
}
impl Plan {
    pub fn expression_count(&self, role: Role) -> u32 {
        self.counts[role as usize - 1]
    }
}
/// Copied metadata. SQL kinds/collations use the matched host build's IDs.
/// Constant values, complete output schemas and mutation are not exposed here.
pub struct Expression(sys::ExprInfo);
/// The current rewritten query block, not the output schema of each candidate.
/// A SELECT target can be an aggregate/window expression not yet computable in
/// a child path. Names, bound values and nested-query traversal are not exposed.
#[derive(Clone, Copy, Debug)]
pub struct Query {
    /// Matched-build statement kind.
    pub statement_type: u32,
    /// None for a statement without a supported list; Some(0) is an empty list.
    pub target_count: Option<u32>,
    pub set_operation: bool,
}
impl Expression {
    pub fn expression_type(&self) -> u32 {
        self.0.expression_type
    }
    pub fn sql_type(&self) -> u32 {
        self.0.sql_type
    }
    pub fn argument_count(&self) -> u32 {
        self.0.argument_count
    }
    pub fn collation(&self) -> i32 {
        self.0.collation
    }
    pub fn precision(&self) -> i32 {
        self.0.precision
    }
    pub fn scale(&self) -> i32 {
        self.0.scale
    }
    pub fn not_null(&self) -> bool {
        self.0.flags & 16 != 0
    }
    pub fn constant(&self) -> bool {
        self.0.flags & 8 != 0
    }
    pub fn stored(&self) -> bool {
        self.0.flags & 2 != 0
    }
    pub fn column(&self) -> Option<(u64, u64)> {
        (self.0.flags & 4 != 0).then_some((self.0.table_id, self.0.column_id))
    }
    pub fn plugin_type(&self) -> Option<&CStr> {
        (self.0.flags & 1 != 0).then(|| unsafe { CStr::from_ptr(self.0.type_id.as_ptr()) })
    }
}
impl<'a> Context<'a> {
    fn query_api(&mut self) -> Result<&'a sys::CandidateContextV4> {
        status(self.error)?;
        if let Some(api) = self.query {
            Ok(api)
        } else {
            self.error = sys::UNSUPPORTED_ABI;
            Err(self.error)
        }
    }
    pub fn query(&mut self) -> Result<Query> {
        let api = self.query_api()?;
        let mut out = sys::QueryInfo {
            struct_size: size_of::<sys::QueryInfo>() as u32,
            ..Default::default()
        };
        let result = unsafe { api.query.unwrap()(self.raw.host_context, &mut out) };
        self.inspected(result)?;
        if out.struct_size != size_of::<sys::QueryInfo>() as u32
            || out.reserved != [0; 4]
            || out.flags & !3 != 0
            || (out.flags & 1 == 0 && (out.target_count != 0 || out.flags & 2 != 0))
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok(Query {
            statement_type: out.statement_type,
            target_count: (out.flags & 1 != 0).then_some(out.target_count),
            set_operation: out.flags & 2 != 0,
        })
    }
    /// SELECT-list ordinal to the same invocation-local identity used by the
    /// graph and custom_with_layout. Preserves list order and duplicate slots.
    /// ```compile_fail
    /// use seekdb_extension::candidate::{Context, ExpressionId};
    /// fn escape(c: &mut Context<'_>) -> ExpressionId<'static> { c.target(0).unwrap() }
    /// ```
    pub fn target(&mut self, ordinal: u32) -> Result<ExpressionId<'a>> {
        let api = self.query_api()?;
        let mut id = u32::MAX;
        let result = unsafe { api.target.unwrap()(self.raw.host_context, ordinal, &mut id) };
        self.inspected(result)?;
        Ok(ExpressionId(self.valid_handle(id)?, PhantomData))
    }
    fn inspection(&mut self) -> Result<&'a sys::CandidateContextV3> {
        status(self.error)?;
        if let Some(api) = self.graph {
            Ok(api)
        } else {
            self.error = sys::UNSUPPORTED_ABI;
            Err(self.error)
        }
    }
    pub(super) fn inspected(&mut self, result: sys::Status) -> Result<()> {
        self.error = result;
        self.database_error =
            unsafe { self.builders.unwrap().get_error.unwrap()(self.raw.host_context) };
        if self.error == sys::OK && self.database_error != 0 {
            self.error = sys::FAILED_PRECONDITION;
        }
        status(self.error)
    }
    pub(super) fn valid_handle(&mut self, id: u32) -> Result<u32> {
        if id == u32::MAX {
            self.error = sys::INVALID;
            Err(self.error)
        } else {
            Ok(id)
        }
    }
    pub fn root(&mut self, candidate: u32) -> Result<PlanId<'a>> {
        let api = self.inspection()?;
        let mut id = u32::MAX;
        let result = unsafe { api.root.unwrap()(self.raw.host_context, candidate, &mut id) };
        self.inspected(result)?;
        Ok(PlanId(self.valid_handle(id)?, PhantomData))
    }
    pub fn plan(&mut self, id: PlanId<'a>) -> Result<Plan> {
        let api = self.inspection()?;
        let mut out = sys::PlanInfo {
            struct_size: size_of::<sys::PlanInfo>() as u32,
            ..Default::default()
        };
        let result = unsafe { api.plan.unwrap()(self.raw.host_context, id.0, &mut out) };
        self.inspected(result)?;
        if out.struct_size != size_of::<sys::PlanInfo>() as u32
            || out.reserved_word != 0
            || out.reserved != [0; 4]
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok(Plan {
            operator_type: out.operator_type,
            child_count: out.child_count,
            join_type: (out.join_type != u32::MAX).then_some(out.join_type),
            cost: out.cost,
            rows: out.rows,
            width: out.width,
            counts: out.expression_counts,
        })
    }
    pub fn child(&mut self, id: PlanId<'a>, index: u32) -> Result<PlanId<'a>> {
        let api = self.inspection()?;
        let mut child = u32::MAX;
        let result = unsafe { api.child.unwrap()(self.raw.host_context, id.0, index, &mut child) };
        self.inspected(result)?;
        Ok(PlanId(self.valid_handle(child)?, PhantomData))
    }
    pub fn expression(
        &mut self,
        id: PlanId<'a>,
        role: Role,
        index: u32,
    ) -> Result<(ExpressionId<'a>, Option<u32>)> {
        let api = self.inspection()?;
        let (mut expr, mut ordering) = (u32::MAX, u32::MAX);
        let result = unsafe {
            api.expression.unwrap()(
                self.raw.host_context,
                id.0,
                role as u32,
                index,
                &mut expr,
                &mut ordering,
            )
        };
        self.inspected(result)?;
        if matches!(role, Role::Ordering) != (ordering != u32::MAX) {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok((
            ExpressionId(self.valid_handle(expr)?, PhantomData),
            (ordering != u32::MAX).then_some(ordering),
        ))
    }
    pub fn describe_expression(&mut self, id: ExpressionId<'a>) -> Result<Expression> {
        let api = self.inspection()?;
        // repr(C) contains only integers/byte arrays: all-zero is valid Rust.
        let mut out: sys::ExprInfo = unsafe { std::mem::zeroed() };
        out.struct_size = size_of::<sys::ExprInfo>() as u32;
        let result =
            unsafe { api.describe_expression.unwrap()(self.raw.host_context, id.0, &mut out) };
        self.inspected(result)?;
        let bytes = unsafe {
            std::slice::from_raw_parts(out.type_id.as_ptr().cast::<u8>(), out.type_id.len())
        };
        let name = CStr::from_bytes_until_nul(bytes);
        let valid_name = match name {
            Ok(name) if out.flags & 1 != 0 => crate::result_type::validate_type(name).is_ok(),
            Ok(name) => name.is_empty(),
            Err(_) => false,
        };
        if out.struct_size != size_of::<sys::ExprInfo>() as u32
            || out.flags & !31 != 0
            || out.reserved != [0; 4]
            || !valid_name
            || (out.flags & 2 != 0 && out.flags & 1 == 0)
            || (out.flags & 4 == 0 && (out.table_id != 0 || out.column_id != 0))
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok(Expression(out))
    }
    pub fn argument(&mut self, id: ExpressionId<'a>, index: u32) -> Result<ExpressionId<'a>> {
        let api = self.inspection()?;
        let mut arg = u32::MAX;
        let result = unsafe { api.argument.unwrap()(self.raw.host_context, id.0, index, &mut arg) };
        self.inspected(result)?;
        Ok(ExpressionId(self.valid_handle(arg)?, PhantomData))
    }
}
