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

use crate::{read_at, write_at, AlignedBuffer};
use std::fs::File;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::Arc;

const PROBE_SIZE: usize = 4096;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DirectoryCapabilities {
    BufferedWorker,
    DirectWorker,
    DirectNativeAio,
}

/// Verifies directory I/O, durability, and cleanup before publishing a capability.
pub fn probe_directory(directory: &Path) -> io::Result<DirectoryCapabilities> {
    if directory.as_os_str().is_empty() {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    let (file, path) = temporary_file(directory)?;
    let file = Arc::new(file);
    #[cfg(all(unix, not(target_os = "emscripten")))]
    let mut named = true;
    let result = (|| {
        #[cfg(all(unix, not(target_os = "emscripten")))]
        {
            #[cfg(test)]
            tests::check(tests::Stage::Unlink)?;
            std::fs::remove_file(&path)?;
            named = false;
        }
        let mut buffer = AlignedBuffer::zeroed(PROBE_SIZE, PROBE_SIZE)?;
        let direct = direct_round_trip(&file, &mut buffer)?;
        if !direct {
            return Ok(DirectoryCapabilities::BufferedWorker);
        }
        #[cfg(target_os = "linux")]
        if crate::asynchronous::native::probe(file.clone(), buffer)? {
            return Ok(DirectoryCapabilities::DirectNativeAio);
        }
        Ok(DirectoryCapabilities::DirectWorker)
    })();
    let file = Arc::try_unwrap(file).expect("probe retains no operation after retirement");
    let result = result.and_then(|capability| {
        #[cfg(test)]
        tests::check(tests::Stage::Sync)?;
        file.sync_all()?;
        Ok(capability)
    });
    let closed = close(file);
    #[cfg(all(unix, not(target_os = "emscripten")))]
    if named {
        let _ = std::fs::remove_file(path);
    }
    #[cfg(target_os = "emscripten")]
    let closed = {
        // OPFS cannot remove a file while its sync access handle remains open.
        let removed = (|| {
            #[cfg(test)]
            tests::check(tests::Stage::Unlink)?;
            std::fs::remove_file(path)
        })();
        closed.and(removed)
    };
    #[cfg(windows)]
    let _ = path;
    result.and_then(|capability| closed.map(|()| capability))
}

fn temporary_file(directory: &Path) -> io::Result<(File, PathBuf)> {
    #[cfg(unix)]
    {
        use std::ffi::{CString, OsString};
        use std::os::fd::FromRawFd;
        use std::os::unix::ffi::{OsStrExt, OsStringExt};
        let path = directory.join(".seekdb-io-probe-XXXXXX");
        let mut name = CString::new(path.as_os_str().as_bytes())
            .map_err(|_| io::ErrorKind::InvalidInput)?
            .into_bytes_with_nul();
        // SAFETY: mkstemp modifies a writable, NUL-terminated template and returns a unique fd.
        let fd = unsafe { libc::mkstemp(name.as_mut_ptr().cast()) };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        name.pop();
        // SAFETY: successful exclusive creation transfers its descriptor to File exactly once.
        let file = unsafe { File::from_raw_fd(fd) };
        Ok((file, OsString::from_vec(name).into()))
    }
    #[cfg(windows)]
    {
        use std::fs::OpenOptions;
        use std::os::windows::fs::OpenOptionsExt;
        use std::sync::atomic::{AtomicU64, Ordering};
        use windows_sys::Win32::Storage::FileSystem::FILE_FLAG_DELETE_ON_CLOSE;
        static NEXT: AtomicU64 = AtomicU64::new(0);
        for _ in 0..128 {
            let path = directory.join(format!(
                ".seekdb-io-probe-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            match OpenOptions::new()
                .read(true)
                .write(true)
                .create_new(true)
                .custom_flags(FILE_FLAG_DELETE_ON_CLOSE)
                .open(&path)
            {
                Ok(file) => return Ok((file, path)),
                Err(error) if error.kind() == io::ErrorKind::AlreadyExists => (),
                Err(error) => return Err(error),
            }
        }
        Err(io::ErrorKind::AlreadyExists.into())
    }
}

fn direct_round_trip(file: &File, buffer: &mut AlignedBuffer) -> io::Result<bool> {
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        use std::os::fd::AsRawFd;
        #[cfg(test)]
        tests::check(tests::Stage::GetFlags)?;
        // SAFETY: these fcntl commands use a live descriptor and integer flags.
        let flags = unsafe { libc::fcntl(file.as_raw_fd(), libc::F_GETFL) };
        if flags < 0 {
            return Err(io::Error::last_os_error());
        }
        let enabled = set_flags(file, flags | libc::O_DIRECT);
        match enabled {
            Ok(()) => match sync_round_trip(file, buffer) {
                Ok(()) => return Ok(true),
                Err(error) if unsupported(&error) => set_flags(file, flags & !libc::O_DIRECT)?,
                Err(error) => return Err(error),
            },
            Err(error) if unsupported(&error) => (),
            Err(error) => return Err(error),
        }
    }
    sync_round_trip(file, buffer)?;
    Ok(false)
}

#[cfg(any(target_os = "linux", target_os = "android"))]
fn set_flags(file: &File, flags: i32) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    #[cfg(test)]
    tests::check(if flags & libc::O_DIRECT == 0 {
        tests::Stage::ClearDirect
    } else {
        tests::Stage::EnableDirect
    })?;
    // SAFETY: file owns a live descriptor; F_SETFL takes integer flags.
    if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_SETFL, flags) } < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

fn sync_round_trip(file: &File, buffer: &mut AlignedBuffer) -> io::Result<()> {
    let transfer = |direction, bytes: &mut [u8]| {
        #[cfg(test)]
        if let Some(result) = tests::transfer(direction, bytes) {
            return result;
        }
        match direction {
            crate::Direction::Write => write_at(file, bytes, 0),
            crate::Direction::Read => read_at(file, bytes, 0),
        }
    };
    for direction in [crate::Direction::Write, crate::Direction::Read] {
        buffer
            .as_mut_slice()
            .fill(if direction == crate::Direction::Write {
                0x5a
            } else {
                0
            });
        let count = loop {
            match transfer(direction, buffer.as_mut_slice()) {
                Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
                result => break result?,
            }
        };
        if count != PROBE_SIZE {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "short directory probe transfer",
            ));
        }
    }
    if buffer.as_slice().iter().any(|&byte| byte != 0x5a) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "corrupt directory probe data",
        ));
    }
    Ok(())
}

#[cfg(any(target_os = "linux", target_os = "android"))]
pub(crate) fn unsupported(error: &io::Error) -> bool {
    matches!(
        error.raw_os_error(),
        Some(libc::EINVAL | libc::EOPNOTSUPP | libc::ENOSYS)
    )
}

fn close(file: File) -> io::Result<()> {
    #[cfg(unix)]
    {
        use std::os::fd::IntoRawFd;
        // SAFETY: consuming File prevents a second close, including on EINTR.
        if unsafe { libc::close(file.into_raw_fd()) } < 0 {
            return Err(io::Error::last_os_error());
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::IntoRawHandle;
        // SAFETY: consuming File transfers the unique handle to this single checked close.
        if unsafe { windows_sys::Win32::Foundation::CloseHandle(file.into_raw_handle()) } == 0 {
            return Err(io::Error::last_os_error());
        }
    }
    #[cfg(test)]
    tests::check(tests::Stage::Close)?;
    Ok(())
}

#[cfg(test)]
#[path = "capability_tests.rs"]
mod tests;
