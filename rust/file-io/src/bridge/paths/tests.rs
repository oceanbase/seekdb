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

fn fields(value: &ffi::FileStat) -> [u64; 15] {
    [
        value.rdev,
        value.dev,
        value.inode,
        u64::from(value.mask),
        u64::from(value.mode),
        u64::from(value.nlink),
        value.uid,
        value.gid,
        value.size,
        value.block_cnt,
        value.block_size,
        value.atime_s as u64,
        value.mtime_s as u64,
        value.ctime_s as u64,
        value.btime_s as u64,
    ]
}

#[test]
fn guarded_removal_existing_file_mkdir_and_failed_outputs() {
    let root = Directory::new();
    let file = bytes(&root.0.join("data"));
    fs::write(root.0.join("data"), b"contents").unwrap();
    assert_eq!(mkdir_path(&file, 0o700), 0);
    assert_eq!(rmdir_path(&file), libc::ENOENT);
    assert_eq!(unlink_path(&bytes(&root.0)), libc::ENOENT);
    let child = bytes(&root.0.join("child"));
    assert_eq!(mkdir_path(&child, 0o700), 0);
    assert_eq!(rmdir_path(&child), 0);
    let missing = bytes(&root.0.join("missing/child"));
    let mut exists = true;
    assert_eq!(exists_path(&missing, &mut exists), 0);
    assert!(!exists);
    let mut info = ffi::FileStat::default();
    assert_eq!(stat_path(&file, &mut info), 0);
    let saved = fields(&info);
    assert_eq!(stat_path(&missing, &mut info), libc::ENOENT);
    assert_eq!(fields(&info), saved);
    for path in [&b""[..], &b"nul\0path"[..]] {
        exists = true;
        assert_eq!(exists_path(path, &mut exists), libc::EINVAL);
        assert!(exists);
        assert_eq!(stat_path(path, &mut info), libc::EINVAL);
        assert_eq!(fields(&info), saved);
        assert_eq!(mkdir_path(path, 0o700), libc::EINVAL);
        assert_eq!(rmdir_path(path), libc::EINVAL);
        assert_eq!(unlink_path(path), libc::EINVAL);
        assert_eq!(truncate_legacy_path(path, 0), libc::EINVAL);
    }
    assert_eq!(rename_path(b"", &file), libc::ENOENT);
    assert_eq!(truncate_legacy_path(&file, -1), libc::EINVAL);
    assert_eq!(stat_path(&file, &mut info), 0);
    assert_eq!(info.size, 8);
    assert_eq!(unlink_path(&file), 0);
}

#[cfg(unix)]
#[test]
fn symlink_guards_and_failed_access_keep_legacy_semantics() {
    use std::os::unix::fs::symlink;
    let root = Directory::new();
    fs::write(root.0.join("data"), b"x").unwrap();
    fs::create_dir(root.0.join("directory")).unwrap();
    symlink("data", root.0.join("file-link")).unwrap();
    symlink("directory", root.0.join("dir-link")).unwrap();
    symlink("missing", root.0.join("dangling")).unwrap();
    assert_eq!(unlink_path(&bytes(&root.0.join("file-link"))), 0);
    assert!(root.0.join("data").exists());
    assert_eq!(unlink_path(&bytes(&root.0.join("dir-link"))), libc::ENOENT);
    assert_eq!(rmdir_path(&bytes(&root.0.join("dir-link"))), libc::ENOTDIR);
    assert_eq!(unlink_path(&bytes(&root.0.join("dangling"))), libc::ENOENT);
    assert!(fs::symlink_metadata(root.0.join("dangling")).is_ok());
    let mut exists = true;
    assert_eq!(
        exists_path(&bytes(&root.0.join("dangling")), &mut exists),
        0
    );
    assert!(!exists);
    exists = true;
    assert_eq!(
        exists_path(&bytes(&root.0.join("data/child")), &mut exists),
        libc::ENOTDIR
    );
    assert!(exists);
    symlink("loop", root.0.join("loop")).unwrap();
    assert_eq!(
        exists_path(&bytes(&root.0.join("loop")), &mut exists),
        libc::ELOOP
    );
    assert!(exists);
}

