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

#[derive(Default)]
pub(super) struct Wake {
    pub(super) epoch: Mutex<u64>,
    pub(super) changed: Condvar,
}
impl Wake {
    pub fn epoch(&self) -> u64 {
        *self.epoch.lock().unwrap()
    }
    pub fn notify(&self) {
        let mut epoch = self.epoch.lock().unwrap();
        *epoch = epoch.wrapping_add(1);
        self.changed.notify_all();
    }
    pub fn wait(&self, epoch: u64, timeout: Option<Duration>) {
        let current = self.epoch.lock().unwrap();
        if *current != epoch {
            return;
        }
        match timeout {
            Some(timeout) => {
                drop(self.changed.wait_timeout(current, timeout).unwrap());
            }
            None => {
                drop(self.changed.wait(current).unwrap());
            }
        }
    }
}
pub(super) struct Ledger {
    pub benchmark: BenchmarkMetadata,
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
pub(super) struct ControlCell {
    pub id: RequestId,
    pub options: RequestOptions,
    pub charge: Arc<Charge>,
    pub phase: AtomicU8,
    pub cancelled: AtomicBool,
    pub wake: Weak<Wake>,
}
impl ControlCell {
    pub fn cancel(&self) -> CancelOutcome {
        if self.phase.load(Ordering::Acquire) >= FINISHED {
            return CancelOutcome::NotPending;
        }
        let was_cancelled = self.cancelled.swap(true, Ordering::AcqRel);
        if let Some(wake) = self.wake.upgrade() {
            wake.notify();
        }
        if was_cancelled {
            CancelOutcome::NotPending
        } else {
            CancelOutcome::Cancelled
        }
    }
    pub fn check(&self) -> io::Result<()> {
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
    state: Preparation,
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
        if !gate.accepting || gate.devices.get(&options.device) != Some(&true) {
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
            cancelled: AtomicBool::new(false),
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
            if self.cell.phase.load(Ordering::Acquire) == RESERVED {
                self.state = Preparation::Reserved;
                return Ok(());
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
    fn check_open(&self) -> io::Result<()> {
        self.cell.check()?;
        let gate = self.shared.gate.lock().unwrap();
        if !gate.accepting || gate.devices.get(&self.cell.options.device) != Some(&true) {
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
        self.prepare_with(target, direction, bytes, AlignedBuffer::zeroed)
    }
    pub(super) fn prepare_with(
        &mut self,
        target: Target,
        direction: Direction,
        bytes: &[u8],
        allocate: impl FnOnce(usize, usize) -> io::Result<AlignedBuffer>,
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
        let mut buffer = allocate(options.length, options.alignment)?;
        if buffer.as_slice().len() != options.length {
            return Err(invalid("allocation does not match reservation"));
        }
        if direction == Direction::Write {
            buffer.as_mut_slice().copy_from_slice(bytes);
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
        self.state = Preparation::Prepared(operation);
        Ok(())
    }
    pub(super) fn prepare_benchmark_with(
        &mut self,
        target: Target,
        direction: Direction,
        allocate: impl FnOnce(usize, usize) -> io::Result<AlignedBuffer>,
    ) -> io::Result<()> {
        if !matches!(self.state, Preparation::Reserved) {
            return Err(invalid("benchmark admission is not reserved"));
        }
        self.check_open()?;
        let options = &self.cell.options;
        let mut buffer = allocate(options.length, options.alignment)?;
        if direction == Direction::Write {
            for (i, byte) in buffer.as_mut_slice().iter_mut().enumerate() {
                *byte = (i % 251 + 1) as u8;
            }
        }
        self.prepare_buffer(target, direction, buffer)
    }
    pub(super) fn publish_benchmark(
        &mut self,
        child: benchmark::BenchChild,
    ) -> Result<RequestId, (io::Error, benchmark::BenchChild)> {
        let mut gate = self.shared.gate.lock().unwrap();
        let check = self.cell.check().and_then(|()| {
            if !gate.accepting || gate.devices.get(&self.cell.options.device) != Some(&true) {
                Err(io::ErrorKind::BrokenPipe.into())
            } else if !matches!(self.state, Preparation::Prepared(_)) {
                Err(invalid("benchmark child is not prepared"))
            } else {
                Ok(())
            }
        });
        if let Err(error) = check {
            return Err((error, child));
        }
        let Preparation::Prepared(operation) =
            std::mem::replace(&mut self.state, Preparation::Published)
        else {
            unreachable!()
        };
        #[cfg(feature = "ownership-test-support")]
        let operation = if let Some(hold) = child
            .proof
            .as_ref()
            .and_then(|proof| proof.claim(child.identity().1, self.id().0))
        {
            operation.with_benchmark_hold(hold)
        } else {
            operation
        };
        self.cell.phase.store(PUBLISHED, Ordering::Release);
        gate.published.push_back(Incoming {
            operation,
            cell: Arc::clone(&self.cell),
            destination: engine::Destination::Benchmark,
            owner: engine::WorkOwner::Benchmark(child),
        });
        drop(gate);
        self.shared.wake.notify();
        Ok(self.id())
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
        if !gate.accepting || gate.devices.get(&self.cell.options.device) != Some(&true) {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        self.state = Preparation::Published;
        self.cell.phase.store(PUBLISHED, Ordering::Release);
        gate.published.push_back(Incoming {
            operation,
            cell: Arc::clone(&self.cell),
            destination: engine::Destination::Diagnosis,
            owner: engine::WorkOwner::Probe(pass),
        });
        drop(gate);
        self.shared.wake.notify();
        Ok(self.id())
    }
    pub(crate) fn publish(
        &mut self,
        destination: mpsc::Sender<Completion>,
    ) -> io::Result<RequestId> {
        let mut gate = self.shared.gate.lock().unwrap();
        self.cell.check()?;
        if !gate.accepting || gate.devices.get(&self.cell.options.device) != Some(&true) {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        if !matches!(self.state, Preparation::Prepared(_)) {
            return Err(invalid("request is not prepared"));
        }
        let Preparation::Prepared(operation) =
            std::mem::replace(&mut self.state, Preparation::Published)
        else {
            unreachable!()
        };
        self.cell.phase.store(PUBLISHED, Ordering::Release);
        gate.published.push_back(Incoming {
            operation,
            cell: Arc::clone(&self.cell),
            destination: engine::Destination::Caller(destination),
            owner: engine::WorkOwner::Ordinary,
        });
        drop(gate);
        self.shared.wake.notify();
        Ok(self.id())
    }
}
impl Drop for Admission {
    fn drop(&mut self) {
        if !matches!(self.state, Preparation::Published) {
            self.cell.phase.store(ABORTED, Ordering::Release);
            self.shared.wake.notify();
        }
    }
}
