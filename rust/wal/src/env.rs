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

use crate::format::{Error, Result};
use crate::stream::{directory_is_empty, sync_directory, BaseInfo, Stream};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, MutexGuard};

struct State {
    stream: Option<Stream>,
    running: bool,
}

pub struct Env {
    directory: PathBuf,
    disk_soft_target: AtomicU64,
    state: Mutex<State>,
}

impl Env {
    pub fn new(directory: &str, disk_soft_target: u64) -> Self {
        Self {
            directory: PathBuf::from(directory),
            disk_soft_target: AtomicU64::new(disk_soft_target),
            state: Mutex::new(State {
                stream: None,
                running: false,
            }),
        }
    }

    fn lock(&self) -> Result<MutexGuard<'_, State>> {
        self.state.lock().map_err(|_| Error::Io)
    }

    fn stream_directory(&self) -> PathBuf {
        self.directory.clone()
    }

    pub fn start(&self) -> Result<()> {
        if self.directory.as_os_str().is_empty() {
            return Err(Error::Invalid);
        }
        let mut state = self.lock()?;
        if state.running {
            return Ok(());
        }
        let directory = self.stream_directory();
        if !directory.try_exists()? {
            return Err(Error::NotFound);
        }
        let stream = if directory_is_empty(&directory)? {
            None
        } else {
            let stream = Stream::open(directory.to_str().ok_or(Error::Invalid)?);
            stream.status()?;
            Some(stream)
        };
        state.stream = stream;
        state.running = true;
        Ok(())
    }

    pub fn stop(&self) {
        if let Ok(mut state) = self.lock() {
            state.running = false;
            if let Some(stream) = &state.stream {
                let _ = stream.close();
            }
        }
    }

    pub fn create(&self, base: BaseInfo, ref_scn: u64) -> Result<Stream> {
        let mut state = self.lock()?;
        if !state.running {
            return Err(Error::Closed);
        }
        let directory = self.stream_directory();
        if state.stream.is_some() {
            return Err(Error::Duplicate);
        }
        if !directory.try_exists()? {
            return Err(Error::NotFound);
        }
        if !directory_is_empty(&directory)? {
            return Err(Error::Duplicate);
        }
        let result = (|| {
            let stream =
                Stream::create(directory.to_str().ok_or(Error::Invalid)?, base, ref_scn);
            stream.status()?;
            Ok(stream)
        })();
        let stream = match result {
            Ok(stream) => stream,
            Err(error) => {
                let _ = clear_directory(&directory);
                let _ = sync_directory(&directory);
                return Err(error);
            }
        };
        state.stream = Some(stream.clone());
        Ok(stream)
    }

    pub fn open(&self) -> Result<Stream> {
        let state = self.lock()?;
        if !state.running {
            return Err(Error::Closed);
        }
        state.stream.clone().ok_or(Error::NotFound)
    }

    pub fn remove(&self) -> Result<()> {
        let mut state = self.lock()?;
        if !state.running {
            return Err(Error::Closed);
        }
        if let Some(stream) = state.stream.take() {
            stream.close()?;
        }
        clear_directory(&self.directory)?;
        sync_directory(&self.directory)?;
        Ok(())
    }

    pub fn disk_usage(&self) -> Result<(u64, u64)> {
        let state = self.lock()?;
        let used = match &state.stream {
            Some(stream) => stream.progress()?.used_disk,
            None => 0,
        };
        Ok((used, self.disk_soft_target()))
    }

    pub fn update_disk_soft_target(&self, disk_soft_target: u64) {
        self.disk_soft_target
            .store(disk_soft_target, Ordering::Release);
    }

    pub fn disk_soft_target(&self) -> u64 {
        self.disk_soft_target.load(Ordering::Acquire)
    }
}

fn remove_path_if_exists(path: &Path) -> Result<()> {
    match fs::symlink_metadata(path) {
        Ok(metadata) if metadata.is_dir() => fs::remove_dir_all(path)?,
        Ok(_) => fs::remove_file(path)?,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(error) => return Err(error.into()),
    }
    Ok(())
}

