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

use std::cell::Cell;
use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

pub mod config;
mod ffi;
#[cfg(windows)]
mod windows_file;

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
thread_local! { static IN_CHECKER: Cell<bool> = const { Cell::new(false) }; }

fn lock_writers() -> Result<std::sync::MutexGuard<'static, ()>, Error> {
    if IN_CHECKER.with(Cell::get) {
        return Err(Error::new(
            0,
            None,
            "configuration write or snapshot reentered a checker",
        ));
    }
    WRITER_LOCK
        .get_or_init(|| Mutex::new(()))
        .lock()
        .map_err(|_| Error::new(0, None, "config writer lock poisoned"))
}

fn run_checker(check: impl FnOnce() -> Result<(), Error>) -> Result<(), Error> {
    IN_CHECKER.with(|active| {
        if active.replace(true) {
            return Err(Error::new(0, None, "configuration checker reentered"));
        }
        struct Reset<'a>(&'a Cell<bool>);
        impl Drop for Reset<'_> {
            fn drop(&mut self) {
                self.0.set(false);
            }
        }
        let _reset = Reset(active);
        check()
    })
}

/// Return an explicit error at startup on platforms without this durability protocol.
pub fn ensure_supported() -> Result<(), Error> {
    #[cfg(any(unix, windows))]
    {
        Ok(())
    }
    #[cfg(not(any(unix, windows)))]
    {
        Err(Error::new(
            0,
            None,
            "durable config replacement is unsupported on this platform",
        ))
    }
}

/// Check that the deployment directory is accessible at startup.
pub fn check_storage_directory(path: &Path) -> Result<(), Error> {
    ensure_supported()?;
    let parent = path
        .parent()
        .ok_or_else(|| Error::new(0, None, "config has no parent"))?;
    sync_directory(parent, false)
}

fn sync_directory(path: &Path, after_replace: bool) -> Result<(), Error> {
    #[cfg(windows)]
    {
        let metadata = fs::metadata(path)
            .map_err(|error| Error::io("stat config directory", error, after_replace))?;
        if !metadata.is_dir() {
            return Err(Error::io(
                "stat config directory",
                io::Error::new(io::ErrorKind::InvalidInput, "parent is not a directory"),
                after_replace,
            ));
        }
        // Windows has no directory fsync equivalent. The process-crash
        // guarantee comes from flushing the files and replacing by rename.
        return Ok(());
    }
    #[cfg(not(windows))]
    {
        let result = File::open(path).and_then(|directory| directory.sync_all());
        // Some filesystems reject fsync on directories; the file syncs and rename
        // still protect against a process dying during replacement.
        #[cfg(unix)]
        if let Err(error) = &result {
            if matches!(
                error.kind(),
                io::ErrorKind::InvalidInput | io::ErrorKind::Unsupported
            ) {
                return Ok(());
            }
        }
        result.map_err(|error| Error::io("sync config directory", error, after_replace))
    }
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
        Err(error) => return Err(Error::io("open config file", error, false)),
    };
    let mut bytes = Vec::new();
    file.take(MAX_FILE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| Error::io("read config file", error, false))?;
    if bytes.len() as u64 > MAX_FILE_BYTES {
        return Err(Error::new(0, None, "config file is too large"));
    }
    let source = String::from_utf8(bytes).map_err(|error| {
        let position = error.utf8_error().valid_up_to();
        let line = error.as_bytes()[..position]
            .iter()
            .filter(|&&byte| byte == b'\n')
            .count()
            + 1;
        Error::new(line, None, "config file is not UTF-8")
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
            Err(error) => return Err(Error::io("open temporary config file", error, false)),
        }
    }
    Err(Error::new(
        0,
        None,
        "could not choose a unique temporary file",
    ))
}

#[cfg(windows)]
fn create_private_temp(path: &Path) -> Result<(PathBuf, File), Error> {
    for _ in 0..1024 {
        let id = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed);
        let mut name = path.as_os_str().to_os_string();
        name.push(format!(".tmp.{}.{}", std::process::id(), id));
        let candidate = PathBuf::from(name);
        match windows_file::create_private(&candidate) {
            Ok(file) => return Ok((candidate, file)),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(Error::io("open temporary config file", error, false)),
        }
    }
    Err(Error::new(
        0,
        None,
        "could not choose a unique temporary file",
    ))
}

#[cfg(not(any(unix, windows)))]
fn create_private_temp(_path: &Path) -> Result<(PathBuf, File), Error> {
    ensure_supported()?;
    unreachable!()
}

