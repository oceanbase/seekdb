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

//! seekdb's timestamped file convention on top of spdlog's public Sink API.
//! Open files, append/reopen/rotation and retention have one owner.
use chrono::Local;
use std::ffi::CString;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

#[cfg(unix)]
use std::os::unix::{ffi::OsStrExt, fs::MetadataExt, fs::OpenOptionsExt, io::AsRawFd};

#[cfg(windows)]
use std::os::windows::{ffi::OsStrExt, io::AsRawHandle};

#[cfg(windows)]
fn identity(file: &File) -> io::Result<(u32, u32, u32)> {
    use windows_sys::Win32::Storage::FileSystem::{
        GetFileInformationByHandle, BY_HANDLE_FILE_INFORMATION,
    };
    let mut info = std::mem::MaybeUninit::<BY_HANDLE_FILE_INFORMATION>::uninit();
    if unsafe { GetFileInformationByHandle(file.as_raw_handle(), info.as_mut_ptr()) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let info = unsafe { info.assume_init() };
    Ok((
        info.dwVolumeSerialNumber,
        info.nFileIndexHigh,
        info.nFileIndexLow,
    ))
}

pub(crate) struct Limits {
    pub size: u64,
    pub count: u64,
}

pub(crate) struct Output {
    path: PathBuf,
    file: Option<File>,
    redirect: bool,
    size: u64,
    last_check: Instant,
    last_disk_check: Instant,
    can_print: bool,
    last_archive: String,
    buffer: Vec<u8>,
    buffered_records: usize,
}

pub(crate) const BUFFER_CAPACITY: usize = 64 * 1024;
const BUFFER_RECORDS: usize = 256;

// Unlike write_all, retain the progress on failure. Buffered records are
// discarded after an error, never retried with an already written prefix.
fn write_bytes(writer: &mut impl Write, bytes: &[u8]) -> (usize, io::Result<()>) {
    let mut written = 0;
    while written < bytes.len() {
        match writer.write(&bytes[written..]) {
            Ok(0) => return (written, Err(io::ErrorKind::WriteZero.into())),
            Ok(n) => written += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return (written, Err(e)),
        }
    }
    (written, Ok(()))
}

fn open(path: &Path) -> io::Result<File> {
    let mut options = OpenOptions::new();
    options.create(true).append(true);
    #[cfg(unix)]
    options.mode(0o644);
    options.open(path)
}

impl Output {
    pub fn new(path: PathBuf, redirect: bool) -> io::Result<Self> {
        let file = if path.as_os_str().is_empty() {
            None
        } else {
            Some(open(&path)?)
        };
        let size = file
            .as_ref()
            .map(|f| f.metadata().map(|m| m.len()))
            .transpose()?
            .unwrap_or(0);
        let output = Self {
            path,
            file,
            redirect,
            size,
            last_check: Instant::now(),
            last_disk_check: Instant::now(),
            can_print: true,
            last_archive: String::new(),
            buffer: Vec::new(),
            buffered_records: 0,
        };
        output.redirect_stdio()?;
        Ok(output)
    }

    fn redirect_stdio(&self) -> io::Result<()> {
        if !self.redirect {
            return Ok(());
        }
        if let Some(file) = &self.file {
            #[cfg(unix)]
            let fd = file.as_raw_fd();
            // std::File uses Win32 HANDLEs; stdout/stderr use CRT descriptors.
            #[cfg(windows)]
            let fd = {
                let _ = file;
                let path = CString::new(self.path.to_string_lossy().as_bytes())?;
                let fd = unsafe {
                    libc::open(
                        path.as_ptr(),
                        libc::O_WRONLY | libc::O_APPEND | libc::O_BINARY,
                    )
                };
                if fd < 0 {
                    return Err(io::Error::last_os_error());
                }
                fd
            };
            let result = (|| {
                for target in [1, 2] {
                    if unsafe { libc::dup2(fd, target) } < 0 {
                        return Err(io::Error::last_os_error());
                    }
                }
                Ok(())
            })();
            #[cfg(windows)]
            unsafe {
                libc::close(fd);
            }
            result?;
        }
        Ok(())
    }

    fn reopen(&mut self) -> io::Result<()> {
        self.flush()?;
        let file = open(&self.path)?;
        let size = file.metadata()?.len();
        // Keep the previous file usable if opening the replacement fails.
        let previous = self.file.replace(file);
        if let Err(error) = self.redirect_stdio() {
            self.file = previous;
            let _ = self.redirect_stdio();
            return Err(error);
        }
        self.size = size;
        Ok(())
    }

    pub fn check(&mut self) -> io::Result<()> {
        self.flush()?;
        self.last_check = Instant::now();
        if let Some(file) = &self.file {
            match fs::metadata(&self.path) {
                Err(e) if e.kind() == io::ErrorKind::NotFound => self.reopen()?,
                Err(e) => return Err(e),
                Ok(metadata) => {
                    #[cfg(unix)]
                    {
                        let current = file.metadata()?;
                        if metadata.ino() != current.ino() || metadata.dev() != current.dev() {
                            self.reopen()?;
                        } else {
                            self.size = metadata.len();
                        }
                    }
                    #[cfg(windows)]
                    {
                        if identity(file)? != identity(&open(&self.path)?)? {
                            self.reopen()?;
                        } else {
                            self.size = metadata.len();
                        }
                    }
                }
            }
        }
        Ok(())
    }

    fn check_disk(&mut self) -> io::Result<()> {
        self.last_disk_check = Instant::now();
        #[cfg(unix)]
        if self.file.is_some() {
            let path = CString::new(self.path.as_os_str().as_bytes())?;
            let mut stat = std::mem::MaybeUninit::<libc::statvfs>::uninit();
            if unsafe { libc::statvfs(path.as_ptr(), stat.as_mut_ptr()) } == 0 {
                let stat = unsafe { stat.assume_init() };
                // statvfs field widths differ between supported Unix targets.
                #[allow(clippy::unnecessary_cast)]
                let available = (stat.f_bfree as u64).saturating_mul(stat.f_frsize as u64);
                self.can_print = available > 32 * 1024 * 1024;
            }
        }
        #[cfg(windows)]
        if self.file.is_some() {
            let directory = self
                .path
                .parent()
                .filter(|p| !p.as_os_str().is_empty())
                .unwrap_or(Path::new("."));
            let path: Vec<u16> = directory.as_os_str().encode_wide().chain(Some(0)).collect();
            let mut available = 0;
            if unsafe {
                windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW(
                    path.as_ptr(),
                    &mut available,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                )
            } != 0
            {
                self.can_print = available > 32 * 1024 * 1024;
            }
        }
        Ok(())
    }

    pub fn write(
        &mut self,
        bytes: &[u8],
        limits: &Limits,
        info: Option<&str>,
        asynchronous: bool,
    ) -> io::Result<()> {
        if self.last_check.elapsed() >= Duration::from_secs(5) {
            self.check()?;
        }
        if self.last_disk_check.elapsed() >= Duration::from_secs(2) {
            self.check_disk()?;
        }
        if !self.can_print {
            return Err(io::Error::from_raw_os_error(libc::ENOSPC));
        }
        if self.file.is_some() {
            if asynchronous && bytes.len() < BUFFER_CAPACITY {
                if self.buffer.len() + bytes.len() > BUFFER_CAPACITY
                    || self.buffered_records == BUFFER_RECORDS
                {
                    self.flush()?;
                }
                if self.buffer.capacity() == 0 {
                    self.buffer.reserve_exact(BUFFER_CAPACITY);
                }
                self.buffer.extend_from_slice(bytes);
                self.buffered_records += 1;
                self.size = self.size.saturating_add(bytes.len() as u64);
                if self.buffer.len() == BUFFER_CAPACITY {
                    self.flush()?;
                }
            } else {
                self.flush()?;
                let (written, result) = write_bytes(self.file.as_mut().unwrap(), bytes);
                self.size = self.size.saturating_add(written as u64);
                result?;
            }
            if limits.size > 0 && self.size >= limits.size {
                self.flush()?;
                // The record is already written. A maintenance failure must
                // not make it appear dropped or encourage callers to resend.
                let _ = self.rotate(limits, info);
            }
        } else {
            io::stderr().lock().write_all(bytes)?;
        }
        Ok(())
    }

    pub fn buffered(&self) -> bool {
        self.buffered_records != 0
    }

    pub fn flush(&mut self) -> io::Result<()> {
        if !self.buffered() {
            return Ok(());
        }
        let (written, result) = write_bytes(self.file.as_mut().unwrap(), &self.buffer);
        self.size = self
            .size
            .saturating_sub((self.buffer.len() - written) as u64);
        self.buffer.clear();
        self.buffered_records = 0;
        result
    }

    fn rotate(&mut self, limits: &Limits, info: Option<&str>) -> io::Result<()> {
        // Keep the existing millisecond timestamp naming convention.
        // Never replace an archive if two rotations happen in one millisecond.
        let mut now = Local::now().naive_local();
        if let Ok(last) =
            chrono::NaiveDateTime::parse_from_str(&self.last_archive, "%Y%m%d%H%M%S%3f")
        {
            if now <= last {
                now = last + chrono::Duration::milliseconds(1);
            }
        }
        let archive = loop {
            let suffix = now.format("%Y%m%d%H%M%S%3f").to_string();
            let mut name = self.path.as_os_str().to_os_string();
            name.push(".");
            name.push(&suffix);
            let path = PathBuf::from(name);
            if suffix > self.last_archive && !path.try_exists()? {
                self.last_archive = suffix;
                break path;
            }
            now += chrono::Duration::milliseconds(1);
        };
        // Open the replacement before renaming the active file so a failed
        // open leaves the original file and path usable.
        let mut temporary = self.path.as_os_str().to_os_string();
        temporary.push(format!(".new.{}.{}", std::process::id(), self.last_archive));
        let temporary = PathBuf::from(temporary);
        let mut options = OpenOptions::new();
        options.create_new(true).append(true);
        #[cfg(unix)]
        options.mode(0o644);
        let replacement = options.open(&temporary)?;
        if let Err(error) = fs::rename(&self.path, &archive) {
            drop(replacement);
            let _ = fs::remove_file(&temporary);
            return Err(error);
        }
        if let Err(error) = fs::rename(&temporary, &self.path) {
            let _ = fs::rename(&archive, &self.path);
            drop(replacement);
            let _ = fs::remove_file(&temporary);
            return Err(error);
        }
        let previous = self.file.replace(replacement);
        if let Err(error) = self.redirect_stdio() {
            self.file = previous;
            let _ = fs::rename(&self.path, &temporary);
            let _ = fs::rename(&archive, &self.path);
            let _ = self.redirect_stdio();
            let _ = fs::remove_file(&temporary);
            return Err(error);
        }
        self.size = 0;
        if let (Some(info), Some(file)) = (info, &mut self.file) {
            let line = format!(
                "[{}] INFO  New syslog file info: [{}]\n",
                Local::now().format("%Y-%m-%d %H:%M:%S%.6f"),
                info
            );
            file.write_all(line.as_bytes())?;
            self.size += line.len() as u64;
        }
        if limits.count > 0 {
            self.prune(limits.count)?;
        }
        Ok(())
    }

    pub fn prune(&self, count: u64) -> io::Result<()> {
        if count == 0 || self.file.is_none() {
            return Ok(());
        }
        let dir = self
            .path
            .parent()
            .filter(|p| !p.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
        let mut prefix = self.path.file_name().unwrap().to_os_string();
        prefix.push(".");
        let prefix = prefix.to_string_lossy();
        let mut archives = Vec::new();
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if let Some(suffix) = name.strip_prefix(prefix.as_ref()) {
                if suffix.len() == 17 && suffix.bytes().all(|b| b.is_ascii_digit()) {
                    archives.push(entry.path());
                }
            }
        }
        archives.sort_unstable();
        // max_file_index counts the active file as in the original writer.
        let remove = archives
            .len()
            .saturating_sub(count.saturating_sub(1) as usize);
        for file in archives.into_iter().take(remove) {
            match fs::remove_file(file) {
                Err(e) if e.kind() == io::ErrorKind::NotFound => (),
                result => result?,
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn interrupted_and_partial_writes_retain_exact_progress() {
        struct Partial {
            calls: usize,
            bytes: Vec<u8>,
        }
        impl Write for Partial {
            fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
                self.calls += 1;
                match self.calls {
                    1 => Err(io::ErrorKind::Interrupted.into()),
                    2 => {
                        self.bytes.extend_from_slice(&bytes[..3]);
                        Ok(3)
                    }
                    _ => Err(io::Error::from_raw_os_error(libc::ENOSPC)),
                }
            }
            fn flush(&mut self) -> io::Result<()> {
                Ok(())
            }
        }
        let mut writer = Partial {
            calls: 0,
            bytes: Vec::new(),
        };
        let (written, result) = write_bytes(&mut writer, b"ab\ncd\n");
        assert_eq!(written, 3);
        assert_eq!(writer.bytes, b"ab\n");
        assert_eq!(result.unwrap_err().raw_os_error(), Some(libc::ENOSPC));
    }

    #[test]
    fn clock_rollback_does_not_walk_milliseconds_or_replace_archives() {
        let directory =
            std::env::temp_dir().join(format!("logger-clock-test-{}", std::process::id()));
        fs::create_dir(&directory).unwrap();
        let path = directory.join("seekdb.log");
        let mut output = Output::new(path.clone(), false).unwrap();
        let previous = Local::now().naive_local() + chrono::Duration::days(365);
        output.last_archive = previous.format("%Y%m%d%H%M%S%3f").to_string();
        let old = output.last_archive.clone();
        output.file.as_mut().unwrap().write_all(b"old\n").unwrap();
        let start = Instant::now();
        output.rotate(&Limits { size: 1, count: 0 }, None).unwrap();
        assert!(start.elapsed() < Duration::from_secs(2));
        assert!(output.last_archive > old);
        let archive = directory.join(format!("seekdb.log.{}", output.last_archive));
        assert_eq!(fs::read(archive).unwrap(), b"old\n");
        drop(output);
        fs::remove_dir_all(directory).unwrap();
    }
}