fn clear_directory(path: &Path) -> Result<()> {
    for entry in fs::read_dir(path)? {
        remove_path_if_exists(&entry?.path())?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(0);

    struct Directory(PathBuf);

    impl Directory {
        fn new() -> Self {
            let sequence = NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir()
                .join(format!("seekdb-wal-env-{}-{sequence}", std::process::id()));
            let _ = fs::remove_dir_all(&path);
            fs::create_dir(&path).unwrap();
            Self(path)
        }
    }

    impl Drop for Directory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn base() -> BaseInfo {
        BaseInfo {
            curr_lsn: 0,
            prev_lsn: 0,
            prev_scn: 0,
            prev_log_id: 0,
            prev_accumulated_checksum: 0,
        }
    }

    #[test]
    fn lifecycle_reloads_one_stream_and_removes_it() {
        let directory = Directory::new();
        let env = Env::new(directory.0.to_str().unwrap(), 1024);
        env.start().unwrap();
        assert_eq!(env.open().err().unwrap(), Error::NotFound);
        let stream = env.create(base(), 0).unwrap();
        assert_eq!(env.create(base(), 0).err().unwrap(), Error::Duplicate);
        let appended = stream.append(b"one", 10).unwrap();
        assert_eq!(
            env.disk_usage().unwrap(),
            (stream.progress().unwrap().used_disk, 1024)
        );
        env.update_disk_soft_target(2048);
        assert_eq!(env.disk_usage().unwrap().1, 2048);

        env.stop();
        env.start().unwrap();
        let reopened = env.open().unwrap();
        assert_eq!(reopened.progress().unwrap().end_lsn, appended.end_lsn);
        env.remove().unwrap();
        assert_eq!(env.open().err().unwrap(), Error::NotFound);
        assert!(directory_is_empty(&env.stream_directory()).unwrap());
        env.remove().unwrap();
    }

    #[test]
    fn failed_creation_rolls_back() {
        let directory = Directory::new();
        let env = Env::new(directory.0.to_str().unwrap(), 1024);
        env.start().unwrap();

        let mut invalid = base();
        invalid.prev_lsn = 1;
        assert_eq!(env.create(invalid, 0).err().unwrap(), Error::Invalid);
        assert!(directory_is_empty(&env.stream_directory()).unwrap());
        env.create(base(), 0).unwrap();
        assert!(env.stream_directory().is_dir());
    }

    #[test]
    fn creation_requires_an_existing_directory() {
        let directory = Directory::new();
        fs::remove_dir(&directory.0).unwrap();
        let env = Env::new(directory.0.to_str().unwrap(), 1024);

        assert_eq!(env.start().err(), Some(Error::NotFound));
    }

    #[test]
    fn remove_cleans_an_unopened_partial_stream() {
        let directory = Directory::new();
        let env = Env::new(directory.0.to_str().unwrap(), 1024);
        env.start().unwrap();
        fs::write(env.stream_directory().join("manifest"), b"partial").unwrap();

        env.remove().unwrap();

        assert!(directory_is_empty(&env.stream_directory()).unwrap());
        env.remove().unwrap();
    }

    #[test]
    fn corrupt_stream_is_preserved_and_refuses_open() {
        let directory = Directory::new();
        let env = Env::new(directory.0.to_str().unwrap(), 1024);
        env.start().unwrap();
        env.create(base(), 0).unwrap().append(b"one", 10).unwrap();
        env.stop();
        let manifest = env.stream_directory().join("manifest");
        fs::write(&manifest, [0u8; 256]).unwrap();
        let damaged = fs::read(&manifest).unwrap();

        assert!(env.start().is_err());
        assert!(env.stream_directory().is_dir());
        assert_eq!(fs::read(manifest).unwrap(), damaged);
    }
}
