// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Version-bound selection of real host planner candidates. Requires Server-dev
//! admission; this is not the Public optimizer hook or arbitrary path creation.
use crate::{boundary, optimizer::Definition, status, sys, Registration, Result};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, rc::Rc};
mod bindings;
mod graph;
mod semantics;
mod sort;
mod values;
pub use bindings::{BindingRole, ParameterBinding};
pub use graph::{Expression, ExpressionId, Plan, PlanId, Query, Role};
pub use semantics::{
    ComparisonKind, DependencyScope, ExpressionSemantics, PlanSemantics, RelationKind, ValueKind,
};
pub use sort::{Sort, SortKey};
pub use values::Value;

/// A unary equivalent implementation of an existing candidate. SQL column
/// identities and relational results must be preserved; ordering is optional.
/// The host copies service identity and plan bytes during submission.
pub struct CustomPath<'a> {
    pub input: u32,
    pub service_id: &'a CStr,
    pub service_major: u32,
    pub minimum_minor: u32,
    pub plan: &'a [u8],
    pub operator_cost: f64,
    pub preserves_order: bool,
    pub blocking: bool,
}
/// One actual input subtree and its independent wire-ordered dependencies.
/// Plan and expression identities must belong to the current hook invocation.
pub struct FragmentInput<'a, 'b> {
    pub plan: PlanId<'a>,
    pub expressions: &'b [ExpressionId<'a>],
}
/// Transfer a parameter from the removed target into the custom executor.
/// Its source is a declared input slot, not an arbitrary writable SQL parameter.
pub struct InputBinding<'a> {
    pub parameter: ExpressionId<'a>,
    pub source_input: u32,
    pub source_column: u32,
    pub target_input: u32,
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Mode {
    Around = 1,
    Replace = 2,
}
/// Independent upper-relation contribution points. Native candidates already
/// implement the named operation; subsequent upper operations are not built.
/// Registration does not imply that every aggregate/window algorithm can yet
/// be expressed by the existing graph metadata and fragment builders.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UpperStage {
    Group,
    Window,
    Distinct,
    Ordered,
}
impl UpperStage {
    fn point(self) -> &'static CStr {
        match self {
            Self::Group => c"optimizer.upper.group.paths.v1",
            Self::Window => c"optimizer.upper.window.paths.v1",
            Self::Distinct => c"optimizer.upper.distinct.paths.v1",
            Self::Ordered => c"optimizer.upper.ordered.paths.v1",
        }
    }
}
impl Registration<'_> {
    /// Contribute at one upper stage, independently of relation/JOIN/select.
    /// Requires BUILDERS, Around mode and exactly one next; never select.
    /// Ordered-stage custom candidates must preserve the target's ordering.
    pub fn upper_paths_hook(
        &mut self,
        stage: UpperStage,
        definition: &Definition<'_>,
    ) -> Result<()> {
        self.candidate_hook_at(definition, stage.point())
    }
    pub fn candidate_hook(&mut self, definition: &Definition<'_>) -> Result<()> {
        self.candidate_hook_at(definition, c"optimizer.candidate.select.v1")
    }
    /// Contribute equivalent relation candidates before upper operators such
    /// as aggregation, ORDER BY and LIMIT. The service must enable BUILDERS and
    /// use Around mode. Call next exactly once; do not call select. All original
    /// and added paths remain available to the host's normal costing/pruning.
    /// This does not register at the late candidate-selection hook point.
    pub fn relation_paths_hook(&mut self, definition: &Definition<'_>) -> Result<()> {
        self.candidate_hook_at(definition, c"optimizer.relation.paths.v1")
    }
    /// Contribute alternatives for one JOIN-enumeration subproblem. This is
    /// independent of relation_paths_hook: query targets/columns still describe
    /// the whole query block, so check dependency scopes for partial relations.
    /// Requires BUILDERS and Around; call next exactly once and never select.
    pub fn join_paths_hook(&mut self, definition: &Definition<'_>) -> Result<()> {
        self.candidate_hook_at(definition, c"optimizer.join.paths.v1")
    }
    fn candidate_hook_at(&mut self, definition: &Definition<'_>, point: &CStr) -> Result<()> {
        let descriptor = sys::OptimizerHookDescriptor {
            struct_size: size_of::<sys::OptimizerHookDescriptor>() as u32,
            object_id: definition.object_id.as_ptr(),
            hook_point: point.as_ptr(),
            priority: definition.priority,
            reserved_word: 0,
            flags: definition.flags,
            implementation: definition.implementation.raw(),
            reserved: [0; 4],
        };
        self.register_descriptor(5, &descriptor)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Info {
    /// Numeric operator kind belongs to the matched server build.
    pub operator_type: u32,
    pub cost: f64,
    pub rows: f64,
    pub width: f64,
}
/// Only valid during the synchronous hook. No plan pointer or borrowed C++
/// object escapes through the safe API.
/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::candidate::Context<'static>>();
/// ```
pub struct Context<'a> {
    raw: &'a sys::CandidateContext,
    builders: Option<&'a sys::CandidateContextV2>,
    graph: Option<&'a sys::CandidateContextV3>,
    query: Option<&'a sys::CandidateContextV4>,
    semantics: Option<&'a sys::CandidateContextV5>,
    bindings: Option<&'a sys::CandidateContextV6>,
    sorts: Option<&'a sys::CandidateContextV7>,
    values: Option<&'a sys::CandidateContextV8>,
    called: bool,
    error: sys::Status,
    database_error: i32,
    _thread: PhantomData<Rc<()>>,
}
impl<'a> Context<'a> {
    /// Replace the equivalent result candidate `path.input` with an algorithm
    /// consuming the declared subtrees, not necessarily that candidate's root.
    /// Inputs must be disjoint subtrees within the target. Each has <=1024
    /// expressions; outputs are unique writable slots, with <=1024 entries.
    /// Empty inputs explicitly mean a source operator, not inherited inputs.
    ///
    /// Currently requests local serial placement. Distributed/PX execution
    /// needs an additional placement contract and is rejected by the host.
    /// `preserves_order` declares the target's order, not the first input's.
    /// Relation/value semantics are the trusted plugin author's responsibility.
    pub fn custom_fragment(
        &mut self,
        path: &CustomPath<'_>,
        inputs: &[FragmentInput<'a, '_>],
        outputs: &[ExpressionId<'a>],
    ) -> Result<u32> {
        self.fragment_request(path, inputs, outputs, None)
    }
    /// Adopt the removed JOIN's complete binding list. Initially the host
    /// supports unchanged left/right inputs with NestedLoop parameters flowing
    /// from input zero to input one. Execution must bind_rescan_input before
    /// reading the dependent child; the host snapshots source values.
    pub fn custom_bound_fragment(
        &mut self,
        path: &CustomPath<'_>,
        inputs: &[FragmentInput<'a, '_>],
        outputs: &[ExpressionId<'a>],
        bindings: &[InputBinding<'a>],
    ) -> Result<u32> {
        self.fragment_request(path, inputs, outputs, Some(bindings))
    }
    fn fragment_request(
        &mut self,
        path: &CustomPath<'_>,
        inputs: &[FragmentInput<'a, '_>],
        outputs: &[ExpressionId<'a>],
        bindings: Option<&[InputBinding<'a>]>,
    ) -> Result<u32> {
        let mut base = self.custom_request(path)?;
        if self.graph.is_none() {
            self.error = sys::UNSUPPORTED_ABI;
            return Err(self.error);
        }
        let mut bound = Vec::new();
        if let Some(bindings) = bindings {
            if self.bindings.is_none() {
                self.error = sys::UNSUPPORTED_ABI;
                return Err(self.error);
            }
            if bindings.is_empty()
                || bindings.len() > 1024
                || bindings.iter().enumerate().any(|(i, b)| {
                    b.source_input as usize >= inputs.len()
                        || b.target_input as usize >= inputs.len()
                        || b.source_input == b.target_input
                        || b.source_column as usize
                            >= inputs[b.source_input as usize].expressions.len()
                        || bindings[..i].iter().any(|p| p.parameter == b.parameter)
                })
            {
                self.error = sys::INVALID;
                return Err(self.error);
            }
            if bound.try_reserve_exact(bindings.len()).is_err() {
                self.error = sys::NO_MEMORY;
                return Err(self.error);
            }
            for b in bindings {
                bound.push(sys::InputBinding {
                    parameter: b.parameter.raw(),
                    source_input: b.source_input,
                    source_column: b.source_column,
                    target_input: b.target_input,
                });
            }
        }
        if inputs.len() > 64
            || outputs.len() > 1024
            || inputs.iter().any(|input| input.expressions.len() > 1024)
            || inputs
                .iter()
                .enumerate()
                .any(|(i, input)| inputs[..i].iter().any(|v| v.plan == input.plan))
            || outputs
                .iter()
                .enumerate()
                .any(|(i, value)| outputs[..i].contains(value))
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        let copy = || -> Result<_> {
            let mut plans = Vec::new();
            let mut offsets = Vec::new();
            let mut expressions = Vec::new();
            let mut result = Vec::new();
            plans
                .try_reserve_exact(inputs.len())
                .map_err(|_| sys::NO_MEMORY)?;
            offsets
                .try_reserve_exact(inputs.len() + 1)
                .map_err(|_| sys::NO_MEMORY)?;
            expressions
                .try_reserve_exact(inputs.iter().map(|i| i.expressions.len()).sum())
                .map_err(|_| sys::NO_MEMORY)?;
            result
                .try_reserve_exact(outputs.len())
                .map_err(|_| sys::NO_MEMORY)?;
            offsets.push(0);
            for input in inputs {
                plans.push(input.plan.raw());
                expressions.extend(input.expressions.iter().map(|id| id.raw()));
                offsets.push(expressions.len() as u32);
            }
            result.extend(outputs.iter().map(|id| id.raw()));
            Ok((plans, offsets, expressions, result))
        };
        let (plans, offsets, expressions, outputs) = match copy() {
            Ok(fields) => fields,
            Err(error) => {
                self.error = error;
                return Err(error);
            }
        };
        base.v1.struct_size = if bindings.is_some() {
            size_of::<sys::CustomPathRequestV4>()
        } else {
            size_of::<sys::CustomPathRequestV3>()
        } as u32;
        let request = sys::CustomPathRequestV3 {
            v2: sys::CustomPathRequestV2 {
                v1: base,
                inputs: expressions.as_ptr(),
                input_count: expressions.len() as u32,
                output_count: outputs.len() as u32,
                outputs: outputs.as_ptr(),
                reserved: [0; 4],
            },
            input_plans: plans.as_ptr(),
            plan_count: plans.len() as u32,
            execution: 1,
            input_offsets: offsets.as_ptr(),
            reserved: [0; 4],
        };
        if bindings.is_some() {
            let request = sys::CustomPathRequestV4 {
                v3: request,
                bindings: bound.as_ptr(),
                binding_count: bound.len() as u32,
                reserved_word: 0,
                reserved: [0; 4],
            };
            self.submit_custom(&request.v3.v2.v1.v1)
        } else {
            self.submit_custom(&request.v2.v1.v1)
        }
    }
    /// Submit plugin-owned execution, not a request for host MATERIAL.
    pub fn custom(&mut self, path: &CustomPath<'_>) -> Result<u32> {
        let request = self.custom_request(path)?;
        self.submit_custom(&request.v1)
    }
    /// Declare input dependencies and plugin-produced SQL expressions in wire
    /// order. IDs belong to this invocation's graph; no IDs enter cached plans.
    /// Outputs are unique writable expression slots, not a new SELECT list.
    /// Empty lists explicitly request zero columns. The plugin must implement
    /// the declared expressions' SQL semantics; the host cannot infer an
    /// algorithm from these IDs or from opaque plan bytes.
    pub fn custom_with_layout(
        &mut self,
        path: &CustomPath<'_>,
        inputs: &[ExpressionId<'a>],
        outputs: &[ExpressionId<'a>],
    ) -> Result<u32> {
        let mut base = self.custom_request(path)?;
        if self.graph.is_none() {
            self.error = sys::UNSUPPORTED_ABI;
            return Err(self.error);
        }
        if inputs.len() > 1024
            || outputs.len() > 1024
            || outputs
                .iter()
                .enumerate()
                .any(|(i, id)| outputs[..i].contains(id))
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        let copy = |ids: &[ExpressionId<'a>]| -> Result<Vec<u32>> {
            let mut result = Vec::new();
            result
                .try_reserve_exact(ids.len())
                .map_err(|_| sys::NO_MEMORY)?;
            result.extend(ids.iter().map(|id| id.raw()));
            Ok(result)
        };
        let (inputs, outputs) =
            match copy(inputs).and_then(|inputs| copy(outputs).map(|outputs| (inputs, outputs))) {
                Ok(ids) => ids,
                Err(error) => {
                    self.error = error;
                    return Err(error);
                }
            };
        base.v1.struct_size = size_of::<sys::CustomPathRequestV2>() as u32;
        let request = sys::CustomPathRequestV2 {
            v1: base,
            inputs: inputs.as_ptr(),
            input_count: inputs.len() as u32,
            output_count: outputs.len() as u32,
            outputs: outputs.as_ptr(),
            reserved: [0; 4],
        };
        self.submit_custom(&request.v1.v1)
    }
    fn custom_request(&mut self, path: &CustomPath<'_>) -> Result<sys::CustomPathRequest> {
        status(self.error)?;
        if self.builders.is_none() {
            self.error = sys::UNSUPPORTED_ABI;
            return Err(self.error);
        }
        if path.input >= self.count()
            || path.service_id.to_bytes().is_empty()
            || path.service_id.to_bytes().len() > 255
            || path.service_major == 0
            || path.plan.len() > 65536
            || !path.operator_cost.is_finite()
            || path.operator_cost < 0.0
        {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok(sys::CustomPathRequest {
            v1: sys::PathRequest {
                struct_size: size_of::<sys::CustomPathRequest>() as u32,
                kind: 2,
                input_index: path.input,
                reserved_word: 0,
                reserved: [0; 4],
            },
            service_id: path.service_id.as_ptr(),
            service_major: path.service_major,
            minimum_minor: path.minimum_minor,
            plan: path.plan.as_ptr(),
            plan_size: path.plan.len() as u32,
            flags: u32::from(path.preserves_order) | (u32::from(path.blocking) << 1),
            operator_cost: path.operator_cost,
            reserved: [0; 4],
        })
    }
    fn submit_custom(&mut self, request: &sys::PathRequest) -> Result<u32> {
        let api = self.builders.unwrap();
        let before = self.count();
        let mut index = u32::MAX;
        self.error = unsafe { api.build.unwrap()(self.raw.host_context, request, &mut index) };
        self.database_error = unsafe { api.get_error.unwrap()(self.raw.host_context) };
        if self.error == sys::OK
            && (self.database_error != 0
                || index != before
                || before.checked_add(1) != Some(self.count()))
        {
            self.error = sys::FAILED_PRECONDITION;
        }
        status(self.error)?;
        Ok(index)
    }
    pub fn count(&self) -> u32 {
        match self.builders {
            Some(api) => unsafe { api.current_count.unwrap()(self.raw.host_context) },
            None => self.raw.candidate_count,
        }
    }
    /// Add a host-owned materialization path without selecting it. Its actual
    /// properties/cost come from the host planner. Subsequent hooks and default
    /// cost selection see it through the same candidate set.
    pub fn materialize(&mut self, input: u32) -> Result<u32> {
        status(self.error)?;
        let Some(api) = self.builders else {
            self.error = sys::UNSUPPORTED_ABI;
            return Err(self.error);
        };
        if input >= self.count() {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        let before = self.count();
        let request = sys::PathRequest {
            struct_size: size_of::<sys::PathRequest>() as u32,
            kind: 1,
            input_index: input,
            reserved_word: 0,
            reserved: [0; 4],
        };
        let mut index = u32::MAX;
        self.error = unsafe { api.build.unwrap()(self.raw.host_context, &request, &mut index) };
        self.database_error = unsafe { api.get_error.unwrap()(self.raw.host_context) };
        if self.error == sys::OK
            && (self.database_error != 0
                || index != before
                || before.checked_add(1) != Some(self.count()))
        {
            self.error = sys::FAILED_PRECONDITION;
        }
        status(self.error)?;
        Ok(index)
    }
    pub fn database_error(&self) -> i32 {
        self.database_error
    }
    pub fn get(&mut self, index: u32) -> Result<Info> {
        status(self.error)?;
        if index >= self.count() {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        let mut info = sys::CandidateInfo {
            struct_size: size_of::<sys::CandidateInfo>() as u32,
            ..Default::default()
        };
        self.error = unsafe { self.raw.get.unwrap()(self.raw.host_context, index, &mut info) };
        status(self.error)?;
        if info.struct_size != size_of::<sys::CandidateInfo>() as u32 || info.reserved != [0; 4] {
            self.error = sys::INVALID;
            return Err(self.error);
        }
        Ok(Info {
            operator_type: info.operator_type,
            cost: info.cost,
            rows: info.rows,
            width: info.width,
        })
    }
    /// The latest selection wins. Calling next afterwards runs the downstream
    /// policy/default selector, which may replace this provisional choice.
    pub fn select(&mut self, index: u32) -> Result<()> {
        status(self.error)?;
        self.error = if index >= self.count() {
            sys::INVALID
        } else {
            unsafe { self.raw.select.unwrap()(self.raw.host_context, index) }
        };
        status(self.error)
    }
    pub fn call_next(&mut self) -> Result<()> {
        status(self.error)?;
        if self.called {
            self.error = sys::FAILED_PRECONDITION;
            return Err(self.error);
        }
        self.called = true;
        self.error =
            unsafe { self.raw.next.unwrap()(self.raw.continuation, &mut self.database_error) };
        if self.error == sys::OK && self.database_error != 0 {
            self.error = sys::FAILED_PRECONDITION;
        }
        status(self.error)
    }
}
pub trait Hook {
    const MODE: Mode;
    const BUILDERS: bool = false;
    /// Read the invocation-local planning graph; also enables builders.
    const INSPECT: bool = false;
    /// Inspect this query block's complete logical SELECT list. Also enables
    /// graph inspection/builders; requires candidate service minor 3.
    const QUERY_TARGETS: bool = false;
    /// Normalized semantics, relation scope and all resolved query columns.
    /// Also enables query/graph/builders; requires candidate service minor 4.
    const SEMANTICS: bool = false;
    /// Inspect JOIN execution-parameter bindings and their source expressions.
    /// Implies semantics/query/graph/builders; requires service minor 5.
    const PARAMETERS: bool = false;
    /// Inspect normalized SORT semantics, including special sort operations.
    /// Implies parameters/semantics/query/graph/builders; service minor 6.
    const SORTS: bool = false;
    /// Inspect values at retained plan boundaries; implies all earlier
    /// inspection capabilities and requires candidate service minor 7.
    const VALUES: bool = false;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()>;
    fn invoke(instance: *mut sys::Handle, context: &mut Context<'_>) -> Result<()>;
}
pub struct Service<H: Hook>(PhantomData<H>);
impl<H: Hook> Service<H> {
    pub const ABI: sys::CandidateService = sys::CandidateService {
        struct_size: size_of::<sys::CandidateService>() as u32,
        spi_major: 1,
        spi_minor: if H::VALUES {
            7
        } else if H::SORTS {
            6
        } else if H::PARAMETERS {
            5
        } else if H::SEMANTICS {
            4
        } else if H::QUERY_TARGETS {
            3
        } else if H::INSPECT {
            2
        } else if H::BUILDERS {
            1
        } else {
            0
        },
        mode: H::MODE as u32,
        invoke: Some(invoke::<H>),
        reserved: [0; 4],
    };
}
unsafe extern "C" fn invoke<H: Hook>(
    instance: *mut sys::Handle,
    raw: *const sys::CandidateContext,
) -> sys::Status {
    boundary(|| {
        H::validate_instance(instance)?;
        if raw.is_null() {
            return Err(sys::INVALID);
        }
        let raw = unsafe { &*raw };
        let expected_size = if H::VALUES {
            size_of::<sys::CandidateContextV8>()
        } else if H::SORTS {
            size_of::<sys::CandidateContextV7>()
        } else if H::PARAMETERS {
            size_of::<sys::CandidateContextV6>()
        } else if H::SEMANTICS {
            size_of::<sys::CandidateContextV5>()
        } else if H::QUERY_TARGETS {
            size_of::<sys::CandidateContextV4>()
        } else if H::INSPECT {
            size_of::<sys::CandidateContextV3>()
        } else if H::BUILDERS {
            size_of::<sys::CandidateContextV2>()
        } else {
            size_of::<sys::CandidateContext>()
        };
        if raw.struct_size != expected_size as u32
            || raw.candidate_count == 0
            || raw.host_context.is_null()
            || raw.get.is_none()
            || raw.select.is_none()
            || raw.next.is_none()
            || raw.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        let builders = if H::BUILDERS
            || H::INSPECT
            || H::QUERY_TARGETS
            || H::SEMANTICS
            || H::PARAMETERS
            || H::SORTS
            || H::VALUES
        {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV2>()
            };
            if api.current_count.is_none()
                || api.build.is_none()
                || api.get_error.is_none()
                || api.reserved != [0; 4]
            {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let graph = if H::INSPECT
            || H::QUERY_TARGETS
            || H::SEMANTICS
            || H::PARAMETERS
            || H::SORTS
            || H::VALUES
        {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV3>()
            };
            if api.root.is_none()
                || api.plan.is_none()
                || api.child.is_none()
                || api.expression.is_none()
                || api.describe_expression.is_none()
                || api.argument.is_none()
                || api.reserved != [0; 4]
            {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let query = if H::QUERY_TARGETS || H::SEMANTICS || H::PARAMETERS || H::SORTS || H::VALUES {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV4>()
            };
            if api.query.is_none() || api.target.is_none() || api.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let semantics = if H::SEMANTICS || H::PARAMETERS || H::SORTS || H::VALUES {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV5>()
            };
            if api.plan_semantics.is_none()
                || api.expression_semantics.is_none()
                || api.scope.is_none()
                || api.column_count.is_none()
                || api.column.is_none()
                || api.reserved != [0; 4]
            {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let bindings = if H::PARAMETERS || H::SORTS || H::VALUES {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV6>()
            };
            if api.binding_count.is_none() || api.binding.is_none() || api.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let sorts = if H::SORTS || H::VALUES {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV7>()
            };
            if api.sort_info.is_none() || api.sort_key.is_none() || api.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let values = if H::VALUES {
            let api = unsafe {
                &*(raw as *const sys::CandidateContext).cast::<sys::CandidateContextV8>()
            };
            if api.value_info.is_none() || api.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(api)
        } else {
            None
        };
        let mut context = Context {
            raw,
            builders,
            graph,
            query,
            semantics,
            bindings,
            sorts,
            values,
            called: false,
            error: sys::OK,
            database_error: 0,
            _thread: PhantomData,
        };
        let result = H::invoke(instance, &mut context);
        status(context.error)?;
        result?;
        if H::MODE == Mode::Around && !context.called {
            return Err(sys::FAILED_PRECONDITION);
        }
        Ok(())
    })
}
