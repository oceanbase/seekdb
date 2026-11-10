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

mod bridge;

use globset::Glob;
use std::ffi::OsStr;
use std::fs;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use walkdir::WalkDir;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GlobError {
    InvalidPattern,
    Io,
}

pub fn expand(pattern: &str) -> Result<Vec<PathBuf>, GlobError> {
    let (root, match_pattern, max_depth) = split_pattern(pattern)?;
    let matcher = Glob::new(match_pattern.to_str().ok_or(GlobError::InvalidPattern)?)
        .map_err(|_| GlobError::InvalidPattern)?
        .compile_matcher();
    match fs::metadata(&root) {
        Ok(metadata) if metadata.is_dir() => {}
        Ok(_) => return Ok(Vec::new()),
        Err(error) if error.kind() == ErrorKind::NotFound => return Ok(Vec::new()),
        Err(_) => return Err(GlobError::Io),
    }

    let mut walker = WalkDir::new(&root).follow_links(true);
    if let Some(max_depth) = max_depth {
        walker = walker.max_depth(max_depth);
    }
    let mut paths = Vec::new();
    for entry in walker {
        let entry = entry.map_err(|_| GlobError::Io)?;
        let relative = entry
            .path()
            .strip_prefix(&root)
            .map_err(|_| GlobError::Io)?;
        if !relative.as_os_str().is_empty()
            && hidden_components_match(&match_pattern, relative)
            && matcher.is_match(relative)
        {
            paths.push(entry.into_path());
        }
    }
    paths.sort();
    paths.dedup();
    Ok(paths)
}

fn split_pattern(pattern: &str) -> Result<(PathBuf, PathBuf, Option<usize>), GlobError> {
    let mut root = PathBuf::new();
    let mut match_pattern = PathBuf::new();
    let mut in_match = false;
    let mut recursive = false;
    for component in Path::new(pattern).components() {
        let text = component
            .as_os_str()
            .to_str()
            .ok_or(GlobError::InvalidPattern)?;
        if !in_match && !has_meta(text) {
            root.push(component.as_os_str());
        } else {
            in_match = true;
            recursive |= text == "**";
            match_pattern.push(component.as_os_str());
        }
    }
    if match_pattern.as_os_str().is_empty() {
        return Err(GlobError::InvalidPattern);
    }
    let max_depth = if recursive {
        None
    } else {
        Some(match_pattern.components().count())
    };
    if root.as_os_str().is_empty() {
        root.push(".");
    }
    Ok((root, match_pattern, max_depth))
}

fn has_meta(component: &str) -> bool {
    component
        .bytes()
        .any(|byte| matches!(byte, b'*' | b'?' | b'[' | b'{'))
}

fn hidden_components_match(pattern: &Path, candidate: &Path) -> bool {
    let pattern = pattern
        .components()
        .map(|component| component.as_os_str())
        .collect::<Vec<_>>();
    let candidate = candidate
        .components()
        .map(|component| component.as_os_str())
        .collect::<Vec<_>>();
    let mut memo = vec![None; (pattern.len() + 1) * (candidate.len() + 1)];
    hidden_components_match_at(&pattern, &candidate, 0, 0, &mut memo)
}

fn hidden_components_match_at(
    pattern: &[&OsStr],
    candidate: &[&OsStr],
    pattern_index: usize,
    candidate_index: usize,
    memo: &mut [Option<bool>],
) -> bool {
    let memo_index = pattern_index * (candidate.len() + 1) + candidate_index;
    if let Some(result) = memo[memo_index] {
        return result;
    }
    let result = if pattern_index == pattern.len() {
        candidate_index == candidate.len()
    } else if pattern[pattern_index] == "**" {
        hidden_components_match_at(pattern, candidate, pattern_index + 1, candidate_index, memo)
            || (candidate_index < candidate.len()
                && !is_hidden(candidate[candidate_index])
                && hidden_components_match_at(
                    pattern,
                    candidate,
                    pattern_index,
                    candidate_index + 1,
                    memo,
                ))
    } else {
        candidate_index < candidate.len()
            && (!is_hidden(candidate[candidate_index])
                || pattern[pattern_index].as_encoded_bytes().starts_with(b"."))
            && hidden_components_match_at(
                pattern,
                candidate,
                pattern_index + 1,
                candidate_index + 1,
                memo,
            )
    };
    memo[memo_index] = Some(result);
    result
}

