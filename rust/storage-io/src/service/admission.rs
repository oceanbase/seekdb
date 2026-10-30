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
    pub health_payload_bytes: usize,
    pub limits: AdmissionLimits,
    pub usage: [ResourceUsage; 2],
}
impl Ledger {
    pub fn debt(&self) -> [ResourceUsage; 2] {
        [RequestClass::Normal, RequestClass::Probe].map(|class| {
            let use_ = self.usage[class.index()];
            ResourceUsage {
                records: use_.records.saturating_sub(self.limits.records(class)),
                bytes: use_.bytes.saturating_sub(self.limits.bytes(class)),
            }
        })
    }
    pub fn fits_bytes(&self, class: RequestClass, bytes: usize) -> bool {
        let used = self.usage[class.index()].bytes;
        bytes <= self.limits.bytes(class).saturating_sub(used)
            && bytes
                <= self
                    .limits
                    .max_bytes
                    .saturating_sub(self.usage[0].bytes + self.usage[1].bytes)
    }
    pub(super) fn reserve_bytes(&mut self, cell: &ControlCell) -> bool {
        let class = cell.options.class;
        let bytes = cell.options.length;
        if !self.fits_bytes(class, bytes) {
            return false;
        }
        self.usage[class.index()].bytes += bytes;
        cell.charge.bytes.store(bytes, Ordering::Relaxed);
        if cell
            .phase
            .compare_exchange(WAITING, RESERVED, Ordering::Release, Ordering::Relaxed)
            .is_err()
        {
            self.usage[class.index()].bytes -= bytes;
            cell.charge.bytes.store(0, Ordering::Relaxed);
            return false;
        }
        true
    }
}
pub(super) struct Charge {
    pub ledger: Arc<Mutex<Ledger>>,
    pub class: RequestClass,
    pub bytes: AtomicUsize,
    pub wake: Weak<Wake>,
    #[cfg(any(test, feature = "ownership-test-support"))]
    pub drop_witness: crate::drop_witness::DropRegistration,
}
impl Drop for Charge {
    fn drop(&mut self) {
        #[cfg(any(test, feature = "ownership-test-support"))]
        let _witness = self.drop_witness.enter();
        let mut ledger = self.ledger.lock().unwrap();
        let used = &mut ledger.usage[self.class.index()];
        used.records -= 1;
        used.bytes -= self.bytes.load(Ordering::Relaxed);
        drop(ledger);
        if let Some(wake) = self.wake.upgrade() {
            wake.notify();
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
    _charge: Arc<Charge>,
}
pub(super) struct ControlCell {
    pub id: RequestId,
    pub options: RequestOptions,
    pub charge: Arc<Charge>,
    pub phase: AtomicU8,
    pub cancelled: Arc<AtomicBool>,
    pub device_open: Arc<AtomicBool>,
    cancellations: Weak<Mutex<VecDeque<Cancellation>>>,
    pub wake: Weak<Wake>,
}
impl ControlCell {
    pub fn cancel(&self) -> CancelOutcome {
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
    pub(super) fn enqueue_cancellation(&self) {
        if let Some(cancellations) = self.cancellations.upgrade() {
            cancellations.lock().unwrap().push_back(Cancellation {
                id: self.id,
                _charge: Arc::clone(&self.charge),
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
    control_taken: bool,
}
impl Admission {
    pub(super) fn begin(shared: Arc<Shared>, options: RequestOptions) -> io::Result<Self> {
        if options.length == 0
            || options.window.start > options.window.end
            || options.window.end > options.length
            || std::alloc::Layout::from_size_align(options.length, options.alignment).is_err()
        {
            return Err(invalid("invalid request allocation or byte window"));
        }
        let mut gate = shared.gate.lock().unwrap();
        if !gate.accepting
            || !gate
                .devices
                .get(&options.device)
                .is_some_and(|open| open.load(Ordering::Acquire))
        {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        let limits = gate
            .config
            .devices
            .get(&options.device)
            .ok_or_else(|| invalid("unknown device"))?;
        if options.class == RequestClass::Probe && options.length > limits.max_probe_bytes {
            return Err(invalid(
                "Probe exceeds the configured maximum aligned buffer",
            ));
        }
        let mut ledger = shared.ledger.lock().unwrap();
        let class = options.class;
        if options.length > ledger.limits.bytes(class) {
            return Err(invalid("request cannot fit its byte partition"));
        }
        if ledger.usage[class.index()].records >= ledger.limits.records(class)
            || ledger.usage[0].records + ledger.usage[1].records >= ledger.limits.max_records
        {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        let id = NEXT_ID
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
            .map(RequestId)
            .map_err(|_| io::Error::other("request identifiers exhausted"))?;
        ledger.usage[class.index()].records += 1;
        let cell = Arc::new(ControlCell {
            id,
            device_open: Arc::clone(&gate.devices[&options.device]),
            cancellations: Arc::downgrade(&shared.cancellations),
            options,
            charge: Arc::new(Charge {
                ledger: Arc::clone(&shared.ledger),
                class,
                bytes: AtomicUsize::new(0),
                wake: Arc::downgrade(&shared.wake),
                #[cfg(any(test, feature = "ownership-test-support"))]
                drop_witness: crate::drop_witness::DropRegistration::default(),
            }),
            phase: AtomicU8::new(WAITING),
            cancelled: Arc::new(AtomicBool::new(false)),
            wake: Arc::downgrade(&shared.wake),
        });
        let reserved = gate.waiting.iter().all(BTreeMap::is_empty) && ledger.reserve_bytes(&cell);
        if !reserved {
            gate.waiting[class.index()].insert(cell.id, Arc::clone(&cell));
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
            control_taken: false,
        })
    }
    pub(crate) fn lifetime(&self) -> RequestLifetime {
        RequestLifetime::reserved(Arc::clone(&self.cell.charge))
    }
    pub(super) fn ready(&mut self) -> io::Result<bool> {
        self.check_open()?;
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
            let epoch = self.shared.wake.epoch();
            self.check_open()?;
            self.reserve_capacity();
            if self.cell.phase.load(Ordering::Acquire) == RESERVED {
                self.state = Preparation::Reserved;
                return Ok(());
            }
            if let Some(driver) = self.shared.driver.lock().unwrap().upgrade() {
                driver.lock().unwrap().progress(None);
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
            let class = self.cell.options.class.index();
            let first = gate.waiting[class].first_key_value().map(|(&id, _)| id);
            if first == Some(self.cell.id) {
                let mut ledger = self.shared.ledger.lock().unwrap();
                if ledger.reserve_bytes(&self.cell) {
                    gate.waiting[class].remove(&self.cell.id);
                    !gate.waiting[class].is_empty()
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
        let gate = self.shared.gate.lock().unwrap();
        if !gate.accepting
            || !gate
                .devices
                .get(&self.cell.options.device)
                .is_some_and(|open| open.load(Ordering::Acquire))
        {
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
            Direction::Read => AlignedBuffer::zeroed(len, alignment),
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
        if buffer.as_slice().len() != options.length {
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
    #[cfg(feature = "cxx")]
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
    pub(super) fn publish_probe(
        &mut self,
        operation: Operation,
        pass: ProbePass,
    ) -> io::Result<RequestId> {
        let mut gate = self.shared.gate.lock().unwrap();
        self.cell.check()?;
        if !gate.accepting
            || !gate
                .devices
                .get(&self.cell.options.device)
                .is_some_and(|open| open.load(Ordering::Acquire))
        {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        self.state = Preparation::Published;
        self.cell.phase.store(PUBLISHED, Ordering::Release);
        gate.pending_probes.push_back(Incoming {
            operation,
            cell: Arc::clone(&self.cell),
            destination: engine::Destination::Diagnosis,
            owner: engine::WorkOwner::Probe(pass),
        });
        drop(gate);
        self.shared.wake.notify();
        Ok(self.id())
    }
    pub(crate) fn publish(&mut self, destination: engine::Destination) -> io::Result<RequestId> {
        self.cell.check()?;
        if !matches!(self.state, Preparation::Prepared(_)) {
            return Err(invalid("request is not prepared"));
        }
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
        if !gate.accepting
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
            destination,
            owner: engine::WorkOwner::Ordinary,
        });
        driver.dispatch(Instant::now());
        drop(gate);
        drop(driver);
        Ok(self.id())
    }
}
impl Drop for Admission {
    fn drop(&mut self) {
        if !matches!(self.state, Preparation::Published) {
            self.cell.phase.store(ABORTED, Ordering::Release);
            let removed = self.shared.gate.lock().unwrap().waiting[self.cell.options.class.index()]
                .remove(&self.cell.id);
            drop(removed);
            self.shared.wake.notify();
        }
    }
}
