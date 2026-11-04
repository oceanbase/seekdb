// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Prepared nullable signed-integer filter/project programs.
//!
//! Programs are immutable and shareable. Each execution owns its input staging,
//! selection, intermediate values and per-row evaluated flags in one backing
//! allocation. Native callers can supply a `Vec<u64>`; adapters can supply
//! query-accounted storage implementing `AsRef<[u64]> + AsMut<[u64]>`.

use std::sync::atomic::{AtomicU64, Ordering};

#[cfg(not(test))]
mod bridge;

#[cfg(test)]
mod tests;

pub type NodeId = usize;
pub const MAX_NODES: usize = 256;
pub const MAX_DEPTH: usize = 64;
pub const MAX_ARGUMENTS: usize = 4096;
const INSTRUCTION_WORDS: usize = 5;
static NEXT_PROGRAM_ID: AtomicU64 = AtomicU64::new(1);
pub const EVALUATED: u64 = 1;
pub const VALID: u64 = 2;
const POLL_INTERVAL: usize = 4096;

/// Every node has the signature `Nullable<I64>`. Predicates return 0 or 1.
#[derive(Clone, Debug)]
pub struct Node {
    pub operation: Operation,
}

#[derive(Clone, Debug)]
pub enum Operation {
    Input(usize),
    Constant(Option<i64>),
    Add(NodeId, NodeId),
    Sub(NodeId, NodeId),
    Mul(NodeId, NodeId),
    Div {
        left: NodeId,
        right: NodeId,
        error_on_zero: bool,
    },
    Eq(NodeId, NodeId),
    Ne(NodeId, NodeId),
    Lt(NodeId, NodeId),
    Le(NodeId, NodeId),
    Gt(NodeId, NodeId),
    Ge(NodeId, NodeId),
    NullSafeEq(NodeId, NodeId),
    And(Vec<NodeId>),
    Or(Vec<NodeId>),
    Not(NodeId),
    IsNull(NodeId),
    IsNotNull(NodeId),
    Truth(NodeId),
    Case {
        arms: Vec<(NodeId, NodeId)>,
        otherwise: Option<NodeId>,
    },
    Identity(NodeId),
}

/// The resolved operation vocabulary shared by native preparation and adapters.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum Opcode {
    #[default]
    Input = 0,
    Constant = 1,
    Add = 2,
    Sub = 3,
    Mul = 4,
    Div = 5,
    Eq = 6,
    Ne = 7,
    Lt = 8,
    Le = 9,
    Gt = 10,
    Ge = 11,
    NullSafeEq = 12,
    And = 13,
    Or = 14,
    Not = 15,
    IsNull = 16,
    IsNotNull = 17,
    Case = 18,
    Identity = 19,
    Truth = 20,
}

