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
use std::sync::Mutex;

mod bridge;
pub mod config;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Entry {
    pub name: String,
    pub value: String,
}

#[derive(Debug)]
pub struct Error {
    pub name: Option<String>,
    pub commit_uncertain: bool,
    pub message: String,
}

impl Error {
    fn new(name: Option<String>, message: impl Into<String>) -> Self {
        Self {
            name,
            commit_uncertain: false,
            message: message.into(),
        }
    }

    fn storage(operation: &str, error: kv::Error, committing: bool) -> Self {
        Self {
            name: None,
            commit_uncertain: committing && matches!(error, kv::Error::Io(_) | kv::Error::Poisoned),
            message: format!("{operation}: {error}"),
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(name) = &self.name {
            write!(f, "{name}: ")?;
        }
        write!(f, "{}", self.message)
    }
}
impl std::error::Error for Error {}

const CONFIG_PREFIX: &[u8] = b"config/";
const MAX_CONFIG_BYTES: usize = 32 * 1024 * 1024;
static WRITER_LOCK: Mutex<()> = Mutex::new(());
thread_local! { static IN_CHECKER: Cell<bool> = const { Cell::new(false) }; }

fn lock_writers() -> Result<std::sync::MutexGuard<'static, ()>, Error> {
    if IN_CHECKER.with(Cell::get) {
        return Err(Error::new(
            None,
            "configuration write or snapshot reentered a checker",
        ));
    }
    WRITER_LOCK
        .lock()
        .map_err(|_| Error::new(None, "config writer lock poisoned"))
}

fn run_checker(check: impl FnOnce() -> Result<(), Error>) -> Result<(), Error> {
    IN_CHECKER.with(|active| {
        if active.replace(true) {
            return Err(Error::new(None, "configuration checker reentered"));
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

fn key(name: &str) -> Vec<u8> {
    [CONFIG_PREFIX, name.as_bytes()].concat()
}

pub fn load(store: &kv::Store) -> Result<Vec<Entry>, Error> {
    let snapshot = store
        .snapshot()
        .map_err(|error| Error::storage("snapshot config storage", error, false))?;
    let cursor = snapshot
        .scan(kv::Range::prefix(CONFIG_PREFIX))
        .map_err(|error| Error::storage("scan config storage", error, false))?;
    let mut entries = Vec::new();
    let mut bytes = 0_usize;
    for row in cursor {
        let (key, value) =
            row.map_err(|error| Error::storage("read config storage", error, false))?;
        bytes = bytes.saturating_add(key.len()).saturating_add(value.len());
        if bytes > MAX_CONFIG_BYTES {
            return Err(Error::new(None, "configuration storage is too large"));
        }
        let name = String::from_utf8(key[CONFIG_PREFIX.len()..].to_vec())
            .map_err(|_| Error::new(None, "configuration name is not UTF-8"))?;
        let value = String::from_utf8(value)
            .map_err(|_| Error::new(Some(name.clone()), "configuration value is not UTF-8"))?;
        entries.push(Entry { name, value });
    }
    Ok(entries)
}

pub(crate) fn update_with_commit(
    store: &kv::Store,
    name: &str,
    value: Option<&str>,
    check: impl FnOnce() -> Result<(), Error>,
    publish: impl FnOnce(),
) -> Result<(), Error> {
    let _guard = lock_writers()?;
    if let Some(value) = value {
        config::validate_entry(&Entry {
            name: name.to_owned(),
            value: value.to_owned(),
        })?;
    }
    run_checker(check)?;
    let mut tx = store
        .begin()
        .map_err(|error| Error::storage("begin config transaction", error, false))?;
    let key = key(name);
    if let Some(value) = value {
        tx.put(&key, value.as_bytes())
            .map_err(|error| Error::storage("write config entry", error, false))?;
    } else {
        tx.delete(&key)
            .map_err(|error| Error::storage("delete config entry", error, false))?;
    }
    tx.commit()
        .map_err(|error| Error::storage("commit config transaction", error, true))?;
    publish();
    Ok(())
}

#[cfg(test)]
mod tests;
