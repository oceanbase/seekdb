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

use std::fs::{File, OpenOptions};
use std::io;
use std::path::Path;

/// Enables direct I/O before the file (or any cloned handle) is used concurrently.
/// Alignment requirements remain filesystem-specific.
pub fn enable_direct_io(file: &File) -> io::Result<()> {
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        use std::os::fd::AsRawFd;
        // SAFETY: file owns a live descriptor; these fcntl commands take integer arguments.
        let flags = unsafe { libc::fcntl(file.as_raw_fd(), libc::F_GETFL) };
        if flags == -1 {
            return Err(io::Error::last_os_error());
        }
        if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_SETFL, flags | libc::O_DIRECT) } == -1 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
    #[cfg(target_vendor = "apple")]
    {
        use std::os::fd::AsRawFd;
        // SAFETY: file owns a live descriptor; F_NOCACHE takes an integer argument.
        if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_NOCACHE, 1) } == -1 {
            Err(io::Error::last_os_error())
        } else {
            Ok(())
        }
    }
    #[cfg(not(any(target_os = "linux", target_os = "android", target_vendor = "apple")))]
    {
        let _ = file;
        Err(io::ErrorKind::Unsupported.into())
    }
}

pub fn open_with_direct_io(
    options: &mut OpenOptions,
    path: &Path,
    direct_io: bool,
) -> io::Result<File> {
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.custom_flags(direct_io_flag(direct_io));
        options.open(path)
    }
    #[cfg(not(windows))]
    {
        let file = options.open(path)?;
        if direct_io {
            enable_direct_io(&file)?;
        }
        Ok(file)
    }
}

#[cfg(target_vendor = "apple")]
pub(crate) fn disable_direct_io(file: &File) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    // SAFETY: file owns a live descriptor; F_NOCACHE takes an integer argument.
    if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_NOCACHE, 0) } == -1 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

#[cfg(windows)]
pub(crate) fn direct_io_flag(direct_io: bool) -> u32 {
    if direct_io {
        windows_sys::Win32::Storage::FileSystem::FILE_FLAG_NO_BUFFERING
    } else {
        0
    }
}