impl TryFrom<u8> for Opcode {
    type Error = EvalError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        Ok(match value {
            0 => Self::Input,
            1 => Self::Constant,
            2 => Self::Add,
            3 => Self::Sub,
            4 => Self::Mul,
            5 => Self::Div,
            6 => Self::Eq,
            7 => Self::Ne,
            8 => Self::Lt,
            9 => Self::Le,
            10 => Self::Gt,
            11 => Self::Ge,
            12 => Self::NullSafeEq,
            13 => Self::And,
            14 => Self::Or,
            15 => Self::Not,
            16 => Self::IsNull,
            17 => Self::IsNotNull,
            18 => Self::Case,
            19 => Self::Identity,
            20 => Self::Truth,
            _ => return Err(invalid_program(0)),
        })
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Instruction {
    pub opcode: Opcode,
    pub input: u32,
    pub argument_start: u32,
    pub argument_count: u32,
    pub value: i64,
    pub is_null: bool,
    pub error_on_zero: bool,
}

#[derive(Clone, Copy)]
pub struct ProgramSpec<'a> {
    pub instructions: &'a [Instruction],
    pub arguments: &'a [u32],
    pub filters: &'a [u32],
    pub projections: &'a [u32],
    pub inputs: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ErrorKind {
    InvalidProgram,
    InvalidBatch,
    Memory,
    Overflow,
    DivisionByZero,
    Cancelled,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EvalError {
    pub kind: ErrorKind,
    pub node: NodeId,
    pub row: usize,
    pub left: Option<i64>,
    pub right: Option<i64>,
    pub host_code: i32,
}

impl EvalError {
    fn new(kind: ErrorKind, node: NodeId, row: usize) -> Self {
        Self {
            kind,
            node,
            row,
            left: None,
            right: None,
            host_code: 0,
        }
    }

    fn arithmetic(kind: ErrorKind, node: NodeId, row: usize, left: i64, right: i64) -> Self {
        Self {
            left: Some(left),
            right: Some(right),
            ..Self::new(kind, node, row)
        }
    }
}

/// Immutable compiled instructions and roots in caller-owned storage.
///
/// Native callers may share a `&Program` or put the program in their own `Arc`.
/// No allocator or reference-counting policy is imposed on adapters.
pub struct Program<B = Vec<u64>> {
    backing: B,
    id: u64,
    nodes: usize,
    inputs: usize,
    arguments_start: usize,
    filters_start: usize,
    projections_start: usize,
    end: usize,
}

fn invalid_program(node: NodeId) -> EvalError {
    EvalError::new(ErrorKind::InvalidProgram, node, 0)
}

/// Validates the whole program before any caller-owned storage is modified.
pub fn program_words(spec: &ProgramSpec<'_>) -> Result<usize, EvalError> {
    if spec.instructions.is_empty()
        || spec.instructions.len() > MAX_NODES
        || spec.inputs as usize > MAX_NODES
        || spec.arguments.len() > MAX_ARGUMENTS
        || spec.filters.len() > MAX_NODES
        || spec.projections.len() > MAX_NODES
    {
        return Err(invalid_program(0));
    }
    let mut depths = [0usize; MAX_NODES];
    for (id, instruction) in spec.instructions.iter().enumerate() {
        let start = instruction.argument_start as usize;
        let count = instruction.argument_count as usize;
        let end = start
            .checked_add(count)
            .ok_or_else(|| invalid_program(id))?;
        let arguments = spec
            .arguments
            .get(start..end)
            .ok_or_else(|| invalid_program(id))?;
        use Opcode::*;
        let valid_arity = match instruction.opcode {
            Input | Constant => count == 0,
            Add | Sub | Mul | Div | Eq | Ne | Lt | Le | Gt | Ge | NullSafeEq => count == 2,
            Not | IsNull | IsNotNull | Identity | Truth => count == 1,
            And | Or => count > 0 && count <= MAX_NODES,
            Case => (2..=MAX_NODES * 2 + 1).contains(&count),
        };
        if !valid_arity || (instruction.opcode == Input && instruction.input >= spec.inputs) {
            return Err(invalid_program(id));
        }
        let mut depth = 1;
        for &reference in arguments {
            if reference as usize >= id {
                return Err(invalid_program(id));
            }
            depth = depth.max(depths[reference as usize] + 1);
        }
        if depth > MAX_DEPTH {
            return Err(invalid_program(id));
        }
        depths[id] = depth;
    }
    for &root in spec.filters.iter().chain(spec.projections) {
        if root as usize >= spec.instructions.len() {
            return Err(invalid_program(root as usize));
        }
    }
    Ok(spec.instructions.len() * INSTRUCTION_WORDS
        + spec.arguments.len()
        + spec.filters.len()
        + spec.projections.len())
}

impl<B: AsRef<[u64]> + AsMut<[u64]>> Program<B> {
    pub fn from_instructions_in(spec: ProgramSpec<'_>, mut backing: B) -> Result<Self, EvalError> {
        let words = program_words(&spec)?;
        if backing.as_ref().len() < words {
            return Err(EvalError::new(ErrorKind::Memory, 0, 0));
        }
        let id = NEXT_PROGRAM_ID
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
            .map_err(|_| EvalError::new(ErrorKind::Memory, 0, 0))?;
        let arguments_start = spec.instructions.len() * INSTRUCTION_WORDS;
        let filters_start = arguments_start + spec.arguments.len();
        let projections_start = filters_start + spec.filters.len();
        let data = backing.as_mut();
        for (id, instruction) in spec.instructions.iter().enumerate() {
            let start = id * INSTRUCTION_WORDS;
            data[start] = u64::from(instruction.opcode as u8)
                | (u64::from(instruction.is_null) << 8)
                | (u64::from(instruction.error_on_zero) << 9);
            data[start + 1] = u64::from(instruction.input);
            data[start + 2] = u64::from(instruction.argument_start);
            data[start + 3] = u64::from(instruction.argument_count);
            data[start + 4] = instruction.value as u64;
        }
        for (destination, &source) in data[arguments_start..words].iter_mut().zip(
            spec.arguments
                .iter()
                .chain(spec.filters)
                .chain(spec.projections),
        ) {
            *destination = u64::from(source);
        }
        Ok(Self {
            backing,
            id,
            nodes: spec.instructions.len(),
            inputs: spec.inputs as usize,
            arguments_start,
            filters_start,
            projections_start,
            end: words,
        })
    }
}

impl<B: AsRef<[u64]>> Program<B> {
    pub fn node_count(&self) -> usize {
        self.nodes
    }
    pub fn input_count(&self) -> usize {
        self.inputs
    }
    pub fn filters(&self) -> &[u64] {
        &self.backing.as_ref()[self.filters_start..self.projections_start]
    }
    pub fn projections(&self) -> &[u64] {
        &self.backing.as_ref()[self.projections_start..self.end]
    }
    pub fn storage_words(&self) -> usize {
        self.end
    }

    fn instruction(&self, node: NodeId) -> InstructionView<'_> {
        let data = self.backing.as_ref();
        let start = node * INSTRUCTION_WORDS;
        let bits = data[start];
        let opcode = Opcode::try_from((bits & 0xff) as u8)
            .expect("prepared instruction opcode must remain valid");
        let arguments = self.arguments_start + data[start + 2] as usize;
        let count = data[start + 3] as usize;
        InstructionView {
            opcode,
            input: data[start + 1] as usize,
            arguments: &data[arguments..arguments + count],
            value: (bits & (1 << 8) == 0).then_some(data[start + 4] as i64),
            error_on_zero: bits & (1 << 9) != 0,
        }
    }
}

struct InstructionView<'a> {
    opcode: Opcode,
    input: usize,
    arguments: &'a [u64],
    value: Option<i64>,
    error_on_zero: bool,
}

