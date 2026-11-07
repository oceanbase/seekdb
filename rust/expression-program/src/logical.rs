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

//! Fused three-valued logical programs with lazy, masked external inputs.

use crate::{Control, ErrorKind, EvalError, EVALUATED, VALID};

/// Availability is known, but its datum must only be read if demanded.
pub const MATERIALIZED_PENDING: u64 = 4;

const VERSION: u64 = 1;
const HEADER: usize = 3;
const WIDTH: usize = 3;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Opcode {
    Input = 0,
    And = 13,
    Or = 14,
    Not = 15,
}

#[derive(Clone, Copy, Debug)]
pub struct Instruction {
    pub opcode: Opcode,
    pub argument_start: u32,
    pub argument_count: u32,
}

#[derive(Clone, Copy)]
pub struct ProgramSpec<'a> {
    pub instructions: &'a [Instruction],
    pub arguments: &'a [u32],
}

fn invalid(node: usize) -> EvalError {
    EvalError::new(ErrorKind::InvalidProgram, node, 0)
}
fn memory() -> EvalError {
    EvalError::new(ErrorKind::Memory, 0, 0)
}

fn words(nodes: usize, arguments: usize) -> Result<usize, EvalError> {
    if nodes == 0 || nodes > u32::MAX as usize || arguments > u32::MAX as usize {
        return Err(invalid(0));
    }
    nodes
        .checked_mul(WIDTH)
        .and_then(|n| n.checked_add(HEADER))
        .and_then(|n| n.checked_add(arguments))
        .filter(|&n| n <= isize::MAX as usize / 8)
        .ok_or_else(memory)
}

fn validate_instruction<T: Copy + Into<u64>>(
    instruction: Instruction,
    id: usize,
    arguments: &[T],
) -> Result<(), EvalError> {
    let start = instruction.argument_start as usize;
    let count = instruction.argument_count as usize;
    let end = start.checked_add(count).ok_or_else(|| invalid(id))?;
    let refs = arguments.get(start..end).ok_or_else(|| invalid(id))?;
    if !match instruction.opcode {
        Opcode::Input => count == 0,
        Opcode::And | Opcode::Or => count >= 2,
        Opcode::Not => count == 1,
    } || refs.iter().any(|&child| child.into() >= id as u64)
    {
        return Err(invalid(id));
    }
    Ok(())
}

fn decode(source: &[u64], id: usize) -> Result<Instruction, EvalError> {
    let opcode = match source[0] {
        0 => Opcode::Input,
        13 => Opcode::And,
        14 => Opcode::Or,
        15 => Opcode::Not,
        _ => return Err(invalid(id)),
    };
    Ok(Instruction {
        opcode,
        argument_start: u32::try_from(source[1]).map_err(|_| invalid(id))?,
        argument_count: u32::try_from(source[2]).map_err(|_| invalid(id))?,
    })
}

/// Immutable pointer-free program, with its final node as the demanded root.
pub struct ProgramImage<B> {
    backing: B,
    nodes: usize,
    arguments: usize,
}

impl<B: AsRef<[u64]>> ProgramImage<B> {
    pub fn parse(backing: B) -> Result<Self, EvalError> {
        let data = backing.as_ref();
        let header = data.get(..HEADER).ok_or_else(|| invalid(0))?;
        if header[0] != VERSION {
            return Err(invalid(0));
        }
        let nodes = usize::try_from(header[1]).map_err(|_| invalid(0))?;
        let arguments = usize::try_from(header[2]).map_err(|_| invalid(0))?;
        if data.len() != words(nodes, arguments)? {
            return Err(invalid(0));
        }
        let refs = &data[HEADER + nodes * WIDTH..];
        if refs.iter().any(|&child| child > u32::MAX as u64) {
            return Err(invalid(0));
        }
        for id in 0..nodes {
            validate_instruction(decode(&data[HEADER + id * WIDTH..][..WIDTH], id)?, id, refs)?;
        }
        Ok(Self {
            backing,
            nodes,
            arguments,
        })
    }
    pub fn words(&self) -> &[u64] {
        self.backing.as_ref()
    }
    pub fn node_count(&self) -> usize {
        self.nodes
    }
    fn instruction(&self, node: usize) -> Instruction {
        decode(&self.words()[HEADER + node * WIDTH..][..WIDTH], node)
            .expect("validated immutable logical image")
    }
    fn child(&self, instruction: Instruction, argument: usize) -> usize {
        self.words()[HEADER + self.nodes * WIDTH + instruction.argument_start as usize + argument]
            as usize
    }
    pub fn argument_count(&self) -> usize {
        self.arguments
    }
}

