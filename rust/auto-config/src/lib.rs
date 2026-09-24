// Copyright (c) 2025 OceanBase.
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

use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Entry {
    pub name: String,
    pub value: String,
    pub line: usize,
}

#[derive(Debug)]
pub struct Error {
    pub line: usize,
    pub name: Option<String>,
    pub after_replace: bool,
    pub message: String,
}

impl Error {
    fn new(line: usize, name: Option<String>, message: impl Into<String>) -> Self {
        Self {
            line,
            name,
            after_replace: false,
            message: message.into(),
        }
    }

    fn io(operation: &str, error: io::Error, after_replace: bool) -> Self {
        Self {
            line: 0,
            name: None,
            after_replace,
            message: format!("{operation}: {error}"),
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.line != 0 {
            write!(f, "line {}: ", self.line)?;
        }
        if let Some(name) = &self.name {
            write!(f, "{name}: ")?;
        }
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for Error {}

type Entries = BTreeMap<String, Entry>;
// Covers all declared parameters even if every 64 KiB value is populated.
const MAX_FILE_BYTES: u64 = 32 * 1024 * 1024;
static WRITER_LOCK: OnceLock<Mutex<()>> = OnceLock::new();
static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

fn lock_writers() -> Result<std::sync::MutexGuard<'static, ()>, Error> {
    WRITER_LOCK
        .get_or_init(|| Mutex::new(()))
        .lock()
        .map_err(|_| Error::new(0, None, "auto-config writer lock poisoned"))
}

/// Return an explicit error at startup on platforms without this durability protocol.
pub fn ensure_supported() -> Result<(), Error> {
    #[cfg(unix)]
    {
        Ok(())
    }
    #[cfg(not(unix))]
    {
        Err(Error::new(
            0,
            None,
            "durable auto-config replacement is unsupported on this platform",
        ))
    }
}

/// Check the deployment filesystem's directory-sync capability at startup.
pub fn check_storage_directory(path: &Path) -> Result<(), Error> {
    ensure_supported()?;
    let parent = path
        .parent()
        .ok_or_else(|| Error::new(0, None, "auto-config has no parent"))?;
    File::open(parent)
        .and_then(|directory| directory.sync_all())
        .map_err(|error| Error::io("sync auto-config directory at startup", error, false))
}

fn parse_name(source: &str, line: usize) -> Result<(String, &str), Error> {
    let input = source.trim_start_matches([' ', '\t']);
    let len = input
        .bytes()
        .take_while(|byte| byte.is_ascii_alphanumeric() || *byte == b'_' || *byte == b'.')
        .count();
    let name = &input[..len];
    if len == 0 || !matches!(name.as_bytes()[0], b'a'..=b'z' | b'A'..=b'Z' | b'_') {
        return Err(Error::new(line, None, "expected parameter name"));
    }
    Ok((name.to_ascii_lowercase(), &input[len..]))
}

fn parse_line(source: &str, line: usize) -> Result<Option<Entry>, Error> {
    let input = source.trim_start_matches([' ', '\t']);
    if input.is_empty() || input.starts_with('#') {
        return Ok(None);
    }
    let (name, remaining) = parse_name(input, line)?;
    let mut remaining = remaining.trim_start_matches([' ', '\t']);
    if !remaining.starts_with('=') {
        return Err(Error::new(line, Some(name), "expected '='"));
    }
    remaining = remaining[1..].trim_start_matches([' ', '\t']);
    if !remaining.starts_with('\'') {
        return Err(Error::new(line, Some(name), "expected quoted value"));
    }
    let bytes = remaining.as_bytes();
    let mut value = Vec::new();
    let mut pos = 1;
    while pos < bytes.len() {
        match bytes[pos] {
            b'\'' if pos + 1 < bytes.len() && bytes[pos + 1] == b'\'' => {
                value.push(b'\'');
                pos += 2;
            }
            b'\'' => {
                let trailing = remaining[pos + 1..].trim_start_matches([' ', '\t']);
                if !trailing.is_empty() && !trailing.starts_with('#') {
                    return Err(Error::new(line, Some(name), "unexpected text after value"));
                }
                return Ok(Some(Entry {
                    name,
                    value: String::from_utf8(value)
                        .map_err(|_| Error::new(line, None, "value is not UTF-8"))?,
                    line,
                }));
            }
            b'\\' if pos + 1 < bytes.len() && bytes[pos + 1] == b'\\' => {
                value.push(b'\\');
                pos += 2;
            }
            b'\\' => {
                return Err(Error::new(line, Some(name), "expected doubled backslash"));
            }
            0 => {
                return Err(Error::new(line, Some(name), "value contains NUL"));
            }
            byte => {
                value.push(byte);
                pos += 1;
            }
        }
    }
    Err(Error::new(line, Some(name), "unterminated quoted value"))
}

pub fn parse(source: &str) -> Result<Vec<Entry>, Error> {
    let mut entries = Entries::new();
    for (index, line) in source.split_terminator('\n').enumerate() {
        let line = line.strip_suffix('\r').unwrap_or(line);
        if let Some(entry) = parse_line(line, index + 1)? {
            entries.insert(entry.name.clone(), entry);
        }
    }
    Ok(entries.into_values().collect())
}

pub fn load(path: &Path) -> Result<Vec<Entry>, Error> {
    ensure_supported()?;
    let file = match File::open(path) {
        Ok(file) => file,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(error) => return Err(Error::io("open auto-config file", error, false)),
    };
    let mut bytes = Vec::new();
    file.take(MAX_FILE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| Error::io("read auto-config file", error, false))?;
    if bytes.len() as u64 > MAX_FILE_BYTES {
        return Err(Error::new(0, None, "auto-config file is too large"));
    }
    let source = String::from_utf8(bytes).map_err(|error| {
        let position = error.utf8_error().valid_up_to();
        let line = error.as_bytes()[..position]
            .iter()
            .filter(|&&byte| byte == b'\n')
            .count()
            + 1;
        Error::new(line, None, "auto-config file is not UTF-8")
    })?;
    parse(&source)
}

fn serialize(entries: &Entries) -> String {
    let mut output = String::from("# Generated by seekdb. Do not edit while seekdb is running.\n");
    for entry in entries.values() {
        output.push_str(&entry.name);
        output.push_str(" = '");
        for ch in entry.value.chars() {
            if ch == '\'' || ch == '\\' {
                output.push(ch);
            }
            output.push(ch);
        }
        output.push_str("'\n");
    }
    output
}

#[cfg(unix)]
fn create_private_temp(path: &Path) -> Result<(PathBuf, File), Error> {
    use std::os::unix::fs::OpenOptionsExt;
    for _ in 0..1024 {
        let id = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed);
        let mut name = path.as_os_str().to_os_string();
        name.push(format!(".tmp.{}.{}", std::process::id(), id));
        let candidate = PathBuf::from(name);
        match OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(&candidate)
        {
            Ok(file) => return Ok((candidate, file)),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(Error::io("open temporary auto-config file", error, false)),
        }
    }
    Err(Error::new(
        0,
        None,
        "could not choose a unique temporary file",
    ))
}

#[cfg(not(unix))]
fn create_private_temp(_path: &Path) -> Result<(PathBuf, File), Error> {
    ensure_supported()?;
    unreachable!()
}

fn sync_existing(path: &Path) -> Result<(), Error> {
    match File::open(path) {
        Ok(file) => file
            .sync_all()
            .map_err(|error| Error::io("sync existing auto-config file", error, false)),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(Error::io("open existing auto-config file", error, false)),
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ReplaceStage {
    TemporaryFileSynced,
    ExistingFileSynced,
    Replaced,
    NewFileSynced,
    DirectorySynced,
}

fn replace_with_hook(
    path: &Path,
    entries: &Entries,
    mut after_stage: impl FnMut(ReplaceStage) -> Result<(), Error>,
) -> Result<(), Error> {
    let parent = path
        .parent()
        .ok_or_else(|| Error::new(0, None, "auto-config has no parent"))?;
    let (temp_path, mut file) = create_private_temp(path)?;
    let result = (|| {
        file.write_all(serialize(entries).as_bytes())
            .map_err(|error| Error::io("write temporary auto-config file", error, false))?;
        file.sync_all()
            .map_err(|error| Error::io("sync temporary auto-config file", error, false))?;
        drop(file);
        after_stage(ReplaceStage::TemporaryFileSynced)?;
        sync_existing(path)?;
        after_stage(ReplaceStage::ExistingFileSynced)?;
        fs::rename(&temp_path, path)
            .map_err(|error| Error::io("replace auto-config file", error, false))?;
        after_stage(ReplaceStage::Replaced)?;
        File::open(path)
            .and_then(|file| file.sync_all())
            .map_err(|error| Error::io("sync replaced auto-config file", error, true))?;
        after_stage(ReplaceStage::NewFileSynced)?;
        File::open(parent)
            .and_then(|directory| directory.sync_all())
            .map_err(|error| Error::io("sync auto-config directory", error, true))?;
        after_stage(ReplaceStage::DirectorySynced)?;
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(&temp_path);
    }
    result
}

fn update_with_hook(
    path: &Path,
    name: &str,
    value: Option<&str>,
    after_stage: impl FnMut(ReplaceStage) -> Result<(), Error>,
) -> Result<(), Error> {
    ensure_supported()?;
    let _guard = lock_writers()?;
    let (canonical_name, rest) = parse_name(name, 0)?;
    if !rest.is_empty() {
        return Err(Error::new(
            0,
            Some(canonical_name),
            "invalid parameter name",
        ));
    }
    if value.is_some_and(|value| value.contains(['\n', '\r', '\0'])) {
        return Err(Error::new(
            0,
            Some(canonical_name),
            "value contains a forbidden character",
        ));
    }
    let mut entries: Entries = load(path)?
        .into_iter()
        .map(|entry| (entry.name.clone(), entry))
        .collect();
    if let Some(value) = value {
        entries.insert(
            canonical_name.clone(),
            Entry {
                name: canonical_name,
                value: value.to_owned(),
                line: 0,
            },
        );
    } else {
        entries.remove(&canonical_name);
    }
    replace_with_hook(path, &entries, after_stage)
}

pub fn update(path: &Path, name: &str, value: Option<&str>) -> Result<(), Error> {
    update_with_hook(path, name, value, |_| Ok(()))
}

#[cfg(test)]
mod tests {
    use super::*;

    static NEXT_TEST_DIR: AtomicU64 = AtomicU64::new(1);

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new() -> Self {
            let id = NEXT_TEST_DIR.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir()
                .join(format!("seekdb-auto-config-{}-{id}", std::process::id()));
            fs::create_dir(&path).unwrap();
            Self(path)
        }

        fn file(&self) -> PathBuf {
            self.0.join("seekdb.auto.conf")
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.0).unwrap();
        }
    }

    #[test]
    fn quoted_values_round_trip_and_last_duplicate_wins() {
        let entries = parse("# edit\nname = 'old'\nname = 'a''b\\\\c' # note\n").unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].value, "a'b\\c");
        let map = entries
            .into_iter()
            .map(|entry| (entry.name.clone(), entry))
            .collect();
        assert!(serialize(&map).contains("name = 'a''b\\\\c'"));
    }