impl Program<Vec<u64>> {
    /// Convenience preparation for native callers using Rust-owned storage.
    pub fn new(
        nodes: Vec<Node>,
        filters: Vec<NodeId>,
        projections: Vec<NodeId>,
        inputs: usize,
    ) -> Result<Self, EvalError> {
        if nodes.is_empty()
            || nodes.len() > MAX_NODES
            || filters.len() > MAX_NODES
            || projections.len() > MAX_NODES
            || inputs > MAX_NODES
        {
            return Err(invalid_program(0));
        }
        let mut instructions = [Instruction::default(); MAX_NODES];
        let mut arguments = [0u32; MAX_ARGUMENTS];
        let mut argument_count = 0;
        for (id, node) in nodes.iter().enumerate() {
            let start = argument_count;
            let mut push = |reference: NodeId| -> Result<(), EvalError> {
                let destination = arguments
                    .get_mut(argument_count)
                    .ok_or_else(|| invalid_program(id))?;
                *destination = u32::try_from(reference).map_err(|_| invalid_program(id))?;
                argument_count += 1;
                Ok(())
            };
            let instruction = &mut instructions[id];
            instruction.argument_start = start as u32;
            use Operation::*;
            instruction.opcode = match &node.operation {
                Input(index) => {
                    instruction.input = u32::try_from(*index).map_err(|_| invalid_program(id))?;
                    Opcode::Input
                }
                Constant(value) => {
                    instruction.value = value.unwrap_or(0);
                    instruction.is_null = value.is_none();
                    Opcode::Constant
                }
                Add(a, b)
                | Sub(a, b)
                | Mul(a, b)
                | Eq(a, b)
                | Ne(a, b)
                | Lt(a, b)
                | Le(a, b)
                | Gt(a, b)
                | Ge(a, b)
                | NullSafeEq(a, b) => {
                    push(*a)?;
                    push(*b)?;
                    match &node.operation {
                        Add(_, _) => Opcode::Add,
                        Sub(_, _) => Opcode::Sub,
                        Mul(_, _) => Opcode::Mul,
                        Eq(_, _) => Opcode::Eq,
                        Ne(_, _) => Opcode::Ne,
                        Lt(_, _) => Opcode::Lt,
                        Le(_, _) => Opcode::Le,
                        Gt(_, _) => Opcode::Gt,
                        Ge(_, _) => Opcode::Ge,
                        NullSafeEq(_, _) => Opcode::NullSafeEq,
                        _ => unreachable!(),
                    }
                }
                Div {
                    left,
                    right,
                    error_on_zero,
                } => {
                    push(*left)?;
                    push(*right)?;
                    instruction.error_on_zero = *error_on_zero;
                    Opcode::Div
                }
                And(children) | Or(children) => {
                    for &reference in children {
                        push(reference)?;
                    }
                    if matches!(node.operation, And(_)) {
                        Opcode::And
                    } else {
                        Opcode::Or
                    }
                }
                Not(a) | IsNull(a) | IsNotNull(a) | Identity(a) | Truth(a) => {
                    push(*a)?;
                    match node.operation {
                        Not(_) => Opcode::Not,
                        IsNull(_) => Opcode::IsNull,
                        IsNotNull(_) => Opcode::IsNotNull,
                        Identity(_) => Opcode::Identity,
                        Truth(_) => Opcode::Truth,
                        _ => unreachable!(),
                    }
                }
                Case { arms, otherwise } => {
                    if arms.is_empty() {
                        return Err(invalid_program(id));
                    }
                    for &(condition, value) in arms {
                        push(condition)?;
                        push(value)?;
                    }
                    if let Some(reference) = otherwise {
                        push(*reference)?;
                    }
                    Opcode::Case
                }
            };
            instruction.argument_count = (argument_count - start) as u32;
        }
        let mut filter_roots = [0u32; MAX_NODES];
        let mut projection_roots = [0u32; MAX_NODES];
        for (destination, &source) in filter_roots.iter_mut().zip(&filters) {
            *destination = u32::try_from(source).map_err(|_| invalid_program(source))?;
        }
        for (destination, &source) in projection_roots.iter_mut().zip(&projections) {
            *destination = u32::try_from(source).map_err(|_| invalid_program(source))?;
        }
        let spec = ProgramSpec {
            instructions: &instructions[..nodes.len()],
            arguments: &arguments[..argument_count],
            filters: &filter_roots[..filters.len()],
            projections: &projection_roots[..projections.len()],
            inputs: inputs as u32,
        };
        let words = program_words(&spec)?;
        let mut backing = Vec::new();
        backing
            .try_reserve_exact(words)
            .map_err(|_| EvalError::new(ErrorKind::Memory, 0, 0))?;
        backing.resize(words, 0);
        Self::from_instructions_in(spec, backing)
    }
}