fn sync_existing(path: &Path) -> Result<(), Error> {
    match open_for_sync(path) {
        Ok(file) => file
            .sync_all()
            .map_err(|error| Error::io("sync existing config file", error, false)),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(Error::io("open existing config file", error, false)),
    }
}

fn open_for_sync(path: &Path) -> io::Result<File> {
    #[cfg(windows)]
    {
        // FlushFileBuffers requires a handle with write access.
        OpenOptions::new().read(true).write(true).open(path)
    }
    #[cfg(not(windows))]
    {
        File::open(path)
    }
}

fn replace_file(from: &Path, to: &Path) -> io::Result<()> {
    #[cfg(windows)]
    {
        windows_file::replace(from, to)
    }
    #[cfg(not(windows))]
    {
        fs::rename(from, to)
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
        .ok_or_else(|| Error::new(0, None, "config has no parent"))?;
    let (temp_path, mut file) = create_private_temp(path)?;
    let result = (|| {
        file.write_all(serialize(entries).as_bytes())
            .map_err(|error| Error::io("write temporary config file", error, false))?;
        file.sync_all()
            .map_err(|error| Error::io("sync temporary config file", error, false))?;
        drop(file);
        after_stage(ReplaceStage::TemporaryFileSynced)?;
        sync_existing(path)?;
        after_stage(ReplaceStage::ExistingFileSynced)?;
        replace_file(&temp_path, path)
            .map_err(|error| Error::io("replace config file", error, false))?;
        after_stage(ReplaceStage::Replaced)?;
        open_for_sync(path)
            .and_then(|file| file.sync_all())
            .map_err(|error| Error::io("sync replaced config file", error, true))?;
        after_stage(ReplaceStage::NewFileSynced)?;
        sync_directory(parent, true)?;
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
    update_with_hook_and_commit(path, name, value, None, after_stage, || {})
}

pub(crate) fn update_with_commit(
    path: &Path,
    name: &str,
    value: Option<&str>,
    check: impl FnOnce() -> Result<(), Error>,
    commit: impl FnOnce(),
) -> Result<(), Error> {
    update_with_hook_and_commit_checked(
        path,
        name,
        value,
        Some(config::validate_file_entry),
        |_| Ok(()),
        check,
        commit,
    )
}

fn update_with_hook_and_commit(
    path: &Path,
    name: &str,
    value: Option<&str>,
    validator: Option<fn(&Entry) -> Result<(), Error>>,
    after_stage: impl FnMut(ReplaceStage) -> Result<(), Error>,
    commit: impl FnOnce(),
) -> Result<(), Error> {
    update_with_hook_and_commit_checked(
        path,
        name,
        value,
        validator,
        after_stage,
        || Ok(()),
        commit,
    )
}

fn update_with_hook_and_commit_checked(
    path: &Path,
    name: &str,
    value: Option<&str>,
    validator: Option<fn(&Entry) -> Result<(), Error>>,
    after_stage: impl FnMut(ReplaceStage) -> Result<(), Error>,
    check: impl FnOnce() -> Result<(), Error>,
    commit: impl FnOnce(),
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
    if let Some(validate) = validator {
        for entry in entries.values() {
            validate(entry)?;
        }
    }
    run_checker(check)?;
    let result = replace_with_hook(path, &entries, after_stage);
    if result.is_ok() || result.as_ref().is_err_and(|error| error.after_replace) {
        commit();
    }
    result
}

pub fn update(path: &Path, name: &str, value: Option<&str>) -> Result<(), Error> {
    update_with_hook(path, name, value, |_| Ok(()))
}

#[cfg(test)]
mod tests {
    use super::*;

    static NEXT_TEST_DIR: AtomicU64 = AtomicU64::new(1);
    static CONFIG_TEST_LOCK: Mutex<()> = Mutex::new(());

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new() -> Self {
            let id = NEXT_TEST_DIR.fetch_add(1, Ordering::Relaxed);
            let path =
                std::env::temp_dir().join(format!("config-{}-{id}", std::process::id()));
            fs::create_dir(&path).unwrap();
            Self(path)
        }

        fn file(&self) -> PathBuf {
            self.0.join("seekdb.conf")
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.0).unwrap();
        }
    }

    fn reset_active_defaults() {
        let directory = TestDirectory::new();
        crate::config::load_active(&directory.file(), true).unwrap();
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
        let Ok(path) = std::env::var("CONFIG_TEST_PATH") else {
            return;
        };
        let crash_at = std::env::var("CONFIG_TEST_STAGE").unwrap();
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
                .env("CONFIG_TEST_PATH", &path)
                .env("CONFIG_TEST_STAGE", format!("{crash_at:?}"))
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

    #[test]
    fn declared_cpu_count_update_preserves_file_and_runtime_value_on_rejection() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        reset_active_defaults();
        assert_eq!(crate::config::cpu_count(), 0);
        let declaration = crate::config::find("CPU_COUNT").unwrap();
        assert_eq!(declaration.name, "cpu_count");
        assert_eq!(declaration.default, "0");
        assert_eq!(declaration.edit_level, "DYNAMIC_EFFECTIVE");
        assert_eq!(
            crate::config::find("major_freeze_duty_time")
                .unwrap()
                .default,
            "02:00"
        );
        assert!(crate::config::find("server_create_time").is_none());
        crate::config::update_parameter(&path, "cpu_count", Some("8")).unwrap();
        assert_eq!(crate::config::cpu_count(), 8);
        assert_eq!(load(&path).unwrap()[0].value, "8");
        assert!(crate::config::update_parameter(&path, "cpu_count", Some("-1")).is_err());
        assert_eq!(crate::config::cpu_count(), 8);
        assert_eq!(load(&path).unwrap()[0].value, "8");
        crate::config::update_parameter(&path, "cpu_count", None).unwrap();
    }

    #[test]
    fn declared_defaults_have_typed_readers() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        reset_active_defaults();
        assert!(crate::config::enable_record_trace_log());
        assert_eq!(crate::config::datafile_size(), 32 * 1024 * 1024);
        assert_eq!(crate::config::internal_sql_execute_timeout(), 30_000_000);
        assert_eq!(crate::config::cpu_quota_concurrency(), 10.0);
        assert_eq!(crate::config::data_dir(), "store");
        crate::config::apply("enable_record_trace_log", "off").unwrap();
        crate::config::apply("datafile_size", "5MB").unwrap();
        crate::config::apply("internal_sql_execute_timeout", "250ms").unwrap();
        crate::config::apply("cpu_quota_concurrency", "1.5").unwrap();
        crate::config::apply("data_dir", "other").unwrap();
        assert!(!crate::config::enable_record_trace_log());
        assert_eq!(crate::config::datafile_size(), 5 * 1024 * 1024);
        assert_eq!(crate::config::internal_sql_execute_timeout(), 250_000);
        assert_eq!(crate::config::cpu_quota_concurrency(), 1.5);
        assert_eq!(crate::config::data_dir(), "other");
        crate::config::apply("enable_record_trace_log", "True").unwrap();
        crate::config::apply("datafile_size", "32M").unwrap();
        crate::config::apply("internal_sql_execute_timeout", "30s").unwrap();
        crate::config::apply("cpu_quota_concurrency", "10").unwrap();
        crate::config::apply("data_dir", "store").unwrap();
    }

    #[test]
    fn declared_numeric_ranges_reject_without_publishing() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        reset_active_defaults();
        let directory = TestDirectory::new();
        let path = directory.file();
        assert_eq!(crate::config::rpc_port(), 2882);
        assert!(crate::config::update_parameter(&path, "rpc_port", Some("1024")).is_err());
        assert_eq!(crate::config::rpc_port(), 2882);
        crate::config::update_parameter(&path, "rpc_port", Some("1025")).unwrap();
        assert_eq!(crate::config::rpc_port(), 1025);
        assert!(crate::config::update_parameter(&path, "rpc_port", Some("65536")).is_err());
        assert_eq!(crate::config::rpc_port(), 1025);
        crate::config::update_parameter(&path, "rpc_port", Some("2882")).unwrap();

        assert!(
            crate::config::update_parameter(&path, "cpu_quota_concurrency", Some("0.99")).is_err()
        );
        assert_eq!(crate::config::cpu_quota_concurrency(), 10.0);
        assert!(crate::config::update_parameter(&path, "datafile_size", Some("-1M")).is_err());
        assert!(crate::config::update_parameter(&path, "datafile_size", Some("5")).is_err());
        assert_eq!(crate::config::datafile_size(), 32 * 1024 * 1024);
        assert!(crate::config::update_parameter(
            &path,
            "internal_sql_execute_timeout",
            Some("999us")
        )
        .is_err());
        assert_eq!(crate::config::internal_sql_execute_timeout(), 30_000_000);
    }

    #[test]
    fn every_declared_default_is_valid_and_options_do_not_restrict_values() {
        for parameter in crate::config::CATALOG {
            crate::config::validate(parameter.name, parameter.default)
                .unwrap_or_else(|error| panic!("{}: {error}", parameter.name));
        }
        assert!(crate::config::validate("not_declared", "1").is_err());
        assert!(crate::config::validate("server_create_time", "123").is_err());
        assert!(crate::config::validate("major_freeze_duty_time", "24:00").is_err());
        assert!(crate::config::validate("major_freeze_duty_time", "disable").is_ok());
        assert!(crate::config::validate("_parallel_ddl_control", "CREATE_INDEX:on").is_ok());
        assert!(crate::config::validate("_parallel_ddl_control", "UNKNOWN:on").is_ok());
        assert!(crate::config::validate("default_table_organization", "HEAP").is_ok());
        assert!(crate::config::validate("default_table_organization", "UNKNOWN").is_ok());
        assert!(crate::config::validate("syslog_level", "share.pt:trace").is_ok());
        assert_eq!(
            crate::config::find("syslog_level").unwrap().options,
            "DEBUG, TRACE, WDIAG, EDIAG, INFO, WARN, ERROR"
        );
        let index = crate::config::CATALOG
            .iter()
            .position(|parameter| parameter.name == "syslog_level")
            .unwrap();
        assert_eq!(
            crate::config::parameter_row(index).options,
            "DEBUG, TRACE, WDIAG, EDIAG, INFO, WARN, ERROR"
        );
    }

    #[test]
    fn declared_moment_value_has_typed_reader() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        reset_active_defaults();
        let moment = crate::config::major_freeze_duty_time_parts();
        assert!(!moment.disabled);
        assert_eq!((moment.hour, moment.minute), (2, 0));
        crate::config::apply("major_freeze_duty_time", "disable").unwrap();
        assert!(crate::config::major_freeze_duty_time_parts().disabled);
        crate::config::apply("major_freeze_duty_time", "02:00").unwrap();
    }

    #[test]
    fn declared_dynamic_and_static_updates_follow_file_and_effective_state() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        crate::config::load_active(&path, true).unwrap();
        crate::config::update_parameter(&path, "rpc_port", Some("3001")).unwrap();
        assert_eq!(crate::config::rpc_port(), 3001);
        assert_eq!(
            crate::config::effective_value("rpc_port").as_deref(),
            Some("3001")
        );
        assert_eq!(
            crate::config::configured_value("rpc_port").as_deref(),
            Some("3001")
        );
        let port_row = crate::config::parameter_row(
            crate::config::CATALOG
                .iter()
                .position(|parameter| parameter.name == "rpc_port")
                .unwrap(),
        );
        assert_eq!(port_row.value, "3001");
        assert_eq!(port_row.source, "DEFAULT");
        crate::config::update_parameter(&path, "enable_record_trace_log", Some("off")).unwrap();
        assert_eq!(
            crate::config::effective_value("enable_record_trace_log").as_deref(),
            Some("False")
        );

        crate::config::update_parameter(&path, "net_thread_count", Some("5")).unwrap();
        assert_eq!(crate::config::net_thread_count(), 0);
        assert_eq!(
            crate::config::effective_value("net_thread_count").as_deref(),
            Some("0")
        );
        assert_eq!(
            crate::config::configured_value("net_thread_count").as_deref(),
            Some("5")
        );
        crate::config::load_active(&path, false).unwrap();
        assert_eq!(crate::config::net_thread_count(), 0);
        crate::config::load_active(&path, true).unwrap();
        assert_eq!(crate::config::net_thread_count(), 5);
        crate::config::update_parameter(&path, "net_thread_count", None).unwrap();
        crate::config::update_parameter(&path, "rpc_port", None).unwrap();
        crate::config::update_parameter(&path, "enable_record_trace_log", None).unwrap();
        assert_eq!(crate::config::rpc_port(), 2882);
        crate::config::load_active(&path, true).unwrap();
        assert_eq!(crate::config::net_thread_count(), 0);
        assert!(load(&path).unwrap().is_empty());
    }

    #[test]
    fn bootstrap_overrides_are_effective_before_writeback() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        crate::config::load_active(&path, true).unwrap();
        crate::config::bootstrap_set("memory_budget", "2G").unwrap();
        crate::config::bootstrap_set("net_thread_count", "3").unwrap();
        assert_eq!(crate::config::memory_budget(), 2 * 1024 * 1024 * 1024);
        assert_eq!(crate::config::net_thread_count(), 3);
        assert!(!path.exists());
        assert!(crate::config::bootstrap_set("server_create_time", "1").is_err());
        crate::config::save_bootstrap(&path).unwrap();
        let entries = load(&path).unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(
            entries
                .iter()
                .find(|entry| entry.name == "memory_budget")
                .unwrap()
                .value,
            "2G"
        );
        assert_eq!(
            entries
                .iter()
                .find(|entry| entry.name == "net_thread_count")
                .unwrap()
                .value,
            "3"
        );
        crate::config::load_active(&path, true).unwrap();
        assert_eq!(crate::config::net_thread_count(), 3);
    }

    #[test]
    fn checker_rejection_and_reentry_leave_file_unchanged() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        crate::config::load_active(&path, true).unwrap();
        let rejected =
            crate::config::update_parameter_checked(&path, "cpu_count", Some("4"), || {
                Err(Error::new(0, None, "rejected"))
            });
        assert!(rejected.is_err());
        assert!(!path.exists());
        let reentered =
            crate::config::update_parameter_checked(&path, "cpu_count", Some("4"), || {
                crate::config::snapshot(&["cpu_count"]).map(|_| ())
            });
        assert!(reentered.unwrap_err().message.contains("reentered"));
        assert!(!path.exists());
    }

    #[test]
    fn internal_state_is_loaded_and_published_without_becoming_a_parameter() {
        use std::ffi::CString;

        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        crate::config::load_active(&path, true).unwrap();
        assert_eq!(crate::config::server_create_time(), 0);
        assert_eq!(crate::config::server_role_info(), "");
        crate::config::update_internal_state(&path, "server_create_time", "123").unwrap();
        crate::config::update_internal_state(&path, "server_role_info", "PRIMARY:INVALID:NORMAL:0")
            .unwrap();
        assert_eq!(crate::config::server_create_time(), 123);
        assert_eq!(
            crate::config::server_role_info(),
            "PRIMARY:INVALID:NORMAL:0"
        );
        assert!(crate::config::find("server_create_time").is_none());
        let path_arg = CString::new(path.to_str().unwrap()).unwrap();
        let name_arg = CString::new("server_create_time").unwrap();
        let value_arg = CString::new("124").unwrap();
        let mut error = ffi::ConfigError {
            line: 0,
            after_replace: 0,
            message: [0; 512],
        };
        let parameter_name = CString::new("cpu_count").unwrap();
        assert_eq!(
            ffi::config_update_internal_state(
                path_arg.as_ptr(),
                parameter_name.as_ptr(),
                value_arg.as_ptr(),
                &mut error,
            ),
            1
        );
        assert_eq!(
            ffi::config_update_internal_state(
                path_arg.as_ptr(),
                name_arg.as_ptr(),
                value_arg.as_ptr(),
                &mut error,
            ),
            0
        );
        crate::config::load_active(&path, true).unwrap();
        assert_eq!(crate::config::server_create_time(), 124);
    }

    #[test]
    fn corrupt_existing_declared_file_blocks_update_and_load() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        fs::write(&path, "rpc_port = 'not-a-port'\n").unwrap();
        let error = crate::config::load_active(&path, true).unwrap_err();
        assert_eq!(error.line, 1);
        assert!(crate::config::update_parameter(&path, "cpu_count", Some("2")).is_err());
        assert_eq!(
            fs::read_to_string(&path).unwrap(),
            "rpc_port = 'not-a-port'\n"
        );
        fs::write(&path, "unknown = '1'\n").unwrap();
        assert!(crate::config::load_active(&path, true).is_err());
        assert!(crate::config::update_parameter(&path, "rpc_port", Some("3001")).is_err());
    }

    #[test]
    fn file_reload_skips_write_policy_but_requires_storable_values() {
        let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
        let directory = TestDirectory::new();
        let path = directory.file();
        crate::config::load_active(&path, true).unwrap();
        assert!(crate::config::validate("cpu_count", "-1").is_err());
        fs::write(&path, "cpu_count = '-1'\n").unwrap();
        crate::config::load_active(&path, false).unwrap();
        assert_eq!(crate::config::cpu_count(), -1);
        fs::write(&path, "cpu_count = 'invalid'\n").unwrap();
        assert_eq!(
            crate::config::load_active(&path, false).unwrap_err().line,
            1
        );
        assert_eq!(crate::config::cpu_count(), -1);
    }
}
