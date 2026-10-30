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

use crate::tests::Directory;
use crate::{
    create_dir, exists, metadata, read_dir, remove_dir, remove_file, rename, truncate_path,
};
use std::fs::{self, File};
use std::io;

#[test]
fn native_paths_keep_standard_errors_and_sparse_lengths() {
    let root = Directory::new();
    let directory = root.0.join("directory");
    let file = directory.join("data");
    create_dir(&directory, 0o700).unwrap();
    assert_eq!(
        create_dir(&directory, 0o700).unwrap_err().kind(),
        io::ErrorKind::AlreadyExists
    );
    File::create(&file).unwrap();
    assert_eq!(
        create_dir(&file, 0o700).unwrap_err().kind(),
        io::ErrorKind::AlreadyExists
    );
    let large = (1_u64 << 31) + 127;
    truncate_path(&file, large).unwrap();
    assert_eq!(metadata(&file).unwrap().len(), large);
    truncate_path(&file, 13).unwrap();
    assert_eq!(metadata(&file).unwrap().len(), 13);
    assert_eq!(
        truncate_path(&file, u64::MAX).unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
    assert_eq!(metadata(&file).unwrap().len(), 13);
    let moved = directory.join("renamed");
    rename(&file, &moved).unwrap();
    assert!(!exists(&file).unwrap());
    assert!(exists(&moved).unwrap());
    let entries = read_dir(&directory)
        .unwrap()
        .collect::<io::Result<Vec<_>>>()
        .unwrap();
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].file_name(), "renamed");
    assert!(remove_dir(&directory).is_err());
    remove_file(&moved).unwrap();
    remove_dir(&directory).unwrap();
    assert_eq!(
        truncate_path(&file, 0).unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
}

#[cfg(unix)]
#[test]
fn native_paths_keep_non_utf8_names_and_following_metadata() {
    use std::os::unix::ffi::OsStrExt;
    use std::os::unix::fs::symlink;
    let root = Directory::new();
    let name = std::ffi::OsStr::from_bytes(b"data-\xff");
    let file = root.0.join(name);
    fs::write(&file, b"contents").unwrap();
    let link = root.0.join("link");
    symlink(name, &link).unwrap();
    assert_eq!(metadata(&link).unwrap().len(), 8);
    let names = read_dir(&root.0)
        .unwrap()
        .map(|entry| entry.unwrap().file_name())
        .collect::<Vec<_>>();
    assert!(names
        .iter()
        .any(|entry| entry.as_bytes() == name.as_bytes()));
    remove_file(&file).unwrap();
    assert!(!exists(&link).unwrap());
    remove_file(&link).unwrap();
}

#[cfg(unix)]
#[test]
fn mode_obeys_umask_and_truncate_needs_only_write_permission() {
    use std::os::unix::fs::PermissionsExt;
    const CHILD: &str = "FILE_IO_FILESYSTEM_UMASK_CHILD";
    if std::env::var_os(CHILD).is_none() {
        let result = std::process::Command::new(std::env::current_exe().unwrap())
            .args([
                "--exact",
                "filesystem::tests::mode_obeys_umask_and_truncate_needs_only_write_permission",
                "--nocapture",
            ])
            .env(CHILD, "1")
            .status()
            .unwrap();
        assert!(result.success());
        return;
    }
    // umask is process-wide, so this fixture runs alone in its own test process.
    unsafe {
        libc::umask(0o027);
    }
    let root = Directory::new();
    let directory = root.0.join("mode");
    create_dir(&directory, 0o777).unwrap();
    assert_eq!(
        metadata(&directory).unwrap().permissions().mode() & 0o777,
        0o750
    );
    let nested = directory.join("one/two");
    crate::create_dir_all(&nested, 0o777).unwrap();
    for path in [directory.join("one"), nested] {
        assert_eq!(metadata(&path).unwrap().permissions().mode() & 0o777, 0o750);
    }
    let file = directory.join("write-only");
    fs::write(&file, b"contents").unwrap();
    fs::set_permissions(&file, fs::Permissions::from_mode(0o200)).unwrap();
    truncate_path(&file, 4).unwrap();
    assert_eq!(metadata(&file).unwrap().len(), 4);
    fs::set_permissions(&file, fs::Permissions::from_mode(0o400)).unwrap();
    if unsafe { libc::geteuid() } != 0 {
        assert_eq!(
            truncate_path(&file, 0).unwrap_err().kind(),
            io::ErrorKind::PermissionDenied
        );
    }
    fs::set_permissions(&file, fs::Permissions::from_mode(0o600)).unwrap();
}

#[cfg(windows)]
#[test]
fn native_paths_accept_supplementary_utf16_names() {
    let root = Directory::new();
    let file = root.0.join("native-\u{1f33f}");
    fs::write(&file, b"native").unwrap();
    truncate_path(&file, 3).unwrap();
    assert_eq!(metadata(&file).unwrap().len(), 3);
    assert_eq!(
        read_dir(&root.0)
            .unwrap()
            .next()
            .unwrap()
            .unwrap()
            .file_name(),
        file.file_name().unwrap()
    );
    remove_file(&file).unwrap();
}

#[test]
fn recursive_creation_accepts_directories_and_rejects_file_collisions() {
    let root = Directory::new();
    let nested = root.0.join("one/two");
    crate::create_dir_all(&nested, 0o700).unwrap();
    crate::create_dir_all(&nested, 0o700).unwrap();
    let file = nested.join("file");
    fs::write(&file, b"contents").unwrap();
    assert_eq!(
        crate::create_dir_all(&file, 0o700).unwrap_err().kind(),
        io::ErrorKind::AlreadyExists
    );
    assert!(crate::create_dir_all(&file.join("child"), 0o700).is_err());
    assert_eq!(fs::read(&file).unwrap(), b"contents");
}

#[test]
fn lexical_resolution_does_not_require_an_existing_target() {
    let root = Directory::new();
    let missing = root.0.join("missing");
    assert!(crate::lexical_absolute(&missing).unwrap().is_absolute());
    assert_eq!(
        crate::canonical_existing(&missing).unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
    assert!(crate::canonical_existing(&root.0).unwrap().is_absolute());
}

#[cfg(unix)]
#[test]
fn nofollow_metadata_observes_dangling_links() {
    let root = Directory::new();
    let target = root.0.join("target");
    let link = root.0.join("link");
    crate::symlink_file(&target, &link).unwrap();
    assert!(crate::symlink_metadata(&link).unwrap().is_symlink());
    assert_eq!(metadata(&link).unwrap_err().kind(), io::ErrorKind::NotFound);
    fs::write(&target, b"target").unwrap();
    assert_eq!(
        crate::canonical_existing(&link).unwrap(),
        crate::canonical_existing(&target).unwrap()
    );
    assert_ne!(
        crate::lexical_absolute(&link).unwrap(),
        crate::canonical_existing(&link).unwrap()
    );
    remove_file(&target).unwrap();
    remove_file(&link).unwrap();
}
