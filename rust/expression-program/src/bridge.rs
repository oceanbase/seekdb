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

use crate::{
    Control, ErrorKind, Execution, Instruction, Opcode, Program, ProgramImage, ProgramSpec,
    MAX_NODES,
};
use std::{ptr::NonNull, slice};

#[cxx::bridge(namespace = "oceanbase::expression")]
mod ffi {
    struct Instruction {
        opcode: u8,
        input: u32,
        argument_start: u32,
        argument_count: u32,
        value: i64,
        is_null: bool,
        error_on_zero: bool,
    }

    struct LogicalInstruction {
        opcode: u8,
        argument_start: u32,
        argument_count: u32,
    }

    struct RunStatus {
        code: i32,
        node: usize,
        row: usize,
        left: i64,
        right: i64,
    }

    struct ProgramShape {
        nodes: usize,
        inputs: usize,
        filters: usize,
        projections: usize,
        first_projection: usize,
    }

    unsafe extern "C++" {
        include!("rust/expression-program/include/host.h");
        type ILogicalHost;
        unsafe fn demand_logical_input(
            host: *mut ILogicalHost,
            node: usize,
            demand: &[u64],
            values: &mut [u64],
            flags: &mut [u64],
        ) -> i32;
        unsafe fn check_logical_cancel(host: *mut ILogicalHost) -> i32;
        type IExecutionHost;
        unsafe fn allocate_workspace(host: *mut IExecutionHost, words: usize) -> *mut u64;
        unsafe fn release_workspace(host: *mut IExecutionHost, data: *mut u64, words: usize);
        unsafe fn check_cancel(host: *mut IExecutionHost) -> i32;
    }

    extern "Rust" {
        fn logical_image_words(nodes: &[LogicalInstruction], arguments: &[u32]) -> Result<usize>;
        fn compile_logical_image(
            nodes: &[LogicalInstruction],
            arguments: &[u32],
            storage: &mut [u64],
        ) -> Result<()>;
        fn logical_node_count(image: &[u64]) -> Result<usize>;
        fn logical_workspace_words(image: &[u64], rows: usize) -> Result<usize>;
        unsafe fn execute_logical(
            image: &[u64],
            host: *mut ILogicalHost,
            rows: usize,
            skip: &[u8],
            storage: &mut [u64],
        ) -> RunStatus;
        type Prepared;
        type Runner;
        fn image_words(
            instructions: &[Instruction],
            arguments: &[u32],
            filters: &[u32],
            projections: &[u32],
            inputs: u32,
        ) -> Result<usize>;
        fn compile_image(
            instructions: &[Instruction],
            arguments: &[u32],
            filters: &[u32],
            projections: &[u32],
            inputs: u32,
            storage: &mut [u64],
        ) -> Result<()>;
        fn validate_image(image: &[u64]) -> Result<ProgramShape>;
        fn program_shape(program: &Prepared) -> ProgramShape;
        unsafe fn restore(image: &[u64], host: *mut IExecutionHost) -> Result<Box<Prepared>>;
        unsafe fn prepare(
            instructions: &[Instruction],
            arguments: &[u32],
            filters: &[u32],
            projections: &[u32],
            inputs: u32,
            host: *mut IExecutionHost,
        ) -> Result<Box<Prepared>>;
        unsafe fn make_runner(
            program: &Prepared,
            host: *mut IExecutionHost,
            capacity: usize,
        ) -> Result<Box<Runner>>;
        fn reserve_rows(
            runner: &mut Runner,
            program: &Prepared,
            minimum_capacity: usize,
        ) -> Result<usize>;
        fn input_storage(runner: &mut Runner) -> &mut [u64];
        fn execute(runner: &mut Runner, program: &Prepared, rows: usize, skip: &[u8]) -> RunStatus;
        fn selection(runner: &Runner) -> &[u64];
        fn node_values(runner: &Runner, node: usize) -> &[u64];
        fn node_flags(runner: &Runner, node: usize) -> &[u64];
    }
}

pub struct Prepared {
    program: Program<HostBacking>,
}

// SAFETY: preparation finishes all writes before the owner publishes Prepared.
// Shared access reads only immutable program words; it never calls the allocation
// host. The owner keeps the region and host alive through all borrowed
// executions, then drops Prepared after they retire.
// Send is intentionally absent because destruction calls the owning host.
unsafe impl Sync for Prepared {}

pub struct Runner {
    execution: Execution<HostBacking>,
    host: NonNull<ffi::IExecutionHost>,
}

// Prepared and Runner each own one allocator-accounted region. Their hosts
// outlive the owner. Datum frames receive result copies.
struct HostBacking {
    data: NonNull<u64>,
    words: usize,
    host: NonNull<ffi::IExecutionHost>,
}

