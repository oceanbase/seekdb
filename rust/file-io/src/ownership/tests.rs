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

use super::*;
use std::fs::{self, OpenOptions};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Barrier};

struct Directory(PathBuf);

impl Directory {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = std::env::temp_dir().join(format!(
            "file-owner-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        fs::write(path.join("file"), b"owned").unwrap();
        Self(path)
    }

    fn path(&self) -> PathBuf {
        self.0.join("file")
    }
}

impl Drop for Directory {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}

fn claim(path: &Path) -> io::Result<ExclusiveFile> {
    ExclusiveFile::try_new(OpenOptions::new().read(true).write(true).open(path)?)
}

fn assert_claimed(path: &Path) {
    let error = claim(path).err().expect("another owner must exclude open");
    assert_eq!(error.kind(), io::ErrorKind::WouldBlock);
}

#[test]
fn last_shared_owner_releases_exclusion() {
    let directory = Directory::new();
    let owner = Arc::new(claim(&directory.path()).unwrap());
    let retained = Arc::clone(&owner);
    drop(owner);
    assert_claimed(&directory.path());
    let owner = Arc::try_unwrap(retained).ok().unwrap();
    assert_claimed(&directory.path());
    crate::write_all_at(owner.as_file(), b"still owned", 0).unwrap();
    drop(owner);
    let reopened = claim(&directory.path()).unwrap();
    let mut bytes = [0; 11];
    crate::read_exact_at(reopened.as_file(), &mut bytes, 0).unwrap();
    assert_eq!(&bytes, b"still owned");
}

#[test]
fn renamed_and_replaced_paths_follow_file_identity() {
    let directory = Directory::new();
    let owner = claim(&directory.path()).unwrap();
    let renamed = directory.0.join("renamed");
    fs::rename(directory.path(), &renamed).unwrap();
    assert_claimed(&renamed);
    fs::write(directory.path(), b"replacement").unwrap();
    let replacement = claim(&directory.path()).unwrap();
    let mut bytes = [0; 5];
    crate::read_exact_at(owner.as_file(), &mut bytes, 0).unwrap();
    assert_eq!(&bytes, b"owned");
    drop(owner);
    drop(claim(&renamed).unwrap());
    assert_claimed(&directory.path());
    drop(replacement);
    drop(claim(&directory.path()).unwrap());
}

#[test]
#[cfg(unix)]
fn relative_dot_and_symlink_paths_share_exclusion() {
    use std::os::unix::fs::symlink;

    let directory = Directory::new();
    let owner = claim(&directory.path()).unwrap();
    let symlink_path = directory.0.join("symlink");
    symlink(directory.path(), &symlink_path).unwrap();
    let cwd = std::env::current_dir().unwrap();
    let mut relative = PathBuf::new();
    for _ in cwd.ancestors().skip(1) {
        relative.push("..");
    }
    relative.push(directory.path().strip_prefix("/").unwrap());
    for path in [relative, directory.0.join(".").join("file"), symlink_path] {
        assert_claimed(&path);
    }
    drop(owner);
    drop(claim(&directory.path()).unwrap());
}

#[test]
#[cfg(not(target_os = "emscripten"))]
fn hardlink_alias_shares_native_exclusion() {
    let directory = Directory::new();
    let alias = directory.0.join("hardlink");
    fs::hard_link(directory.path(), &alias).unwrap();
    let owner = claim(&directory.path()).unwrap();
    assert_claimed(&alias);
    drop(owner);
    drop(claim(&alias).unwrap());
}

#[test]
#[cfg(target_os = "emscripten")]
fn wasmfs_reports_hardlinks_as_unsupported() {
    let directory = Directory::new();
    let error = fs::hard_link(directory.path(), directory.0.join("hardlink")).unwrap_err();
    assert_eq!(error.raw_os_error(), Some(libc::EMLINK));
}

#[test]
fn concurrent_claims_admit_one_owner() {
    let directory = Directory::new();
    let barrier = Arc::new(Barrier::new(8));
    let threads: Vec<_> = (0..8)
        .map(|_| {
            let path = directory.path();
            let barrier = Arc::clone(&barrier);
            std::thread::spawn(move || {
                let file = OpenOptions::new()
                    .read(true)
                    .write(true)
                    .open(path)
                    .unwrap();
                barrier.wait();
                let result = ExclusiveFile::try_new(file);
                barrier.wait();
                result
            })
        })
        .collect();
    let results: Vec<_> = threads
        .into_iter()
        .map(|thread| thread.join().unwrap())
        .collect();
    assert_eq!(results.iter().filter(|result| result.is_ok()).count(), 1);
    for error in results.iter().filter_map(|result| result.as_ref().err()) {
        assert_eq!(error.kind(), io::ErrorKind::WouldBlock);
    }
    assert_claimed(&directory.path());
    drop(results);
    drop(claim(&directory.path()).unwrap());
}
