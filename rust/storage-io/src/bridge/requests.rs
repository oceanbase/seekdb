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

use super::ffi;
use crate::service::{CallbackOwner, Consumer, PublishedCompletion, RequestOptions};
use crate::{
    AdmissionLimits, CancelOutcome, CompletionRequirement, CompletionStatus, Config, ConfigError,
    Execution, Request, RequestPolicy, Service, Target,
};
use crate::file_session::{Endpoint, FileSession};
use file_io::bridge::error_number;
use file_io::Direction;
use std::collections::BTreeMap;
use std::io;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

pub struct ServiceHandle(Mutex<Option<Arc<Service>>>);
pub(super) struct RequestPlan {
    pub error: i32,
    pub offset: u64,
    pub length: usize,
    pub window_start: usize,
    pub window_length: usize,
}
pub fn new_service_handle() -> Box<ServiceHandle> {
    Box::new(ServiceHandle(Mutex::new(None)))
}
struct ForeignCallback(cxx::UniquePtr<ffi::CallbackAdapter>);
// SAFETY: these adapters wrap callbacks already executed by the database's asynchronous
// callback threads. process has exclusive ownership before publication; after insertion
// into the immutable outcome, only const result access is exposed through shared handles.
unsafe impl Send for ForeignCallback {}
unsafe impl Sync for ForeignCallback {}
impl Consumer for ForeignCallback {
    fn process(&mut self, bytes: &[u8]) -> Result<i32, ()> {
        self.0.pin_mut().process(bytes).map_err(|_| ())
    }
    fn foreign(&self) -> Option<&ffi::CallbackAdapter> {
        self.0.as_ref()
    }
}
pub(super) fn status(result: io::Result<()>) -> i32 {
    result.err().map_or(0, error_number)
}
fn invalid() -> io::Error {
    io::ErrorKind::InvalidInput.into()
}
fn deadline(micros: u64) -> io::Result<Instant> {
    Instant::now()
        .checked_add(Duration::from_micros(micros))
        .ok_or_else(invalid)
}
fn decode_worker_fault(value: i32) -> io::Result<Option<file_io::WorkerFault>> {
    if value > 0 {
        return Err(invalid());
    }
    let magnitude = u64::from(value.unsigned_abs());
    Ok((value != 0).then(|| {
        file_io::WorkerFault::new(
            Duration::from_millis(magnitude / 10 * 10),
            magnitude % 10 != 0,
        )
    }))
}
fn buffer_limit(server_memory_bytes: u64, io_buffer_bytes: u64) -> io::Result<usize> {
    if server_memory_bytes == 0 {
        return Err(invalid());
    }
    usize::try_from(if io_buffer_bytes == 0 {
        (server_memory_bytes / 5).max(1)
    } else {
        io_buffer_bytes
    })
    .map_err(|_| invalid())
}
fn callback_count(threads: usize) -> usize {
    if threads == 0 {
        16
    } else {
        threads
    }
}
fn runtime_config(input: &ffi::RuntimeConfig) -> io::Result<Config> {
    Ok(Config {
        callback_threads: callback_count(input.callback_threads),
        statistics_lanes: input.statistics_lanes,
        admission: AdmissionLimits {
            max_records: input.request_limit,
            max_bytes: buffer_limit(input.server_memory_bytes, input.io_buffer_bytes)?,
        },
        devices: BTreeMap::new(),
    })
}

pub(super) fn plan_request(
    offset: u64,
    length: usize,
    alignment: usize,
    mode: ffi::RequestMode,
) -> RequestPlan {
    let mut plan = RequestPlan {
        error: 0,
        offset: 0,
        length: 0,
        window_start: 0,
        window_length: 0,
    };
    let result = (|| {
        if length == 0 || !alignment.is_power_of_two() {
            return Err(invalid());
        }
        let alignment = u64::try_from(alignment).map_err(|_| invalid())?;
        let end = offset
            .checked_add(u64::try_from(length).map_err(|_| invalid())?)
            .ok_or_else(invalid)?;
        let start = offset & !(alignment - 1);
        let end = end.checked_add(alignment - 1).ok_or_else(invalid)? & !(alignment - 1);
        if end > i64::MAX as u64 {
            return Err(invalid());
        }
        match mode {
            ffi::RequestMode::Read => (),
            ffi::RequestMode::Write if offset == start && length as u64 == end - start => (),
            _ => return Err(invalid()),
        }
        let physical_length = usize::try_from(end - start).map_err(|_| invalid())?;
        std::alloc::Layout::from_size_align(physical_length, alignment as usize)
            .map_err(|_| invalid())?;
        plan.offset = start;
        plan.length = physical_length;
        plan.window_start = (offset - start) as usize;
        plan.window_length = length;
        Ok(())
    })();
    plan.error = status(result);
    plan
}

