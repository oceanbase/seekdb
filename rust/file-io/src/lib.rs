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

mod allocation;
mod asynchronous;
#[cfg(feature = "ownership-test-support")]
pub use asynchronous::benchmark_hold::BenchmarkHold;
#[cfg(feature = "cxx")]
pub mod bridge;
mod buffer;
mod capability;
mod direct;
mod directory;
mod durability;
mod filesystem;
mod identity;
mod ownership;
mod positioned;
mod space;
mod stdio;
mod stream;
pub use stream::{AppendError, Appender, BufferLayout, CreateMode, EffectiveMode, IoMode, Reader};

pub use allocation::{allocate, extend_file, Allocation};
pub use asynchronous::{
    toggle_worker_fault_delay, Backend, BackendKind, Direction, FileOwner, Operation, Retired,
    SubmissionId, SubmitFailure, WorkerFault,
};
pub use buffer::AlignedBuffer;
pub use capability::{probe_directory, DirectoryCapabilities};
pub use direct::enable_direct_io;
pub use directory::{
    read_dir, BindingRequirement, Directory, Entries, Entry, EntryKind, Follow, LeafName, Mutation,
    Observation,
};
pub use durability::{
    sync_directory, DirectoryDurability, DirectorySyncError, DirectorySyncSummary,
    UnconfirmedDirectorySync,
};
pub use filesystem::{
    canonical_existing, create_dir, create_dir_all, create_new_read_write, exists, lexical_absolute,
    metadata, open_read_write, publish_file, remove_dir, remove_file, rename, symlink_file,
    symlink_metadata, truncate_path,
};
pub use identity::FileIdentity;
pub use ownership::{close_file, ExclusiveFile};
pub use positioned::{
    read_at, read_exact_at, read_up_to_at, write_all_at, write_at, write_fully_at,
};
pub use space::{space, usable_space, Space, UsableSpace};
pub use stdio::redirect_stdio;

use std::io::{self, Write};

/// Retains the successfully transferred prefix even when a later write fails.
#[derive(Debug)]
pub struct Transfer {
    pub bytes: usize,
    pub result: io::Result<()>,
}

pub fn write_all(writer: &mut (impl Write + ?Sized), bytes: &[u8]) -> Transfer {
    let mut written = 0;
    let result = loop {
        if written == bytes.len() {
            break Ok(());
        }
        match writer.write(&bytes[written..]) {
            Ok(0) => break Err(io::ErrorKind::WriteZero.into()),
            Ok(count) => written += count,
            Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
            Err(error) => break Err(error),
        }
    };
    Transfer {
        bytes: written,
        result,
    }
}

#[cfg(test)]
mod tests;