#[cfg(unix)]
#[test]
fn path_stat_matches_all_unix_fields_and_signed_times() {
    use std::os::unix::fs::MetadataExt;
    let root = Directory::new();
    let path = root.0.join("data");
    let file = fs::File::create(&path).unwrap();
    let before_epoch = std::time::UNIX_EPOCH - std::time::Duration::from_secs(123);
    let path = bytes(&path);
    let large = (1_i64 << 31) + 123;
    assert_eq!(truncate_legacy_path(&path, large), 0);
    file.set_times(
        fs::FileTimes::new()
            .set_accessed(before_epoch)
            .set_modified(before_epoch),
    )
    .unwrap();
    let m = file.metadata().unwrap();
    let mut value = ffi::FileStat::default();
    assert_eq!(stat_path(&path, &mut value), 0);
    assert_eq!(
        fields(&value),
        [
            m.rdev(),
            m.dev(),
            m.ino(),
            0,
            u64::from(m.mode()),
            m.nlink() as u32 as u64,
            u64::from(m.uid()),
            u64::from(m.gid()),
            large as u64,
            m.blocks(),
            m.blksize(),
            m.atime() as u64,
            m.mtime() as u64,
            m.ctime() as u64,
            i64::MAX as u64
        ]
    );
    assert_eq!(value.atime_s, -123);
    assert_eq!(value.mtime_s, -123);
    assert_eq!(truncate_legacy_path(&path, 7), 0);
    assert_eq!(stat_path(&path, &mut value), 0);
    assert_eq!(value.size, 7);
}

#[cfg(windows)]
#[test]
fn windows_crt_rename_and_stat_keep_their_contract() {
    let root = Directory::new();
    let source = root.0.join("source");
    let target = root.0.join("target");
    fs::write(&source, b"source").unwrap();
    fs::write(&target, b"target").unwrap();
    assert_eq!(rename_path(&bytes(&source), &bytes(&target)), libc::EACCES);
    assert_eq!(fs::read(&source).unwrap(), b"source");
    assert_eq!(fs::read(&target).unwrap(), b"target");
    assert_eq!(truncate_legacy_path(&bytes(&source), (1_i64 << 31) + 3), 0);
    let mut actual = ffi::FileStat::default();
    assert_eq!(stat_path(&bytes(&source), &mut actual), 0);
    assert_eq!(actual.size, (1_u64 << 31) + 3);
    assert_eq!(actual.block_cnt, 0);
    assert_eq!(actual.block_size, 4096);
    assert_eq!(actual.btime_s, i64::MAX);
    assert_eq!(actual.inode, 0);
}

#[cfg(unix)]
#[test]
fn permission_failures_leave_stat_and_exists_untouched() {
    use std::os::unix::fs::PermissionsExt;
    if unsafe { libc::geteuid() } == 0 {
        return;
    }
    let root = Directory::new();
    let parent = root.0.join("private");
    fs::create_dir(&parent).unwrap();
    fs::write(parent.join("data"), b"secret").unwrap();
    struct Restore(std::path::PathBuf);
    impl Drop for Restore {
        fn drop(&mut self) {
            fs::set_permissions(&self.0, fs::Permissions::from_mode(0o700)).unwrap();
        }
    }
    let _restore = Restore(parent.clone());
    let path = bytes(&parent.join("data"));
    let mut info = ffi::FileStat::default();
    assert_eq!(stat_path(&path, &mut info), 0);
    let saved = fields(&info);
    fs::set_permissions(&parent, fs::Permissions::from_mode(0o000)).unwrap();
    let mut exists = true;
    assert_eq!(exists_path(&path, &mut exists), libc::EACCES);
    assert!(exists);
    assert_eq!(stat_path(&path, &mut info), libc::EACCES);
    assert_eq!(fields(&info), saved);
}
