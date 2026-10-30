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
use crate::service::health::CapturedTarget;
use crate::service::{Admission, RequestOptions};
use crate::{
    AdmissionLimits, CancelOutcome, Completion, CompletionRequirement, CompletionStatus, Config,
    ConfigError, DeviceConfig, DeviceId, DeviceLimits, ExcessShare, Execution, Generation, GroupId,
    GroupLimits, Rate, RequestClass, RequestControl, RequestPolicy, Service, Target,
};
use crate::{FinalFacts, HealthConfig, RequestLifetime};
use file_io::bridge::{error_number, FileRegistry};
use file_io::{Backend, BackendKind, Direction};
use std::collections::BTreeMap;
use std::io;
use std::num::{NonZeroU32, NonZeroUsize};
use std::sync::{mpsc, Arc, Mutex};
use std::time::{Duration, Instant};

pub struct ServiceHandle(pub(super) Option<Arc<Service>>);
struct Endpoint {
    service: Arc<Service>,
    id: DeviceId,
    destination: mpsc::Sender<Completion>,
    completed: Mutex<mpsc::Receiver<Completion>>,
}
pub struct DeviceHandle(Option<Endpoint>);
struct Pending {
    admission: Admission,
    destination: mpsc::Sender<Completion>,
    direction: Direction,
    offset: u64,
    length: usize,
    fault: Option<file_io::WorkerFault>,
    target: Option<Arc<CapturedTarget>>,
    observe_health: bool,
}
pub struct RequestSlot(Option<Pending>);
pub struct CancelHandle(Option<Arc<RequestControl>>);
pub struct CompletionSlot(Option<Completion>);