impl HostBacking {
    unsafe fn allocate(
        host: NonNull<ffi::IExecutionHost>,
        words: usize,
    ) -> Result<Self, &'static str> {
        let bytes = words
            .checked_mul(std::mem::size_of::<u64>())
            .filter(|&bytes| bytes <= isize::MAX as usize)
            .ok_or("workspace overflow")?;
        if bytes == 0 {
            return Err("empty expression region");
        }
        // SAFETY: the host supplies an exclusive, u64-aligned region of words u64s.
        let data = NonNull::new(unsafe { ffi::allocate_workspace(host.as_ptr(), words) })
            .ok_or("expression allocation failed")?;
        // SAFETY: every requested word belongs to this owner and is writable.
        unsafe {
            data.as_ptr().write_bytes(0, words);
        }
        Ok(Self { data, words, host })
    }
}

impl AsRef<[u64]> for HostBacking {
    fn as_ref(&self) -> &[u64] {
        // SAFETY: allocate initializes all words; the owner keeps the region live.
        unsafe { slice::from_raw_parts(self.data.as_ptr(), self.words) }
    }
}

impl AsMut<[u64]> for HostBacking {
    fn as_mut(&mut self) -> &mut [u64] {
        // SAFETY: this owner has exclusive access for the duration of the borrow.
        unsafe { slice::from_raw_parts_mut(self.data.as_ptr(), self.words) }
    }
}

impl Drop for HostBacking {
    fn drop(&mut self) {
        // SAFETY: the adapter drops both owners before their host. The pointer
        // and length match the original allocation and no borrow escapes drop.
        unsafe { ffi::release_workspace(self.host.as_ptr(), self.data.as_ptr(), self.words) }
    }
}

struct HostControl(NonNull<ffi::IExecutionHost>);

impl Control for HostControl {
    fn check(&mut self) -> Result<(), i32> {
        // SAFETY: execute is synchronous and the caller owns the live host.
        let code = unsafe { ffi::check_cancel(self.0.as_ptr()) };
        if code == 0 {
            Ok(())
        } else {
            Err(code)
        }
    }
}

fn native_spec<'a>(
    instructions: &[ffi::Instruction],
    arguments: &'a [u32],
    filters: &'a [u32],
    projections: &'a [u32],
    inputs: u32,
    native: &'a mut [Instruction; MAX_NODES],
) -> Result<ProgramSpec<'a>, &'static str> {
    if instructions.len() > MAX_NODES {
        return Err("expression program exceeds preparation limits");
    }
    for (target, source) in native.iter_mut().zip(instructions) {
        *target = Instruction {
            opcode: Opcode::try_from(source.opcode).map_err(|_| "invalid expression opcode")?,
            input: source.input,
            argument_start: source.argument_start,
            argument_count: source.argument_count,
            value: source.value,
            is_null: source.is_null,
            error_on_zero: source.error_on_zero,
        };
    }
    Ok(ProgramSpec {
        instructions: &native[..instructions.len()],
        arguments,
        filters,
        projections,
        inputs,
    })
}

fn image_words(
    instructions: &[ffi::Instruction],
    arguments: &[u32],
    filters: &[u32],
    projections: &[u32],
    inputs: u32,
) -> Result<usize, &'static str> {
    let mut native = [Instruction::default(); MAX_NODES];
    let spec = native_spec(
        instructions,
        arguments,
        filters,
        projections,
        inputs,
        &mut native,
    )?;
    crate::image_words(&spec).map_err(|_| "invalid expression image specification")
}

fn compile_image(
    instructions: &[ffi::Instruction],
    arguments: &[u32],
    filters: &[u32],
    projections: &[u32],
    inputs: u32,
    storage: &mut [u64],
) -> Result<(), &'static str> {
    let mut native = [Instruction::default(); MAX_NODES];
    let spec = native_spec(
        instructions,
        arguments,
        filters,
        projections,
        inputs,
        &mut native,
    )?;
    let words = crate::image_words(&spec).map_err(|_| "invalid expression image specification")?;
    if storage.len() != words {
        return Err("expression image storage size mismatch");
    }
    crate::compile_image_in(spec, storage).map_err(|_| "invalid expression image")?;
    Ok(())
}

fn validate_image(image: &[u64]) -> Result<ffi::ProgramShape, &'static str> {
    let image = ProgramImage::parse(image).map_err(|_| "invalid expression image")?;
    Ok(ffi::ProgramShape {
        nodes: image.node_count(),
        inputs: image.input_count(),
        filters: image.filters().len(),
        projections: image.projections().len(),
        first_projection: image.projections().first().copied().unwrap_or(0) as usize,
    })
}

