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
use crate::tests::Directory as TempDirectory;
use std::fs;

fn names(entries: Entries) -> Vec<OsString> {
    let mut names = entries
        .map(|entry| entry.unwrap().file_name())
        .collect::<Vec<_>>();
    names.sort();
    names
}

#[test]
fn independent_cursors_retain_the_directory_after_owner_drop() {
    let temporary = TempDirectory::new();
    for name in ["a", "b", "c"] {
        fs::write(temporary.0.join(name), b"x").unwrap();
    }
    let directory =
        Directory::open_with(&temporary.0, BindingRequirement::ResolvedPathAllowed).unwrap();
    let mut first = directory.entries().unwrap();
    let first_name = first.next_entry().unwrap().unwrap().file_name();
    let second = directory.entries().unwrap();
    drop(directory);
    let expected = vec![
        OsString::from("a"),
        OsString::from("b"),
        OsString::from("c"),
    ];
    assert_eq!(names(second), expected);
    let mut first_names = names(first);
    first_names.push(first_name);
    first_names.sort();
    assert_eq!(first_names, expected);
}

#[test]
fn open_errors_and_terminal_eof_are_distinct() {
    let temporary = TempDirectory::new();
    assert_eq!(
        read_dir(temporary.0.join("missing")).err().unwrap().kind(),
        io::ErrorKind::NotFound
    );
    let mut entries = read_dir(&temporary.0).unwrap();
    assert!(entries.next_entry().unwrap().is_none());
    fs::write(temporary.0.join("later"), b"x").unwrap();
    assert!(entries.next_entry().unwrap().is_none());
    assert!(entries.next().is_none());
    assert!(read_dir(temporary.0.join("later")).is_err());
}

#[cfg(unix)]
#[test]
fn held_identity_survives_parent_rename_and_replacement() {
    let temporary = TempDirectory::new();
    let original = temporary.0.join("original");
    let moved = temporary.0.join("moved");
    fs::create_dir(&original).unwrap();
    fs::write(original.join("held"), b"x").unwrap();
    let directory = Directory::open(&original).unwrap();
    let first = directory.entries().unwrap();
    fs::rename(&original, &moved).unwrap();
    fs::create_dir(&original).unwrap();
    fs::write(original.join("replacement"), b"x").unwrap();
    assert_eq!(names(first), [OsString::from("held")]);
    assert_eq!(
        names(directory.entries().unwrap()),
        [OsString::from("held")]
    );
    assert_eq!(
        names(read_dir(&original).unwrap()),
        [OsString::from("replacement")]
    );
}

#[cfg(unix)]
#[test]
fn names_preserve_bytes_and_do_not_follow_symlinks() {
    use std::os::unix::ffi::OsStringExt;
    let temporary = TempDirectory::new();
    let name = OsString::from_vec(b"bad-\xff".to_vec());
    fs::write(temporary.0.join(&name), b"x").unwrap();
    std::os::unix::fs::symlink("missing", temporary.0.join("link")).unwrap();
    let entries = names(Directory::open(&temporary.0).unwrap().entries().unwrap());
    assert_eq!(entries, [name, OsString::from("link")]);
}

#[cfg(target_os = "linux")]
fn handles(path: &Path) -> usize {
    fs::read_dir("/proc/self/fd")
        .unwrap()
        .filter_map(Result::ok)
        .filter(|entry| fs::read_link(entry.path()).is_ok_and(|target| target == path))
        .count()
}

#[cfg(target_os = "linux")]
#[test]
fn streams_close_on_drop_eof_and_read_failure() {
    use std::os::fd::AsRawFd;
    let temporary = TempDirectory::new();
    let entries = read_dir(&temporary.0).unwrap();
    assert_eq!(handles(&temporary.0), 2);
    drop(entries);
    assert_eq!(handles(&temporary.0), 0);
    let mut entries = read_dir(&temporary.0).unwrap();
    assert!(entries.next_entry().unwrap().is_none());
    assert_eq!(handles(&temporary.0), 0);
    let regular_path = temporary.0.join("regular");
    let regular = fs::File::create(&regular_path).unwrap();
    let mut entries = read_dir(&temporary.0).unwrap();
    let fd = entries.state.as_ref().unwrap().0.descriptor();
    assert_eq!(unsafe { libc::dup2(regular.as_raw_fd(), fd) }, fd);
    platform::CLOSE_ERROR.with(|error| error.set(Some(libc::EIO)));
    assert_eq!(
        entries.next_entry().unwrap_err().raw_os_error(),
        Some(libc::ENOTDIR)
    );
    assert_eq!(handles(&regular_path), 1);
    platform::CLOSE_ERROR.with(|error| assert_eq!(error.get(), None));
    assert_eq!(handles(&temporary.0), 0);
    assert!(entries.next_entry().unwrap().is_none());
}

