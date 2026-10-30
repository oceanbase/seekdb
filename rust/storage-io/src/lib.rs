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

mod block_file;
mod named;
pub use named::{NamedOpenedFile, NamedOwner, NamedSnapshot, OwnerPhase};
#[cfg(any(test, feature = "ownership-test-support"))]
mod drop_witness;
#[cfg(any(test, feature = "ownership-test-support"))]
pub use drop_witness::DropWitness;
#[cfg(feature = "cxx")]
mod bridge;
mod header;
mod service;
pub mod statistics;

pub use block_file::{
    BlockConfig, BlockFile, BlockIndex, BlockRegion, BlockSnapshot, ReleaseOutcome, SpaceBudget,
    UnmarkedBlockFile,
};
pub use header::{encode_block_header, validate_block_header, HEADER_PAGE_SIZE};
pub use service::{
    AdmissionLimits, CancelOutcome, Completion, CompletionRequirement, CompletionStatus, Config,
    ConfigError, ConfigReceipt, DepthCost, DeviceConfig, DeviceId, DeviceLimits, DeviceSnapshot,
    ExcessShare, Execution, FinalFacts, FinalObserver, Generation, GroupId, GroupLimits,
    GroupSnapshot, HealthConfig, HealthSnapshot, ObservationCounters, ObservationOutcome,
    ObservationReceipt, Rate, RequestClass, RequestControl, RequestId, RequestLifetime,
    RequestPolicy, RequestSpec, ResourceUsage, Service, ServiceSnapshot, Target, Waiter,
    HEALTH_ACTIVE_CAPACITY, HEALTH_QUEUE_CAPACITY,
};

use std::collections::TryReserveError;
use std::{fmt, io};

#[derive(Debug)]
pub struct CreationCleanupFailure {
    pub close: Option<io::Error>,
    pub remove: Option<io::Error>,
    pub sync: Option<file_io::DirectorySyncError>,
}
impl fmt::Display for CreationCleanupFailure {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(error) = &self.close {
            write!(f, "close: {error}; ")?;
        }
        if let Some(error) = &self.remove {
            write!(f, "remove: {error}; ")?;
        }
        if let Some(error) = &self.sync {
            write!(f, "{error}")?;
        }
        Ok(())
    }
}

#[derive(Debug)]
pub enum BlockError {
    InvalidInput(&'static str),
    InvalidHeader,
    ChecksumMismatch,
    InvalidBlock(u64),
    NotAllocated(u64),
    MarkingAborted,
    Exhausted,
    Io(io::Error),
    DirectorySync {
        path: std::path::PathBuf,
        source: file_io::DirectorySyncError,
    },
    CreationCleanup {
        primary: Box<BlockError>,
        cleanup: CreationCleanupFailure,
    },
    Memory(TryReserveError),
}

impl fmt::Display for BlockError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidInput(message) => f.write_str(message),
            Self::InvalidHeader => f.write_str("invalid physical block file header"),
            Self::ChecksumMismatch => f.write_str("physical block file header checksum mismatch"),
            Self::InvalidBlock(index) => write!(f, "invalid physical block index {index}"),
            Self::NotAllocated(index) => write!(f, "physical block {index} is not allocated"),
            Self::MarkingAborted => f.write_str("startup marking must be restarted"),
            Self::Exhausted => f.write_str("physical block capacity exhausted"),
            Self::Io(error) => error.fmt(f),
            Self::DirectorySync { path, source } => {
                write!(f, "directory sync {}: {source}", path.display())
            }
            Self::CreationCleanup { primary, cleanup } => {
                write!(f, "{primary}; creation cleanup failed: {cleanup}")
            }
            Self::Memory(error) => error.fmt(f),
        }
    }
}

impl std::error::Error for BlockError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            Self::CreationCleanup { primary, .. } => Some(primary.as_ref()),
            Self::DirectorySync { source, .. } => Some(source),
            Self::Memory(error) => Some(error),
            _ => None,
        }
    }
}

impl From<io::Error> for BlockError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<TryReserveError> for BlockError {
    fn from(error: TryReserveError) -> Self {
        Self::Memory(error)
    }
}

#[cfg(test)]
mod tests;

pub mod calibration;
