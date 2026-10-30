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

use crate::{read_at, write_at, AlignedBuffer, ExclusiveFile};
use std::collections::{HashSet, VecDeque};
use std::fs::File;
use std::io;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::Instant;

#[cfg(feature = "ownership-test-support")]
pub mod benchmark_hold;

#[cfg(target_os = "linux")]
pub(crate) mod native;
#[cfg(test)]
mod tests;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Direction {
    Read,
    Write,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WorkerFault {
    delay: std::time::Duration,
    fail: bool,
}

static SKIP_WORKER_DELAY: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub fn toggle_worker_fault_delay() {
    SKIP_WORKER_DELAY.fetch_xor(true, Ordering::Relaxed);
}

impl WorkerFault {
    pub fn new(delay: std::time::Duration, fail: bool) -> Self {
        Self { delay, fail }
    }

    fn apply(&self) -> io::Result<()> {
        let mut remaining = self.delay;
        while !remaining.is_zero() && !SKIP_WORKER_DELAY.load(Ordering::Relaxed) {
            let interval = remaining.min(std::time::Duration::from_millis(10));
            thread::sleep(interval);
            remaining -= interval;
        }
        if self.fail {
            Err(io::Error::other("injected worker I/O failure"))
        } else {
            Ok(())
        }
    }
}

pub struct FileOwner(FileOwnerKind);

enum FileOwnerKind {
    Ordinary(Arc<File>),
    Exclusive(Arc<ExclusiveFile>),
}

impl From<Arc<File>> for FileOwner {
    fn from(file: Arc<File>) -> Self {
        Self(FileOwnerKind::Ordinary(file))
    }
}

impl From<Arc<ExclusiveFile>> for FileOwner {
    fn from(file: Arc<ExclusiveFile>) -> Self {
        Self(FileOwnerKind::Exclusive(file))
    }
}

impl FileOwner {
    fn as_file(&self) -> &File {
        match &self.0 {
            FileOwnerKind::Ordinary(file) => file,
            FileOwnerKind::Exclusive(file) => file.as_file(),
        }
    }
}

/// Owns the entire allocation, including any prefix completed by earlier attempts.
pub struct Operation {
    file: FileOwner,
    buffer: AlignedBuffer,
    direction: Direction,
    start: usize,
    offset: u64,
    keep_alive: Option<Box<dyn Send>>,
    worker_fault: Option<WorkerFault>,
    #[cfg(feature = "ownership-test-support")]
    benchmark_hold: Option<Arc<benchmark_hold::BenchmarkHold>>,
}

impl std::fmt::Debug for Operation {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Operation")
            .field("direction", &self.direction)
            .field("offset", &self.offset)
            .field("remaining", &self.remaining_len())
            .finish_non_exhaustive()
    }
}

impl Operation {
    pub fn read(
        file: impl Into<FileOwner>,
        buffer: AlignedBuffer,
        offset: u64,
    ) -> io::Result<Self> {
        Self::new(file.into(), buffer, offset, Direction::Read)
    }

    pub fn write(
        file: impl Into<FileOwner>,
        buffer: AlignedBuffer,
        offset: u64,
    ) -> io::Result<Self> {
        Self::new(file.into(), buffer, offset, Direction::Write)
    }