#[cfg(target_os = "linux")]
#[test]
fn eof_close_failure_is_reported_once_and_does_not_leak() {
    let temporary = TempDirectory::new();
    let mut entries = read_dir(&temporary.0).unwrap();
    platform::CLOSE_ERROR.with(|error| error.set(Some(libc::EIO)));
    assert_eq!(
        entries.next_entry().unwrap_err().raw_os_error(),
        Some(libc::EIO)
    );
    assert_eq!(handles(&temporary.0), 0);
    assert!(entries.next_entry().unwrap().is_none());
}

#[cfg(windows)]
#[test]
fn native_unicode_does_not_require_legacy_encoding() {
    let temporary = TempDirectory::new();
    let name = OsString::from("native-\u{1f33f}");
    fs::write(temporary.0.join(&name), b"x").unwrap();
    assert_eq!(
        Directory::open(&temporary.0).err().unwrap().kind(),
        io::ErrorKind::Unsupported
    );
    assert_eq!(names(read_dir(&temporary.0).unwrap()), [name]);
}

#[test]
fn leaf_names_reject_traversal_and_keep_native_spelling() {
    for name in ["", ".", "..", "/", "a/b", "a/", "a\0b"] {
        assert!(LeafName::new(OsStr::new(name)).is_err(), "{name:?}");
    }
    assert_eq!(
        LeafName::new(OsStr::new("ordinary.tmp"))
            .unwrap()
            .as_os_str(),
        "ordinary.tmp"
    );
}

#[cfg(unix)]
#[test]
fn relative_inspect_child_and_removal_keep_the_opened_parent() {
    use std::os::unix::ffi::OsStrExt;
    let temporary = TempDirectory::new();
    let original = temporary.0.join("original");
    let moved = temporary.0.join("moved");
    fs::create_dir(&original).unwrap();
    fs::create_dir(original.join("child")).unwrap();
    let name = LeafName::new(OsStr::from_bytes(b"bad-\xff")).unwrap();
    fs::write(original.join(name.as_os_str()), b"held").unwrap();
    std::os::unix::fs::symlink("child", original.join("link")).unwrap();
    let parent = Directory::open(&original).unwrap();
    let child = LeafName::new(OsStr::new("child")).unwrap();
    let link = LeafName::new(OsStr::new("link")).unwrap();
    let identity = crate::FileIdentity::at(&original.join(name.as_os_str())).unwrap();
    fs::rename(&original, &moved).unwrap();
    fs::create_dir(&original).unwrap();
    fs::write(original.join(name.as_os_str()), b"replacement").unwrap();
    assert_eq!(
        parent.inspect(&name, Follow::No).unwrap(),
        Observation::Present {
            kind: EntryKind::File,
            len: 4,
            identity
        }
    );
    assert!(matches!(
        parent.inspect(&link, Follow::No).unwrap(),
        Observation::Present {
            kind: EntryKind::Link,
            ..
        }
    ));
    assert!(matches!(
        parent.inspect(&link, Follow::Yes).unwrap(),
        Observation::Present {
            kind: EntryKind::Directory,
            ..
        }
    ));
    assert!(parent.open_child(&link).is_err());
    let opened_child = parent.open_child(&child).unwrap();
    assert!(opened_child
        .entries()
        .unwrap()
        .next_entry()
        .unwrap()
        .is_none());
    parent.remove_file(&name).unwrap();
    assert_eq!(
        parent.inspect(&name, Follow::No).unwrap(),
        Observation::Missing
    );
    assert_eq!(
        fs::read(original.join(name.as_os_str())).unwrap(),
        b"replacement"
    );
    parent.remove_dir(&child).unwrap();
    assert!(!moved.join("child").exists());
    assert!(parent.remove_dir(&link).is_err());
    parent.remove_file(&link).unwrap();
}