/// Words for two input planes, two node planes and the selection plane.
pub fn required_words<P: AsRef<[u64]>>(
    program: &Program<P>,
    capacity: usize,
) -> Result<usize, EvalError> {
    let planes = (program.input_count() + program.node_count()) * 2 + 1;
    capacity
        .checked_mul(planes)
        .filter(|words| *words <= isize::MAX as usize / std::mem::size_of::<u64>())
        .ok_or_else(|| EvalError::new(ErrorKind::Memory, 0, 0))
}

/// Execution polls at entry and at most every 4096 row/node visits.
pub trait Control {
    fn check(&mut self) -> Result<(), i32>;
}

impl Control for () {
    fn check(&mut self) -> Result<(), i32> {
        Ok(())
    }
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum BatchState {
    Unbound,
    Ready,
    Complete,
    Failed,
}

pub struct Execution<B> {
    program_id: u64,
    nodes: usize,
    inputs: usize,
    backing: B,
    capacity: usize,
    rows: usize,
    state: BatchState,
    visits_until_poll: usize,
}

impl<B: AsRef<[u64]> + AsMut<[u64]>> Execution<B> {
    pub fn new<P: AsRef<[u64]>>(
        program: &Program<P>,
        capacity: usize,
        mut backing: B,
    ) -> Result<Self, EvalError> {
        let words = required_words(program, capacity)?;
        if backing.as_ref().len() < words {
            return Err(EvalError::new(ErrorKind::Memory, 0, 0));
        }
        backing.as_mut()[..words].fill(0);
        Ok(Self {
            program_id: program.id,
            nodes: program.node_count(),
            inputs: program.input_count(),
            backing,
            capacity,
            rows: 0,
            state: BatchState::Unbound,
            visits_until_poll: POLL_INTERVAL,
        })
    }

    /// Column-major slots, with `capacity` words per input. Write before begin_batch.
    pub fn input_storage_mut(&mut self) -> &mut [u64] {
        self.state = BatchState::Unbound;
        let end = self.inputs * self.capacity * 2;
        &mut self.backing.as_mut()[..end]
    }

    /// Column-major slots, with `capacity` words per input. Write before begin_batch.
    pub fn input_values_mut(&mut self) -> &mut [u64] {
        self.state = BatchState::Unbound;
        let end = self.inputs * self.capacity;
        &mut self.backing.as_mut()[..end]
    }

    /// Column-major validity slots. Zero means NULL; one means a value is present.
    pub fn input_validity_mut(&mut self) -> &mut [u64] {
        self.state = BatchState::Unbound;
        let start = self.inputs * self.capacity;
        &mut self.backing.as_mut()[start..2 * start]
    }

