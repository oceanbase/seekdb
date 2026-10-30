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

#![cfg(target_os = "linux")]

use std::fs;
use storage_io::{BlockConfig, BlockError, SpaceBudget, UnmarkedBlockFile, HEADER_PAGE_SIZE};

const BLOCK: u64 = HEADER_PAGE_SIZE as u64;

#[test]
fn failed_new_file_extension_removes_the_file_and_allows_retry() {
    const CHILD: &str = "STORAGE_IO_CREATION_LIMIT_CHILD";
    if std::env::var_os(CHILD).is_none() {
        let output = std::process::Command::new(std::env::current_exe().unwrap())
            .args([
                "--exact",
                "failed_new_file_extension_removes_the_file_and_allows_retry",
                "--nocapture",
            ])
            .env(CHILD, "1")
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "child failed: {}{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        );
        return;
    }
    let directory =
        std::env::temp_dir().join(format!("storage-io-creation-test-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let path = directory.join("block_file");
    let config = BlockConfig {
        block_size: BLOCK,
        initial_size: BLOCK * 3,
        direct_io: false,
        punch_holes: false,
    };
    let mut original = libc::rlimit {
        rlim_cur: 0,
        rlim_max: 0,
    };
    // SAFETY: this isolated child owns its signal disposition and resource limits;
    // each syscall receives a valid native structure or signal constant.
    unsafe {
        assert_eq!(libc::getrlimit(libc::RLIMIT_FSIZE, &mut original), 0);
        assert_ne!(libc::signal(libc::SIGXFSZ, libc::SIG_IGN), libc::SIG_ERR);
        let limited = libc::rlimit {
            rlim_cur: BLOCK,
            rlim_max: original.rlim_max,
        };
        assert_eq!(libc::setrlimit(libc::RLIMIT_FSIZE, &limited), 0);
    }
    let result = UnmarkedBlockFile::open(
        &path,
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    );
    // SAFETY: original is the limit obtained above in the same child process.
    assert_eq!(unsafe { libc::setrlimit(libc::RLIMIT_FSIZE, &original) }, 0);
    assert!(
        matches!(result, Err(BlockError::Io(ref error)) if error.raw_os_error() == Some(libc::EFBIG))
    );
    assert!(
        !path.exists(),
        "failed new block-file extension left an invalid file behind"
    );
    let owner = UnmarkedBlockFile::open(
        &path,
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    assert_eq!(owner.snapshot().capacity, BLOCK * 3);
    drop(owner);
    let owner = UnmarkedBlockFile::open(
        &path,
        BlockConfig {
            initial_size: BLOCK * 99,
            ..config
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    assert_eq!(owner.snapshot().capacity, BLOCK * 3);
    drop(owner);
    fs::remove_dir_all(directory).unwrap();
}
