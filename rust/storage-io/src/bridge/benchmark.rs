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

use super::{ffi, BlockOwner, ServiceHandle};
use crate::calibration::Mode;
use crate::{
    BeginBenchmark, BenchmarkError, BenchmarkPreparation, BenchmarkRun, BenchmarkShape,
    BenchmarkStatus, BenchmarkTarget, CheckedWorkloads, CompletionRequirement, DeviceId, Execution,
    NativeFinalPolicy, OperationPolicy, SamplePolicy, Target, WaitPolicy, Workload,
};
use file_io::bridge::FileRegistry;
use std::num::{NonZeroU64, NonZeroUsize};
use std::time::Duration;

pub struct BenchmarkPreparationSlot(Option<BenchmarkPreparation>);
pub struct BenchmarkRunSlot(Option<BenchmarkRun>);
pub fn new_benchmark_preparation_slot() -> Box<BenchmarkPreparationSlot> {
    Box::new(BenchmarkPreparationSlot(None))
}
pub fn new_benchmark_run_slot() -> Box<BenchmarkRunSlot> {
    Box::new(BenchmarkRunSlot(None))
}
pub fn production_benchmark_plan() -> ffi::BenchmarkPlanDto {
    ffi::BenchmarkPlanDto {
        workloads: Vec::new(),
        timeout_us: 300_000_000,
        live_wait_cap: true,
        observe_reads: true,
        full_buffer: false,
        integer_sample_seconds: 5,
        production: true,
    }
}
fn code(error: BenchmarkError) -> i32 {
    match error {
        BenchmarkError::InvalidPlan => -4002,
        BenchmarkError::Memory => -4013,
        BenchmarkError::Cancelled => -4072,
        BenchmarkError::Unavailable => -4006,
        BenchmarkError::NoSuccess => -4016,
        BenchmarkError::Arithmetic => -4019,
        BenchmarkError::External(code) => code,
        BenchmarkError::Io { .. } => -4009,
    }
}
fn encode(status: BenchmarkStatus) -> ffi::BenchmarkStatusDto {
    let mut dto = ffi::BenchmarkStatusDto {
        state: 0,
        started_wall_us: 0,
        finished_wall_us: 0,
        ret_code: 0,
        workload: 0,
        completed: 0,
        elapsed_us: 0,
    };
    match status {
        BenchmarkStatus::NotStarted => (),
        BenchmarkStatus::Preparing { started_wall_us } => {
            dto.state = 1;
            dto.started_wall_us = started_wall_us;
        }
        BenchmarkStatus::Running {
            started_wall_us,
            workload,
            completed,
            elapsed,
        } => {
            dto.state = 2;
            dto.started_wall_us = started_wall_us;
            dto.workload = workload;
            dto.completed = completed;
            dto.elapsed_us = elapsed.as_micros().min(u64::MAX as u128) as u64;
        }
        BenchmarkStatus::Draining {
            started_wall_us,
            completed,
            elapsed,
        } => {
            dto.state = 3;
            dto.started_wall_us = started_wall_us;
            dto.completed = completed;
            dto.elapsed_us = elapsed.as_micros().min(u64::MAX as u128) as u64;
        }
        BenchmarkStatus::Finished {
            started_wall_us,
            finished_wall_us,
            completed,
            elapsed,
            result,
        } => {
            dto.state = 4;
            dto.started_wall_us = started_wall_us;
            dto.finished_wall_us = finished_wall_us;
            dto.completed = completed;
            dto.elapsed_us = elapsed.as_micros().min(u64::MAX as u128) as u64;
            dto.ret_code = result.err().map_or(0, code);
        }
    }
    dto
}
impl ServiceHandle {
    pub fn benchmark_begin(
        &self,
        shape: ffi::BenchmarkShapeDto,
        preparation: &mut BenchmarkPreparationSlot,
        run: &mut BenchmarkRunSlot,
    ) -> ffi::BeginBenchmarkDto {
        let result = (|| {
            if preparation.0.is_some() || run.0.is_some() {
                return Err(BenchmarkError::InvalidPlan);
            }
            let shape = BenchmarkShape {
                target_count: NonZeroUsize::new(shape.targets)
                    .ok_or(BenchmarkError::InvalidPlan)?,
                workload_count: NonZeroUsize::new(shape.workloads)
                    .ok_or(BenchmarkError::InvalidPlan)?,
                concurrency: NonZeroUsize::new(shape.workers).ok_or(BenchmarkError::InvalidPlan)?,
            };
            match self
                .0
                .as_ref()
                .ok_or(BenchmarkError::Unavailable)?
                .begin_benchmark(shape)?
            {
                BeginBenchmark::Preparing(value) => {
                    preparation.0 = Some(value);
                    Ok(true)
                }
                BeginBenchmark::AlreadyRunning(value) => {
                    run.0 = Some(value);
                    Ok(false)
                }
            }
        })();
        match result {
            Ok(preparing) => ffi::BeginBenchmarkDto {
                error: 0,
                preparing,
            },
            Err(error) => ffi::BeginBenchmarkDto {
                error: code(error),
                preparing: false,
            },
        }
    }
    pub fn stop_benchmarks(&self) {
        if let Some(service) = &self.0 {
            service.stop_benchmarks();
        }
    }
    pub fn benchmark_status(&self) -> ffi::BenchmarkStatusDto {
        encode(
            self.0
                .as_ref()
                .map_or(BenchmarkStatus::NotStarted, |s| s.benchmark_status()),
        )
    }
}
fn decode(plan: &ffi::BenchmarkPlanDto) -> Result<CheckedWorkloads, BenchmarkError> {
    if plan.production {
        return CheckedWorkloads::production();
    }
    if plan.workloads.len() > crate::benchmark::MAX_WORKLOADS {
        return Err(BenchmarkError::InvalidPlan);
    }
    let mut workloads = Vec::new();
    workloads
        .try_reserve_exact(plan.workloads.len())
        .map_err(|_| BenchmarkError::Memory)?;
    for item in &plan.workloads {
        workloads.push(Workload {
            mode: match item.mode {
                0 => Mode::Read,
                1 => Mode::Write,
                _ => return Err(BenchmarkError::InvalidPlan),
            },
            size: NonZeroUsize::new(item.size).ok_or(BenchmarkError::InvalidPlan)?,
            duration: Duration::from_micros(item.duration_us),
        });
    }
    CheckedWorkloads::new(
        workloads,
        OperationPolicy {
            timeout: Duration::from_micros(plan.timeout_us),
            wait: if plan.live_wait_cap {
                WaitPolicy::LiveGeneralCapAtPublication
            } else {
                WaitPolicy::NoAdditionalCap
            },
            completion: if plan.full_buffer {
                CompletionRequirement::FullBuffer
            } else {
                CompletionRequirement::RequestedWindow
            },
            health: if plan.observe_reads {
                NativeFinalPolicy::ObserveEligibleRead
            } else {
                NativeFinalPolicy::None
            },
        },
        NonZeroU64::new(plan.integer_sample_seconds)
            .map_or(SamplePolicy::Elapsed, SamplePolicy::NominalIntegerSeconds),
    )
}
impl BenchmarkPreparationSlot {
    pub fn benchmark_commit(
        &mut self,
        plan: &ffi::BenchmarkPlanDto,
        run: &mut BenchmarkRunSlot,
    ) -> i32 {
        let result = (|| {
            if run.0.is_some() {
                return Err(BenchmarkError::InvalidPlan);
            }
            let preparation = self.0.as_mut().ok_or(BenchmarkError::InvalidPlan)?;
            let plan = decode(plan)?;
            let value = preparation.commit(plan)?;
            run.0 = Some(value);
            self.0 = None;
            Ok(())
        })();
        result.err().map_or(0, code)
    }
    pub fn benchmark_discard_targets(&mut self) {
        if let Some(preparation) = &mut self.0 {
            preparation.discard_targets();
        }
    }
    pub fn benchmark_abort(&mut self, error: i32, run: &mut BenchmarkRunSlot) -> i32 {
        if run.0.is_some() || error == 0 {
            return code(BenchmarkError::InvalidPlan);
        }
        let Some(preparation) = self.0.take() else {
            return code(BenchmarkError::InvalidPlan);
        };
        run.0 = Some(preparation.abort(BenchmarkError::External(error)));
        0
    }
}
impl BenchmarkRunSlot {
    /// Copies the count of sealed phases. Later calls may include newly drained phases.
    pub fn benchmark_measurement_count(&self) -> usize {
        self.0.as_ref().map_or(0, |run| run.report().len())
    }
    /// Copies one sealed phase, which remains unchanged for this run slot.
    pub fn benchmark_measurement(
        &self,
        index: usize,
        output: &mut ffi::BenchmarkMeasurementDto,
    ) -> bool {
        let Some(value) = self.0.as_ref().and_then(|run| run.report().get(index)) else {
            return false;
        };
        *output = ffi::BenchmarkMeasurementDto {
            mode: if value.mode == Mode::Read { 0 } else { 1 },
            size: value.size.get(),
            elapsed_us: value.elapsed.as_micros().min(u64::MAX as u128) as u64,
            successful_count: value.successful_count,
            successful_latency_us: value.successful_latency_us,
        };
        true
    }
    pub fn benchmark_run_status(&self) -> ffi::BenchmarkStatusDto {
        encode(
            self.0
                .as_ref()
                .map_or(BenchmarkStatus::NotStarted, BenchmarkRun::status),
        )
    }
    pub fn benchmark_stop(&self) {
        if let Some(run) = &self.0 {
            run.request_stop();
        }
    }
    pub fn benchmark_wait_retired(&self) -> ffi::BenchmarkStatusDto {
        encode(
            self.0
                .as_ref()
                .map_or(BenchmarkStatus::NotStarted, BenchmarkRun::wait_retired),
        )
    }
}
pub fn benchmark_add_block(
    preparation: &mut BenchmarkPreparationSlot,
    owner: &BlockOwner,
    device: u64,
    index: u64,
    start: u64,
    length: usize,
    alignment: usize,
) -> i32 {
    let result = (|| {
        let preparation = preparation.0.as_mut().ok_or(BenchmarkError::InvalidPlan)?;
        preparation.check_add()?;
        check_geometry(start, length, alignment)?;
        let region = super::blocks::checked_region(owner, index, start, length)?;
        preparation.add_target(BenchmarkTarget::new(
            Target::Block(region),
            DeviceId(device),
            Execution::PreferNative,
            length,
            alignment,
        )?)?;
        Ok(())
    })();
    result.err().map_or(0, code)
}
pub fn benchmark_add_file(
    preparation: &mut BenchmarkPreparationSlot,
    files: &FileRegistry,
    device: u64,
    file: u64,
    start: u64,
    length: usize,
    alignment: usize,
) -> i32 {
    let result = (|| {
        let preparation = preparation.0.as_mut().ok_or(BenchmarkError::InvalidPlan)?;
        preparation.check_add()?;
        check_geometry(start, length, alignment)?;
        let file = files.file(file)?;
        preparation.add_target(BenchmarkTarget::new(
            Target::File {
                file,
                offset: start,
            },
            DeviceId(device),
            Execution::PreferNative,
            length,
            alignment,
        )?)
    })();
    result.err().map_or(0, code)
}

fn check_geometry(start: u64, length: usize, alignment: usize) -> Result<(), BenchmarkError> {
    if length == 0
        || !alignment.is_power_of_two()
        || !start.is_multiple_of(alignment as u64)
        || !length.is_multiple_of(alignment)
        || start.checked_add(length as u64).is_none()
    {
        Err(BenchmarkError::InvalidPlan)
    } else {
        Ok(())
    }
}

pub fn benchmark_add_named(
    preparation: &mut BenchmarkPreparationSlot,
    bindings: &super::NamedBindings,
    device: u64,
    token: u64,
    start: u64,
    length: usize,
    alignment: usize,
) -> i32 {
    let result = (|| {
        let preparation = preparation.0.as_mut().ok_or(BenchmarkError::InvalidPlan)?;
        preparation.check_add()?;
        check_geometry(start, length, alignment)?;
        let file = bindings.get(token)?;
        file.check_range(start, length as u64)?;
        preparation.add_target(BenchmarkTarget::new(
            Target::Named {
                file,
                offset: start,
            },
            DeviceId(device),
            Execution::PreferNative,
            length,
            alignment,
        )?)
    })();
    result.err().map_or(0, code)
}