    fn new(
        file: FileOwner,
        buffer: AlignedBuffer,
        offset: u64,
        direction: Direction,
    ) -> io::Result<Self> {
        let end = offset.checked_add(buffer.as_slice().len() as u64);
        if end.is_none_or(|end| end > i64::MAX as u64) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "file range exceeds signed offsets",
            ));
        }
        Ok(Self {
            file,
            buffer,
            direction,
            start: 0,
            offset,
            keep_alive: None,
            worker_fault: None,
            #[cfg(feature = "ownership-test-support")]
            benchmark_hold: None,
        })
    }

    /// Moves an additional owner with the physical operation, including through backend failure.
    pub fn keep_alive<T: Send + 'static>(mut self, resource: T) -> Self {
        self.keep_alive = Some(Box::new((self.keep_alive.take(), resource)));
        self
    }

    #[cfg(feature = "ownership-test-support")]
    pub fn with_benchmark_hold(mut self, hold: Arc<benchmark_hold::BenchmarkHold>) -> Self {
        self.benchmark_hold = Some(hold);
        self
    }

    pub fn with_worker_fault(mut self, fault: WorkerFault) -> Self {
        self.worker_fault = (!fault.delay.is_zero() || fault.fail).then_some(fault);
        self
    }

    /// An operation is accessible to its caller only before submission or after physical retirement.
    pub fn release_keep_alive(&mut self) {
        self.keep_alive = None;
    }

    pub fn buffer(&self) -> &AlignedBuffer {
        &self.buffer
    }
    pub fn buffer_mut(&mut self) -> &mut AlignedBuffer {
        &mut self.buffer
    }
    pub fn direction(&self) -> Direction {
        self.direction
    }
    pub fn offset(&self) -> u64 {
        self.offset
    }
    pub fn remaining_len(&self) -> usize {
        self.buffer.as_slice().len() - self.start
    }

    pub fn advance(mut self, count: usize) -> io::Result<Self> {
        if count > self.remaining_len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "advance exceeds remaining buffer",
            ));
        }
        self.start += count;
        self.offset += count as u64;
        Ok(self)
    }

    fn execute(&mut self) -> io::Result<usize> {
        if let Some(fault) = self.worker_fault {
            fault.apply()?;
        }
        match self.direction {
            Direction::Read => read_at(
                self.file.as_file(),
                &mut self.buffer.as_mut_slice()[self.start..],
                self.offset,
            ),
            Direction::Write => write_at(
                self.file.as_file(),
                &self.buffer.as_slice()[self.start..],
                self.offset,
            ),
        }
    }
}

/// Unique across backend instances, never reused after retirement.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SubmissionId(u64);

static NEXT_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Debug)]
pub struct SubmitFailure {
    pub operation: Operation,
    pub error: io::Error,
}

#[derive(Debug)]
pub struct Retired {
    pub operation: Operation,
    pub result: io::Result<usize>,
}

#[derive(Clone, Copy, Debug)]
pub enum BackendKind {
    Worker { threads: usize },
    NativeAio,
}

struct State {
    accepting: bool,
    discard: bool,
    workers: usize,
    failure: Option<String>,
    admitted: HashSet<SubmissionId>,
    work: VecDeque<(SubmissionId, Operation)>,
    cancel: HashSet<SubmissionId>,
    completed: VecDeque<(SubmissionId, Retired)>,
}

struct Shared {
    depth: usize,
    state: Mutex<State>,
    work_available: Condvar,
    completion_available: Condvar,
    #[cfg(target_os = "linux")]
    wake: Option<Arc<native::EventFd>>,
}

impl Shared {
    fn lock(&self) -> MutexGuard<'_, State> {
        self.state.lock().unwrap_or_else(|error| error.into_inner())
    }

    fn wake(&self) {
        self.work_available.notify_all();
        self.completion_available.notify_all();
        #[cfg(target_os = "linux")]
        if let Some(wake) = &self.wake {
            wake.notify();
        }
    }

    fn complete(&self, id: SubmissionId, operation: Operation, result: io::Result<usize>) {
        let mut state = self.lock();
        if state.discard {
            state.admitted.remove(&id);
        } else {
            state
                .completed
                .push_back((id, Retired { operation, result }));
        }
        self.completion_available.notify_one();
    }

    fn fail(&self, error: &io::Error) {
        let mut state = self.lock();
        state.accepting = false;
        state.failure = Some(error.to_string());
        self.work_available.notify_all();
        self.completion_available.notify_all();
    }
}

struct WorkerExit(Arc<Shared>);