pub(crate) fn image_words_iter<I>(instructions: I, arguments: &[u32]) -> Result<usize, EvalError>
where
    I: ExactSizeIterator<Item = Result<Instruction, EvalError>>,
{
    let count = instructions.len();
    let size = words(count, arguments.len())?;
    for (id, instruction) in instructions.enumerate() {
        let instruction = instruction?;
        validate_instruction(instruction, id, arguments)?;
    }
    Ok(size)
}

pub fn image_words(spec: ProgramSpec<'_>) -> Result<usize, EvalError> {
    image_words_iter(spec.instructions.iter().copied().map(Ok), spec.arguments)
}

pub(crate) fn compile_iter<B, I>(
    instructions: I,
    arguments: &[u32],
    mut backing: B,
) -> Result<ProgramImage<B>, EvalError>
where
    B: AsRef<[u64]> + AsMut<[u64]>,
    I: ExactSizeIterator<Item = Result<Instruction, EvalError>> + Clone,
{
    let size = image_words_iter(instructions.clone(), arguments)?;
    if backing.as_ref().len() != size {
        return Err(memory());
    }
    let nodes = instructions.len();
    let data = backing.as_mut();
    data[..HEADER].copy_from_slice(&[VERSION, nodes as u64, arguments.len() as u64]);
    for (id, instruction) in instructions.enumerate() {
        let instruction = instruction?;
        data[HEADER + id * WIDTH..][..WIDTH].copy_from_slice(&[
            instruction.opcode as u64,
            instruction.argument_start as u64,
            instruction.argument_count as u64,
        ]);
    }
    for (target, &source) in data[HEADER + nodes * WIDTH..].iter_mut().zip(arguments) {
        *target = source as u64;
    }
    Ok(ProgramImage {
        backing,
        nodes,
        arguments: arguments.len(),
    })
}

/// Validates before modifying exact caller-provided image storage.
pub fn compile_in<B: AsRef<[u64]> + AsMut<[u64]>>(
    spec: ProgramSpec<'_>,
    backing: B,
) -> Result<ProgramImage<B>, EvalError> {
    compile_iter(
        spec.instructions.iter().copied().map(Ok),
        spec.arguments,
        backing,
    )
}

/// Supplies only demanded external values. Errors are returned unchanged.
/// A successful callback marks every demanded row EVALUATED; VALID denotes non-NULL.
pub trait InputProvider: Control {
    fn demand(
        &mut self,
        node: usize,
        demand: &[u64],
        values: &mut [u64],
        flags: &mut [u64],
    ) -> Result<(), i32>;
}

pub fn required_words<B: AsRef<[u64]>>(
    program: &ProgramImage<B>,
    rows: usize,
) -> Result<usize, EvalError> {
    program
        .nodes
        .checked_mul(rows)
        .and_then(|n| n.checked_mul(3))
        .and_then(|n| n.checked_add(rows))
        .and_then(|n| {
            program
                .nodes
                .checked_mul(2)
                .and_then(|stack| n.checked_add(stack))
        })
        .filter(|&n| n <= isize::MAX as usize / 8)
        .ok_or_else(memory)
}

/// One synchronous demand scope. Cached snapshots may be imported before execution.
/// Mutable values, validity/evaluated flags and demand masks belong to this scope.
pub struct Execution<'a, P, B> {
    program: &'a ProgramImage<P>,
    backing: B,
    rows: usize,
    complete: bool,
    has_run: bool,
}

