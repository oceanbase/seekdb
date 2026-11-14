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

use crate::{enable_direct_io, read_at, write_at, AlignedBuffer, Transfer};
use std::alloc::Layout;
use std::fs::{File, OpenOptions};
use std::io;
use std::path::Path;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CreateMode {
    Existing,
    IfMissing,
    TruncateExisting,
    CreateOrTruncate,
    Exclusive,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IoMode {
    Buffered,
    PreferDirect,
    RequireDirect,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EffectiveMode {
    Buffered,
    Direct,
}

#[derive(Clone, Copy, Debug)]
pub struct BufferLayout {
    capacity: usize,
    alignment: usize,
}
impl BufferLayout {
    pub fn new(capacity: usize, alignment: usize) -> io::Result<Self> {
        if capacity == 0
            || !alignment.is_power_of_two()
            || !capacity.is_multiple_of(alignment)
            || Layout::from_size_align(capacity, alignment).is_err()
        {
            return Err(io::ErrorKind::InvalidInput.into());
        }
        Ok(Self {
            capacity,
            alignment,
        })
    }
}

fn range_end(offset: u64, size: usize, alignment: usize) -> io::Result<u64> {
    offset
        .checked_add(size as u64)
        .filter(|end| {
            end.checked_add(alignment as u64 - 1)
                .is_some_and(|n| n <= i64::MAX as u64)
        })
        .ok_or_else(|| io::ErrorKind::InvalidInput.into())
}

fn select_mode(file: &File, mode: IoMode) -> io::Result<EffectiveMode> {
    if mode == IoMode::Buffered {
        return Ok(EffectiveMode::Buffered);
    }
    match enable_direct_io(file) {
        Ok(()) => Ok(EffectiveMode::Direct),
        Err(e)
            if mode == IoMode::PreferDirect
                && (e.kind() == io::ErrorKind::Unsupported
                    || e.raw_os_error() == Some(libc::EINVAL)
                    || e.raw_os_error() == Some(libc::EOPNOTSUPP)) =>
        {
            Ok(EffectiveMode::Buffered)
        }
        Err(e) => Err(e),
    }
}

/// Owns a read-only file and bounded scratch space for unaligned direct reads.
pub struct Reader {
    file: File,
    layout: BufferLayout,
    scratch: Option<AlignedBuffer>,
    #[cfg(test)]
    read_failure_at: Option<usize>,
}
impl Reader {
    pub fn open(path: impl AsRef<Path>, mode: IoMode, layout: BufferLayout) -> io::Result<Self> {
        let file = File::open(path)?;
        let mode = select_mode(&file, mode)?;
        let scratch = if mode == EffectiveMode::Direct {
            Some(AlignedBuffer::zeroed(layout.capacity, layout.alignment)?)
        } else {
            None
        };
        Ok(Self {
            file,
            layout,
            scratch,
            #[cfg(test)]
            read_failure_at: None,
        })
    }
    pub fn len(&self) -> io::Result<u64> {
        Ok(self.file.metadata()?.len())
    }
    pub fn is_empty(&self) -> io::Result<bool> {
        Ok(self.len()? == 0)
    }
    pub fn effective_mode(&self) -> EffectiveMode {
        if self.scratch.is_some() {
            EffectiveMode::Direct
        } else {
            EffectiveMode::Buffered
        }
    }
    /// Returns initialized bytes together with any error. A short read, including EOF, is successful.
    pub fn read_at(&mut self, dst: &mut [u8], offset: u64) -> Transfer {
        let mut bytes = 0;
        let result = (|| {
            range_end(offset, dst.len(), self.layout.alignment)?;
            let Some(scratch) = &mut self.scratch else {
                bytes = read_at(&self.file, dst, offset)?;
                return Ok(());
            };
            while bytes < dst.len() {
                let position = offset + bytes as u64;
                let base = position / self.layout.alignment as u64 * self.layout.alignment as u64;
                let prefix = (position - base) as usize;
                let wanted = (dst.len() - bytes).min(self.layout.capacity - prefix);
                let extent = (prefix + wanted).next_multiple_of(self.layout.alignment);
                #[cfg(test)]
                if self.read_failure_at == Some(bytes) {
                    return Err(io::Error::from_raw_os_error(libc::EIO));
                }
                let count = read_at(&self.file, &mut scratch.as_mut_slice()[..extent], base)?;
                let copied = count.saturating_sub(prefix).min(wanted);
                dst[bytes..bytes + copied]
                    .copy_from_slice(&scratch.as_slice()[prefix..prefix + copied]);
                bytes += copied;
                if count < extent {
                    break;
                }
            }
            Ok(())
        })();
        Transfer { bytes, result }
    }
}

#[derive(Debug)]
pub enum AppendError {
    Rejected(io::Error),
    Failed { accepted: usize, error: io::Error },
}
impl std::fmt::Display for AppendError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Rejected(e) => write!(f, "append rejected: {e}"),
            Self::Failed { accepted, error } => {
                write!(f, "append failed after accepting {accepted} bytes: {error}")
            }
        }
    }
}
impl std::error::Error for AppendError {}