fn config_error(error: ConfigError) -> io::Error {
    match error {
        ConfigError::Invalid(error) | ConfigError::Unavailable(error) => error,
    }
}

impl ServiceHandle {
    pub(super) fn service(&self) -> io::Result<Arc<Service>> {
        self.0
            .lock()
            .unwrap()
            .as_ref()
            .cloned()
            .ok_or_else(|| io::ErrorKind::NotConnected.into())
    }
    fn initialize(&self, config: Config) -> io::Result<()> {
        let mut service = self.0.lock().unwrap();
        if service.is_some() {
            return Err(io::ErrorKind::AlreadyExists.into());
        }
        *service = Some(Arc::new(Service::prepare(config)?));
        Ok(())
    }
    pub fn prepare_service(&self, input: &ffi::RuntimeConfig) -> i32 {
        status(runtime_config(input).and_then(|config| self.initialize(config)))
    }
    pub fn activate_service(&self) -> i32 {
        status(self.service().and_then(|service| service.activate()))
    }
    pub fn stop_service(&self) {
        if let Ok(service) = self.service() {
            service.stop();
        }
    }
    pub fn shutdown_service(&self) -> i32 {
        let service = self.0.lock().unwrap().clone();
        status((|| {
            if let Some(service) = service {
                service.shutdown()?;
                let mut slot = self.0.lock().unwrap();
                if slot
                    .as_ref()
                    .is_some_and(|current| Arc::ptr_eq(current, &service))
                {
                    *slot = None;
                }
            }
            Ok(())
        })())
    }
    pub fn configure_resources(
        &self,
        server_memory_bytes: u64,
        io_buffer_bytes: u64,
        callback_threads: usize,
    ) -> i32 {
        let result = (|| {
            let max_bytes =
                buffer_limit(server_memory_bytes, io_buffer_bytes).map_err(ConfigError::Invalid)?;
            self.service()
                .map_err(ConfigError::Unavailable)?
                .configure_resources(max_bytes, callback_count(callback_threads))
        })();
        status(result.map(|_| ()).map_err(config_error))
    }

    pub fn statistics_handle(&self) -> Box<super::statistics::StatisticsHandle> {
        Box::new(super::statistics::StatisticsHandle(
            self.service()
                .ok()
                .map(|service| service.statistics_owner()),
        ))
    }
}
impl Endpoint {
    fn submit(
        &self,
        options: &ffi::RequestOptions,
        plan: &RequestPlan,
        target: Target,
        bytes: &[u8],
        callback: Option<CallbackOwner>,
    ) -> Box<Request> {
        let converted = (|| {
            let direction = match options.mode {
                ffi::RequestMode::Read => Direction::Read,
                ffi::RequestMode::Write => Direction::Write,
                _ => return Err(invalid()),
            };
            let end = plan.window_start + plan.window_length;
            if options.timeout_us < -1 {
                return Err(invalid());
            }
            match &target {
                Target::File { offset, .. } | Target::Named { offset, .. }
                    if *offset != plan.offset =>
                {
                    return Err(invalid());
                }
                Target::Block(region) if region.file_region().2 < plan.length as u64 => {
                    return Err(invalid());
                }
                _ => (),
            }
            let fault = decode_worker_fault(if self.native_aio() && !options.worker_required {
                0
            } else {
                options.worker_fault
            })?;
            let execution = if options.worker_required || fault.is_some() {
                Execution::WorkerRequired
            } else {
                Execution::PreferNative
            };
            let alignment = 4096;
            let deadline = if options.timeout_us == -1 {
                None
            } else {
                Some(deadline(options.timeout_us as u64)?)
            };
            let request = RequestOptions {
                device: self.id,
                execution,
                length: plan.length,
                alignment,
                window: plan.window_start..end,
                policy: RequestPolicy::for_storage_io(
                    deadline,
                    execution,
                    self.native_aio(),
                    alignment,
                    if callback.is_some() {
                        CompletionRequirement::FullBuffer
                    } else {
                        CompletionRequirement::RequestedWindow
                    },
                ),
                tag: 0,
                statistics_lane: Some(options.statistics_lane),
            };
            Ok((request, target, direction, fault))
        })();
        match converted {
            Ok((request, target, direction, fault)) => match self
                .service
                .submit_request(request, target, direction, bytes, callback, fault)
            {
                Ok(request) => Box::new(request),
                Err(error) => Box::new(Request::rejected(error, None)),
            },
            Err(error) => Box::new(Request::rejected(error, callback)),
        }
    }
}
pub(super) fn prepare_session(
    session: &FileSession,
    service: &ServiceHandle,
    directory: &[u8],
    execution: &ffi::ExecutionOptions,
) -> i32 {
    status(session.prepare(
        || service.service(),
        || file_io::bridge::legacy_path(directory),
        execution.worker_threads,
        execution.max_depth,
    ))
}

