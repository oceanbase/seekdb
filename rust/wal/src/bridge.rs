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

use crate::env::Env;
use crate::format::{copied_bytes, Error, Result};
use crate::stream::{self, Cursor, Next, Stream};

#[cxx::bridge(namespace = "oceanbase::wal::wal_rs")]
mod ffi {
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Code {
        Ok,
        Invalid,
        NotInit,
        NotFound,
        End,
        OutLower,
        OutUpper,
        Partial,
        Duplicate,
        Corrupt,
        UnknownVersion,
        Io,
        NoSpace,
        NoMemory,
        Closed,
    }

    struct BaseInfo {
        curr_lsn: u64,
        prev_lsn: u64,
        prev_scn: u64,
        prev_log_id: i64,
        prev_accumulated_checksum: u64,
    }

    struct AppendResult {
        code: Code,
        lsn: u64,
        scn: u64,
        end_lsn: u64,
        end_scn: u64,
    }

    struct FlushResult {
        code: Code,
        generation: u64,
        end_lsn: u64,
        end_scn: u64,
    }

    struct ImportedGroup {
        source_lsn: u64,
        source_scn: u64,
        bytes: Vec<u8>,
    }

    struct ImportedGroupResult {
        code: Code,
        group: ImportedGroup,
    }

    struct Progress {
        code: Code,
        begin_scn: u64,
        base_lsn: u64,
        end_lsn: u64,
        end_scn: u64,
        ref_scn: u64,
        used_disk: u64,
        unrecyclable_disk: u64,
        integrity: bool,
    }

    struct BaseInfoResult {
        code: Code,
        info: BaseInfo,
    }

    struct PositionResult {
        code: Code,
        lsn: u64,
        scn: u64,
    }

    struct DiskUsageResult {
        code: Code,
        used: u64,
        total: u64,
    }

    extern "Rust" {
        type Env;
        fn create_env(path: &str, disk_soft_target: u64) -> Box<Env>;
        fn start_env_ffi(env: &Env) -> Code;
        fn stop_env_ffi(env: &Env);
        fn create_env_stream_ffi(env: &Env, base: BaseInfo, ref_scn: u64) -> Box<Stream>;
        fn open_env_stream_ffi(env: &Env) -> Box<Stream>;
        fn remove_env_ffi(env: &Env) -> Code;
        fn disk_usage_env_ffi(env: &Env) -> DiskUsageResult;
        fn update_disk_soft_target_env_ffi(env: &Env, disk_soft_target: u64);
        fn disk_soft_target_env_ffi(env: &Env) -> u64;

        type Stream;
        fn status_ffi(stream: &Stream) -> Code;
        fn clone_stream_ffi(stream: &Stream) -> Box<Stream>;
        fn submit_ffi(stream: &Stream, payload: &[u8], ref_scn: u64) -> AppendResult;
        fn wait_flush_ffi(stream: &Stream, after_generation: u64) -> FlushResult;
        fn copy_imported_group(source_lsn: u64, source_scn: u64, bytes: &[u8])
            -> ImportedGroupResult;
        fn append_imported_ffi(stream: &Stream, groups: &[ImportedGroup]) -> AppendResult;
        fn progress_ffi(stream: &Stream) -> Progress;
        fn base_info_ffi(stream: &Stream, lsn: u64) -> BaseInfoResult;
        fn set_base_lsn_ffi(stream: &Stream, lsn: u64) -> Progress;
        fn locate_by_scn_ffi(stream: &Stream, scn: u64) -> PositionResult;
        fn locate_by_lsn_ffi(stream: &Stream, lsn: u64) -> PositionResult;
        fn close_ffi(stream: &Stream) -> Code;
        fn cursor_ffi(stream: &Stream, lsn: u64, group_mode: bool) -> Box<Cursor>;

        type Cursor;
        fn reuse_ffi(cursor: &mut Cursor, lsn: u64) -> Code;
        fn next_ffi(cursor: &mut Cursor, replayable_point: u64) -> Code;
        fn bytes(cursor: &Cursor) -> &[u8];
        fn payload(cursor: &Cursor) -> &[u8];
        fn lsn(cursor: &Cursor) -> u64;
        fn end_lsn(cursor: &Cursor) -> u64;
        fn scn(cursor: &Cursor) -> u64;
        fn log_id(cursor: &Cursor) -> i64;
        fn next_min_scn(cursor: &Cursor) -> u64;
    }
}

pub use ffi::{
    AppendResult, BaseInfo, BaseInfoResult, Code, DiskUsageResult, FlushResult, ImportedGroup,
    ImportedGroupResult, PositionResult, Progress,
};

fn code(result: Result<()>) -> Code {
    match result {
        Ok(()) => Code::Ok,
        Err(error) => error.into(),
    }
}

