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

use super::{error_number, ffi, legacy_path, result};
use crate::{AppendError, Appender, BufferLayout, CreateMode, IoMode, Reader};
use std::io;

pub struct ReaderFacade {
    reader: Option<Reader>,
}
pub struct AppenderFacade {
    appender: Option<Appender>,
}
pub fn new_reader() -> Box<ReaderFacade> {
    Box::new(ReaderFacade { reader: None })
}
pub fn new_appender() -> Box<AppenderFacade> {
    Box::new(AppenderFacade { appender: None })
}
fn mode(direct: bool) -> IoMode {
    if direct {
        IoMode::PreferDirect
    } else {
        IoMode::Buffered
    }
}
fn code(result: io::Result<()>) -> i32 {
    result.err().map_or(0, error_number)
}
impl ReaderFacade {
    pub fn open_reader(&mut self, path: &[u8], direct: bool, alignment: usize) -> i32 {
        if self.reader.is_some() {
            return libc::EALREADY;
        }
        code((|| {
            let layout = BufferLayout::new(1 << 20, alignment)?;
            let owner = Reader::open(legacy_path(path)?, mode(direct), layout)?;
            self.reader = Some(owner);
            Ok(())
        })())
    }
    pub fn read(&mut self, bytes: &mut [u8], offset: u64) -> ffi::IoResult {
        match &mut self.reader {
            Some(reader) => result(reader.read_at(bytes, offset)),
            None => ffi::IoResult {
                bytes: 0,
                error: libc::EBADF,
            },
        }
    }
    pub fn reader_len(&self, length: &mut u64) -> i32 {
        *length = 0;
        code((|| {
            *length = self
                .reader
                .as_ref()
                .ok_or(io::ErrorKind::NotConnected)?
                .len()?;
            Ok(())
        })())
    }
}
impl AppenderFacade {
    pub fn open_appender(
        &mut self,
        path: &[u8],
        create: u8,
        direct: bool,
        alignment: usize,
    ) -> i32 {
        if self.is_open() {
            return libc::EALREADY;
        }
        code((|| {
            let create = match create {
                0 => CreateMode::Existing,
                1 => CreateMode::IfMissing,
                2 => CreateMode::TruncateExisting,
                3 => CreateMode::CreateOrTruncate,
                4 => CreateMode::Exclusive,
                _ => return Err(io::ErrorKind::InvalidInput.into()),
            };
            let layout = BufferLayout::new(2 << 20, alignment)?;
            let owner = Appender::open(legacy_path(path)?, create, mode(direct), layout)?;
            self.appender = Some(owner);
            Ok(())
        })())
    }
    pub fn append(&mut self, bytes: &[u8]) -> ffi::IoResult {
        match &mut self.appender {
            None => ffi::IoResult {
                bytes: 0,
                error: libc::EBADF,
            },
            Some(appender) => match appender.append(bytes) {
                Ok(()) => ffi::IoResult {
                    bytes: bytes.len(),
                    error: 0,
                },
                Err(AppendError::Rejected(error)) => ffi::IoResult {
                    bytes: 0,
                    error: error_number(error),
                },
                Err(AppendError::Failed { accepted, error }) => ffi::IoResult {
                    bytes: accepted,
                    error: error_number(error),
                },
            },
        }
    }
    pub fn sync(&mut self) -> i32 {
        code(
            self.appender
                .as_mut()
                .ok_or_else(|| io::ErrorKind::NotConnected.into())
                .and_then(Appender::sync),
        )
    }
    pub fn finish(&mut self) -> i32 {
        self.appender.as_mut().map_or(0, |a| code(a.finish()))
    }
    pub fn position(&self) -> u64 {
        self.appender.as_ref().map_or(0, Appender::position)
    }
    pub fn is_open(&self) -> bool {
        self.appender.as_ref().is_some_and(Appender::is_open)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reopen_rejection_and_failed_construction_preserve_lifecycles() {
        let path = std::env::temp_dir().join(format!("facade-reopen-{}", std::process::id()));
        let bytes = path.to_str().unwrap().as_bytes();
        let _ = std::fs::remove_file(&path);
        let mut a = new_appender();
        assert_eq!(a.open_appender(b"", 4, false, 4096), libc::EINVAL);
        assert_eq!(a.open_appender(bytes, 4, false, 4 << 20), libc::EINVAL);
        assert!(!path.exists());
        assert_eq!(a.open_appender(bytes, 4, false, 4096), 0);
        assert_eq!(a.append(b"first").error, 0);
        assert_eq!(a.open_appender(b"", 4, false, 4096), libc::EALREADY);
        assert_eq!(a.position(), 5);
        assert!(a.is_open());
        assert_eq!(a.append(b"second").error, 0);
        assert_eq!(a.finish(), 0);
        assert_eq!(a.finish(), 0);
        assert_eq!(std::fs::read(&path).unwrap(), b"firstsecond");
        assert_eq!(a.open_appender(bytes, 4, false, 4096), libc::EEXIST);
        assert_eq!(a.open_appender(bytes, 0, false, 4096), 0);
        assert_eq!(a.append(b"third").error, 0);
        assert_eq!(a.finish(), 0);
        let mut reader = new_reader();
        assert_eq!(reader.open_reader(b"", false, 4096), libc::EINVAL);
        assert_eq!(reader.open_reader(bytes, false, 4096), 0);
        assert_eq!(reader.open_reader(b"", false, 4096), libc::EALREADY);
        let mut length = 0;
        assert_eq!(reader.reader_len(&mut length), 0);
        assert_eq!(length, 16);
        let mut output = [0; 32];
        let result = reader.read(&mut output, 0);
        assert_eq!(result.error, 0);
        assert_eq!(result.bytes, 16);
        assert_eq!(&output[..16], b"firstsecondthird");
        drop(reader);
        drop(a);
        std::fs::remove_file(path).unwrap();
    }
}
