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

use std::io;
use std::path::Path;

#[derive(Clone, Copy, Debug)]
pub struct Space {
    pub capacity: u64,
    pub free: u64,
    /// Space available to the calling user, excluding reserved blocks or quota.
    pub available: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UsableSpace {
    pub total: u64,
    pub available: u64,
}

#[cfg(unix)]
struct Observation {
    blocks: u64,
    free: u64,
    available: u64,
    fragment_size: u64,
    block_size: u64,
}

#[cfg(unix)]
impl Observation {
    fn capacity(&self) -> Space {
        Space {
            capacity: self.blocks.saturating_mul(self.fragment_size),
            free: self.free.saturating_mul(self.fragment_size),
            available: self.available.saturating_mul(self.fragment_size),
        }
    }

    fn usable(&self) -> io::Result<UsableSpace> {
        let total = self
            .blocks
            .checked_add(self.available)
            .and_then(|blocks| blocks.checked_sub(self.free))
            .and_then(|blocks| blocks.checked_mul(self.block_size))
            .ok_or(io::ErrorKind::InvalidData)?;
        let available = self
            .available
            .checked_mul(self.block_size)
            .ok_or(io::ErrorKind::InvalidData)?;
        Ok(UsableSpace { total, available })
    }
}

#[cfg(unix)]
fn observe(path: &Path) -> io::Result<Observation> {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;
    let directory = CString::new(path.as_os_str().as_bytes())?;
    let mut value = std::mem::MaybeUninit::<libc::statvfs>::uninit();
    if unsafe { libc::statvfs(directory.as_ptr(), value.as_mut_ptr()) } != 0 {
        return Err(io::Error::last_os_error());
    }
    let value = unsafe { value.assume_init() };
    #[allow(clippy::unnecessary_cast)]
    Ok(Observation {
        blocks: value.f_blocks as u64,
        free: value.f_bfree as u64,
        available: value.f_bavail as u64,
        fragment_size: value.f_frsize as u64,
        block_size: value.f_bsize as u64,
    })
}

#[cfg(unix)]
pub fn space(path: &Path) -> io::Result<Space> {
    Ok(observe(path)?.capacity())
}

#[cfg(unix)]
pub fn usable_space(path: &Path) -> io::Result<UsableSpace> {
    observe(path)?.usable()
}

#[cfg(windows)]
pub fn space(path: &Path) -> io::Result<Space> {
    observe(path)
}

#[cfg(windows)]
pub fn usable_space(path: &Path) -> io::Result<UsableSpace> {
    let value = observe(path)?;
    Ok(UsableSpace {
        total: value.capacity,
        available: value.available,
    })
}

#[cfg(windows)]
fn observe(path: &Path) -> io::Result<Space> {
    use windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW;
    let resolved = std::fs::canonicalize(path)?;
    let directory = if resolved.metadata()?.is_dir() {
        resolved.as_path()
    } else {
        resolved.parent().ok_or(io::ErrorKind::InvalidInput)?
    };
    let directory = crate::filesystem::wide_path(directory)?;
    let mut value = Space {
        capacity: 0,
        free: 0,
        available: 0,
    };
    if unsafe {
        GetDiskFreeSpaceExW(
            directory.as_ptr(),
            &mut value.available,
            &mut value.capacity,
            &mut value.free,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    Ok(value)
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    #[test]
    fn usable_space_uses_block_units_and_excludes_reserved_blocks() {
        let value = Observation {
            blocks: 100,
            free: 20,
            available: 10,
            fragment_size: 512,
            block_size: 4096,
        };
        assert_eq!(value.capacity().capacity, 51200);
        assert_eq!(
            value.usable().unwrap(),
            UsableSpace {
                total: 90 * 4096,
                available: 10 * 4096
            }
        );
    }

    #[test]
    fn usable_space_rejects_overflow_and_inconsistent_counts() {
        for (blocks, free, available, block_size) in
            [(u64::MAX, 0, 1, 1), (1, 2, 0, 1), (u64::MAX, 0, 0, 2)]
        {
            let value = Observation {
                blocks,
                free,
                available,
                fragment_size: 1,
                block_size,
            };
            assert!(value.usable().is_err());
        }
    }
}
