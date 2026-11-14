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

use crate::directory::sync_path;
use std::{
    fmt, io,
    path::{Path, PathBuf},
};

#[derive(Debug)]
pub enum DirectorySyncError {
    Open(io::Error),
    Flush {
        source: io::Error,
        close_error: Option<io::Error>,
    },
    Close(io::Error),
}

impl DirectorySyncError {
    pub fn primary(&self) -> &io::Error {
        match self {
            Self::Open(error) | Self::Close(error) => error,
            Self::Flush { source, .. } => source,
        }
    }
}
impl fmt::Display for DirectorySyncError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Open(error) => write!(f, "directory open failed: {error}"),
            Self::Close(error) => write!(f, "directory close failed: {error}"),
            Self::Flush {
                source,
                close_error,
            } => {
                write!(f, "directory flush failed: {source}")?;
                if let Some(error) = close_error {
                    write!(f, "; directory close failed: {error}")?;
                }
                Ok(())
            }
        }
    }
}
impl std::error::Error for DirectorySyncError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        Some(self.primary())
    }
}

pub fn sync_directory(path: &Path) -> Result<(), DirectorySyncError> {
    sync_path(path)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DirectoryDurability {
    Required,
    WindowsCompatible,
}

#[derive(Debug)]
pub struct UnconfirmedDirectorySync {
    pub path: PathBuf,
    pub source: io::Error,
}

/// Bounded observations for one public open attempt, including a failed attempt.
#[derive(Debug, Default)]
pub struct DirectorySyncSummary {
    first: Option<UnconfirmedDirectorySync>,
    count: u64,
}
impl DirectorySyncSummary {
    pub fn first(&self) -> Option<&UnconfirmedDirectorySync> {
        self.first.as_ref()
    }
    /// At u64::MAX the count is a lower bound.
    pub fn count(&self) -> u64 {
        self.count
    }
    pub fn clear(&mut self) {
        *self = Self::default();
    }
}
impl DirectoryDurability {
    pub fn sync_directory(
        self,
        path: &Path,
        summary: &mut DirectorySyncSummary,
    ) -> Result<(), DirectorySyncError> {
        self.accept(path, summary, sync_directory(path), cfg!(windows))
    }
    fn accept(
        self,
        path: &Path,
        summary: &mut DirectorySyncSummary,
        result: Result<(), DirectorySyncError>,
        windows: bool,
    ) -> Result<(), DirectorySyncError> {
        match result {
            Err(DirectorySyncError::Flush {
                source,
                close_error: None,
            }) if windows && self == Self::WindowsCompatible => {
                if summary.first.is_none() {
                    summary.first = Some(UnconfirmedDirectorySync {
                        path: path.to_owned(),
                        source,
                    });
                }
                summary.count = summary.count.saturating_add(1);
                Ok(())
            }
            result => result,
        }
    }
}

#[cfg(test)]
mod tests;