pub fn new_service_handle() -> Box<ServiceHandle> {
    Box::new(ServiceHandle(None))
}
pub fn new_device_handle() -> Box<DeviceHandle> {
    Box::new(DeviceHandle(None))
}
pub fn new_request_lifetime() -> Box<RequestLifetime> {
    Box::default()
}
pub fn new_request_slot() -> Box<RequestSlot> {
    Box::new(RequestSlot(None))
}
pub fn new_cancel_handle() -> Box<CancelHandle> {
    Box::new(CancelHandle(None))
}
pub fn new_completion_slot() -> Box<CompletionSlot> {
    Box::new(CompletionSlot(None))
}
fn status(result: io::Result<()>) -> i32 {
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
fn config(input: &ffi::ResourceConfig) -> io::Result<Config> {
    let mut groups = BTreeMap::new();
    for group in &input.groups {
        let value = GroupLimits {
            minimum_iops: group.minimum_iops,
            maximum_iops: if group.unlimited {
                Rate::Unlimited
            } else {
                Rate::PerSecond(group.maximum_iops)
            },
            excess: NonZeroU32::new(group.excess_weight)
                .map_or(ExcessShare::None, ExcessShare::Weighted),
            burst: group.burst,
        };
        if groups.insert(GroupId(group.id), value).is_some() {
            return Err(invalid());
        }
    }
    let mut devices = BTreeMap::new();
    for device in &input.devices {
        let value = DeviceLimits {
            depth: device.depth,
            probe_depth: device.probe_depth,
            max_probe_bytes: device.max_probe_bytes,
        };
        if devices.insert(DeviceId(device.id), value).is_some() {
            return Err(invalid());
        }
    }
    Ok(Config {
        health: HealthConfig {
            warning_tolerance: Duration::from_micros(input.health_warning_tolerance_us),
            blacklist_interval: Duration::from_micros(input.health_blacklist_interval_us),
            wait_cap: Duration::from_micros(input.health_wait_cap_us),
        },
        admission: AdmissionLimits {
            max_records: input.max_records,
            max_bytes: input.max_bytes,
            probe_records: input.probe_records,
            probe_bytes: input.probe_bytes,
        },
        devices,
        groups,
        default_group: GroupId(input.default_group),
        group_capacity_iops: if input.unlimited_capacity {
            Rate::Unlimited
        } else {
            Rate::PerSecond(input.capacity_iops)
        },
    })
}

fn config_receipt(
    expected: u64,
    result: Result<crate::ConfigReceipt, ConfigError>,
) -> ffi::ConfigReceipt {
    let mut receipt = ffi::ConfigReceipt {
        error: 0,
        generation: expected,
        normal_record_debt: 0,
        normal_byte_debt: 0,
        probe_record_debt: 0,
        probe_byte_debt: 0,
        physical_debt: false,
    };
    match result {
        Ok(value) => {
            receipt.generation = value.generation.get();
            receipt.normal_record_debt = value.debt[0].records;
            receipt.normal_byte_debt = value.debt[0].bytes;
            receipt.probe_record_debt = value.debt[1].records;
            receipt.probe_byte_debt = value.debt[1].bytes;
            receipt.physical_debt = value.physical_debt;
        }
        Err(ConfigError::Stale(generation)) => {
            receipt.generation = generation.get();
            receipt.error = error_number(io::ErrorKind::WouldBlock.into());
        }
        Err(ConfigError::Invalid(error) | ConfigError::Unavailable(error)) => {
            receipt.error = error_number(error)
        }
    }
    receipt
}

impl ServiceHandle {
    fn service(&self) -> io::Result<&Arc<Service>> {
        self.0
            .as_ref()
            .ok_or_else(|| io::ErrorKind::NotConnected.into())
    }
    pub fn start_service(&mut self, input: &ffi::ResourceConfig) -> i32 {
        status((|| {
            if self.0.is_some() {
                return Err(io::ErrorKind::AlreadyExists.into());
            }
            self.0 = Some(Arc::new(Service::start(config(input)?)?));
            Ok(())
        })())
    }
    pub fn stop_service(&self) {
        if let Some(service) = &self.0 {
            service.stop();
        }
    }
    pub fn shutdown_service(&mut self) -> i32 {
        let Some(service) = self.0.take() else {
            return 0;
        };
        service.stop();
        match Arc::try_unwrap(service) {
            Ok(service) => status(service.shutdown()),
            Err(service) => {
                self.0 = Some(service);
                error_number(io::ErrorKind::WouldBlock.into())
            }
        }
    }
    pub fn register_device(
        &self,
        expected: u64,
        input: &ffi::ResourceConfig,
        options: &ffi::DeviceOptions,
        handle: &mut DeviceHandle,
    ) -> ffi::ConfigReceipt {
        let result = (|| {
            if handle.0.is_some() {
                return Err(ConfigError::Invalid(io::ErrorKind::AlreadyExists.into()));
            }
            let service = Arc::clone(self.service().map_err(ConfigError::Unavailable)?);
            let config = config(input).map_err(ConfigError::Invalid)?;
            let worker = Backend::new(
                BackendKind::Worker {
                    threads: options.worker_threads,
                },
                options.backend_capacity,
            )
            .map_err(ConfigError::Invalid)?;
            let native = if options.native_aio {
                Some(
                    Backend::new(BackendKind::NativeAio, options.backend_capacity)
                        .map_err(ConfigError::Invalid)?,
                )
            } else {
                None
            };
            let id = DeviceId(options.id);
            let (destination, completed) = mpsc::channel();
            let endpoint = Endpoint {
                service,
                id,
                destination,
                completed: Mutex::new(completed),
            };
            let receipt = endpoint.service.register_device_with_config(
                Generation(expected),
                config,
                DeviceConfig { id, worker, native },
            )?;
            handle.0 = Some(endpoint);
            Ok(receipt)
        })();
        config_receipt(expected, result)
    }
    pub fn remove_device(&self, expected: u64, device: u64) -> ffi::DeviceRemovalReceipt {
        let result = self
            .service()
            .map_err(ConfigError::Unavailable)
            .and_then(|service| service.remove_device_at(Generation(expected), DeviceId(device)));
        match result {
            Ok(generation) => ffi::DeviceRemovalReceipt {
                error: 0,
                generation: generation.get(),
            },
            Err(ConfigError::Stale(generation)) => ffi::DeviceRemovalReceipt {
                error: error_number(io::ErrorKind::WouldBlock.into()),
                generation: generation.get(),
            },
            Err(ConfigError::Invalid(error) | ConfigError::Unavailable(error)) => {
                ffi::DeviceRemovalReceipt {
                    error: error_number(error),
                    generation: expected,
                }
            }
        }
    }
    pub fn apply_config(&self, expected: u64, input: &ffi::ResourceConfig) -> ffi::ConfigReceipt {
        let result = (|| {
            let service = self.service().map_err(ConfigError::Unavailable)?;
            service.reconfigure(
                Generation(expected),
                config(input).map_err(ConfigError::Invalid)?,
            )
        })();
        config_receipt(expected, result)
    }
    pub fn health_status(&self, output: &mut ffi::HealthStatus) -> i32 {
        status((|| {
            let value = self.service()?.health_status();
            *output = ffi::HealthStatus {
                warning: value.warning,
                warning_wall_us: value.warning_wall_us,
                queued: value.queued,
                cleaning: value.cleaning,
                active: value.active,
                waiting_capacity: value.waiting_capacity,
                active_capacity: value.active_capacity,
                owned_payload_bytes: value.owned_payload_bytes,
                accepted_error: value.observations.accepted_error,
                accepted_timeout: value.observations.accepted_timeout,
                full: value.observations.full,
                busy: value.observations.busy,
                stopped: value.observations.stopped,
                already_observed: value.observations.already_observed,
            };
            Ok(())
        })())
    }
    pub fn service_stats(&self, stats: &mut ffi::ServiceStats) -> i32 {
        status((|| {
            let value = self.service()?.snapshot()?;
            *stats = ffi::ServiceStats {
                generation: value.generation.get(),
                probe_attempts: value.probe_attempts,
                admission_debt: value.debt.iter().any(|d| d.records != 0 || d.bytes != 0),
                physical_debt: value.devices.iter().any(|d| d.depth_debt != [0, 0]),
                groups: value
                    .groups
                    .into_iter()
                    .map(|g| ffi::GroupStats {
                        id: g.id.get(),
                        physical_attempts: g.physical_attempts,
                        rate_exempt_attempts: g.rate_exempt_attempts,
                        rate_throttled: g.rate_throttled,
                        device_blocked: g.device_blocked,
                    })
                    .collect(),
                admitted_requests: value.admitted_requests,
                reserved_bytes: value.reserved_bytes,
                queued_requests: value.queued_requests,
                awaiting_retirement: value.awaiting_retirement,
            };
            Ok(())
        })())
    }
}
impl DeviceHandle {
    pub fn close_device(&self) -> i32 {
        status(
            self.0
                .as_ref()
                .map_or(Ok(()), |e| e.service.remove_device(e.id)),
        )
    }
    pub fn begin_request(
        &self,
        options: &ffi::RequestOptions,
        slot: &mut RequestSlot,
        control: &mut CancelHandle,
        lifetime: &mut RequestLifetime,
    ) -> i32 {
        status((|| {
            if slot.0.is_some() || control.0.is_some() || !lifetime.is_empty() {
                return Err(io::ErrorKind::AlreadyExists.into());
            }
            let endpoint = self.0.as_ref().ok_or(io::ErrorKind::NotConnected)?;
            let direction = match options.mode {
                ffi::RequestMode::Read => Direction::Read,
                ffi::RequestMode::Write => Direction::Write,
                _ => return Err(invalid()),
            };
            let end = options
                .window_start
                .checked_add(options.window_length)
                .filter(|end| *end <= options.length)
                .ok_or_else(invalid)?;
            if options.timeout_us < -1 {
                return Err(invalid());
            }
            let fault = decode_worker_fault(options.worker_fault)?;
            let execution = if options.worker_required || fault.is_some() {
                Execution::WorkerRequired
            } else {
                Execution::PreferNative
            };
            let policy = RequestPolicy {
                deadline: if options.timeout_us == -1 {
                    None
                } else {
                    Some(deadline(options.timeout_us as u64)?)
                },
                retry_alignment: NonZeroUsize::new(options.retry_alignment),
                max_error_retries: options.max_error_retries,
                completion: if options.full_buffer {
                    CompletionRequirement::FullBuffer
                } else {
                    CompletionRequirement::RequestedWindow
                },
            };
            let mut admission = endpoint.service.begin(RequestOptions {
                device: endpoint.id,
                group: GroupId(options.group),
                class: if options.probe {
                    RequestClass::Probe
                } else {
                    RequestClass::Normal
                },
                execution,
                rate_exempt: options.rate_exempt,
                length: options.length,
                alignment: options.alignment,
                window: options.window_start..end,
                policy,
                tag: options.tag,
            })?;
            *lifetime = admission.lifetime();
            control.0 = Some(Arc::new(admission.take_control()?));
            slot.0 = Some(Pending {
                admission,
                destination: endpoint.destination.clone(),
                direction,
                offset: options.offset,
                length: options.length,
                fault,
                target: None,
                observe_health: options.observe_health
                    && direction == Direction::Read
                    && !options.probe,
            });
            Ok(())
        })())
    }
    pub fn poll_completion(&self, wait_us: u64, slot: &mut CompletionSlot) -> i32 {
        status((|| {
            if slot.0.is_some() {
                return Err(io::ErrorKind::AlreadyExists.into());
            }
            let endpoint = self.0.as_ref().ok_or(io::ErrorKind::NotConnected)?;
            slot.0 = Some(
                endpoint
                    .completed
                    .lock()
                    .unwrap()
                    .recv_timeout(Duration::from_micros(wait_us))
                    .map_err(|e| match e {
                        mpsc::RecvTimeoutError::Timeout => io::ErrorKind::WouldBlock,
                        mpsc::RecvTimeoutError::Disconnected => io::ErrorKind::BrokenPipe,
                    })?,
            );
            Ok(())
        })())
    }
}
impl CancelHandle {
    pub fn clone_cancel_handle(&self) -> Box<Self> {
        Box::new(Self(self.0.clone()))
    }
    pub fn request_cancel(&self) -> bool {
        self.0
            .as_ref()
            .is_some_and(|c| c.request_cancel() == CancelOutcome::Cancelled)
    }
}
impl RequestSlot {
    pub fn has_request(&self) -> bool {
        self.0.is_some()
    }
    pub fn clear_request(&mut self) {
        self.0 = None;
    }
    pub fn wait_capacity(&mut self) -> i32 {
        status(
            self.0
                .as_mut()
                .ok_or_else(invalid)
                .and_then(|p| p.admission.wait_capacity()),
        )
    }
    pub(super) fn geometry(&self, lifetime: &RequestLifetime) -> io::Result<(u64, usize)> {
        let pending = self.0.as_ref().ok_or_else(invalid)?;
        if pending.target.is_some() || lifetime.target.is_some() {
            return Err(io::ErrorKind::AlreadyExists.into());
        }
        if !lifetime.belongs_to(&pending.admission) {
            return Err(invalid());
        }
        Ok((pending.offset, pending.length))
    }
    pub(super) fn bind_target(
        &mut self,
        target: io::Result<Target>,
        lifetime: &mut RequestLifetime,
    ) -> i32 {
        status((|| {
            let pending = self.0.as_mut().ok_or_else(invalid)?;
            if pending.target.is_some() || lifetime.target.is_some() || lifetime.is_empty() {
                return Err(io::ErrorKind::AlreadyExists.into());
            }
            lifetime.capture(&pending.admission, target?, pending.observe_health);
            pending.target = lifetime.target.clone();
            Ok(())
        })())
    }
    pub fn prepare_reserved(&mut self, write_bytes: &[u8]) -> i32 {
        status((|| {
            let pending = self.0.as_mut().ok_or_else(invalid)?;
            let target = pending.target.as_ref().ok_or_else(invalid)?.target.clone();
            pending
                .admission
                .prepare(target, pending.direction, write_bytes)?;
            if let Some(fault) = pending.fault {
                pending.admission.set_worker_fault(fault)?;
            }
            Ok(())
        })())
    }
    pub fn publish(&mut self) -> ffi::SubmitResult {
        let result = (|| {
            let pending = self.0.as_mut().ok_or_else(invalid)?;
            pending.admission.publish(pending.destination.clone())
        })();
        match result {
            Ok(id) => {
                self.0 = None;
                ffi::SubmitResult {
                    error: 0,
                    id: id.get(),
                }
            }
            Err(error) => ffi::SubmitResult {
                error: error_number(error),
                id: 0,
            },
        }
    }
}
pub fn bind_registered_target(
    files: &FileRegistry,
    id: u64,
    slot: &mut RequestSlot,
    lifetime: &mut RequestLifetime,
) -> i32 {
    let target = slot.geometry(lifetime).and_then(|(offset, _)| {
        Ok(Target::File {
            file: files.file(id)?,
            offset,
        })
    });
    slot.bind_target(target, lifetime)
}

impl RequestLifetime {
    pub fn observe_final(&self, facts: &ffi::FinalFacts) -> ffi::ObservationReceipt {
        let receipt = self.observe(FinalFacts {
            io_error: facts.io_error,
            raw_os_error: std::num::NonZeroI32::new(facts.os_error),
            deadline_exceeded: facts.deadline_exceeded,
            finished_wall_us: facts.finished_wall_us,
        });
        fn code(outcome: crate::ObservationOutcome) -> ffi::ObservationOutcome {
            match outcome {
                crate::ObservationOutcome::NotOffered => ffi::ObservationOutcome::NotOffered,
                crate::ObservationOutcome::Accepted => ffi::ObservationOutcome::Accepted,
                crate::ObservationOutcome::Full => ffi::ObservationOutcome::Full,
                crate::ObservationOutcome::Busy => ffi::ObservationOutcome::Busy,
                crate::ObservationOutcome::Stopped => ffi::ObservationOutcome::Stopped,
                crate::ObservationOutcome::AlreadyObserved => {
                    ffi::ObservationOutcome::AlreadyObserved
                }
            }
        }
        ffi::ObservationReceipt {
            error: code(receipt.error),
            timeout: code(receipt.timeout),
        }
    }
}
impl CompletionSlot {
    pub fn completion_info(&self) -> ffi::CompletionInfo {
        let mut info = ffi::CompletionInfo {
            code: ffi::CompletionCode::Empty,
            id: 0,
            tag: 0,
            transferred: 0,
            error: 0,
            os_error: 0,
            has_buffer: false,
            group: 0,
            physical_attempts: 0,
            raw_depth: 0,
            effective_depth: 0,
        };
        if let Some(completion) = &self.0 {
            info.id = completion.id.get();
            info.group = completion.group.get();
            info.physical_attempts = completion.physical_attempts;
            info.raw_depth = completion.depth_cost.raw;
            info.effective_depth = completion.depth_cost.effective;
            info.tag = completion.tag;
            info.transferred = completion.transferred;
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

    pub fn completion_data(&self) -> &[u8] {
        self.0.as_ref().and_then(Completion::data).unwrap_or(&[])
    }

    pub fn clear_completion(&mut self) {
        self.0 = None;
    }
}

#[cfg(test)]
#[test]
fn worker_fault_boundary_decodes_delay_and_failure_independently() {
    for (value, delay, fail) in [
        (-1, 0, true),
        (-10, 10, false),
        (-11, 10, true),
        (-300, 300, false),
        (-301, 300, true),
        (i32::MIN, 2_147_483_640, true),
    ] {
        assert_eq!(
            decode_worker_fault(value).unwrap(),
            Some(file_io::WorkerFault::new(
                Duration::from_millis(delay),
                fail
            ))
        );
    }
    assert_eq!(decode_worker_fault(0).unwrap(), None);
    assert_eq!(
        decode_worker_fault(1).unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
}

#[cfg(test)]
mod registration_tests {
    use super::*;

    fn initial() -> ffi::ResourceConfig {
        ffi::ResourceConfig {
            health_warning_tolerance_us: 5_000_000,
            health_blacklist_interval_us: 60_000_000,
            health_wait_cap_us: 120_000_000,
            max_records: 32,
            max_bytes: 16 * 1024 * 1024,
            probe_records: 4,
            probe_bytes: 2 * 1024 * 1024,
            default_group: 1,
            capacity_iops: 0,
            unlimited_capacity: true,
            groups: vec![ffi::GroupPolicy {
                id: 1,
                minimum_iops: 0,
                maximum_iops: 0,
                unlimited: true,
                excess_weight: 1,
                burst: 1,
            }],
            devices: vec![ffi::DevicePolicy {
                id: 1,
                depth: 4,
                probe_depth: 1,
                max_probe_bytes: 2 * 1024 * 1024,
            }],
        }
    }
    fn options(id: u64) -> ffi::DeviceOptions {
        ffi::DeviceOptions {
            id,
            native_aio: false,
            worker_threads: 1,
            backend_capacity: 4,
        }
    }
    #[test]
    fn bridge_registration_stages_endpoint_and_preserves_capacity_on_backend_failure() {
        let mut service = new_service_handle();
        assert_eq!(service.start_service(&initial()), 0);
        let mut first = new_device_handle();
        let receipt = service.register_device(0, &initial(), &options(1), &mut first);
        assert_eq!((receipt.error, receipt.generation), (0, 1));
        let before = service.service().unwrap().snapshot().unwrap();
        let mut expanded = initial();
        expanded.probe_bytes = 8 * 1024 * 1024;
        expanded.devices.push(ffi::DevicePolicy {
            id: 2,
            depth: 4,
            probe_depth: 1,
            max_probe_bytes: 8 * 1024 * 1024,
        });
        let mut second = new_device_handle();
        let mut bad = options(2);
        bad.worker_threads = 0;
        let receipt = service.register_device(1, &expanded, &bad, &mut second);
        assert_ne!(receipt.error, 0);
        assert_eq!(receipt.generation, 1);
        assert!(second.0.is_none());
        assert_eq!(service.service().unwrap().snapshot().unwrap(), before);
        let mut admission = first
            .0
            .as_ref()
            .unwrap()
            .service
            .begin(RequestOptions {
                device: DeviceId(1),
                group: GroupId(1),
                class: RequestClass::Normal,
                execution: Execution::WorkerRequired,
                rate_exempt: false,
                length: 12 * 1024 * 1024,
                alignment: 4096,
                window: 0..12 * 1024 * 1024,
                policy: RequestPolicy::default(),
                tag: 0,
            })
            .unwrap();
        admission.wait_capacity().unwrap();
        assert_eq!(
            service
                .service()
                .unwrap()
                .snapshot()
                .unwrap()
                .reserved_bytes,
            12 * 1024 * 1024
        );
        drop(admission);
        let stale = service.register_device(0, &expanded, &options(2), &mut second);
        assert_ne!(stale.error, 0);
        assert_eq!(stale.generation, 1);
        assert!(second.0.is_none());
        let receipt = service.register_device(1, &expanded, &options(2), &mut second);
        assert_eq!((receipt.error, receipt.generation), (0, 2));
        assert_eq!(second.0.as_ref().unwrap().id, DeviceId(2));
        let before = service.service().unwrap().snapshot().unwrap();
        assert_eq!(before.devices.len(), 2);
        let occupied = service.register_device(2, &expanded, &options(2), &mut second);
        assert_ne!(occupied.error, 0);
        assert_eq!(service.service().unwrap().snapshot().unwrap(), before);
        drop((first, second));
        assert_eq!(service.shutdown_service(), 0);
    }
}
