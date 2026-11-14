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

/// Replaces the process stdout and stderr descriptors. Callers serialize changes.
pub fn redirect_stdio(file: &File) -> io::Result<()> {
    #[cfg(unix)]
    let descriptor = {
        use std::os::fd::AsRawFd;
        file.as_raw_fd()
    };
    #[cfg(windows)]
    let owned = CrtDescriptor::duplicate(file)?;
    #[cfg(windows)]
    let descriptor = owned.0;
    for target in [1, 2] {
        if unsafe { libc::dup2(descriptor, target) } < 0 {
            #[cfg(unix)]
            return Err(io::Error::last_os_error());
            #[cfg(windows)]
            return Err(crt_error());
        }
    }
    Ok(())
}

#[cfg(windows)]
struct CrtDescriptor(i32);

#[cfg(windows)]
impl CrtDescriptor {
    fn duplicate(file: &File) -> io::Result<Self> {
        use std::os::windows::io::{AsRawHandle, IntoRawHandle};
        let file = file.try_clone()?;
        let descriptor = unsafe {
            libc::open_osfhandle(file.as_raw_handle() as _, libc::O_APPEND | libc::O_BINARY)
        };
        if descriptor < 0 {
            return Err(crt_error());
        }
        // The CRT now owns the cloned HANDLE and closes it with the descriptor.
        let _ = file.into_raw_handle();
        Ok(Self(descriptor))
    }
}

#[cfg(windows)]
impl Drop for CrtDescriptor {
    fn drop(&mut self) {
        unsafe { libc::close(self.0) };
    }
}

#[cfg(windows)]
fn crt_error() -> io::Error {
    unsafe extern "C" {
        #[link_name = "_errno"]
        fn crt_errno() -> *mut i32;
    }
    let code = unsafe { *crt_errno() };
    let kind = match code {
        libc::EACCES => io::ErrorKind::PermissionDenied,
        libc::EBADF | libc::EINVAL => io::ErrorKind::InvalidInput,
        _ => io::ErrorKind::Other,
    };
    io::Error::new(kind, format!("CRT descriptor error {code}"))
}