struct SavedError {
    kind: io::ErrorKind,
    os: Option<i32>,
    message: String,
}
impl SavedError {
    fn new(error: &io::Error) -> Self {
        Self {
            kind: error.kind(),
            os: error.raw_os_error(),
            message: error.to_string(),
        }
    }
    fn error(&self) -> io::Error {
        self.os.map_or_else(
            || io::Error::new(self.kind, self.message.clone()),
            io::Error::from_raw_os_error,
        )
    }
}

/// A bounded synchronous appender. Physical write failures are terminal for this owner.
/// Drop is best-effort cleanup; callers must use `finish` to observe completion errors.
pub struct Appender {
    state: State,
}
enum State {
    Open(OpenAppender),
    Failed {
        owner: OpenAppender,
        cause: SavedError,
    },
    Closed {
        position: u64,
        mode: EffectiveMode,
        error: Option<SavedError>,
    },
}
struct OpenAppender {
    file: File,
    buffer: AlignedBuffer,
    layout: BufferLayout,
    mode: EffectiveMode,
    logical_end: u64,
    written_end: u64,
    physical_extent: u64,
    base: u64,
    #[cfg(test)]
    faults: Faults,
}

impl Appender {
    pub fn open(
        path: impl AsRef<Path>,
        create: CreateMode,
        mode: IoMode,
        layout: BufferLayout,
    ) -> io::Result<Self> {
        let mut options = OpenOptions::new();
        options.write(true).read(mode != IoMode::Buffered);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o644);
        }
        match create {
            CreateMode::Existing => (),
            CreateMode::IfMissing => {
                options.create(true);
            }
            CreateMode::TruncateExisting => {
                options.truncate(true);
            }
            CreateMode::CreateOrTruncate => {
                options.create(true).truncate(true);
            }
            CreateMode::Exclusive => {
                options.create_new(true);
            }
        }
        let file = options.open(path)?;
        let mode = select_mode(&file, mode)?;
        let logical_end = file.metadata()?.len();
        range_end(logical_end, 0, layout.alignment)?;
        let mut buffer = AlignedBuffer::zeroed(layout.capacity, layout.alignment)?;
        let base = if mode == EffectiveMode::Direct {
            logical_end / layout.alignment as u64 * layout.alignment as u64
        } else {
            logical_end
        };
        if base != logical_end {
            let count = read_at(&file, &mut buffer.as_mut_slice()[..layout.alignment], base)?;
            if count != (logical_end - base) as usize {
                return Err(io::ErrorKind::UnexpectedEof.into());
            }
        }
        Ok(Self {
            state: State::Open(OpenAppender {
                file,
                buffer,
                layout,
                mode,
                logical_end,
                written_end: logical_end,
                physical_extent: logical_end,
                base,
                #[cfg(test)]
                faults: Faults::default(),
            }),
        })
    }
    /// Logical bytes admitted, including buffered bytes. This is not a durability acknowledgement.
    pub fn position(&self) -> u64 {
        match &self.state {
            State::Open(o) | State::Failed { owner: o, .. } => o.logical_end,
            State::Closed { position, .. } => *position,
        }
    }
    pub fn effective_mode(&self) -> EffectiveMode {
        match &self.state {
            State::Open(o) | State::Failed { owner: o, .. } => o.mode,
            State::Closed { mode, .. } => *mode,
        }
    }
    pub fn is_open(&self) -> bool {
        !matches!(self.state, State::Closed { .. })
    }
    fn fail(&mut self, error: &io::Error) {
        let placeholder = State::Closed {
            position: self.position(),
            mode: self.effective_mode(),
            error: None,
        };
        let old = std::mem::replace(&mut self.state, placeholder);
        self.state = match old {
            State::Open(owner) => State::Failed {
                owner,
                cause: SavedError::new(error),
            },
            state => state,
        };
    }
    /// Accepts all input on success and retains no borrow after return.
    /// On terminal failure, `accepted` counts this call's copied or transferred prefix.
    /// It does not guarantee that a partially overwritten direct tail remains intact.
    pub fn append(&mut self, bytes: &[u8]) -> Result<(), AppendError> {
        let owner = match &mut self.state {
            State::Open(owner) => owner,
            State::Failed { cause, .. } => {
                return Err(AppendError::Failed {
                    accepted: 0,
                    error: cause.error(),
                })
            }
            State::Closed { .. } => {
                return Err(AppendError::Rejected(io::ErrorKind::NotConnected.into()))
            }
        };
        range_end(owner.logical_end, bytes.len(), owner.layout.alignment)
            .map_err(AppendError::Rejected)?;
        let mut accepted = 0;
        let result = (|| {
            while accepted < bytes.len() {
                let used = (owner.logical_end - owner.base) as usize;
                if used == owner.layout.capacity {
                    owner.flush()?;
                    continue;
                }
                let count = (bytes.len() - accepted).min(owner.layout.capacity - used);
                owner.buffer.as_mut_slice()[used..used + count]
                    .copy_from_slice(&bytes[accepted..accepted + count]);
                owner.logical_end += count as u64;
                accepted += count;
            }
            Ok(())
        })();
        if let Err(error) = result {
            self.fail(&error);
            Err(AppendError::Failed { accepted, error })
        } else {
            Ok(())
        }
    }
    /// Flushes accepted input, repairs padded length, and acknowledges durability.
    /// Truncate and sync errors can be retried without replaying accepted input.
    pub fn sync(&mut self) -> io::Result<()> {
        let owner = match &mut self.state {
            State::Open(owner) => owner,
            State::Failed { cause, .. } => return Err(cause.error()),
            State::Closed { .. } => return Err(io::ErrorKind::NotConnected.into()),
        };
        if let Err(error) = owner.flush() {
            self.fail(&error);
            return Err(error);
        }
        if owner.physical_extent != owner.logical_end {
            #[cfg(test)]
            owner.faults.truncate()?;
            owner.file.set_len(owner.logical_end)?;
            owner.physical_extent = owner.logical_end;
        }
        #[cfg(test)]
        owner.faults.sync()?;
        owner.file.sync_all()
    }
    /// Syncs then consumes OS ownership exactly once. A close failure remains an error
    /// on every repeated finish. A truncate or sync failure retains the open owner.
    pub fn finish(&mut self) -> io::Result<()> {
        if let State::Closed { error, .. } = &self.state {
            return error.as_ref().map_or(Ok(()), |e| Err(e.error()));
        }
        self.sync()?;
        let placeholder = State::Closed {
            position: self.position(),
            mode: self.effective_mode(),
            error: None,
        };
        if let State::Open(owner) = std::mem::replace(&mut self.state, placeholder) {
            let result = checked_close(owner.file);
            #[cfg(test)]
            let result = result.and_then(|()| owner.faults.close());
            if let State::Closed { error, .. } = &mut self.state {
                *error = result.as_ref().err().map(SavedError::new);
            }
            result
        } else {
            unreachable!()
        }
    }
}

