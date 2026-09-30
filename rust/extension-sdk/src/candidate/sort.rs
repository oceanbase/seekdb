// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;

/// Actual SORT semantics, not a guarantee that a replacement is equivalent.
/// Optional limit expressions are invocation-local handles, not evaluated values.
/// ```compile_fail
/// use seekdb_extension::candidate::{Context, Sort};
/// fn escape(c: &mut Context<'_>) -> Sort<'static> {
///     let root = c.root(0).unwrap(); c.sort(root).unwrap().unwrap()
/// }
/// ```
pub struct Sort<'a> {
    pub key_count: u32,
    pub prefix_key_count: u32,
    pub partition_key_count: u32,
    pub encoded_keys: bool,
    pub local_merge: bool,
    pub with_ties: bool,
    pub runtime_filter: bool,
    pub topn: Option<ExpressionId<'a>>,
    pub topk_limit: Option<ExpressionId<'a>>,
    pub topk_offset: Option<ExpressionId<'a>>,
    pub hash: Option<ExpressionId<'a>>,
}
pub struct SortKey<'a> {
    pub expression: ExpressionId<'a>,
    pub descending: bool,
    /// Position in the final output, independent of ascending/descending.
    pub nulls_first: bool,
}
impl<'a> Context<'a> {
    fn sorts_api(&mut self) -> Result<&'a sys::CandidateContextV7> {
        status(self.error)?;
        self.sorts.ok_or_else(|| {
            self.error = sys::UNSUPPORTED_ABI;
            self.error
        })
    }
    fn sort_invalid<T>(&mut self) -> Result<T> {
        self.error = sys::INVALID;
        Err(self.error)
    }
    /// None means a known non-SORT node, never a missing capability or an error.
    pub fn sort(&mut self, plan: PlanId<'a>) -> Result<Option<Sort<'a>>> {
        let api = self.sorts_api()?;
        let mut out = sys::SortInfo {
            struct_size: size_of::<sys::SortInfo>() as u32,
            topn_expression: u32::MAX,
            topk_limit_expression: u32::MAX,
            topk_offset_expression: u32::MAX,
            hash_expression: u32::MAX,
            ..Default::default()
        };
        let result = unsafe { api.sort_info.unwrap()(self.raw.host_context, plan.raw(), &mut out) };
        self.inspected(result)?;
        if out.struct_size != size_of::<sys::SortInfo>() as u32
            || out.flags & !31 != 0
            || out.reserved_word != 0
            || out.reserved != [0; 4]
            || out.prefix_key_count > out.key_count
            || out.partition_key_count > out.key_count
        {
            return self.sort_invalid();
        }
        if out.flags & 1 == 0 {
            if out.flags != 0
                || out.key_count != 0
                || [
                    out.topn_expression,
                    out.topk_limit_expression,
                    out.topk_offset_expression,
                    out.hash_expression,
                ] != [u32::MAX; 4]
            {
                return self.sort_invalid();
            }
            return Ok(None);
        }
        let expr = |id| (id != u32::MAX).then(|| ExpressionId::from_raw(id));
        Ok(Some(Sort {
            key_count: out.key_count,
            prefix_key_count: out.prefix_key_count,
            partition_key_count: out.partition_key_count,
            encoded_keys: out.flags & 2 != 0,
            local_merge: out.flags & 4 != 0,
            with_ties: out.flags & 8 != 0,
            runtime_filter: out.flags & 16 != 0,
            topn: expr(out.topn_expression),
            topk_limit: expr(out.topk_limit_expression),
            topk_offset: expr(out.topk_offset_expression),
            hash: expr(out.hash_expression),
        }))
    }
    /// Read one unencoded SQL key with normalized direction/NULL placement.
    /// This intentionally does not change Role::Ordering's host-specific value.
    pub fn sort_key(&mut self, plan: PlanId<'a>, ordinal: u32) -> Result<SortKey<'a>> {
        let api = self.sorts_api()?;
        let mut expression = u32::MAX;
        let mut flags = 0;
        let result = unsafe {
            api.sort_key.unwrap()(
                self.raw.host_context,
                plan.raw(),
                ordinal,
                &mut expression,
                &mut flags,
            )
        };
        self.inspected(result)?;
        if flags & !3 != 0 {
            return self.sort_invalid();
        }
        Ok(SortKey {
            expression: ExpressionId::from_raw(self.valid_handle(expression)?),
            descending: flags & 1 != 0,
            nulls_first: flags & 2 != 0,
        })
    }
}
