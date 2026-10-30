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
use crate::tests::Directory;
use std::fs;
use std::path::Path;

fn bytes(path: &Path) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        path.as_os_str().as_bytes().to_vec()
    }
    #[cfg(windows)]
    {
        path.to_str().unwrap().as_bytes().to_vec()
    }
}

#[test]
fn empty_and_failed_cursors_end_permanently() {
    let directory = Directory::new();
    let mut empty = read_directory(&bytes(&directory.0));
    assert!(empty.next_entry().code == ffi::DirectoryStepCode::End);
    fs::write(directory.0.join("later"), b"x").unwrap();
    assert!(empty.next_entry().code == ffi::DirectoryStepCode::End);
    for path in [&b""[..], &bytes(&directory.0.join("missing"))] {
        let mut failed = read_directory(path);
        let step = failed.next_entry();
        assert!(step.code == ffi::DirectoryStepCode::OpenError);
        assert_eq!(step.error, libc::ENOENT);
        assert!(failed.next_entry().code == ffi::DirectoryStepCode::End);
    }
    let mut wrong_type = read_directory(&bytes(&directory.0.join("later")));
    assert!(wrong_type.next_entry().code == ffi::DirectoryStepCode::OpenError);
    assert!(wrong_type.next_entry().code == ffi::DirectoryStepCode::End);
}

#[test]
fn owned_entry_survives_the_next_pull_and_directory_mutation() {
    let directory = Directory::new();
    fs::write(directory.0.join("first"), b"a").unwrap();
    fs::write(directory.0.join("second"), b"b").unwrap();
    let mut cursor = read_directory(&bytes(&directory.0));
    let first = cursor.next_entry();
    assert!(first.code == ffi::DirectoryStepCode::Entry);
    assert!(first.name == b"first" || first.name == b"second");
    let name = String::from_utf8(first.name.clone()).unwrap();
    fs::rename(directory.0.join(&name), directory.0.join("moved")).unwrap();
    let mut pulls = 0;
    loop {
        let entry = cursor.next_entry();
        assert!(
            entry.code == ffi::DirectoryStepCode::Entry
                || entry.code == ffi::DirectoryStepCode::End
        );
        if entry.code == ffi::DirectoryStepCode::End {
            break;
        }
        assert!(entry.name != b"." && entry.name != b"..");
        pulls += 1;
        assert!(pulls < 10);
    }
    assert_eq!(first.name, name.as_bytes());
    assert_eq!(
        fs::read(directory.0.join("moved")).unwrap(),
        if name == "first" { b"a" } else { b"b" }
    );
}

#[cfg(unix)]
#[test]
fn raw_names_and_symlink_types_survive_without_stat() {
    use std::os::unix::ffi::OsStrExt;
    use std::os::unix::fs::{symlink, MetadataExt};
    let directory = Directory::new();
    let name = std::ffi::OsStr::from_bytes(b"bad-\xff");
    fs::write(directory.0.join(name), b"x").unwrap();
    symlink("nonexistent", directory.0.join("link")).unwrap();
    let mut cursor = read_directory(&bytes(&directory.0));
    let mut found = 0;
    loop {
        let entry = cursor.next_entry();
        if entry.code == ffi::DirectoryStepCode::End {
            break;
        }
        assert!(entry.code == ffi::DirectoryStepCode::Entry);
        assert_eq!(entry.name_length, entry.name.len() as u64);
        let info = fs::symlink_metadata(directory.0.join(std::ffi::OsStr::from_bytes(&entry.name)))
            .unwrap();
        assert_eq!(entry.inode, info.ino());
        assert!(usize::from(entry.record_length) > entry.name.len());
        if entry.name == b"link" {
            assert_eq!(entry.raw_type, u32::from(libc::DT_LNK));
        } else {
            assert_eq!(entry.name, name.as_bytes());
            assert_eq!(entry.raw_type, u32::from(libc::DT_REG));
        }
        found += 1;
    }
    assert_eq!(found, 2);
}

#[cfg(target_os = "linux")]
#[test]
fn directory_descriptors_close_on_drop_and_end() {
    fn handles(path: &Path) -> usize {
        fs::read_dir("/proc/self/fd")
            .unwrap()
            .filter_map(Result::ok)
            .filter(|entry| fs::read_link(entry.path()).is_ok_and(|target| target == path))
            .count()
    }
    let directory = Directory::new();
    let path = bytes(&directory.0);
    let cursor = read_directory(&path);
    assert_eq!(handles(&directory.0), 2);
    drop(cursor);
    assert_eq!(handles(&directory.0), 0);
    let mut cursor = read_directory(&path);
    assert!(cursor.next_entry().code == ffi::DirectoryStepCode::End);
    assert_eq!(handles(&directory.0), 0);
}

#[test]
fn handles_retain_owners_and_reject_invalid_transitions() {
    let temporary = Directory::new();
    fs::create_dir(temporary.0.join("child")).unwrap();
    fs::write(temporary.0.join("file"), b"abc").unwrap();
    let mut handle = new_directory_handle();
    let missing = inspect_at(&handle, b"file", false);
    assert_ne!(missing.error.error, 0);
    assert_eq!(missing.length, 0);
    assert!(missing.presence == ffi::Presence::Missing);
    assert_ne!(
        handle
            .open_directory(&bytes(&temporary.0.join("missing")))
            .error,
        0
    );
    assert_eq!(handle.open_directory(&bytes(&temporary.0)).error, 0);
    assert_ne!(
        handle
            .open_directory(&bytes(&temporary.0.join("missing")))
            .error,
        0
    );
    assert_eq!(inspect_at(&handle, b"file", false).length, 3);
    assert_ne!(inspect_at(&handle, b"../file", false).error.error, 0);
    let mut child = new_directory_handle();
    assert_eq!(open_child_directory(&handle, b"child", &mut child).error, 0);
    let mut cursor = directory_entries(&handle);
    drop(handle);
    let mut count = 0;
    loop {
        let entry = cursor.next_entry();
        if entry.code == ffi::DirectoryStepCode::End {
            break;
        }
        assert!(entry.code == ffi::DirectoryStepCode::Entry);
        assert_eq!(entry.detail.error, 0);
        count += 1;
    }
    assert_eq!(count, 2);
    assert!(directory_entries(&child).next_entry().code == ffi::DirectoryStepCode::End);
}

#[cfg(target_os = "linux")]
#[test]
fn terminal_close_error_keeps_native_detail_and_zero_outputs() {
    let temporary = Directory::new();
    let mut cursor = read_directory(&bytes(&temporary.0));
    crate::directory::fail_next_stream_close(libc::EIO);
    let step = cursor.next_entry();
    assert!(step.code == ffi::DirectoryStepCode::ReadError);
    assert!(step.detail.domain == ffi::ErrorDomain::Posix);
    assert_eq!(step.detail.native_code, libc::EIO);
    assert_eq!(step.error, libc::EIO);
    assert!(step.name.is_empty());
    assert_eq!(step.name_length, 0);
    assert!(cursor.next_entry().code == ffi::DirectoryStepCode::End);
}
