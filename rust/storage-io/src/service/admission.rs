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

use super::*;

pub(super) use file_io::CompletionWake as Wake;
pub(super) struct Ledger {
    limits: AdmissionLimits,
    pub usage: ResourceUsage,
    capacity_waiters: usize,
    driver_has_work: Arc<AtomicBool>,
}
impl Ledger {
    pub(super) fn new(limits: AdmissionLimits, driver_has_work: Arc<AtomicBool>) -> Self {
        Self {
            limits,
            usage: ResourceUsage::default(),
            capacity_waiters: 0,
            driver_has_work,
        }
    }
    pub(super) fn set_limits(&mut self, limits: AdmissionLimits) {
        self.limits = limits;
    }
    pub fn debt(&self) -> ResourceUsage {
        ResourceUsage {
            records: self.usage.records.saturating_sub(self.limits.max_records),
            bytes: self.usage.bytes.saturating_sub(self.limits.max_bytes),
        }
    }
    pub fn fits_bytes(&self, bytes: usize) -> bool {
        bytes <= self.limits.max_bytes.saturating_sub(self.usage.bytes)
    }
    pub(super) fn reserve_bytes(&mut self, cell: &ControlCell) -> bool {
        let bytes = cell.options.length;
        if !self.fits_bytes(bytes) {
            return false;
        }
        self.usage.bytes += bytes;
        cell.charge.bytes.store(bytes, Ordering::Relaxed);
        if cell
            .phase
            .compare_exchange(WAITING, RESERVED, Ordering::Release, Ordering::Relaxed)
            .is_err()
        {
            self.usage.bytes -= bytes;
            cell.charge.bytes.store(0, Ordering::Relaxed);
            return false;
        }
        true
    }
}
impl Gate {
    fn enqueue_waiter(&mut self, cell: Arc<ControlCell>, ledger: &mut Ledger) {
        self.waiting.insert(cell.id, cell);
        ledger.capacity_waiters += 1;
    }
    fn remove_waiter(
        &mut self,
        cell: &ControlCell,
        ledger: &mut Ledger,
    ) -> Option<Arc<ControlCell>> {
        let removed = self.waiting.remove(&cell.id);
        if removed.is_some() {
            ledger.capacity_waiters -= 1;
        }
        removed
    }
}
pub(super) struct Charge {
    pub ledger: Arc<Mutex<Ledger>>,
    pub bytes: AtomicUsize,
    pub wake: Weak<Wake>,
    #[cfg(any(test, feature = "ownership-test-support"))]
    pub drop_witness: crate::drop_witness::DropRegistration,
}
impl Charge {
    pub(super) fn release_bytes(&self) {
        let mut ledger = self.ledger.lock().unwrap();
        let bytes = self.bytes.swap(0, Ordering::AcqRel);
        if bytes == 0 {
            return;
        }
        ledger.usage.bytes -= bytes;
        let needs_wake =
            ledger.capacity_waiters != 0 || ledger.driver_has_work.load(Ordering::Acquire);
        drop(ledger);
        if needs_wake {
            if let Some(wake) = self.wake.upgrade() {
                wake.notify();
            }
        }
    }
}
impl Drop for Charge {
    fn drop(&mut self) {
        #[cfg(any(test, feature = "ownership-test-support"))]
        let _witness = self.drop_witness.enter();
        let mut ledger = self.ledger.lock().unwrap();
        let used = &mut ledger.usage;
        used.records -= 1;
        used.bytes -= self.bytes.load(Ordering::Relaxed);
        let needs_wake =
            ledger.capacity_waiters != 0 || ledger.driver_has_work.load(Ordering::Acquire);
        drop(ledger);
        if needs_wake {
            if let Some(wake) = self.wake.upgrade() {
                wake.notify();
            }
        }
    }
}
pub(super) const WAITING: u8 = 0;
pub(super) const RESERVED: u8 = 1;
pub(super) const PUBLISHED: u8 = 2;
pub(super) const FINISHED: u8 = 3;
pub(super) const ABORTED: u8 = 4;
pub(super) struct Cancellation {
    pub id: RequestId,
    _cell: Arc<ControlCell>,
}
pub(super) struct ControlCell {
    pub id: RequestId,
    pub options: RequestOptions,
    pub charge: Charge,
    pub phase: AtomicU8,
    pub cancelled: Arc<AtomicBool>,
    pub device_open: Arc<AtomicBool>,
    cancellations: Weak<Mutex<VecDeque<Cancellation>>>,
    pub wake: Weak<Wake>,
}
impl ControlCell {
    pub fn cancel(self: &Arc<Self>) -> CancelOutcome {
        if self.phase.load(Ordering::Acquire) >= FINISHED {
            return CancelOutcome::NotPending;
        }
        let was_cancelled = self.cancelled.swap(true, Ordering::AcqRel);
        if !was_cancelled && self.phase.load(Ordering::Acquire) == PUBLISHED {
            self.enqueue_cancellation();
        }
        if let Some(wake) = self.wake.upgrade() {
            wake.notify();
        }
        if was_cancelled {
            CancelOutcome::NotPending
        } else {
            CancelOutcome::Cancelled
        }
    }
    pub(super) fn enqueue_cancellation(self: &Arc<Self>) {
        if let Some(cancellations) = self.cancellations.upgrade() {
            cancellations.lock().unwrap().push_back(Cancellation {
                id: self.id,
                _cell: Arc::clone(self),
            });
        }
    }
    pub fn check(&self) -> io::Result<()> {
        if !self.device_open.load(Ordering::Acquire) {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        if self.cancelled.load(Ordering::Acquire) {
            return Err(io::ErrorKind::Interrupted.into());
        }
        if self
            .options
            .policy
            .deadline
            .is_some_and(|d| d <= Instant::now())
        {
            return Err(io::ErrorKind::TimedOut.into());
        }
        Ok(())
    }
}

pub(super) enum Preparation {
    Awaiting,
    Reserved,
    Prepared(Operation),
    Published,
}
pub(crate) struct Admission {
    pub(super) shared: Arc<Shared>,
    pub(super) cell: Arc<ControlCell>,
    pub(super) state: Preparation,
    pub(super) request: Option<Box<dyn RequestCompletion>>,
    control_taken: bool,
}
impl Admission {
    pub(crate) fn register_completion(
        &mut self,
        request: Box<dyn RequestCompletion>,
    ) -> io::Result<()> {
        if self.request.is_some() {
            return Err(io::ErrorKind::AlreadyExists.into());
        }
        self.request = Some(request);
        Ok(())
    }

