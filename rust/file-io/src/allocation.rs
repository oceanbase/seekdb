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

use std::fs::File;
use std::io;
use std::ops::Range;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Allocation {
    /// Reserve storage and extend the logical length when needed, preserving existing bytes.
    Reserve,
    /// Reserve storage without changing the logical length.
    ReserveKeepLength,
    /// Deallocate full blocks and zero partial blocks without changing logical length.
    PunchHole,
}

/// Operates on a nonempty byte range. The cursor is unchanged.
///
/// The caller must serialize allocation and file-length changes, including writes that
/// extend EOF, through other handles. Apple and Windows reservation and length extension
/// require multiple system calls. A failure may leave reserved storage behind.
/// Unsupported modes/filesystems return `ErrorKind::Unsupported`; other OS errors are retained.
/// Linux/Android support all modes. Apple supports reservation only for tail ranges
/// on files whose allocated bytes cover the current EOF; existing-range reservation
/// and punching return Unsupported. Windows rejects sparse/compressed files and punching.
/// Reservations beyond EOF may be released when the handle closes, depending on the OS.
pub fn allocate(file: &File, range: Range<u64>, mode: Allocation) -> io::Result<()> {
    if range.start >= range.end || range.end > i64::MAX as u64 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid allocation range",
        ));
    }
    platform_allocate(file, range, mode)
}

/// Extends a regular file to at least `minimum_len`, preserving existing bytes.
/// The caller must serialize file-length changes and perform any required sync.
/// Native platforms use `allocate` with its existing restrictions. Emscripten
/// writes a zero-filled tail without promising physical-space reservation.
/// A failed extension may leave a partial tail, which a retry preserves.
pub fn extend_file(file: &File, minimum_len: u64) -> io::Result<()> {
    #[cfg(target_os = "emscripten")]
    {
        extend_with_zeros(file, minimum_len, |bytes, offset| {
            crate::write_at(file, bytes, offset)
        })
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        let range = extension_range(file, minimum_len)?;
        if range.is_empty() {
            Ok(())
        } else {
            allocate(file, range, Allocation::Reserve)
        }
    }
}

fn extension_range(file: &File, minimum_len: u64) -> io::Result<Range<u64>> {
    if minimum_len == 0 || minimum_len > i64::MAX as u64 {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    let metadata = file.metadata()?;
    if !metadata.is_file() {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    Ok(metadata.len().min(minimum_len)..minimum_len)
}

#[cfg(any(target_os = "emscripten", test))]
pub(crate) fn extend_with_zeros(
    file: &File,
    minimum_len: u64,
    mut write: impl FnMut(&[u8], u64) -> io::Result<usize>,
) -> io::Result<()> {
    let range = extension_range(file, minimum_len)?;
    if range.is_empty() {
        return Ok(());
    }
    let zeros = crate::AlignedBuffer::zeroed(64 * 1024, 1)?;
    let mut offset = range.start;
    while offset < range.end {
        let count = (range.end - offset).min(zeros.as_slice().len() as u64) as usize;
        match write(&zeros.as_slice()[..count], offset) {
            Ok(0) => return Err(io::ErrorKind::WriteZero.into()),
            Ok(written) => offset += written as u64,
            Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
            Err(error) => return Err(error),
        }
    }
    Ok(())
}

#[cfg(any(target_os = "linux", target_os = "android", target_vendor = "apple"))]
fn allocation_error() -> io::Error {
    let error = io::Error::last_os_error();
    match error.raw_os_error() {
        Some(code) if code == libc::EOPNOTSUPP || code == libc::ENOTSUP || code == libc::ENOSYS => {
            io::Error::new(io::ErrorKind::Unsupported, error)
        }
        _ => error,
    }
}

#[cfg(any(target_os = "linux", target_os = "android"))]
fn platform_allocate(file: &File, range: Range<u64>, mode: Allocation) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    let flags = match mode {
        Allocation::Reserve => 0,
        Allocation::ReserveKeepLength => libc::FALLOC_FL_KEEP_SIZE,
        Allocation::PunchHole => libc::FALLOC_FL_PUNCH_HOLE | libc::FALLOC_FL_KEEP_SIZE,
    };
    loop {
        // SAFETY: file owns the descriptor; range fits the native signed 64-bit offsets.
        if unsafe {
            libc::fallocate64(
                file.as_raw_fd(),
                flags,
                range.start as i64,
                (range.end - range.start) as i64,
            )
        } == 0
        {
            return Ok(());
        }
        let error = allocation_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

#[cfg(target_vendor = "apple")]
fn platform_allocate(file: &File, range: Range<u64>, mode: Allocation) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    use std::os::unix::fs::MetadataExt;
    if mode == Allocation::PunchHole {
        return Err(io::ErrorKind::Unsupported.into());
    }
    let metadata = file.metadata()?;
    let allocated = metadata
        .blocks()
        .checked_mul(512)
        .ok_or_else(|| io::Error::from(io::ErrorKind::InvalidData))?;
    // F_PREALLOCATE grows from physical EOF; it cannot address existing holes.
    if range.start < metadata.len() || allocated < metadata.len() {
        return Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "Apple reservation requires a non-sparse tail range",
        ));
    }
    let additional = range.end.saturating_sub(allocated);
    if additional != 0 {
        let mut store = libc::fstore_t {
            fst_flags: libc::F_ALLOCATEALL,
            fst_posmode: libc::F_PEOFPOSMODE,
            fst_offset: 0,
            fst_length: additional as i64,
            fst_bytesalloc: 0,
        };
        // SAFETY: the descriptor is owned and store is the native fcntl argument.
        if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_PREALLOCATE, &mut store) } == -1 {
            return Err(allocation_error());
        }
    }
    if mode == Allocation::Reserve && range.end > metadata.len() {
        file.set_len(range.end)?;
    }
    Ok(())
}

