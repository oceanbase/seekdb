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

use super::{Direction, Operation, Retired, Shared, SubmissionId};
use crate::AlignedBuffer;
use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::sync::Arc;

/// Linux UAPI layout, including the endian-dependent key/flags word.
#[repr(C)]
#[derive(Default)]
struct IoControl {
    data: u64,
    #[cfg(target_endian = "little")]
    key: u32,
    flags: u32,
    #[cfg(target_endian = "big")]
    key: u32,
    opcode: u16,
    priority: i16,
    fd: u32,
    buffer: u64,
    length: u64,
    offset: i64,
    reserved: u64,
    aio_flags: u32,
    event_fd: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct IoEvent {
    data: u64,
    object: u64,
    result: i64,
    result2: i64,
}

const _: () = assert!(std::mem::size_of::<IoControl>() == 64);
const _: () = assert!(std::mem::size_of::<IoEvent>() == 32);

pub(super) struct EventFd(OwnedFd);

impl EventFd {
    fn new() -> io::Result<Self> {
        // SAFETY: eventfd has no pointer arguments and returns a new owned descriptor.
        let fd = unsafe { libc::eventfd(0, libc::EFD_CLOEXEC | libc::EFD_NONBLOCK) };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: successful eventfd transfers the unique descriptor to this owner.
        Ok(Self(unsafe { OwnedFd::from_raw_fd(fd) }))
    }

    pub(super) fn notify(&self) {
        let value = 1u64;
        loop {
            // SAFETY: the live descriptor accepts exactly eight initialized bytes.
            let result =
                unsafe { libc::write(self.0.as_raw_fd(), (&value as *const u64).cast(), 8) };
            if result >= 0 || io::Error::last_os_error().kind() != io::ErrorKind::Interrupted {
                // EAGAIN means a wake is already pending. The descriptor remains owned here.
                return;
            }
        }
    }

    fn drain(&self) -> io::Result<()> {
        let mut value = 0u64;
        loop {
            // SAFETY: the live eventfd writes exactly eight bytes into value.
            let result =
                unsafe { libc::read(self.0.as_raw_fd(), (&mut value as *mut u64).cast(), 8) };
            if result >= 0 {
                return Ok(());
            }
            let error = io::Error::last_os_error();
            match error.kind() {
                io::ErrorKind::Interrupted => (),
                io::ErrorKind::WouldBlock => return Ok(()),
                _ => return Err(error),
            }
        }
    }

    fn wait(&self) -> io::Result<()> {
        let mut fd = libc::pollfd {
            fd: self.0.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        };
        loop {
            // SAFETY: fd is one initialized pollfd and remains live during the blocking call.
            let result = unsafe { libc::poll(&mut fd, 1, -1) };
            if result >= 0 {
                if fd.revents & (libc::POLLNVAL | libc::POLLERR | libc::POLLHUP) != 0 {
                    return Err(io::Error::other("native AIO wake descriptor failed"));
                }
                return Ok(());
            }
            let error = io::Error::last_os_error();
            if error.kind() != io::ErrorKind::Interrupted {
                return Err(error);
            }
        }
    }
}

struct Pending {
    control: IoControl,
    operation: Operation,
}

pub(super) struct Context {
    kernel: KernelContext,
    pub(super) wake: Arc<EventFd>,
}

impl Context {
    pub(super) fn new(depth: usize) -> io::Result<Self> {
        let wake = Arc::new(EventFd::new()?);
        let mut kernel = KernelContext::new(depth)?;
        kernel.setup(depth)?;
        Ok(Self { kernel, wake })
    }

    pub(super) fn run(mut self, shared: Arc<Shared>) -> io::Result<()> {
        let result = self.drive(&shared);
        if let Err(error) = &result {
            shared.fail(error);
        }
        // io_destroy waits for all kernel accesses before pending operations can move or drop.
        self.kernel.destroy()?;
        for (id, pending) in self.kernel.pending.drain() {
            shared.complete(
                id,
                pending.operation,
                Err(io::Error::other(
                    "native AIO context stopped before completion",
                )),
            );
        }
        let mut state = shared.lock();
        while let Some((id, operation)) = state.work.pop_front() {
            drop(state);
            shared.complete(
                id,
                operation,
                Err(io::Error::other(
                    "native AIO context stopped before submission",
                )),
            );
            state = shared.lock();
        }
        result
    }

    fn drive(&mut self, shared: &Shared) -> io::Result<()> {
        loop {
            self.wake.drain()?;
            loop {
                let work = shared.lock().work.pop_front();
                let Some((id, operation)) = work else {
                    break;
                };
                self.submit(id, operation, shared)?;
            }
            for _ in 0..shared.depth {
                let mut state = shared.lock();
                let Some(id) = state.cancel.iter().next().copied() else {
                    break;
                };
                state.cancel.remove(&id);
                drop(state);
                self.cancel(id, shared)?;
            }
            self.reap(shared)?;
            {
                let state = shared.lock();
                if !state.accepting && state.work.is_empty() && self.kernel.pending.is_empty() {
                    return Ok(());
                }
                if !state.work.is_empty() || !state.cancel.is_empty() {
                    continue;
                }
            }
            self.wake.wait()?;
        }
    }