    pub(super) fn begin(shared: Arc<Shared>, options: RequestOptions) -> io::Result<Self> {
        if options.length == 0
            || options.window.start > options.window.end
            || options.window.end > options.length
            || std::alloc::Layout::from_size_align(options.length, options.alignment).is_err()
        {
            return Err(invalid("invalid request allocation or byte window"));
        }
        let mut gate = shared.gate.lock().unwrap();
        if gate.phase != ServicePhase::Active
            || !gate
                .devices
                .get(&options.device)
                .is_some_and(|open| open.load(Ordering::Acquire))
        {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        let mut ledger = shared.ledger.lock().unwrap();
        if options.length > ledger.limits.max_bytes {
            return Err(invalid("request cannot fit the byte limit"));
        }
        if ledger.usage.records >= ledger.limits.max_records {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        let id = NEXT_ID
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
            .map(RequestId)
            .map_err(|_| io::Error::other("request identifiers exhausted"))?;
        ledger.usage.records += 1;
        let cell = Arc::new(ControlCell {
            id,
            device_open: Arc::clone(&gate.devices[&options.device]),
            cancellations: Arc::downgrade(&shared.cancellations),
            options,
            charge: Charge {
                ledger: Arc::clone(&shared.ledger),
                bytes: AtomicUsize::new(0),
                wake: Arc::downgrade(&shared.wake),
                #[cfg(any(test, feature = "ownership-test-support"))]
                drop_witness: crate::drop_witness::DropRegistration::default(),
            },
            phase: AtomicU8::new(WAITING),
            cancelled: Arc::new(AtomicBool::new(false)),
            wake: Arc::downgrade(&shared.wake),
        });
        let reserved = gate.waiting.is_empty() && ledger.reserve_bytes(&cell);
        if !reserved {
            gate.enqueue_waiter(Arc::clone(&cell), &mut ledger);
        }
        drop(ledger);
        drop(gate);
        if !reserved {
            shared.wake.notify();
        }
        Ok(Self {
            shared,
            cell,
            state: Preparation::Awaiting,
            request: None,
            control_taken: false,
        })
    }
    pub(crate) fn lifetime(&self) -> RequestLifetime {
        RequestLifetime::reserved(Arc::clone(&self.cell))
    }
    #[cfg(test)]
    pub(super) fn ready(&mut self) -> io::Result<bool> {
        self.check_open()?;
        if self.cell.phase.load(Ordering::Acquire) == RESERVED {
            self.state = Preparation::Reserved;
            return Ok(true);
        }
        self.reserve_capacity();
        if self.cell.phase.load(Ordering::Acquire) == RESERVED {
            self.state = Preparation::Reserved;
            return Ok(true);
        }
        Ok(false)
    }
    pub(crate) fn id(&self) -> RequestId {
        self.cell.id
    }
    pub(crate) fn take_control(&mut self) -> io::Result<RequestControl> {
        if self.control_taken {
            return Err(invalid("request control already taken"));
        }
        self.control_taken = true;
        Ok(RequestControl {
            cell: Arc::clone(&self.cell),
            armed: true,
        })
    }
    pub(crate) fn wait_capacity(&mut self) -> io::Result<()> {
        if !matches!(self.state, Preparation::Awaiting) {
            return Err(invalid("admission has already advanced"));
        }
        loop {
            self.check_open()?;
            if self.cell.phase.load(Ordering::Acquire) == RESERVED {
                self.state = Preparation::Reserved;
                return Ok(());
            }
            let epoch = self.shared.wake.epoch();
            self.reserve_capacity();
            if self.cell.phase.load(Ordering::Acquire) == RESERVED {
                self.state = Preparation::Reserved;
                return Ok(());
            }
            let driver = self.shared.driver.lock().unwrap().upgrade();
            if let Some(driver) = driver {
                Driver::progress_inline(&Arc::downgrade(&driver));
                self.reserve_capacity();
                if self.cell.phase.load(Ordering::Acquire) == RESERVED {
                    self.state = Preparation::Reserved;
                    return Ok(());
                }
            }
            let wait = self
                .cell
                .options
                .policy
                .deadline
                .map(|d| d.saturating_duration_since(Instant::now()));
            self.shared.wake.wait(epoch, wait);
        }
    }
    fn reserve_capacity(&self) {
        let wake_next = {
            let mut gate = self.shared.gate.lock().unwrap();
            let first = gate.waiting.first_key_value().map(|(&id, _)| id);
            if first == Some(self.cell.id) {
                let mut ledger = self.shared.ledger.lock().unwrap();
                if ledger.reserve_bytes(&self.cell) {
                    gate.remove_waiter(&self.cell, &mut ledger);
                    !gate.waiting.is_empty()
                } else {
                    false
                }
            } else {
                false
            }
        };
        if wake_next {
            self.shared.wake.notify();
        }
    }
    fn check_open(&self) -> io::Result<()> {
        self.cell.check()?;
        if !self.shared.accepting.load(Ordering::Acquire) {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        Ok(())
    }
    pub(crate) fn prepare(
        &mut self,
        target: Target,
        direction: Direction,
        bytes: &[u8],
    ) -> io::Result<()> {
        self.prepare_with(target, direction, bytes, |len, alignment| match direction {
            Direction::Read => AlignedBuffer::uninitialized(len, alignment),
            Direction::Write => AlignedBuffer::from_slice(bytes, alignment),
        })
    }
    pub(super) fn prepare_with(
        &mut self,
        target: Target,
        direction: Direction,
        bytes: &[u8],
        make_buffer: impl FnOnce(usize, usize) -> io::Result<AlignedBuffer>,
    ) -> io::Result<()> {
        if !matches!(self.state, Preparation::Reserved) {
            return Err(invalid("buffer capacity has not been reserved"));
        }
        self.check_open()?;
        let options = &self.cell.options;
        if (direction == Direction::Read && !bytes.is_empty())
            || (direction == Direction::Write && bytes.len() != options.length)
        {
            return Err(invalid(
                "write source must match the reserved aligned length",
            ));
        }
        let buffer = make_buffer(options.length, options.alignment)?;
        if buffer.len() != options.length {
            return Err(invalid("allocation does not match reservation"));
        }
        self.prepare_buffer(target, direction, buffer)
    }
    fn prepare_buffer(
        &mut self,
        target: Target,
        direction: Direction,
        buffer: AlignedBuffer,
    ) -> io::Result<()> {
        let options = &self.cell.options;
        let operation = match target {
            Target::File { file, offset } => match direction {
                Direction::Read => Operation::read(file, buffer, offset)?,
                Direction::Write => Operation::write(file, buffer, offset)?,
            },
            Target::Named { file, offset } => file.operation(buffer, offset, direction)?,
            Target::Block(region) => {
                let (file, offset, length) = region.file_region();
                if options.length as u64 > length {
                    return Err(invalid("buffer exceeds block region"));
                }
                let operation = match direction {
                    Direction::Read => Operation::read(file, buffer, offset)?,
                    Direction::Write => Operation::write(file, buffer, offset)?,
                };
                operation.keep_alive(region)
            }
        };
        self.state = Preparation::Prepared(operation.with_control(
            Arc::clone(&self.cell.cancelled),
            self.cell.options.policy.deadline,
        ));
        Ok(())
    }
    pub(crate) fn set_worker_fault(&mut self, fault: file_io::WorkerFault) -> io::Result<()> {
        if self.cell.options.execution != Execution::WorkerRequired
            || !matches!(self.state, Preparation::Prepared(_))
        {
            return Err(invalid("worker fault requires a prepared worker request"));
        }
        let Preparation::Prepared(operation) =
            std::mem::replace(&mut self.state, Preparation::Reserved)
        else {
            unreachable!()
        };
        self.state = Preparation::Prepared(operation.with_worker_fault(fault));
        Ok(())
    }

    pub(crate) fn publish(&mut self) -> io::Result<RequestId> {
        if self.request.is_none() {
            return Err(invalid("request has no completion owner"));
        }
        self.cell.check()?;
        if !matches!(self.state, Preparation::Prepared(_)) {
            return Err(invalid("request is not prepared"));
        }
        {
            let route = self
                .shared
                .worker_routes
                .lock()
                .unwrap()
                .get(&self.cell.options.device)
                .cloned();
            if let Some(route) = route.filter(|route| {
                self.cell.options.execution == Execution::WorkerRequired || !route.native_available
            }) {
                self.check_open()?;
                if let Some(cost) = route.counters.reserve_direct(self.cell.options.length) {
                    if let Err(error) = self.check_open() {
                        route.counters.release_active(cost, true);
                        self.shared.wake.notify();
                        return Err(error);
                    }
                    let Preparation::Prepared(operation) =
                        std::mem::replace(&mut self.state, Preparation::Published)
                    else {
                        unreachable!()
                    };
                    let id = self.id();
                    self.cell.phase.store(PUBLISHED, Ordering::Release);
                    let direct = engine::DirectRequest::new(
                        &self.shared,
                        &route,
                        Arc::clone(&self.cell),
                        cost,
                        self.request.take(),
                    );
                    let submitted = route
                        .backend()
                        .submit(operation.with_completion_context(direct));
                    match submitted {
                        Ok(_) => return Ok(id),
                        Err(failure) => {
                            let mut operation = failure.operation;
                            let mut direct = operation
                                .take_completion_context::<engine::DirectRequest>()
                                .expect("direct submission lost its completion context");
                            if failure.error.kind() == io::ErrorKind::WouldBlock {
                                direct.release_active();
                                self.request = direct.request.take();
                                self.state = Preparation::Prepared(operation);
                                return self.publish_driver();
                            }
                            direct.submit_failed(operation, failure.error);
                            return Ok(id);
                        }
                    }
                }
            }
        }
        self.publish_driver()
    }

    pub(super) fn publish_driver(&mut self) -> io::Result<RequestId> {
        let driver = self
            .shared
            .driver
            .lock()
            .unwrap()
            .upgrade()
            .ok_or(io::ErrorKind::BrokenPipe)?;
        let mut driver = driver.lock().unwrap();
        let gate = self.shared.gate.lock().unwrap();
        self.cell.check()?;
        if gate.phase != ServicePhase::Active
            || !gate
                .devices
                .get(&self.cell.options.device)
                .is_some_and(|open| open.load(Ordering::Acquire))
        {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        let Preparation::Prepared(operation) =
            std::mem::replace(&mut self.state, Preparation::Published)
        else {
            unreachable!()
        };
        self.cell.phase.store(PUBLISHED, Ordering::Release);
        driver.accept(Incoming {
            operation,
            cell: Arc::clone(&self.cell),
            request: self.request.take(),
        });
        driver.dispatch(Instant::now());
        drop(gate);
        drop(driver);
        let driver = self.shared.driver.lock().unwrap().upgrade();
        if let Some(driver) = driver {
            Driver::flush(&driver);
        }
        Ok(self.id())
    }
}
impl Drop for Admission {
    fn drop(&mut self) {
        if !matches!(self.state, Preparation::Published) {
            self.cell.phase.store(ABORTED, Ordering::Release);
            let removed = {
                let mut gate = self.shared.gate.lock().unwrap();
                let mut ledger = self.shared.ledger.lock().unwrap();
                gate.remove_waiter(&self.cell, &mut ledger)
            };
            drop(removed);
            self.shared.wake.notify();
        }
    }
}