#[cfg(windows)]
fn platform_allocate(file: &File, range: Range<u64>, mode: Allocation) -> io::Result<()> {
    use std::mem::{size_of, zeroed};
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Foundation::{ERROR_INVALID_FUNCTION, ERROR_NOT_SUPPORTED};
    use windows_sys::Win32::Storage::FileSystem::*;
    fn error() -> io::Error {
        let error = io::Error::last_os_error();
        match error.raw_os_error().map(|code| code as u32) {
            Some(ERROR_INVALID_FUNCTION | ERROR_NOT_SUPPORTED) => {
                io::Error::new(io::ErrorKind::Unsupported, error)
            }
            _ => error,
        }
    }
    if mode == Allocation::PunchHole {
        return Err(io::ErrorKind::Unsupported.into());
    }
    let handle = file.as_raw_handle();
    // SAFETY: the output structures are plain native integer fields, initially zeroed.
    let mut standard: FILE_STANDARD_INFO = unsafe { zeroed() };
    let mut attributes: FILE_ATTRIBUTE_TAG_INFO = unsafe { zeroed() };
    // SAFETY: handle is live and both output buffers have the requested layouts and sizes.
    unsafe {
        if GetFileInformationByHandleEx(
            handle,
            FileStandardInfo,
            (&mut standard as *mut FILE_STANDARD_INFO).cast(),
            size_of::<FILE_STANDARD_INFO>() as u32,
        ) == 0
        {
            return Err(error());
        }
        if GetFileInformationByHandleEx(
            handle,
            FileAttributeTagInfo,
            (&mut attributes as *mut FILE_ATTRIBUTE_TAG_INFO).cast(),
            size_of::<FILE_ATTRIBUTE_TAG_INFO>() as u32,
        ) == 0
        {
            return Err(error());
        }
    }
    if attributes.FileAttributes & (FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_COMPRESSED) != 0 {
        return Err(io::ErrorKind::Unsupported.into());
    }
    // FileAllocationInfo truncates EOF when the requested allocation is smaller.
    let allocation = FILE_ALLOCATION_INFO {
        AllocationSize: (range.end as i64)
            .max(standard.EndOfFile)
            .max(standard.AllocationSize),
    };
    // SAFETY: the live handle and input structure match the FileAllocationInfo contract.
    if unsafe {
        SetFileInformationByHandle(
            handle,
            FileAllocationInfo,
            (&allocation as *const FILE_ALLOCATION_INFO).cast(),
            size_of::<FILE_ALLOCATION_INFO>() as u32,
        )
    } == 0
    {
        return Err(error());
    }
    if mode == Allocation::Reserve && range.end > standard.EndOfFile as u64 {
        file.set_len(range.end)?;
    }
    Ok(())
}

#[cfg(not(any(
    target_os = "linux",
    target_os = "android",
    target_vendor = "apple",
    windows
)))]
fn platform_allocate(_: &File, _: Range<u64>, _: Allocation) -> io::Result<()> {
    Err(io::ErrorKind::Unsupported.into())
}