fn is_hidden(component: &OsStr) -> bool {
    component.as_encoded_bytes().starts_with(b".")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_DIR: AtomicU64 = AtomicU64::new(0);

    struct TestDir(PathBuf);

    impl TestDir {
        fn new() -> Self {
            let path = std::env::temp_dir().join(format!(
                "seekdb-path-utils-{}-{}",
                std::process::id(),
                NEXT_DIR.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&path).unwrap();
            Self(path)
        }

        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for TestDir {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn expand_relative(root: &Path, pattern: &str) -> Vec<String> {
        let pattern = root.join(pattern);
        expand(pattern.to_str().unwrap())
            .unwrap()
            .iter()
            .map(|path| {
                path.strip_prefix(root)
                    .unwrap()
                    .to_string_lossy()
                    .replace('\\', "/")
            })
            .collect()
    }

    #[test]
    fn expands_multiple_levels_classes_and_recursion() {
        let dir = TestDir::new();
        fs::create_dir_all(dir.path().join("part-a/nested")).unwrap();
        fs::create_dir(dir.path().join("part-b")).unwrap();
        fs::write(dir.path().join("top.csv"), []).unwrap();
        fs::write(dir.path().join("part-a/file1.csv"), []).unwrap();
        fs::write(dir.path().join("part-a/file2.txt"), []).unwrap();
        fs::write(dir.path().join("part-a/nested/deep.csv"), []).unwrap();
        fs::write(dir.path().join("part-b/file2.csv"), []).unwrap();

        assert_eq!(
            expand_relative(dir.path(), "part-?/file[0-9].csv"),
            ["part-a/file1.csv", "part-b/file2.csv"]
        );
        assert_eq!(expand_relative(dir.path(), "*.csv"), ["top.csv"]);
        assert_eq!(
            expand_relative(dir.path(), "**/*.csv"),
            [
                "part-a/file1.csv",
                "part-a/nested/deep.csv",
                "part-b/file2.csv",
                "top.csv"
            ]
        );
    }

    #[test]
    fn hidden_components_require_explicit_pattern_components() {
        let dir = TestDir::new();
        fs::create_dir(dir.path().join(".hidden")).unwrap();
        fs::create_dir(dir.path().join("visible")).unwrap();
        fs::write(dir.path().join(".root.csv"), []).unwrap();
        fs::write(dir.path().join("visible.csv"), []).unwrap();
        fs::write(dir.path().join(".hidden/inside.csv"), []).unwrap();
        fs::write(dir.path().join("visible/.inside.csv"), []).unwrap();
        fs::write(dir.path().join("visible/keep.csv"), []).unwrap();

        assert_eq!(
            expand_relative(dir.path(), "**/*.csv"),
            ["visible/keep.csv", "visible.csv"]
        );
        assert_eq!(
            expand_relative(dir.path(), "**/.*.csv"),
            [".root.csv", "visible/.inside.csv"]
        );
        assert_eq!(
            expand_relative(dir.path(), ".hidden/*.csv"),
            [".hidden/inside.csv"]
        );
    }

    #[cfg(unix)]
    #[test]
    fn preserves_non_utf8_file_names() {
        use std::ffi::OsString;
        use std::os::unix::ffi::{OsStrExt, OsStringExt};

        let dir = TestDir::new();
        let name = OsString::from_vec(b"bad-\xff.csv".to_vec());
        fs::write(dir.path().join(&name), []).unwrap();
        let pattern = dir.path().join("*.csv");
        let paths = expand(pattern.to_str().unwrap()).unwrap();
        assert_eq!(paths.len(), 1);
        assert_eq!(paths[0].file_name().unwrap().as_bytes(), b"bad-\xff.csv");
    }

    #[cfg(unix)]
    #[test]
    fn follows_directory_links() {
        use std::os::unix::fs::symlink;

        let dir = TestDir::new();
        fs::create_dir_all(dir.path().join("actual/nested")).unwrap();
        fs::write(dir.path().join("actual/nested/file.csv"), []).unwrap();
        symlink(dir.path().join("actual"), dir.path().join("link")).unwrap();
        assert_eq!(
            expand_relative(dir.path(), "link/**/*.csv"),
            ["link/nested/file.csv"]
        );
    }

    #[test]
    fn reports_invalid_patterns_and_missing_directories() {
        let dir = TestDir::new();
        let missing = dir.path().join("missing/*.csv");
        assert!(expand(missing.to_str().unwrap()).unwrap().is_empty());
        let invalid = dir.path().join("[");
        assert_eq!(
            expand(invalid.to_str().unwrap()),
            Err(GlobError::InvalidPattern)
        );
    }
}