impl Drop for WorkerExit {
    fn drop(&mut self) {
        let mut state = self.0.lock();
        state.workers -= 1;
        if thread::panicking() {
            state.accepting = false;
            state.failure = Some("I/O retirement thread panicked".into());
        }
        self.0.work_available.notify_all();
        self.0.completion_available.notify_all();
    }
}

/// Admission remains charged until the caller consumes a retirement envelope.
/// Drop stops admission and leaves all accepted work owned by retirement threads.
pub struct Backend {
    shared: Arc<Shared>,
    joins: Vec<JoinHandle<io::Result<()>>>,
    native: bool,
}

impl Backend {
    pub fn capacity(&self) -> usize {
        self.shared.depth
    }

    pub fn is_native(&self) -> bool {
        self.native
    }

    pub fn new(kind: BackendKind, depth: usize) -> io::Result<Self> {
        if depth == 0 || depth > u32::MAX as usize {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid I/O depth",
            ));
        }
        let workers = match kind {
            BackendKind::Worker { threads } if threads != 0 && threads <= depth => threads,
            BackendKind::Worker { .. } => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "worker count must be between one and depth",
                ))
            }
            BackendKind::NativeAio => 1,
        };
        #[cfg(not(target_os = "linux"))]
        if matches!(kind, BackendKind::NativeAio) {
            return Err(io::ErrorKind::Unsupported.into());
        }
        #[cfg(target_os = "linux")]
        let context = if matches!(kind, BackendKind::NativeAio) {
            Some(native::Context::new(depth)?)
        } else {
            None
        };
        let shared = Arc::new(Shared {
            depth,
            state: Mutex::new(State {
                accepting: true,
                discard: false,
                workers: 0,
                failure: None,
                admitted: HashSet::new(),
                work: VecDeque::new(),
                cancel: HashSet::new(),
                completed: VecDeque::new(),
            }),
            work_available: Condvar::new(),
            completion_available: Condvar::new(),
            #[cfg(target_os = "linux")]
            wake: context.as_ref().map(|context| context.wake.clone()),
        });
        {
            let mut state = shared.lock();
            state
                .admitted
                .try_reserve(depth)
                .map_err(io::Error::other)?;
            state.work.try_reserve(depth).map_err(io::Error::other)?;
            state.cancel.try_reserve(depth).map_err(io::Error::other)?;
            state
                .completed
                .try_reserve(depth)
                .map_err(io::Error::other)?;
        }
        let mut backend = Self {
            shared,
            joins: Vec::new(),
            native: matches!(kind, BackendKind::NativeAio),
        };
        backend
            .joins
            .try_reserve(workers)
            .map_err(io::Error::other)?;
        #[cfg(target_os = "linux")]
        if let Some(context) = context {
            backend.spawn(move |shared| context.run(shared))?;
            return Ok(backend);
        }
        for _ in 0..workers {
            backend.spawn(|shared| worker(shared, Operation::execute))?;
        }
        Ok(backend)
    }

    fn spawn(
        &mut self,
        run: impl FnOnce(Arc<Shared>) -> io::Result<()> + Send + 'static,
    ) -> io::Result<()> {
        let shared = self.shared.clone();
        shared.lock().workers += 1;
        match thread::Builder::new()
            .name("file-io-retire".into())
            .spawn(move || {
                let _exit = WorkerExit(shared.clone());
                let result = run(shared.clone());
                if let Err(error) = &result {
                    shared.fail(error);
                }
                result
            }) {
            Ok(join) => {
                self.joins.push(join);
                Ok(())
            }
            Err(error) => {
                self.shared.lock().workers -= 1;
                Err(error)
            }
        }
    }

    #[cfg_attr(
        feature = "ownership-test-support",
        allow(
            clippy::result_large_err,
            reason = "the proof-only owner enlarges Operation; rejection must return it inline"
        )
    )]
    pub fn submit(&self, operation: Operation) -> Result<SubmissionId, SubmitFailure> {
        let mut state = self.shared.lock();
        let error = if self.native && operation.worker_fault.is_some() {
            Some(io::Error::new(
                io::ErrorKind::InvalidInput,
                "worker fault requires a worker backend",
            ))
        } else if !state.accepting {
            Some(io::Error::new(
                io::ErrorKind::BrokenPipe,
                "I/O backend stopped",
            ))
        } else if state.admitted.len() >= self.shared.depth {
            Some(io::Error::new(
                io::ErrorKind::WouldBlock,
                "I/O admission is full",
            ))
        } else {
            None
        };
        if let Some(error) = error {
            return Err(SubmitFailure { operation, error });
        }
        let id = match NEXT_ID
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
        {
            Ok(id) => SubmissionId(id),
            Err(_) => {
                return Err(SubmitFailure {
                    operation,
                    error: io::Error::other("I/O submission identifiers exhausted"),
                })
            }
        };
        state.admitted.insert(id);
        state.work.push_back((id, operation));
        drop(state);
        self.shared.work_available.notify_one();
        #[cfg(target_os = "linux")]
        if let Some(wake) = &self.shared.wake {
            wake.notify();
        }
        Ok(id)
    }

    /// An error is terminal and guarantees that no accepted operation still accesses its file or buffer.
    /// A failed native teardown keeps its retirement thread alive and this call waits until the deadline.
    pub fn retire(&self, deadline: Instant) -> io::Result<Option<(SubmissionId, Retired)>> {
        let mut state = self.shared.lock();
        loop {
            if let Some((id, retired)) = state.completed.pop_front() {
                state.admitted.remove(&id);
                state.cancel.remove(&id);
                return Ok(Some((id, retired)));
            }
            if state.workers == 0 {
                return match &state.failure {
                    Some(error) => Err(io::Error::other(error.clone())),
                    None => Ok(None),
                };
            }
            let Some(wait) = deadline.checked_duration_since(Instant::now()) else {
                return Ok(None);
            };
            state = self
                .shared
                .completion_available
                .wait_timeout(state, wait)
                .unwrap_or_else(|error| error.into_inner())
                .0;
        }
    }

    /// Requests best-effort cancellation. Success does not establish physical retirement.
    pub fn request_cancel(&self, id: SubmissionId) -> io::Result<()> {
        if !self.native {
            return Err(io::ErrorKind::Unsupported.into());
        }
        let mut state = self.shared.lock();
        if !state.admitted.contains(&id) {
            return Err(io::ErrorKind::NotFound.into());
        }
        state.cancel.insert(id);
        drop(state);
        self.shared.wake();
        Ok(())
    }

    pub fn shutdown(mut self) -> io::Result<()> {
        self.stop();
        let mut result = Ok(());
        for join in self.joins.drain(..) {
            let joined = join
                .join()
                .unwrap_or_else(|_| Err(io::Error::other("I/O retirement thread panicked")));
            if result.is_ok() {
                result = joined;
            }
        }
        result
    }

    fn stop(&self) {
        let mut state = self.shared.lock();
        state.accepting = false;
        state.discard = true;
        state.completed.clear();
        drop(state);
        self.shared.wake();
    }
}

impl Drop for Backend {
    fn drop(&mut self) {
        self.stop();
    }
}

fn worker(
    shared: Arc<Shared>,
    mut execute: impl FnMut(&mut Operation) -> io::Result<usize>,
) -> io::Result<()> {
    loop {
        let mut state = shared.lock();
        let (id, mut operation) = loop {
            if let Some(work) = state.work.pop_front() {
                break work;
            }
            if !state.accepting {
                return Ok(());
            }
            state = shared
                .work_available
                .wait(state)
                .unwrap_or_else(|error| error.into_inner());
        };
        drop(state);
        #[cfg(feature = "ownership-test-support")]
        if let Some(hold) = operation.benchmark_hold.take() {
            hold.before_execute();
        }
        let result = execute(&mut operation);
        shared.complete(id, operation, result);
    }
}
