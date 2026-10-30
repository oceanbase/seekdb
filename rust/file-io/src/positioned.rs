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

use crate::Transfer;
use std::fs::File;
use std::io;

fn check_range(offset: u64, length: usize) -> io::Result<()> {
    if length != 0
        && offset
            .checked_add(length as u64)
            .is_none_or(|end| end > i64::MAX as u64)
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "file range exceeds signed offsets",
        ));
    }
    Ok(())
}

/// On Windows, this advances the shared file cursor as `FileExt::seek_read` does.
pub fn read_at(file: &File, bytes: &mut [u8], offset: u64) -> io::Result<usize> {
    check_range(offset, bytes.len())?;
    if bytes.is_empty() {
        return Ok(0);
    }
    loop {
        #[cfg(unix)]
        let result = {
            use std::os::unix::fs::FileExt;
            file.read_at(bytes, offset)
        };
        #[cfg(windows)]
        let result = {
            use std::os::windows::fs::FileExt;
            let count = bytes.len().min(u32::MAX as usize);
            file.seek_read(&mut bytes[..count], offset)
        };
        match result {
            #[cfg(windows)]
            Err(error) if error.raw_os_error() == Some(windows_sys::Win32::Foundation::ERROR_HANDLE_EOF as i32) => return Ok(0),
            Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
            result => return result,
        }
    }
}

/// On Windows, this advances the shared file cursor as `FileExt::seek_write` does.
pub fn write_at(file: &File, bytes: &[u8], offset: u64) -> io::Result<usize> {
    check_range(offset, bytes.len())?;
    if bytes.is_empty() {
        return Ok(0);
    }
    loop {
        #[cfg(unix)]
        let result = {
            use std::os::unix::fs::FileExt;
            file.write_at(bytes, offset)
        };
        #[cfg(windows)]
        let result = {
            use std::os::windows::fs::FileExt;
            let count = bytes.len().min(u32::MAX as usize);
            file.seek_write(&bytes[..count], offset)
        };
        match result {
            Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
            result => return result,
        }
    }
}

/// Reads until the buffer is filled or EOF is reached. EOF is a successful short read.
pub fn read_up_to_at(file: &File, bytes: &mut [u8], offset: u64) -> Transfer {
    let mut transferred = 0;
    let result = (|| {
        check_range(offset, bytes.len())?;
        while transferred < bytes.len() {
            let count = read_at(file, &mut bytes[transferred..], offset + transferred as u64)?;
            if count == 0 {
                break;
            }
            transferred += count;
        }
        Ok(())
    })();
    Transfer {
        bytes: transferred,
        result,
    }
}

pub fn write_fully_at(file: &File, bytes: &[u8], offset: u64) -> Transfer {
    let mut transferred = 0;
    let result = (|| {
        check_range(offset, bytes.len())?;
        while transferred < bytes.len() {
            let count = write_at(file, &bytes[transferred..], offset + transferred as u64)?;
            if count == 0 {
                return Err(io::ErrorKind::WriteZero.into());
            }
            transferred += count;
        }
        Ok(())
    })();
    Transfer {
        bytes: transferred,
        result,
    }
}

pub fn read_exact_at(file: &File, bytes: &mut [u8], offset: u64) -> io::Result<()> {
    let transfer = read_up_to_at(file, bytes, offset);
    transfer.result?;
    if transfer.bytes != bytes.len() {
        return Err(io::ErrorKind::UnexpectedEof.into());
    }
    Ok(())
}

pub fn write_all_at(file: &File, bytes: &[u8], offset: u64) -> io::Result<()> {
    write_fully_at(file, bytes, offset).result
}
