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

#[cfg(test)]
mod tests;

/// Owns an exclusive file claim. On Emscripten the claim covers this module's
/// threads; admission between separate persistent instances remains external.
pub struct ExclusiveFile {
    // Fields drop in declaration order, keeping the claim through descriptor close.
    file: File,
    #[cfg(target_os = "emscripten")]
    _claim: platform::Claim,
}

impl ExclusiveFile {
    pub fn try_new(file: File) -> io::Result<Self> {
        Self::try_new_retaining(file).map_err(|(error, _file)| error)
    }

    /// Returns the file on failed admission so its creator can check close and cleanup.
    pub fn try_new_retaining(file: File) -> Result<Self, (io::Error, File)> {
        #[cfg(not(target_os = "emscripten"))]
        if let Err(error) = file.try_lock() {
            return Err((error.into(), file));
        }
        #[cfg(target_os = "emscripten")]
        let claim = match platform::Claim::acquire(&file) {
            Ok(claim) => claim,
            Err(error) => return Err((error, file)),
        };
        Ok(Self {
            file,
            #[cfg(target_os = "emscripten")]
            _claim: claim,
        })
    }

    pub fn close(self) -> io::Result<()> {
        // The Emscripten claim remains alive through close and drops on return.
        close_file(self.file)
    }

    /// Borrows the descriptor for I/O. Callers must not unlock it or duplicate it
    /// into an owner that can outlive this exclusive claim.
    pub fn as_file(&self) -> &File {
        &self.file
    }
}

#[cfg(target_os = "emscripten")]
mod platform {
    use super::*;
    use crate::FileIdentity;
    use std::collections::HashSet;
    use std::sync::{LazyLock, Mutex};

    static CLAIMS: LazyLock<Mutex<HashSet<FileIdentity>>> =
        LazyLock::new(|| Mutex::new(HashSet::new()));

    pub(super) struct Claim(FileIdentity);

    impl Claim {
        pub(super) fn acquire(file: &File) -> io::Result<Self> {
            let identity = FileIdentity::of(file)?;
            let mut claims = CLAIMS
                .lock()
                .map_err(|_| io::Error::other("exclusive file registry poisoned"))?;
            if !claims.insert(identity) {
                return Err(io::ErrorKind::WouldBlock.into());
            }
            Ok(Self(identity))
        }
    }

    impl Drop for Claim {
        fn drop(&mut self) {
            CLAIMS
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .remove(&self.0);
        }
    }
}

/// Consumes a file and attempts close once, including on an interrupted close.
pub fn close_file(file: File) -> io::Result<()> {
    #[cfg(unix)]
    {
        use std::os::fd::IntoRawFd;
        if unsafe { libc::close(file.into_raw_fd()) } != 0 {
            return Err(io::Error::last_os_error());
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::IntoRawHandle;
        if unsafe { windows_sys::Win32::Foundation::CloseHandle(file.into_raw_handle()) } == 0 {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}