impl From<Error> for Code {
    fn from(error: Error) -> Self {
        match error {
            Error::Invalid => Self::Invalid,
            Error::NotInit => Self::NotInit,
            Error::NotFound => Self::NotFound,
            Error::End => Self::End,
            Error::OutLower => Self::OutLower,
            Error::OutUpper => Self::OutUpper,
            Error::Partial => Self::Partial,
            Error::Duplicate => Self::Duplicate,
            Error::Corrupt => Self::Corrupt,
            Error::UnknownVersion => Self::UnknownVersion,
            Error::Io => Self::Io,
            Error::NoSpace => Self::NoSpace,
            Error::NoMemory => Self::NoMemory,
            Error::Closed => Self::Closed,
        }
    }
}

impl From<BaseInfo> for stream::BaseInfo {
    fn from(info: BaseInfo) -> Self {
        Self {
            curr_lsn: info.curr_lsn,
            prev_lsn: info.prev_lsn,
            prev_scn: info.prev_scn,
            prev_log_id: info.prev_log_id,
            prev_accumulated_checksum: info.prev_accumulated_checksum,
        }
    }
}

impl From<stream::BaseInfo> for BaseInfo {
    fn from(info: stream::BaseInfo) -> Self {
        Self {
            curr_lsn: info.curr_lsn,
            prev_lsn: info.prev_lsn,
            prev_scn: info.prev_scn,
            prev_log_id: info.prev_log_id,
            prev_accumulated_checksum: info.prev_accumulated_checksum,
        }
    }
}

fn create_env(path: &str, disk_soft_target: u64) -> Box<Env> {
    Box::new(Env::new(path, disk_soft_target))
}

fn start_env_ffi(env: &Env) -> Code {
    code(env.start())
}

fn stop_env_ffi(env: &Env) {
    env.stop();
}

fn create_env_stream_ffi(env: &Env, base: BaseInfo, ref_scn: u64) -> Box<Stream> {
    Box::new(match env.create(base.into(), ref_scn) {
        Ok(stream) => stream,
        Err(error) => Stream::failed(error),
    })
}

fn open_env_stream_ffi(env: &Env) -> Box<Stream> {
    Box::new(match env.open() {
        Ok(stream) => stream,
        Err(error) => Stream::failed(error),
    })
}

fn remove_env_ffi(env: &Env) -> Code {
    code(env.remove())
}

fn disk_usage_env_ffi(env: &Env) -> DiskUsageResult {
    match env.disk_usage() {
        Ok((used, total)) => DiskUsageResult {
            code: Code::Ok,
            used,
            total,
        },
        Err(error) => DiskUsageResult {
            code: error.into(),
            used: 0,
            total: 0,
        },
    }
}

fn update_disk_soft_target_env_ffi(env: &Env, disk_soft_target: u64) {
    env.update_disk_soft_target(disk_soft_target);
}

fn disk_soft_target_env_ffi(env: &Env) -> u64 {
    env.disk_soft_target()
}

fn status_ffi(stream: &Stream) -> Code {
    match stream.status() {
        Ok(_) => Code::Ok,
        Err(error) => error.into(),
    }
}

fn clone_stream_ffi(stream: &Stream) -> Box<Stream> {
    Box::new(stream.clone())
}

fn submit_ffi(stream: &Stream, payload: &[u8], ref_scn: u64) -> AppendResult {
    match stream.submit(payload, ref_scn) {
        Ok(result) => AppendResult {
            code: Code::Ok,
            lsn: result.lsn,
            scn: result.scn,
            end_lsn: result.end_lsn,
            end_scn: result.end_scn,
        },
        Err(error) => AppendResult {
            code: error.into(),
            lsn: 0,
            scn: 0,
            end_lsn: 0,
            end_scn: 0,
        },
    }
}

fn wait_flush_ffi(stream: &Stream, after_generation: u64) -> FlushResult {
    match stream.wait_flush(after_generation) {
        Ok((generation, end_lsn, end_scn)) => FlushResult {
            code: Code::Ok,
            generation,
            end_lsn,
            end_scn,
        },
        Err(error) => FlushResult {
            code: error.into(),
            generation: after_generation,
            end_lsn: 0,
            end_scn: 0,
        },
    }
}

fn copy_imported_group(source_lsn: u64, source_scn: u64, bytes: &[u8]) -> ImportedGroupResult {
    match copied_bytes(bytes) {
        Ok(bytes) => ImportedGroupResult {
            code: Code::Ok,
            group: ImportedGroup {
                source_lsn,
                source_scn,
                bytes,
            },
        },
        Err(error) => ImportedGroupResult {
            code: error.into(),
            group: ImportedGroup {
                source_lsn: 0,
                source_scn: 0,
                bytes: Vec::new(),
            },
        },
    }
}