impl OpenAppender {
    fn flush(&mut self) -> io::Result<()> {
        if self.written_end == self.logical_end {
            return Ok(());
        }
        let used = (self.logical_end - self.base) as usize;
        let extent = if self.mode == EffectiveMode::Direct {
            used.next_multiple_of(self.layout.alignment)
        } else {
            used
        };
        self.buffer.as_mut_slice()[used..extent].fill(0);
        let mut done = 0;
        while done < extent {
            #[cfg(test)]
            let count = self.faults.write(
                &self.file,
                &self.buffer.as_slice()[done..extent],
                self.base + done as u64,
            )?;
            #[cfg(not(test))]
            let count = write_at(
                &self.file,
                &self.buffer.as_slice()[done..extent],
                self.base + done as u64,
            )?;
            if count == 0 {
                return Err(io::ErrorKind::WriteZero.into());
            }
            done += count;
            self.physical_extent = self.physical_extent.max(self.base + done as u64);
            self.written_end = self
                .written_end
                .max((self.base + done as u64).min(self.logical_end));
            if done < extent
                && self.mode == EffectiveMode::Direct
                && !done.is_multiple_of(self.layout.alignment)
            {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "unaligned short direct write",
                ));
            }
        }
        let retained = if self.mode == EffectiveMode::Direct {
            used % self.layout.alignment
        } else {
            0
        };
        self.buffer
            .as_mut_slice()
            .copy_within(used - retained..used, 0);
        self.base = self.logical_end - retained as u64;
        Ok(())
    }
}