    fn submit(
        &mut self,
        id: SubmissionId,
        operation: Operation,
        shared: &Shared,
    ) -> io::Result<()> {
        if let Some(retired) = self
            .kernel
            .submit(id, operation, Some(self.wake.0.as_raw_fd()))?
        {
            shared.complete(id, retired.operation, retired.result);
        }
        Ok(())
    }

    fn cancel(&mut self, id: SubmissionId, shared: &Shared) -> io::Result<()> {
        let Some(pending) = self.kernel.pending.get_mut(&id) else {
            return Ok(());
        };
        let mut event = IoEvent::default();
        let result = syscall_retry(|| {
            // SAFETY: pending retains the originally submitted control; event is writable UAPI storage.
            unsafe {
                libc::syscall(
                    libc::SYS_io_cancel,
                    self.kernel.id,
                    &mut pending.control,
                    &mut event,
                )
            }
        });
        if matches!(result, Ok(0)) {
            // Successful cancellation returns its terminal event directly, outside the event queue.
            self.finish(event, shared)?;
        }
        // EAGAIN/EINPROGRESS and other failures are not proof that kernel access ended.
        Ok(())
    }

    fn reap(&mut self, shared: &Shared) -> io::Result<()> {
        let mut events = [IoEvent::default(); 64];
        loop {
            let count = self.kernel.events(&mut events, false)?;
            for event in &events[..count] {
                self.finish(*event, shared)?;
            }
            if count < events.len() {
                return Ok(());
            }
        }
    }

    fn finish(&mut self, event: IoEvent, shared: &Shared) -> io::Result<()> {
        let (id, retired) = self.kernel.finish(event)?;
        shared.complete(id, retired.operation, retired.result);
        Ok(())
    }
}

struct KernelContext {
    id: libc::c_ulong,
    pending: HashMap<SubmissionId, Box<Pending>>,
    #[cfg(test)]
    before_destroy: Option<Box<dyn FnMut() -> io::Result<()> + Send>>,
}

impl KernelContext {
    fn new(depth: usize) -> io::Result<Self> {
        let mut pending = HashMap::new();
        pending.try_reserve(depth).map_err(io::Error::other)?;
        Ok(Self {
            id: 0,
            pending,
            #[cfg(test)]
            before_destroy: None,
        })
    }

    fn setup(&mut self, depth: usize) -> io::Result<()> {
        syscall_retry(|| {
            #[cfg(test)]
            if let Some(result) = tests::inject(tests::Call::Setup) {
                return result;
            }
            // SAFETY: id is zero-initialized aio_context_t storage and depth fits u32.
            unsafe { libc::syscall(libc::SYS_io_setup, depth as libc::c_uint, &mut self.id) }
        })?;
        Ok(())
    }