fn program_shape(program: &Prepared) -> ffi::ProgramShape {
    ffi::ProgramShape {
        nodes: program.program.node_count(),
        inputs: program.program.input_count(),
        filters: program.program.filters().len(),
        projections: program.program.projections().len(),
        first_projection: program.program.projections().first().copied().unwrap_or(0) as usize,
    }
}

// SAFETY: host owns every allocated region and outlives Prepared and Runner.
unsafe fn restore(
    image: &[u64],
    host: *mut ffi::IExecutionHost,
) -> Result<Box<Prepared>, &'static str> {
    let image = ProgramImage::parse(image).map_err(|_| "invalid expression image")?;
    let host = NonNull::new(host).ok_or("missing execution host")?;
    // SAFETY: the caller keeps host alive until the Prepared owner is dropped.
    let backing = unsafe { HostBacking::allocate(host, image.program_words()) }?;
    let program =
        Program::from_image_in(&image, backing).map_err(|_| "expression restore failed")?;
    try_box(Prepared { program })
}

// SAFETY: host owns every allocated region and outlives Prepared and Runner.
unsafe fn prepare(
    instructions: &[ffi::Instruction],
    arguments: &[u32],
    filters: &[u32],
    projections: &[u32],
    inputs: u32,
    host: *mut ffi::IExecutionHost,
) -> Result<Box<Prepared>, &'static str> {
    let host = NonNull::new(host).ok_or("missing execution host")?;
    let mut native = [Instruction::default(); MAX_NODES];
    let spec = native_spec(
        instructions,
        arguments,
        filters,
        projections,
        inputs,
        &mut native,
    )?;
    let words = crate::program_words(&spec).map_err(|_| "invalid expression program size")?;
    // SAFETY: the caller keeps host alive until the Prepared owner is dropped.
    let backing = unsafe { HostBacking::allocate(host, words) }?;
    let program =
        Program::from_instructions_in(spec, backing).map_err(|_| "invalid expression program")?;
    try_box(Prepared { program })
}

fn try_box<T>(value: T) -> Result<Box<T>, &'static str> {
    // CXX requires Box handles. The two fixed-size handles use the Rust system
    // allocator; graph- and row-dependent memory lives in the host regions.
    let layout = std::alloc::Layout::new::<T>();
    // SAFETY: the only callers use nonzero-sized Prepared and Runner. Allocation
    // is checked before initialization, and Box frees through the same allocator.
    unsafe {
        let pointer = std::alloc::alloc(layout).cast::<T>();
        let pointer = NonNull::new(pointer).ok_or("expression handle allocation failed")?;
        pointer.as_ptr().write(value);
        Ok(Box::from_raw(pointer.as_ptr()))
    }
}

// SAFETY: host outlives the returned Runner and exclusively owns its region.
unsafe fn make_runner(
    program: &Prepared,
    host: *mut ffi::IExecutionHost,
    capacity: usize,
) -> Result<Box<Runner>, &'static str> {
    let host = NonNull::new(host).ok_or("missing execution host")?;
    // SAFETY: the caller keeps host alive through runner destruction.
    let execution = unsafe { allocate_execution(program, host, capacity) }?;
    try_box(Runner { execution, host })
}

// SAFETY: host supplies exclusive storage and outlives the returned execution.
unsafe fn allocate_execution(
    program: &Prepared,
    host: NonNull<ffi::IExecutionHost>,
    capacity: usize,
) -> Result<Execution<HostBacking>, &'static str> {
    let words = crate::required_words(&program.program, capacity)
        .map_err(|_| "invalid expression workspace size")?;
    // SAFETY: the caller keeps host alive until this backing is dropped.
    let backing = unsafe { HostBacking::allocate(host, words) }?;
    Execution::new(&program.program, capacity, backing).map_err(|_| "invalid expression workspace")
}

fn reserve_rows(
    runner: &mut Runner,
    program: &Prepared,
    minimum_capacity: usize,
) -> Result<usize, &'static str> {
    if runner.execution.program_id != program.program.id {
        return Err("expression program does not match runner");
    }
    if minimum_capacity > runner.execution.capacity() {
        // SAFETY: the existing runner owns the live host contract. The exclusive
        // borrow ends all prior buffer borrows before replacement; C++ callers
        // must obey the same rule. Allocation failure leaves the old owner live.
        let execution = unsafe { allocate_execution(program, runner.host, minimum_capacity) }?;
        runner.execution = execution;
    }
    Ok(runner.execution.capacity())
}

fn input_storage(runner: &mut Runner) -> &mut [u64] {
    runner.execution.input_storage_mut()
}

