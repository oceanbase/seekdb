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

#[test]
fn compatibility_accepts_only_windows_flush_with_checked_close() {
    for windows in [false, true] {
        for policy in [
            DirectoryDurability::Required,
            DirectoryDurability::WindowsCompatible,
        ] {
            for stage in 0..4 {
                let error = io::Error::from_raw_os_error(13);
                let error = match stage {
                    0 => DirectorySyncError::Open(error),
                    1 => DirectorySyncError::Flush {
                        source: error,
                        close_error: None,
                    },
                    2 => DirectorySyncError::Flush {
                        source: error,
                        close_error: Some(io::Error::from_raw_os_error(5)),
                    },
                    _ => DirectorySyncError::Close(error),
                };
                let mut summary = DirectorySyncSummary::default();
                let accepted =
                    windows && policy == DirectoryDurability::WindowsCompatible && stage == 1;
                let result =
                    policy.accept(Path::new("exact/path"), &mut summary, Err(error), windows);
                assert_eq!(result.is_ok(), accepted);
                assert_eq!(summary.count(), u64::from(accepted));
                if let Some(first) = summary.first() {
                    assert_eq!(first.path, Path::new("exact/path"));
                    assert_eq!(first.source.raw_os_error(), Some(13));
                } else {
                    assert_eq!(result.unwrap_err().primary().raw_os_error(), Some(13));
                }
            }
        }
    }
}

#[test]
fn summary_retains_first_error_and_saturating_count_across_success_and_failure() {
    let mut summary = DirectorySyncSummary::default();
    let policy = DirectoryDurability::WindowsCompatible;
    for (path, code) in [("first", 5), ("second", 13)] {
        policy
            .accept(
                Path::new(path),
                &mut summary,
                Err(DirectorySyncError::Flush {
                    source: io::Error::from_raw_os_error(code),
                    close_error: None,
                }),
                true,
            )
            .unwrap();
    }
    policy
        .accept(Path::new("success"), &mut summary, Ok(()), true)
        .unwrap();
    assert!(policy
        .accept(
            Path::new("fatal"),
            &mut summary,
            Err(DirectorySyncError::Open(io::Error::from_raw_os_error(2))),
            true
        )
        .is_err());
    assert_eq!(summary.count(), 2);
    assert_eq!(summary.first().unwrap().path, Path::new("first"));
    assert_eq!(summary.first().unwrap().source.raw_os_error(), Some(5));
    summary.count = u64::MAX;
    policy
        .accept(
            Path::new("saturated"),
            &mut summary,
            Err(DirectorySyncError::Flush {
                source: io::Error::from_raw_os_error(28),
                close_error: None,
            }),
            true,
        )
        .unwrap();
    assert_eq!(summary.count(), u64::MAX);
    summary.clear();
    assert_eq!(summary.count(), 0);
    assert!(summary.first().is_none());
}

#[cfg(unix)]
#[test]
fn path_sync_preserves_stage_and_secondary_close_and_closes_once() {
    use crate::directory::{SyncFault, SYNC_FAULT};
    for stage in 0..5 {
        SYNC_FAULT.with(|slot| {
            *slot.borrow_mut() = SyncFault {
                open: (stage == 0).then_some(13),
                flush: (stage == 1 || stage == 2).then_some(28),
                close: (stage == 2 || stage == 3).then_some(5),
                closes: 0,
            }
        });
        let result = sync_directory(Path::new("."));
        match stage {
            0 => assert!(
                matches!(result, Err(DirectorySyncError::Open(e)) if e.raw_os_error() == Some(13))
            ),
            1 => assert!(
                matches!(result, Err(DirectorySyncError::Flush { source, close_error: None }) if source.raw_os_error() == Some(28))
            ),
            2 => assert!(
                matches!(result, Err(DirectorySyncError::Flush { source, close_error: Some(close) }) if source.raw_os_error() == Some(28) && close.raw_os_error() == Some(5))
            ),
            3 => assert!(
                matches!(result, Err(DirectorySyncError::Close(e)) if e.raw_os_error() == Some(5))
            ),
            _ => result.unwrap(),
        }
        SYNC_FAULT.with(|slot| assert_eq!(slot.borrow().closes, usize::from(stage != 0)));
    }
}

#[cfg(unix)]
#[test]
fn held_sync_survives_rename_and_does_not_resolve_the_original_path() {
    let path = std::env::temp_dir().join(format!(
        "held-sync-{}-{:?}",
        std::process::id(),
        std::thread::current().id()
    ));
    let moved = path.with_extension("moved");
    std::fs::create_dir(&path).unwrap();
    let owner = crate::Directory::open(&path).unwrap();
    std::fs::rename(&path, &moved).unwrap();
    std::fs::write(&path, b"replacement file").unwrap();
    owner.sync().unwrap();
    assert!(matches!(
        sync_directory(&path),
        Err(DirectorySyncError::Open(_))
    ));
    owner.sync().unwrap();
    drop(owner);
    std::fs::remove_file(&path).unwrap();
    std::fs::remove_dir(&moved).unwrap();
}