impl<'a, P: AsRef<[u64]>, B: AsRef<[u64]> + AsMut<[u64]>> Execution<'a, P, B> {
    pub fn new(program: &'a ProgramImage<P>, rows: usize, backing: B) -> Result<Self, EvalError> {
        if backing.as_ref().len() < required_words(program, rows)? {
            return Err(memory());
        }
        Ok(Self {
            program,
            backing,
            rows,
            complete: false,
            has_run: false,
        })
    }
    fn invalidate_inputs(&mut self) {
        self.complete = false;
        if self.has_run {
            let plane = self.program.nodes * self.rows;
            self.backing.as_mut()[plane..plane * 2].fill(0);
            self.has_run = false;
        }
    }
    pub fn values_mut(&mut self) -> &mut [u64] {
        self.invalidate_inputs();
        &mut self.backing.as_mut()[..self.program.nodes * self.rows]
    }
    pub fn flags_mut(&mut self) -> &mut [u64] {
        self.invalidate_inputs();
        let plane = self.program.nodes * self.rows;
        &mut self.backing.as_mut()[plane..plane * 2]
    }
    pub fn root_values(&self) -> &[u64] {
        if !self.complete {
            return &[];
        }
        let start = (self.program.nodes - 1) * self.rows;
        &self.backing.as_ref()[start..start + self.rows]
    }
    pub fn root_flags(&self) -> &[u64] {
        if !self.complete {
            return &[];
        }
        let start = (self.program.nodes * 2 - 1) * self.rows;
        &self.backing.as_ref()[start..start + self.rows]
    }
    pub fn execute(
        &mut self,
        skip: &[u8],
        provider: &mut impl InputProvider,
    ) -> Result<(), EvalError> {
        self.complete = false;
        let result = self.run(skip, provider);
        self.has_run = true;
        self.complete = result.is_ok();
        result
    }
    fn run(&mut self, skip: &[u8], provider: &mut impl InputProvider) -> Result<(), EvalError> {
        let rows = self.rows;
        if skip.len() != rows {
            return Err(EvalError::new(ErrorKind::InvalidBatch, 0, 0));
        }
        let nodes = self.program.nodes;
        let plane = nodes * rows;
        let demand_base = plane * 2;
        let scratch_base = plane * 3;
        let stack_base = scratch_base + rows;
        let root = nodes - 1;
        self.backing.as_mut()[demand_base..stack_base + nodes * 2].fill(0);
        let mut any = false;
        for (row, &skip) in skip.iter().enumerate() {
            let needed =
                skip == 0 && self.backing.as_ref()[plane + root * rows + row] & EVALUATED == 0;
            self.backing.as_mut()[demand_base + root * rows + row] = u64::from(needed);
            any |= needed;
        }
        if !any {
            return Ok(());
        }
        provider.check().map_err(|host_code| EvalError {
            host_code,
            ..EvalError::new(ErrorKind::Cancelled, root, 0)
        })?;
        self.backing.as_mut()[stack_base] = root as u64;
        let mut depth = 1;
        let mut visits = 0usize;
        while depth > 0 {
            let stack = stack_base + (depth - 1) * 2;
            let node = self.backing.as_ref()[stack] as usize;
            let next = self.backing.as_ref()[stack + 1] as usize;
            let instruction = self.program.instruction(node);
            visits += 1;
            if visits >= 4096 {
                provider.check().map_err(|host_code| EvalError {
                    host_code,
                    ..EvalError::new(ErrorKind::Cancelled, node, 0)
                })?;
                visits = 0;
            }
            if next == 0 && instruction.opcode != Opcode::Input {
                let mut pending = false;
                for row in 0..rows {
                    poll(&mut visits, provider, node, row)?;
                    let needed = self.backing.as_ref()[demand_base + node * rows + row] != 0
                        && self.backing.as_ref()[plane + node * rows + row] & MATERIALIZED_PENDING
                            != 0;
                    self.backing.as_mut()[scratch_base + row] = u64::from(needed);
                    pending |= needed;
                }
                if pending {
                    let (values, tail) = self.backing.as_mut().split_at_mut(plane);
                    let (flags, tail) = tail.split_at_mut(plane);
                    let demand = &tail[plane..][..rows];
                    provider
                        .demand(
                            node,
                            demand,
                            &mut values[node * rows..][..rows],
                            &mut flags[node * rows..][..rows],
                        )
                        .map_err(|host_code| EvalError {
                            host_code,
                            ..EvalError::new(ErrorKind::InvalidBatch, node, 0)
                        })?;
                    for row in 0..rows {
                        if demand[row] != 0 && flags[node * rows + row] & EVALUATED == 0 {
                            return Err(EvalError::new(ErrorKind::InvalidBatch, node, row));
                        }
                    }
                    let mut any = false;
                    for row in 0..rows {
                        if self.backing.as_ref()[plane + node * rows + row] & EVALUATED != 0 {
                            self.backing.as_mut()[demand_base + node * rows + row] = 0;
                        }
                        any |= self.backing.as_ref()[demand_base + node * rows + row] != 0;
                    }
                    if !any {
                        depth -= 1;
                        continue;
                    }
                }
            }
            if instruction.opcode == Opcode::Input {
                let (values, tail) = self.backing.as_mut().split_at_mut(plane);
                let (flags, tail) = tail.split_at_mut(plane);
                let demand = &tail[node * rows..][..rows];
                provider
                    .demand(
                        node,
                        demand,
                        &mut values[node * rows..][..rows],
                        &mut flags[node * rows..][..rows],
                    )
                    .map_err(|host_code| EvalError {
                        host_code,
                        ..EvalError::new(ErrorKind::InvalidBatch, node, 0)
                    })?;
                for row in 0..rows {
                    poll(&mut visits, provider, node, row)?;
                    if demand[row] != 0 && flags[node * rows + row] & EVALUATED == 0 {
                        return Err(EvalError::new(ErrorKind::InvalidBatch, node, row));
                    }
                }
                depth -= 1;
                continue;
            }
            if next == 0 {
                for row in 0..rows {
                    poll(&mut visits, provider, node, row)?;
                    if self.backing.as_ref()[demand_base + node * rows + row] != 0 {
                        self.backing.as_mut()[node * rows + row] =
                            u64::from(instruction.opcode == Opcode::And);
                        self.backing.as_mut()[plane + node * rows + row] = VALID;
                    }
                }
            } else {
                let child = self.program.child(instruction, next - 1);
                for row in 0..rows {
                    poll(&mut visits, provider, node, row)?;
                    if self.backing.as_ref()[demand_base + node * rows + row] == 0 {
                        continue;
                    }
                    let value = self.backing.as_ref()[node * rows + row];
                    let flags = self.backing.as_ref()[plane + node * rows + row];
                    let decisive = flags & VALID != 0
                        && match instruction.opcode {
                            Opcode::And => value == 0,
                            Opcode::Or => value != 0,
                            _ => false,
                        };
                    if decisive {
                        continue;
                    }
                    let child_flags = self.backing.as_ref()[plane + child * rows + row];
                    if child_flags & VALID == 0 {
                        self.backing.as_mut()[plane + node * rows + row] &= !VALID;
                    } else {
                        let truth = self.backing.as_ref()[child * rows + row] != 0;
                        let decisive = match instruction.opcode {
                            Opcode::And => !truth,
                            Opcode::Or => truth,
                            _ => true,
                        };
                        if decisive {
                            self.backing.as_mut()[plane + node * rows + row] |= VALID;
                            self.backing.as_mut()[node * rows + row] =
                                u64::from(match instruction.opcode {
                                    Opcode::Not => !truth,
                                    _ => truth,
                                });
                        }
                    }
                }
            }
            if next == instruction.argument_count as usize {
                for row in 0..rows {
                    poll(&mut visits, provider, node, row)?;
                    if self.backing.as_ref()[demand_base + node * rows + row] != 0 {
                        self.backing.as_mut()[plane + node * rows + row] |= EVALUATED;
                    }
                }
                depth -= 1;
                continue;
            }
            let child = self.program.child(instruction, next);
            self.backing.as_mut()[stack + 1] = (next + 1) as u64;
            let mut any = false;
            for row in 0..rows {
                poll(&mut visits, provider, node, row)?;
                let value = self.backing.as_ref()[node * rows + row];
                let flags = self.backing.as_ref()[plane + node * rows + row];
                let decisive = flags & VALID != 0
                    && match instruction.opcode {
                        Opcode::And => value == 0,
                        Opcode::Or => value != 0,
                        _ => false,
                    };
                let needed = self.backing.as_ref()[demand_base + node * rows + row] != 0
                    && !decisive
                    && self.backing.as_ref()[plane + child * rows + row] & EVALUATED == 0;
                self.backing.as_mut()[demand_base + child * rows + row] = u64::from(needed);
                any |= needed;
            }
            if any {
                self.backing.as_mut()[stack_base + depth * 2] = child as u64;
                self.backing.as_mut()[stack_base + depth * 2 + 1] = 0;
                depth += 1;
            }
        }
        Ok(())
    }
}

fn poll(
    visits: &mut usize,
    control: &mut impl Control,
    node: usize,
    row: usize,
) -> Result<(), EvalError> {
    *visits += 1;
    if *visits >= 4096 {
        *visits = 0;
        control.check().map_err(|host_code| EvalError {
            host_code,
            ..EvalError::new(ErrorKind::Cancelled, node, row)
        })?;
    }
    Ok(())
}