fn append_imported_ffi(stream: &Stream, groups: &[ImportedGroup]) -> AppendResult {
    let result = (|| {
        let mut native = Vec::new();
        native.try_reserve(groups.len()).map_err(|_| Error::NoMemory)?;
        native.extend(groups.iter().map(|group| stream::ImportedGroup {
            source_lsn: group.source_lsn,
            source_scn: group.source_scn,
            bytes: &group.bytes,
        }));
        stream.append_imported(&native)
    })();
    match result {
        Ok(result) => AppendResult {
            code: Code::Ok,
            lsn: result.lsn,
            scn: result.scn,
            end_lsn: result.end_lsn,
            end_scn: result.end_scn,
        },
        Err(error) => AppendResult {
            code: error.into(),
            lsn: 0,
            scn: 0,
            end_lsn: 0,
            end_scn: 0,
        },
    }
}

fn progress_ffi(stream: &Stream) -> Progress {
    match stream.progress() {
        Ok(progress) => Progress {
            code: Code::Ok,
            begin_scn: progress.begin_scn,
            base_lsn: progress.base_lsn,
            end_lsn: progress.end_lsn,
            end_scn: progress.end_scn,
            ref_scn: progress.ref_scn,
            used_disk: progress.used_disk,
            unrecyclable_disk: progress.unrecyclable_disk,
            integrity: progress.integrity,
        },
        Err(error) => Progress {
            code: error.into(),
            begin_scn: 0,
            base_lsn: 0,
            end_lsn: 0,
            end_scn: 0,
            ref_scn: 0,
            used_disk: 0,
            unrecyclable_disk: 0,
            integrity: false,
        },
    }
}

fn base_info_ffi(stream: &Stream, lsn: u64) -> BaseInfoResult {
    match stream.base_info(lsn) {
        Ok(info) => BaseInfoResult {
            code: Code::Ok,
            info: info.into(),
        },
        Err(error) => BaseInfoResult {
            code: error.into(),
            info: BaseInfo {
                curr_lsn: 0,
                prev_lsn: 0,
                prev_scn: 0,
                prev_log_id: 0,
                prev_accumulated_checksum: 0,
            },
        },
    }
}

fn set_base_lsn_ffi(stream: &Stream, lsn: u64) -> Progress {
    match stream.set_base_lsn(lsn) {
        Ok(_) => progress_ffi(stream),
        Err(error) => Progress {
            code: error.into(),
            begin_scn: 0,
            base_lsn: 0,
            end_lsn: 0,
            end_scn: 0,
            ref_scn: 0,
            used_disk: 0,
            unrecyclable_disk: 0,
            integrity: false,
        },
    }
}

fn locate_by_scn_ffi(stream: &Stream, scn: u64) -> PositionResult {
    match stream.locate_by_scn(scn) {
        Ok(lsn) => PositionResult {
            code: Code::Ok,
            lsn,
            scn,
        },
        Err(error) => PositionResult {
            code: error.into(),
            lsn: 0,
            scn: 0,
        },
    }
}

fn locate_by_lsn_ffi(stream: &Stream, lsn: u64) -> PositionResult {
    match stream.locate_by_lsn(lsn) {
        Ok(scn) => PositionResult {
            code: Code::Ok,
            lsn,
            scn,
        },
        Err(error) => PositionResult {
            code: error.into(),
            lsn: 0,
            scn: 0,
        },
    }
}

fn close_ffi(stream: &Stream) -> Code {
    code(stream.close())
}

fn cursor_ffi(stream: &Stream, lsn: u64, group_mode: bool) -> Box<Cursor> {
    Box::new(stream.cursor(lsn, group_mode))
}

fn reuse_ffi(cursor: &mut Cursor, lsn: u64) -> Code {
    code(cursor.reuse(lsn))
}

fn next_ffi(cursor: &mut Cursor, replayable_point: u64) -> Code {
    match cursor.next(replayable_point) {
        Ok(Next::Entry) => Code::Ok,
        Ok(Next::End | Next::BlockedByReplayablePoint) => Code::End,
        Err(error) => error.into(),
    }
}

fn bytes(cursor: &Cursor) -> &[u8] {
    cursor.bytes()
}
fn payload(cursor: &Cursor) -> &[u8] {
    cursor.payload()
}
fn lsn(cursor: &Cursor) -> u64 {
    cursor.lsn()
}
fn end_lsn(cursor: &Cursor) -> u64 {
    cursor.end_lsn()
}
fn scn(cursor: &Cursor) -> u64 {
    cursor.scn()
}
fn log_id(cursor: &Cursor) -> i64 {
    cursor.log_id()
}
fn next_min_scn(cursor: &Cursor) -> u64 {
    cursor.next_min_scn()
}