#[cfg(target_os = "linux")]
#[test]
fn denied_relative_lookup_is_an_error_not_missing() {
    use std::os::unix::fs::PermissionsExt;
    let temporary = TempDirectory::new();
    fs::write(temporary.0.join("present"), b"x").unwrap();
    let directory = Directory::open(&temporary.0).unwrap();
    fs::set_permissions(&temporary.0, fs::Permissions::from_mode(0o600)).unwrap();
    // Capable root bypasses DAC. Drop only this thread's filesystem credentials.
    let previous = unsafe { libc::setfsuid(65534) };
    let result = directory.inspect(&LeafName::new(OsStr::new("present")).unwrap(), Follow::No);
    unsafe {
        libc::setfsuid(previous as u32);
    }
    fs::set_permissions(&temporary.0, fs::Permissions::from_mode(0o700)).unwrap();
    assert_eq!(result.unwrap_err().kind(), io::ErrorKind::PermissionDenied);
}

#[cfg(unix)]
#[test]
fn detached_directory_cannot_report_missing_or_successful_eof() {
    let temporary = TempDirectory::new();
    let path = temporary.0.join("removed");
    fs::create_dir(&path).unwrap();
    let directory = Directory::open(&path).unwrap();
    let mut entries = directory.entries().unwrap();
    fs::remove_dir(&path).unwrap();
    let missing = LeafName::new(OsStr::new("absent")).unwrap();
    assert_eq!(
        directory.inspect(&missing, Follow::No).unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
    assert_eq!(
        entries.next_entry().unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
    assert!(entries.next_entry().unwrap().is_none());
}

#[cfg(windows)]
#[test]
fn windows_literal_lookup_preserves_unicode_and_rejects_ambiguous_names() {
    let temporary = TempDirectory::new();
    let name = LeafName::new(OsStr::new("native-\u{1f33f}")).unwrap();
    fs::write(temporary.0.join(name.as_os_str()), b"unicode").unwrap();
    let directory =
        Directory::open_with(&temporary.0, BindingRequirement::ResolvedPathAllowed).unwrap();
    assert!(matches!(
        directory.inspect(&name, Follow::No).unwrap(),
        Observation::Present {
            kind: EntryKind::File,
            len: 7,
            ..
        }
    ));
    for ambiguous in ["trailing.", "trailing ", "wild*", "wild?", "a:b", "a\\b"] {
        assert!(LeafName::new(OsStr::new(ambiguous)).is_err());
    }
    let missing = LeafName::new(OsStr::new("absent")).unwrap();
    assert_eq!(
        directory.inspect(&missing, Follow::No).unwrap(),
        Observation::Missing
    );
}

#[test]
fn selected_mutations_keep_identity_and_effects() {
    let temporary = TempDirectory::new();
    let root = &temporary.0;
    let directory = Directory::open_with(root, BindingRequirement::ResolvedPathAllowed).unwrap();
    let first = LeafName::new(std::ffi::OsStr::new("first")).unwrap();
    let second = LeafName::new(std::ffi::OsStr::new("second")).unwrap();
    let created = directory.create_file(&first, 0o600);
    assert!(created.postcheck.is_ok());
    let file = created.syscall.unwrap();
    let identity = crate::FileIdentity::of(&file).unwrap();
    file.set_len(23).unwrap();
    let duplicate = directory.create_file(&first, 0o600);
    assert_eq!(
        duplicate.syscall.unwrap_err().kind(),
        std::io::ErrorKind::AlreadyExists
    );
    directory
        .rename_selected(&first, &second, identity)
        .into_result()
        .unwrap();
    assert_eq!(
        directory.inspect(&first, Follow::No).unwrap(),
        Observation::Missing
    );
    let replacement = directory.create_file(&first, 0o600).syscall.unwrap();
    assert!(directory.remove_selected(&first, identity).syscall.is_err());
    assert!(root.join("first").exists());
    let removed = directory.remove_selected(&second, identity);
    assert!(removed.syscall.is_ok());
    assert!(removed.postcheck.is_ok());
    assert_eq!(file.metadata().unwrap().len(), 23);
    drop(file);
    drop(replacement);
    drop(directory);
}

#[test]
fn postcheck_failure_preserves_creation_handle_and_removal_outcome() {
    let temporary = TempDirectory::new();
    let directory =
        Directory::open_with(&temporary.0, BindingRequirement::ResolvedPathAllowed).unwrap();
    let name = LeafName::new(std::ffi::OsStr::new("selected")).unwrap();
    FAIL_POSTCHECK.with(|fail| fail.set(true));
    let created = directory.create_file(&name, 0o600);
    assert!(created.postcheck.is_err());
    let original = created.syscall.unwrap();
    let identity = crate::FileIdentity::of(&original).unwrap();
    original.set_len(17).unwrap();
    FAIL_POSTCHECK.with(|fail| fail.set(true));
    let removed = directory.remove_selected(&name, identity);
    assert!(removed.syscall.is_ok());
    assert!(removed.postcheck.is_err());
    let replacement = directory.create_file(&name, 0o600).syscall.unwrap();
    replacement.set_len(9).unwrap();
    assert!(directory.remove_selected(&name, identity).syscall.is_err());
    assert_eq!(replacement.metadata().unwrap().len(), 9);
    assert_eq!(original.metadata().unwrap().len(), 17);
}

#[test]
fn directory_mutations_preserve_applied_outcomes_after_postcheck_failure() {
    let temporary = TempDirectory::new();
    let parent =
        Directory::open_with(&temporary.0, BindingRequirement::ResolvedPathAllowed).unwrap();
    let source = LeafName::new(OsStr::new("source")).unwrap();
    let dest = LeafName::new(OsStr::new("dest")).unwrap();
    FAIL_POSTCHECK.with(|fail| fail.set(true));
    let effect = parent.create_directory(&source, 0o775);
    effect.syscall.unwrap();
    assert!(effect.postcheck.is_err());
    let child = parent.open_child(&source).unwrap();
    let identity = child.identity().unwrap();
    FAIL_POSTCHECK.with(|fail| fail.set(true));
    let effect = parent.rename_directory_selected(&source, &dest, identity);
    effect.syscall.unwrap();
    assert!(effect.postcheck.is_err());
    assert!(matches!(
        parent.inspect(&source, Follow::No).unwrap(),
        Observation::Missing
    ));
    parent.check_directory(&dest, identity).unwrap();
    child.close().unwrap();
}

#[test]
fn directory_rename_rejects_wrong_identity_and_existing_destination() {
    let temporary = TempDirectory::new();
    let parent =
        Directory::open_with(&temporary.0, BindingRequirement::ResolvedPathAllowed).unwrap();
    let source = LeafName::new(OsStr::new("source")).unwrap();
    let dest = LeafName::new(OsStr::new("dest")).unwrap();
    parent
        .create_directory(&source, 0o775)
        .into_result()
        .unwrap();
    parent.create_directory(&dest, 0o775).into_result().unwrap();
    let source_id = parent.open_child(&source).unwrap().identity().unwrap();
    let dest_id = parent.open_child(&dest).unwrap().identity().unwrap();
    assert!(parent
        .rename_directory_selected(&source, &dest, dest_id)
        .syscall
        .is_err());
    assert_eq!(
        parent
            .rename_directory_selected(&source, &dest, source_id)
            .syscall
            .unwrap_err()
            .kind(),
        io::ErrorKind::AlreadyExists
    );
    parent.check_directory(&source, source_id).unwrap();
    parent.check_directory(&dest, dest_id).unwrap();
}

#[cfg(unix)]
#[test]
fn directory_creation_and_rename_use_the_held_parent_after_path_replacement() {
    let temporary = TempDirectory::new();
    let original = temporary.0.join("original");
    let moved = temporary.0.join("moved");
    fs::create_dir(&original).unwrap();
    let parent = Directory::open(&original).unwrap();
    fs::rename(&original, &moved).unwrap();
    fs::create_dir(&original).unwrap();
    let source = LeafName::new(OsStr::new("source")).unwrap();
    let dest = LeafName::new(OsStr::new("dest")).unwrap();
    parent
        .create_directory(&source, 0o775)
        .into_result()
        .unwrap();
    let identity = parent.open_child(&source).unwrap().identity().unwrap();
    parent
        .rename_directory_selected(&source, &dest, identity)
        .into_result()
        .unwrap();
    assert!(moved.join("dest").is_dir());
    assert!(!original.join("dest").exists());
    assert!(!original.join("source").exists());
}
