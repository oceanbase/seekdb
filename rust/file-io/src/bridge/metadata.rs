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

use super::ffi;
use std::{fs::File, io};

#[cfg(unix)]
pub fn stat(file: &File) -> io::Result<ffi::FileStat> {
    use std::os::unix::fs::MetadataExt;
    let m = file.metadata()?;
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
        btime_s: m
            .created()
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map_or(0, |t| t.as_secs() as i64),
    })
}

#[cfg(windows)]
pub fn stat(file: &File) -> io::Result<ffi::FileStat> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Storage::FileSystem::{
        GetFileInformationByHandle, BY_HANDLE_FILE_INFORMATION,
    };
    let m = file.metadata()?;
    let mut info = std::mem::MaybeUninit::<BY_HANDLE_FILE_INFORMATION>::uninit();
    if unsafe { GetFileInformationByHandle(file.as_raw_handle(), info.as_mut_ptr()) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let info = unsafe { info.assume_init() };
    fn seconds(value: io::Result<std::time::SystemTime>) -> i64 {
        value
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map_or(0, |t| t.as_secs() as i64)
    }
    Ok(ffi::FileStat {
        dev: u64::from(info.dwVolumeSerialNumber),
        inode: (u64::from(info.nFileIndexHigh) << 32) | u64::from(info.nFileIndexLow),
        mode: (if m.is_dir() { 0o040000 } else { 0o100000 })
            | if m.permissions().readonly() {
                0o444
            } else {
                0o666
            },
        nlink: info.nNumberOfLinks,
        size: m.len(),
        atime_s: seconds(m.accessed()),
        mtime_s: seconds(m.modified()),
        ctime_s: seconds(m.created()),
        btime_s: seconds(m.created()),
        ..ffi::FileStat::default()
    })
}