    fn submit(
        &mut self,
        id: SubmissionId,
        mut operation: Operation,
        event_fd: Option<libc::c_int>,
    ) -> io::Result<Option<Retired>> {
        if operation.remaining_len() == 0 {
            return Ok(Some(Retired {
                operation,
                result: Ok(0),
            }));
        }
        let control = IoControl {
            data: id.0,
            opcode: match operation.direction {
                Direction::Read => 0,
                Direction::Write => 1,
            },
            fd: operation.file.as_file().as_raw_fd() as u32,
            buffer: operation.buffer.as_mut_slice()[operation.start..].as_mut_ptr() as u64,
            length: operation.remaining_len() as u64,
            offset: operation.offset as i64,
            aio_flags: u32::from(event_fd.is_some()),
            event_fd: event_fd.unwrap_or(0) as u32,
            ..IoControl::default()
        };
        let pending = self
            .pending
            .entry(id)
            .or_insert_with(|| Box::new(Pending { control, operation }));
        let mut pointer = &mut pending.control as *mut IoControl;
        let submitted = syscall_retry(|| {
            #[cfg(test)]
            if let Some(result) = tests::inject(tests::Call::Submit) {
                return result;
            }
            // SAFETY: the boxed control and its owned buffer/file remain in pending until retirement.
            unsafe {
                libc::syscall(
                    libc::SYS_io_submit,
                    self.id,
                    1 as libc::c_long,
                    &mut pointer,
                )
            }
        });
        match submitted {
            Ok(1) => (),
            Ok(count) if count != 0 => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "native AIO returned invalid submission count",
                ))
            }
            result => {
                let error = match result {
                    Err(error) => error,
                    _ => io::Error::from_raw_os_error(libc::EAGAIN),
                };
                let pending = self.pending.remove(&id).expect("inserted submission");
                return Ok(Some(Retired {
                    operation: pending.operation,
                    result: Err(error),
                }));
            }
        }
        Ok(None)
    }

    fn finish(&mut self, event: IoEvent) -> io::Result<(SubmissionId, Retired)> {
        let id = SubmissionId(event.data);
        let Some(pending) = self.pending.get(&id) else {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "unknown native AIO completion identifier",
            ));
        };
        let pointer = &pending.control as *const IoControl as u64;
        if event.object != pointer
            || event.result2 != 0
            || event.result < -4095
            || (event.result >= 0 && event.result as u64 > pending.operation.remaining_len() as u64)
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "malformed native AIO completion",
            ));
        }
        let result = if event.result < 0 {
            Err(io::Error::from_raw_os_error(-event.result as i32))
        } else {
            Ok(event.result as usize)
        };
        let pending = self.pending.remove(&id).expect("validated completion");
        Ok((
            id,
            Retired {
                operation: pending.operation,
                result,
            },
        ))
    }

    fn events(&mut self, events: &mut [IoEvent], wait: bool) -> io::Result<usize> {
        let mut timeout = libc::timespec {
            tv_sec: 0,
            tv_nsec: 0,
        };
        let count = syscall_retry(|| {
            #[cfg(test)]
            if let Some(result) = tests::inject(tests::Call::Events) {
                return result;
            }
            // SAFETY: events and timeout are writable UAPI storage; the context is live.
            unsafe {
                libc::syscall(
                    libc::SYS_io_getevents,
                    self.id,
                    libc::c_long::from(wait),
                    events.len() as libc::c_long,
                    events.as_mut_ptr(),
                    if wait {
                        std::ptr::null_mut()
                    } else {
                        &mut timeout
                    },
                )
            }
        })? as usize;
        if count > events.len() || (wait && count == 0) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "native AIO returned invalid completion count",
            ));
        }
        #[cfg(test)]
        tests::alter_events(&mut events[..count]);
        Ok(count)
    }

    fn destroy(&mut self) -> io::Result<()> {
        if self.id == 0 {
            return Ok(());
        }
        #[cfg(test)]
        if let Some(before_destroy) = &mut self.before_destroy {
            before_destroy()?;
        }
        syscall_retry(|| {
            #[cfg(test)]
            if let Some(result) = tests::inject(tests::Call::Destroy) {
                return result;
            }
            // SAFETY: this is the unique context owner. io_destroy waits for uncancelled operations.
            unsafe { libc::syscall(libc::SYS_io_destroy, self.id) }
        })?;
        self.id = 0;
        Ok(())
    }
}

impl Drop for KernelContext {
    fn drop(&mut self) {
        while self.destroy().is_err() {
            std::thread::park_timeout(std::time::Duration::from_secs(1));
        }
    }
}

pub(crate) fn probe(file: Arc<File>, buffer: AlignedBuffer) -> io::Result<bool> {
    let mut kernel = KernelContext::new(1)?;
    if let Err(error) = kernel.setup(1) {
        return if error.raw_os_error() == Some(libc::ENOSYS) {
            Ok(false)
        } else {
            Err(error)
        };
    }
    probe_with_context(kernel, file, buffer)
}

fn probe_with_context(
    mut kernel: KernelContext,
    file: Arc<File>,
    mut buffer: AlignedBuffer,
) -> io::Result<bool> {
    let result = (|| {
        buffer.as_mut_slice().fill(0x36);
        let operation = Operation::write(file, buffer, 0)?;
        let mut operation = probe_transfer(&mut kernel, operation)?;
        operation.buffer.as_mut_slice().fill(0);
        operation.direction = Direction::Read;
        let operation = probe_transfer(&mut kernel, operation)?;
        #[cfg(test)]
        let operation = {
            let mut operation = operation;
            tests::alter_read(&mut operation.buffer);
            operation
        };
        if operation.buffer.as_slice().iter().any(|&byte| byte != 0x36) {
            return Err(io::Error::from_raw_os_error(libc::EIO));
        }
        Ok(true)
    })();
    let result = match result {
        Err(error) if crate::capability::unsupported(&error) => Ok(false),
        result => result,
    };
    let destroyed = kernel.destroy();
    drop(kernel);
    result
        .and_then(|supported| destroyed.map(|()| supported))
        .map_err(|error| {
            if error.kind() == io::ErrorKind::InvalidData {
                io::Error::from_raw_os_error(libc::EIO)
            } else {
                error
            }
        })
}

fn probe_transfer(kernel: &mut KernelContext, operation: Operation) -> io::Result<Operation> {
    let expected = operation.remaining_len();
    let retired = match kernel.submit(SubmissionId(1), operation, None)? {
        Some(retired) => retired,
        None => {
            let mut events = [IoEvent::default()];
            kernel.events(&mut events, true)?;
            kernel.finish(events[0])?.1
        }
    };
    if retired.result? != expected {
        return Err(io::Error::from_raw_os_error(libc::EIO));
    }
    Ok(retired.operation)
}

fn syscall_retry(mut call: impl FnMut() -> libc::c_long) -> io::Result<libc::c_long> {
    loop {
        let result = call();
        if result >= 0 {
            return Ok(result);
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

#[cfg(test)]
#[path = "native_tests.rs"]
pub(crate) mod tests;
