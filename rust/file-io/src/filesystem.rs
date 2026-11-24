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
use std::path::{Path, PathBuf};

pub use std::fs::{exists, metadata, remove_dir, remove_file, rename, symlink_metadata};

pub fn open_read_write(path: &Path, direct_io: bool) -> io::Result<std::fs::File> {
    let mut options = std::fs::OpenOptions::new();
    options.read(true).write(true);
    #[cfg(windows)]
    {
        crate::open_with_direct_io(&mut options, path, direct_io)
    }
    #[cfg(not(windows))]
    {
        let _ = direct_io;
        options.open(path)
    }
}

/// Opens an existing file whose successful writes include file data and metadata durability.
pub fn open_synchronous_read_write(path: &Path, direct_io: bool) -> io::Result<std::fs::File> {
    let mut options = std::fs::OpenOptions::new();
    options.read(true).write(true);
    open_synchronous_read_write_with_mode(
        &mut options,
        path,
        direct_io,
        SynchronousMode::DataAndMetadata,
    )
}

/// Opens a file whose successful writes include data and data-retrieval metadata durability.
pub fn open_data_synchronous_read_write_with_options(
    options: &mut std::fs::OpenOptions,
    path: &Path,
    direct_io: bool,
) -> io::Result<std::fs::File> {
    open_synchronous_read_write_with_mode(options, path, direct_io, SynchronousMode::Data)
}

enum SynchronousMode {
    Data,
    DataAndMetadata,
}

fn open_synchronous_read_write_with_mode(
    options: &mut std::fs::OpenOptions,
    path: &Path,
    direct_io: bool,
    mode: SynchronousMode,
) -> io::Result<std::fs::File> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(match mode {
            SynchronousMode::Data => libc::O_DSYNC,
            SynchronousMode::DataAndMetadata => libc::O_SYNC,
        });
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        use windows_sys::Win32::Storage::FileSystem::FILE_FLAG_WRITE_THROUGH;
        let _ = mode;
        options.custom_flags(FILE_FLAG_WRITE_THROUGH | crate::direct::direct_io_flag(direct_io));
    }
    let file = options.open(path)?;
    #[cfg(not(windows))]
    if direct_io {
        crate::enable_direct_io(&file)?;
    }
    Ok(file)
}

pub fn create_new_read_write(path: &Path, mode: u32, direct_io: bool) -> io::Result<std::fs::File> {
    let mut options = std::fs::OpenOptions::new();
    options.read(true).write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(mode);
    }
    #[cfg(windows)]
    let _ = mode;
    #[cfg(windows)]
    {
        crate::open_with_direct_io(&mut options, path, direct_io)
    }
    #[cfg(not(windows))]
    {
        let _ = direct_io;
        options.open(path)
    }
}

pub fn create_dir(path: &Path, mode: u32) -> io::Result<()> {
    let builder = std::fs::DirBuilder::new();
    #[cfg(unix)]
    let builder = {
        use std::os::unix::fs::DirBuilderExt;
        let mut builder = builder;
        builder.mode(mode);
        builder
    };
    #[cfg(windows)]
    let _ = mode;
    builder.create(path)
}

pub fn create_dir_all(path: &Path, mode: u32) -> io::Result<()> {
    let mut builder = std::fs::DirBuilder::new();
    builder.recursive(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::DirBuilderExt;
        builder.mode(mode);
    }
    #[cfg(windows)]
    let _ = mode;
    builder.create(path)
}

pub fn symlink_file(target: &Path, link: &Path) -> io::Result<()> {
    #[cfg(unix)]
    {
        std::os::unix::fs::symlink(target, link)
    }
    #[cfg(windows)]
    {
        std::os::windows::fs::symlink_file(target, link)
    }
}

pub fn canonical_existing(path: &Path) -> io::Result<PathBuf> {
    std::fs::canonicalize(path)
}

/// Makes a path absolute without requiring existence or resolving symlinks.
pub fn lexical_absolute(path: &Path) -> io::Result<PathBuf> {
    std::path::absolute(path)
}

pub fn truncate_path(path: &Path, length: u64) -> io::Result<()> {
    if length > i64::MAX as u64 {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    std::fs::OpenOptions::new()
        .write(true)
        .open(path)?
        .set_len(length)
}

/// Publishes a prepared file. Unix callers must subsequently sync the directory.
#[cfg(unix)]
pub fn publish_file(source: &Path, destination: &Path) -> io::Result<()> {
    std::fs::rename(source, destination)
}

#[cfg(windows)]
pub(crate) fn wide_path(value: &Path) -> io::Result<Vec<u16>> {
    use std::os::windows::ffi::OsStrExt;
    let mut path: Vec<u16> = value.as_os_str().encode_wide().collect();
    if path.contains(&0) {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    path.push(0);
    Ok(path)
}

#[cfg(windows)]
pub fn publish_file(source: &Path, destination: &Path) -> io::Result<()> {
    use windows_sys::Win32::Storage::FileSystem::{MoveFileExW, MOVEFILE_WRITE_THROUGH};
    let source = wide_path(source)?;
    let destination = wide_path(destination)?;
    if unsafe {
        MoveFileExW(
            source.as_ptr(),
            destination.as_ptr(),
            MOVEFILE_WRITE_THROUGH,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

#[cfg(test)]
mod tests;