    #[test]
    fn rejects_noncanonical_values_with_line_number() {
        let error = parse("good = '1'\nbad = plain\n").unwrap_err();
        assert_eq!(error.line, 2);
        assert_eq!(error.name.as_deref(), Some("bad"));
    }

    #[test]
    fn update_rewrites_complete_file_and_reset_removes_override() {
        let directory = TestDirectory::new();
        let path = directory.file();
        check_storage_directory(&path).unwrap();
        assert!(load(&path).unwrap().is_empty());
        fs::write(&path, "# user comment\nNAME = 'first'\nname = 'second'\n").unwrap();
        update(&path, "other", Some("quote' and slash\\")).unwrap();
        let contents = fs::read_to_string(&path).unwrap();
        assert!(!contents.contains("user comment"));
        assert_eq!(contents.matches("name = ").count(), 1);
        assert!(contents.contains("other = 'quote'' and slash\\\\'"));
        assert_eq!(load(&path).unwrap().len(), 2);
        update(&path, "name", None).unwrap();
        assert_eq!(load(&path).unwrap().len(), 1);
        assert_eq!(load(&path).unwrap()[0].name, "other");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(
                fs::metadata(&path).unwrap().permissions().mode() & 0o777,
                0o600
            );
        }
    }

    #[test]
    fn concurrent_updates_keep_each_completed_value() {
        let directory = TestDirectory::new();
        let path = directory.file();
        std::thread::scope(|scope| {
            for index in 0..8 {
                let path = &path;
                scope.spawn(move || update(path, &format!("name_{index}"), Some("value")).unwrap());
            }
        });
        assert_eq!(load(&path).unwrap().len(), 8);
    }

    #[test]
    fn malformed_utf8_reports_line_and_orphan_temp_is_ignored() {
        let directory = TestDirectory::new();
        let path = directory.file();
        fs::write(&path, b"ok = '1'\nbad = '\xff'\n").unwrap();
        assert_eq!(load(&path).unwrap_err().line, 2);
        fs::remove_file(&path).unwrap();
        fs::write(path.with_extension("conf.tmp.1"), "bad = 'stale'\n").unwrap();
        assert!(load(&path).unwrap().is_empty());
    }

    const REPLACEMENT_STAGES: [ReplaceStage; 5] = [
        ReplaceStage::TemporaryFileSynced,
        ReplaceStage::ExistingFileSynced,
        ReplaceStage::Replaced,
        ReplaceStage::NewFileSynced,
        ReplaceStage::DirectorySynced,
    ];

    fn was_replaced(stage: ReplaceStage) -> bool {
        matches!(
            stage,
            ReplaceStage::Replaced | ReplaceStage::NewFileSynced | ReplaceStage::DirectorySynced
        )
    }

    #[test]
    fn injected_errors_preserve_old_or_complete_new_file() {
        for failed_stage in REPLACEMENT_STAGES {
            let directory = TestDirectory::new();
            let path = directory.file();
            update(&path, "setting", Some("old")).unwrap();
            let error = update_with_hook(&path, "setting", Some("new"), |stage| {
                if stage == failed_stage {
                    Err(Error {
                        line: 0,
                        name: None,
                        after_replace: was_replaced(stage),
                        message: "injected error".to_owned(),
                    })
                } else {
                    Ok(())
                }
            })
            .unwrap_err();
            assert_eq!(error.after_replace, was_replaced(failed_stage));
            let saved = load(&path).unwrap();
            assert_eq!(saved.len(), 1);
            assert_eq!(
                saved[0].value,
                if was_replaced(failed_stage) {
                    "new"
                } else {
                    "old"
                }
            );
        }
    }

    #[test]
    #[ignore]
    fn crash_writer_child() {
        let Ok(path) = std::env::var("SEEKDB_AUTO_CONFIG_TEST_PATH") else {
            return;
        };
        let crash_at = std::env::var("SEEKDB_AUTO_CONFIG_TEST_STAGE").unwrap();
        update_with_hook(Path::new(&path), "setting", Some("new"), |stage| {
            if format!("{stage:?}") == crash_at {
                std::process::exit(42);
            }
            Ok(())
        })
        .unwrap();
        panic!("writer did not reach the requested stage");
    }

    #[test]
    fn terminated_writer_leaves_a_complete_old_or_new_file() {
        for crash_at in REPLACEMENT_STAGES {
            let directory = TestDirectory::new();
            let path = directory.file();
            update(&path, "setting", Some("old")).unwrap();
            let output = std::process::Command::new(std::env::current_exe().unwrap())
                .args(["--ignored", "--exact", "tests::crash_writer_child"])
                .env("SEEKDB_AUTO_CONFIG_TEST_PATH", &path)
                .env("SEEKDB_AUTO_CONFIG_TEST_STAGE", format!("{crash_at:?}"))
                .output()
                .unwrap();
            assert_eq!(output.status.code(), Some(42), "{output:?}");
            let saved = load(&path).unwrap();
            assert_eq!(saved.len(), 1);
            assert_eq!(
                saved[0].value,
                if was_replaced(crash_at) { "new" } else { "old" }
            );
        }
    }
}