pub(super) fn take_callback(
    callback: cxx::UniquePtr<ffi::CallbackAdapter>,
) -> Option<CallbackOwner> {
    (!callback.is_null()).then(|| Box::new(ForeignCallback(callback)) as CallbackOwner)
}
pub(super) fn reject(error: io::Error, callback: Option<CallbackOwner>) -> Box<Request> {
    Box::new(Request::rejected(error, callback))
}
pub(super) fn logical_plan(options: &ffi::RequestOptions) -> io::Result<RequestPlan> {
    let plan = plan_request(options.offset, options.length, 4096, options.mode);
    if plan.error != 0 {
        return Err(io::Error::from_raw_os_error(plan.error));
    }
    Ok(plan)
}
pub(super) fn submit_owned(
    endpoint: &Endpoint,
    target: Target,
    plan: &RequestPlan,
    options: &ffi::RequestOptions,
    bytes: &[u8],
    callback: Option<CallbackOwner>,
) -> Box<Request> {
    endpoint.submit(options, plan, target, bytes, callback)
}

impl Request {
    pub fn cancel_for_ffi(&self) -> bool {
        self.request_cancel() == CancelOutcome::Cancelled
    }
    pub fn result(&self) -> ffi::CompletionInfo {
        let mut info = completion_info(self.completed());
        info.callback_error = self.callback_error();
        info
    }
}
fn completion_info(completion: Option<&PublishedCompletion>) -> ffi::CompletionInfo {
    let mut info = ffi::CompletionInfo {
        code: ffi::CompletionCode::Empty,
        id: 0,
        tag: 0,
        transferred: 0,
        error: 0,
        os_error: 0,
        callback_error: 0,
        has_buffer: false,
        physical_attempts: 0,
        raw_depth: 0,
        effective_depth: 0,
    };
    if let Some(completion) = completion {
        info.id = completion.id.get();
        info.tag = completion.tag;
        info.transferred = completion.data_size();
        info.physical_attempts = completion.physical_attempts;
        info.raw_depth = completion.depth_cost.raw;
        info.effective_depth = completion.depth_cost.effective;
        info.has_buffer = completion.buffer().is_some();
        info.code = match &completion.status {
            CompletionStatus::Complete => ffi::CompletionCode::Complete,
            CompletionStatus::ShortIo => ffi::CompletionCode::ShortIo,
            CompletionStatus::IncompleteEnvelope => ffi::CompletionCode::IncompleteEnvelope,
            CompletionStatus::Cancelled => ffi::CompletionCode::Cancelled,
            CompletionStatus::TimedOut => ffi::CompletionCode::TimedOut,
            CompletionStatus::Shutdown => ffi::CompletionCode::Shutdown,
            CompletionStatus::IoError(error) => {
                info.os_error = error.raw_os_error().unwrap_or(0);
                info.error = error_number(error.raw_os_error().map_or_else(
                    || io::Error::from(error.kind()),
                    io::Error::from_raw_os_error,
                ));
                ffi::CompletionCode::IoError
            }
        };
    }
    info
}