fn checked_close(file: File) -> io::Result<()> {
    #[cfg(unix)]
    {
        use std::os::fd::IntoRawFd;
        // Ownership is consumed even on failure. Retrying close can close a reused descriptor.
        let rc = unsafe { libc::close(file.into_raw_fd()) };
        if rc == 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::IntoRawHandle;
        let rc = unsafe { windows_sys::Win32::Foundation::CloseHandle(file.into_raw_handle()) };
        if rc != 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        }
    }
}
impl Drop for Appender {
    fn drop(&mut self) {
        if matches!(self.state, State::Open(_)) {
            let _ = self.finish();
        }
    }
}

#[cfg(test)]
#[derive(Default)]
struct Faults {
    write_limit: Option<usize>,
    write_calls: usize,
    fail_write_at: Option<usize>,
    truncate_failures: usize,
    sync_failures: usize,
    close_failure: bool,
}
#[cfg(test)]
impl Faults {
    fn write(&mut self, file: &File, bytes: &[u8], offset: u64) -> io::Result<usize> {
        self.write_calls += 1;
        if self.fail_write_at == Some(self.write_calls) {
            return Err(io::Error::from_raw_os_error(libc::EIO));
        }
        write_at(
            file,
            &bytes[..bytes.len().min(self.write_limit.unwrap_or(usize::MAX))],
            offset,
        )
    }
    fn truncate(&mut self) -> io::Result<()> {
        Self::once(&mut self.truncate_failures)
    }
    fn sync(&mut self) -> io::Result<()> {
        Self::once(&mut self.sync_failures)
    }
    fn once(failures: &mut usize) -> io::Result<()> {
        if *failures > 0 {
            *failures -= 1;
            Err(io::Error::from_raw_os_error(libc::EIO))
        } else {
            Ok(())
        }
    }
    fn close(self) -> io::Result<()> {
        if self.close_failure {
            Err(io::Error::from_raw_os_error(libc::EIO))
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests;
