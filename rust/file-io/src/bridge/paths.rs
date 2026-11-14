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

#[cfg(unix)]
use super::error_number;
use super::ffi;
use std::ffi::CString;

pub(super) fn c_path(path: &[u8]) -> Result<CString, i32> {
    if path.is_empty() {
        return Err(libc::EINVAL);
    }
    CString::new(path).map_err(|_| libc::EINVAL)
}

pub(super) fn last_errno() -> i32 {
    #[cfg(unix)]
    {
        std::io::Error::last_os_error()
            .raw_os_error()
            .unwrap_or(libc::EIO)
    }
    #[cfg(windows)]
    {
        extern "C" {
            fn _errno() -> *mut i32;
        }
        // CRT errno is distinct from GetLastError, which std::io::Error reads.
        unsafe { *_errno() }
    }
}

fn status(result: Result<(), i32>) -> i32 {
    result.err().unwrap_or(0)
}

pub fn mkdir_path(path: &[u8], mode: u32) -> i32 {
    let result = (|| {
        #[cfg(unix)]
        {
            let path = super::legacy_path(path).map_err(error_number)?;
            crate::create_dir(&path, mode).map_err(error_number)
        }
        #[cfg(windows)]
        {
            let _ = mode;
            let path = c_path(path)?;
            if unsafe { libc::mkdir(path.as_ptr()) } == 0 {
                Ok(())
            } else {
                Err(last_errno())
            }
        }
    })();
    match result {
        Err(libc::EEXIST) => 0,
        result => status(result),
    }
}

#[allow(
    clippy::unnecessary_cast,
    reason = "libc mode constants are u16 on Apple and signed i32 on Windows"
)]
pub fn rmdir_path(path: &[u8]) -> i32 {
    status((|| {
        let info = path_stat(path)?;
        if info.mode & libc::S_IFMT as u32 != libc::S_IFDIR as u32 {
            return Err(libc::ENOENT);
        }
        #[cfg(unix)]
        {
            let path = super::legacy_path(path).map_err(error_number)?;
            crate::remove_dir(&path).map_err(error_number)
        }
        #[cfg(windows)]
        {
            let path = c_path(path)?;
            if unsafe { libc::rmdir(path.as_ptr()) } == 0 {
                Ok(())
            } else {
                Err(last_errno())
            }
        }
    })())
}

#[allow(
    clippy::unnecessary_cast,
    reason = "libc mode constants are u16 on Apple and signed i32 on Windows"
)]
pub fn unlink_path(path: &[u8]) -> i32 {
    status((|| {
        let info = path_stat(path)?;
        if info.mode & libc::S_IFMT as u32 != libc::S_IFREG as u32 {
            return Err(libc::ENOENT);
        }
        #[cfg(unix)]
        {
            let path = super::legacy_path(path).map_err(error_number)?;
            crate::remove_file(&path).map_err(error_number)
        }
        #[cfg(windows)]
        {
            let path = c_path(path)?;
            // The type guard follows links; unlink still removes the named entry.
            if unsafe { libc::unlink(path.as_ptr()) } == 0 {
                Ok(())
            } else {
                Err(last_errno())
            }
        }
    })())
}

pub fn rename_path(source: &[u8], destination: &[u8]) -> i32 {
    status((|| {
        // Empty names reach rename and retain its ENOENT result.
        let source = CString::new(source).map_err(|_| libc::EINVAL)?;
        let destination = CString::new(destination).map_err(|_| libc::EINVAL)?;
        // In particular, Windows CRT rename refuses an existing destination.
        if unsafe { libc::rename(source.as_ptr(), destination.as_ptr()) } == 0 {
            Ok(())
        } else {
            Err(last_errno())
        }
    })())
}

pub fn truncate_legacy_path(path: &[u8], length: i64) -> i32 {
    status((|| {
        let path = c_path(path)?;
        #[cfg(unix)]
        {
            let length = libc::off_t::try_from(length).map_err(|_| libc::EOVERFLOW)?;
            // Preserve truncate's path lookup, write permissions and negative-length errors.
            if unsafe { libc::truncate(path.as_ptr(), length) } == 0 {
                Ok(())
            } else {
                Err(last_errno())
            }
        }
        #[cfg(windows)]
        {
            extern "C" {
                fn _chsize_s(fd: i32, size: i64) -> i32;
            }
            let fd = unsafe { libc::open(path.as_ptr(), libc::O_RDWR | libc::O_BINARY) };
            if fd < 0 {
                return Err(last_errno());
            }
            let error = if length < 0 {
                // Avoid the CRT invalid-parameter handler for a known invalid size.
                libc::EINVAL
            } else {
                unsafe { _chsize_s(fd, length) }
            };
            unsafe {
                libc::close(fd);
            }
            if error == 0 {
                Ok(())
            } else {
                Err(error)
            }
        }
    })())
}

pub fn exists_path(path: &[u8], exists: &mut bool) -> i32 {
    status((|| {
        let path = c_path(path)?;
        // F_OK uses real credentials on Unix and the CRT's access rules on Windows.
        if unsafe { libc::access(path.as_ptr(), 0) } == 0 {
            *exists = true;
            Ok(())
        } else {
            let error = last_errno();
            if error == libc::ENOENT {
                *exists = false;
                Ok(())
            } else {
                Err(error)
            }
        }
    })())
}

pub fn stat_path(path: &[u8], out: &mut ffi::FileStat) -> i32 {
    match path_stat(path) {
        Ok(value) => {
            *out = value;
            0
        }
        Err(error) => error,
    }
}

#[cfg(unix)]
fn path_stat(path: &[u8]) -> Result<ffi::FileStat, i32> {
    use std::os::unix::fs::MetadataExt;
    let path = super::legacy_path(path).map_err(error_number)?;
    let m = crate::metadata(&path).map_err(error_number)?;
    Ok(ffi::FileStat {
        rdev: m.rdev(),
        dev: m.dev(),
        inode: m.ino(),
        mask: 0,
        mode: m.mode(),
        nlink: m.nlink() as u32,
        uid: u64::from(m.uid()),
        gid: u64::from(m.gid()),
        size: m.size(),
        block_cnt: m.blocks(),
        block_size: m.blksize(),
        atime_s: m.atime(),
        mtime_s: m.mtime(),
        ctime_s: m.ctime(),
        btime_s: i64::MAX,
    })
}

#[cfg(windows)]
fn path_stat(path: &[u8]) -> Result<ffi::FileStat, i32> {
    let path = c_path(path)?;
    let mut info = std::mem::MaybeUninit::<libc::stat>::uninit();
    // libc::stat links to _stat64 on Windows, including its narrow-path semantics.
    if unsafe { libc::stat(path.as_ptr(), info.as_mut_ptr()) } != 0 {
        return Err(last_errno());
    }
    let m = unsafe { info.assume_init() };
    Ok(ffi::FileStat {
        rdev: u64::from(m.st_rdev),
        dev: u64::from(m.st_dev),
        inode: u64::from(m.st_ino),
        mask: 0,
        mode: u32::from(m.st_mode),
        nlink: m.st_nlink as u32,
        uid: m.st_uid as u64,
        gid: m.st_gid as u64,
        size: m.st_size as u64,
        block_cnt: 0,
        block_size: 4096,
        atime_s: m.st_atime,
        mtime_s: m.st_mtime,
        ctime_s: m.st_ctime,
        btime_s: i64::MAX,
    })
}

#[cfg(test)]
mod tests;