fn execute(runner: &mut Runner, program: &Prepared, rows: usize, skip: &[u8]) -> ffi::RunStatus {
    let result = runner.execution.begin_batch(rows, skip).and_then(|()| {
        runner
            .execution
            .filter_project(&program.program, &mut HostControl(runner.host))
    });
    match result {
        Ok(()) => ffi::RunStatus {
            code: 0,
            node: 0,
            row: 0,
            left: 0,
            right: 0,
        },
        Err(error) => ffi::RunStatus {
            code: match error.kind {
                ErrorKind::Overflow => 1,
                ErrorKind::DivisionByZero => 2,
                ErrorKind::Memory => 3,
                ErrorKind::Cancelled if error.host_code < 0 => error.host_code,
                _ => 4,
            },
            node: error.node,
            row: error.row,
            left: error.left.unwrap_or(0),
            right: error.right.unwrap_or(0),
        },
    }
}

fn selection(runner: &Runner) -> &[u64] {
    runner.execution.selection()
}
fn node_values(runner: &Runner, node: usize) -> &[u64] {
    runner.execution.node_values(node)
}
fn node_flags(runner: &Runner, node: usize) -> &[u64] {
    runner.execution.node_flags(node)
}

fn logical_instruction(
    instruction: &ffi::LogicalInstruction,
) -> Result<crate::logical::Instruction, crate::EvalError> {
    use crate::logical::Opcode;
    let opcode = match instruction.opcode {
        0 => Opcode::Input,
        13 => Opcode::And,
        14 => Opcode::Or,
        15 => Opcode::Not,
        _ => return Err(crate::EvalError::new(ErrorKind::InvalidProgram, 0, 0)),
    };
    Ok(crate::logical::Instruction {
        opcode,
        argument_start: instruction.argument_start,
        argument_count: instruction.argument_count,
    })
}

fn logical_image_words(
    nodes: &[ffi::LogicalInstruction],
    arguments: &[u32],
) -> Result<usize, &'static str> {
    crate::logical::image_words_iter(nodes.iter().map(logical_instruction), arguments)
        .map_err(|_| "invalid logical program")
}

fn compile_logical_image(
    nodes: &[ffi::LogicalInstruction],
    arguments: &[u32],
    storage: &mut [u64],
) -> Result<(), &'static str> {
    crate::logical::compile_iter(nodes.iter().map(logical_instruction), arguments, storage)
        .map(|_| ())
        .map_err(|_| "invalid logical image storage or specification")
}

fn logical_workspace_words(image: &[u64], rows: usize) -> Result<usize, &'static str> {
    let image = crate::logical::ProgramImage::parse(image).map_err(|_| "invalid logical image")?;
    crate::logical::required_words(&image, rows).map_err(|_| "logical workspace overflow")
}

struct LogicalHost(NonNull<ffi::ILogicalHost>);
impl Control for LogicalHost {
    fn check(&mut self) -> Result<(), i32> {
        // SAFETY: the caller retains the host throughout synchronous execution.
        let status = unsafe { ffi::check_logical_cancel(self.0.as_ptr()) };
        if status == 0 {
            Ok(())
        } else {
            Err(status)
        }
    }
}
impl crate::logical::InputProvider for LogicalHost {
    fn demand(
        &mut self,
        node: usize,
        demand: &[u64],
        values: &mut [u64],
        flags: &mut [u64],
    ) -> Result<(), i32> {
        // SAFETY: these disjoint borrowed planes remain live for this callback;
        // the host neither retains them nor reenters this execution scope.
        let status =
            unsafe { ffi::demand_logical_input(self.0.as_ptr(), node, demand, values, flags) };
        if status == 0 {
            Ok(())
        } else {
            Err(status)
        }
    }
}

// SAFETY: host is live until return, does not retain workspace slices, and nested
// callbacks use separate execution objects and backing allocations.
unsafe fn execute_logical(
    image: &[u64],
    host: *mut ffi::ILogicalHost,
    rows: usize,
    skip: &[u8],
    storage: &mut [u64],
) -> ffi::RunStatus {
    let result = (|| {
        let image = crate::logical::ProgramImage::parse(image)?;
        let host = NonNull::new(host)
            .ok_or_else(|| crate::EvalError::new(ErrorKind::InvalidBatch, 0, 0))?;
        crate::logical::Execution::new(&image, rows, storage)?.execute(skip, &mut LogicalHost(host))
    })();
    match result {
        Ok(()) => ffi::RunStatus {
            code: 0,
            node: 0,
            row: 0,
            left: 0,
            right: 0,
        },
        Err(error) => ffi::RunStatus {
            code: if error.host_code != 0 {
                error.host_code
            } else if error.kind == ErrorKind::Memory {
                3
            } else {
                4
            },
            node: error.node,
            row: error.row,
            left: 0,
            right: 0,
        },
    }
}

fn logical_node_count(image: &[u64]) -> Result<usize, &'static str> {
    crate::logical::ProgramImage::parse(image)
        .map(|image| image.node_count())
        .map_err(|_| "invalid logical image")
}