    pub fn input_count(&self) -> usize {
        self.inputs
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn rows(&self) -> usize {
        self.rows
    }

    /// Invalidates all intermediate cache entries, including constants.
    pub fn begin_batch(&mut self, rows: usize, skip: &[u8]) -> Result<(), EvalError> {
        self.state = BatchState::Unbound;
        if rows > self.capacity || skip.len() != rows {
            return Err(EvalError::new(ErrorKind::InvalidBatch, 0, 0));
        }
        self.rows = rows;
        let flags = self.flags_start();
        let end = flags + self.nodes * self.capacity;
        self.backing.as_mut()[flags..end].fill(0);
        let selection = self.selection_start();
        for (row, &skipped) in skip.iter().enumerate() {
            self.backing.as_mut()[selection + row] = u64::from(skipped != 0);
        }
        self.visits_until_poll = POLL_INTERVAL;
        self.state = BatchState::Ready;
        Ok(())
    }

    /// A failure poisons this batch. The caller must bind a new batch to retry.
    pub fn filter_project<P: AsRef<[u64]>>(
        &mut self,
        program: &Program<P>,
        control: &mut impl Control,
    ) -> Result<(), EvalError> {
        if program.id != self.program_id
            || !matches!(self.state, BatchState::Ready | BatchState::Complete)
        {
            return Err(EvalError::new(ErrorKind::InvalidBatch, 0, 0));
        }
        let result = self.run(program, control);
        self.state = if result.is_ok() {
            BatchState::Complete
        } else {
            BatchState::Failed
        };
        result
    }

    pub fn selection(&self) -> &[u64] {
        let start = self.selection_start();
        &self.backing.as_ref()[start..start + self.rows]
    }

    /// A node's result is usable exactly where its flags contain EVALUATED.
    /// Negative signed values are stored as their u64 bit patterns.
    pub fn node_values(&self, node: NodeId) -> &[u64] {
        assert!(node < self.nodes);
        let start = self.values_start() + node * self.capacity;
        &self.backing.as_ref()[start..start + self.rows]
    }

    pub fn node_flags(&self, node: NodeId) -> &[u64] {
        assert!(node < self.nodes);
        let start = self.flags_start() + node * self.capacity;
        &self.backing.as_ref()[start..start + self.rows]
    }

    fn values_start(&self) -> usize {
        self.inputs * self.capacity * 2
    }

    fn flags_start(&self) -> usize {
        self.values_start() + self.nodes * self.capacity
    }

    fn selection_start(&self) -> usize {
        self.flags_start() + self.nodes * self.capacity
    }

    fn poll(
        &mut self,
        control: &mut impl Control,
        node: NodeId,
        row: usize,
    ) -> Result<(), EvalError> {
        self.visits_until_poll -= 1;
        if self.visits_until_poll == 0 {
            self.check(control, node, row)?;
        }
        Ok(())
    }

    fn check(
        &mut self,
        control: &mut impl Control,
        node: NodeId,
        row: usize,
    ) -> Result<(), EvalError> {
        self.visits_until_poll = POLL_INTERVAL;
        control.check().map_err(|host_code| EvalError {
            host_code,
            ..EvalError::new(ErrorKind::Cancelled, node, row)
        })
    }

    fn run<P: AsRef<[u64]>>(
        &mut self,
        program: &Program<P>,
        control: &mut impl Control,
    ) -> Result<(), EvalError> {
        self.check(control, 0, 0)?;
        let selection = self.selection_start();
        for &filter in program.filters() {
            let filter = filter as NodeId;
            for row in 0..self.rows {
                self.poll(control, filter, row)?;
                if self.backing.as_ref()[selection + row] == 0
                    && !self
                        .eval(program, filter, row, control)?
                        .is_some_and(|v| v != 0)
                {
                    self.backing.as_mut()[selection + row] = 1;
                }
            }
        }
        for &projection in program.projections() {
            let projection = projection as NodeId;
            for row in 0..self.rows {
                self.poll(control, projection, row)?;
                if self.backing.as_ref()[selection + row] == 0 {
                    self.eval(program, projection, row, control)?;
                }
            }
        }
        Ok(())
    }

    fn eval<P: AsRef<[u64]>>(
        &mut self,
        program: &Program<P>,
        node: NodeId,
        row: usize,
        control: &mut impl Control,
    ) -> Result<Option<i64>, EvalError> {
        self.poll(control, node, row)?;
        let offset = node * self.capacity + row;
        let value_slot = self.values_start() + offset;
        let flag_slot = self.flags_start() + offset;
        let flags = self.backing.as_ref()[flag_slot];
        if flags & EVALUATED != 0 {
            return Ok((flags & VALID != 0).then(|| self.backing.as_ref()[value_slot] as i64));
        }
        let instruction = program.instruction(node);
        let arguments = instruction.arguments;
        use Opcode::*;
        let value = match instruction.opcode {
            Input => {
                let slot = instruction.input * self.capacity + row;
                let validity = self.inputs * self.capacity + slot;
                (self.backing.as_ref()[validity] != 0).then(|| self.backing.as_ref()[slot] as i64)
            }
            Constant => instruction.value,
            Identity => self.eval(program, arguments[0] as NodeId, row, control)?,
            Not => self
                .eval(program, arguments[0] as NodeId, row, control)?
                .map(|v| i64::from(v == 0)),
            Truth => self
                .eval(program, arguments[0] as NodeId, row, control)?
                .map(|v| i64::from(v != 0)),
            IsNull => Some(i64::from(
                self.eval(program, arguments[0] as NodeId, row, control)?
                    .is_none(),
            )),
            IsNotNull => Some(i64::from(
                self.eval(program, arguments[0] as NodeId, row, control)?
                    .is_some(),
            )),
            And | Or => {
                let conjunction = instruction.opcode == And;
                let mut any_null = false;
                let mut decided = None;
                for &child in arguments {
                    match self.eval(program, child as NodeId, row, control)? {
                        None => any_null = true,
                        Some(value) if (value == 0) == conjunction => {
                            decided = Some(i64::from(!conjunction));
                            break;
                        }
                        Some(_) => {}
                    }
                }
                decided.or_else(|| (!any_null).then_some(i64::from(conjunction)))
            }
            Case => {
                let mut selected = if arguments.len() % 2 == 1 {
                    arguments.last().copied()
                } else {
                    None
                };
                for arm in arguments.as_chunks::<2>().0 {
                    if self
                        .eval(program, arm[0] as NodeId, row, control)?
                        .is_some_and(|v| v != 0)
                    {
                        selected = Some(arm[1]);
                        break;
                    }
                }
                match selected {
                    Some(branch) => self.eval(program, branch as NodeId, row, control)?,
                    None => None,
                }
            }
            opcode @ (Add | Sub | Mul | Div | Eq | Ne | Lt | Le | Gt | Ge | NullSafeEq) => {
                let lhs = self.eval(program, arguments[0] as NodeId, row, control)?;
                let null_short_circuit = matches!(opcode, Eq | Ne | Lt | Le | Gt | Ge);
                let rhs = if lhs.is_none() && null_short_circuit {
                    None
                } else {
                    self.eval(program, arguments[1] as NodeId, row, control)?
                };
                if opcode == NullSafeEq {
                    Some(i64::from(lhs == rhs))
                } else if let (Some(lhs), Some(rhs)) = (lhs, rhs) {
                    let checked = match opcode {
                        Add => lhs.checked_add(rhs),
                        Sub => lhs.checked_sub(rhs),
                        Mul => lhs.checked_mul(rhs),
                        Div => {
                            if rhs == 0 {
                                if instruction.error_on_zero {
                                    return Err(EvalError::arithmetic(
                                        ErrorKind::DivisionByZero,
                                        node,
                                        row,
                                        lhs,
                                        rhs,
                                    ));
                                }
                                self.backing.as_mut()[flag_slot] = EVALUATED;
                                return Ok(None);
                            }
                            lhs.checked_div(rhs)
                        }
                        Eq => Some(i64::from(lhs == rhs)),
                        Ne => Some(i64::from(lhs != rhs)),
                        Lt => Some(i64::from(lhs < rhs)),
                        Le => Some(i64::from(lhs <= rhs)),
                        Gt => Some(i64::from(lhs > rhs)),
                        Ge => Some(i64::from(lhs >= rhs)),
                        _ => unreachable!(),
                    };
                    Some(checked.ok_or_else(|| {
                        EvalError::arithmetic(ErrorKind::Overflow, node, row, lhs, rhs)
                    })?)
                } else {
                    None
                }
            }
        };
        self.backing.as_mut()[value_slot] = value.unwrap_or(0) as u64;
        self.backing.as_mut()[flag_slot] = EVALUATED | if value.is_some() { VALID } else { 0 };
        Ok(value)
    }
}
