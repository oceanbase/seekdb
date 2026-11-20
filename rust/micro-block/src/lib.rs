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

//! SSTable micro-block layout, encoding, row location, and projection.
//! File I/O, cache pins, MVCC visibility, and SQL expressions stay outside.

mod bridge;
mod checksum;
mod format;
mod read;
mod rowkey;
mod write;

pub use read::{Block, Cell, Projection, RowRange, RowkeyBound};
pub use rowkey::{compare_unicode_ci, RowkeyKind, RowkeyValue, UcaTable};
pub use write::{AppendOutcome, BlockWriter, ColumnSpec, OwnedDatum, RowStoreType, WriterConfig};

/// Errors produced while validating or transforming one micro-block.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidArgument,
    InvalidData,
    ChecksumMismatch,
    Unsupported,
    OutputTooSmall,
    SizeOverflow,
    Frozen,
}

pub type Result<T> = core::result::Result<T, Error>;
