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

use crate::format::{Error, Result};
use crate::stream::{self, Cursor, DiskPolicy as NativeDiskPolicy, Stream};

#[cxx::bridge(namespace = "oceanbase::palf::palf_rs")]
mod ffi {
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Code {
        Ok,
        Invalid,
        NotInit,
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

    struct DiskPolicy {
        capacity: u64,
        stop_percentage: u8,
        throttle_percentage: u8,
        max_throttle_duration_us: u64,
    }

    struct AppendResult {
        code: Code,
        lsn: u64,
        scn: u64,
        end_lsn: u64,
        end_scn: u64,
    }

    struct Progress {
        code: Code,
        begin_lsn: u64,
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

    extern "Rust" {
        type Stream;
        fn create_stream(path: &str, base: BaseInfo, ref_scn: u64, disk: DiskPolicy)
            -> Box<Stream>;
        fn open_stream(path: &str, disk: DiskPolicy) -> Box<Stream>;
        fn status_ffi(stream: &Stream) -> Code;
        fn integrity_ffi(stream: &Stream) -> bool;
        fn append_ffi(stream: &Stream, payload: &[u8], ref_scn: u64) -> AppendResult;
        fn append_imported_ffi(
            stream: &Stream,
            source_lsn: u64,
            source_scn: u64,
            bytes: &[u8],
        ) -> AppendResult;
        fn progress_ffi(stream: &Stream) -> Progress;
        fn base_info_ffi(stream: &Stream, lsn: u64) -> BaseInfoResult;
        fn set_base_lsn_ffi(stream: &Stream, lsn: u64) -> Progress;
        fn set_disk_policy_ffi(stream: &Stream, disk: DiskPolicy) -> Code;
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
        fn committed_end_lsn(cursor: &Cursor) -> u64;
        fn accumulated_checksum(cursor: &Cursor) -> u64;
        fn padding(cursor: &Cursor) -> bool;
        fn next_min_scn(cursor: &Cursor) -> u64;
        fn is_last_ffi(cursor: &Cursor) -> bool;
    }
}

pub use ffi::{AppendResult, BaseInfo, BaseInfoResult, Code, DiskPolicy, PositionResult, Progress};

impl From<DiskPolicy> for NativeDiskPolicy {
    fn from(policy: DiskPolicy) -> Self {
        Self {
            capacity: policy.capacity,
            stop_percentage: policy.stop_percentage,
            throttle_percentage: policy.throttle_percentage,
            max_throttle_duration_us: policy.max_throttle_duration_us,
        }
    }
}

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

fn create_stream(path: &str, base: BaseInfo, ref_scn: u64, disk: DiskPolicy) -> Box<Stream> {
    Box::new(Stream::create_with_policy(
        path,
        base.into(),
        ref_scn,
        disk.into(),
    ))
}

fn open_stream(path: &str, disk: DiskPolicy) -> Box<Stream> {
    Box::new(Stream::open_with_policy(path, disk.into()))
}

fn status_ffi(stream: &Stream) -> Code {
    match stream.status() {
        Ok(_) => Code::Ok,
        Err(error) => error.into(),
    }
}

fn integrity_ffi(stream: &Stream) -> bool {
    stream.status().unwrap_or(false)
}

fn append_ffi(stream: &Stream, payload: &[u8], ref_scn: u64) -> AppendResult {
    match stream.append(payload, ref_scn) {
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

fn append_imported_ffi(
    stream: &Stream,
    source_lsn: u64,
    source_scn: u64,
    bytes: &[u8],
) -> AppendResult {
    match stream.append_imported(source_lsn, source_scn, bytes) {
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
            begin_lsn: progress.begin_lsn,
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
            begin_lsn: 0,
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
            begin_lsn: 0,
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

fn set_disk_policy_ffi(stream: &Stream, disk: DiskPolicy) -> Code {
    code(stream.set_disk_policy(disk.into()))
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
    code(cursor.next(replayable_point).map(|_| ()))
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
fn committed_end_lsn(cursor: &Cursor) -> u64 {
    cursor.committed_end_lsn()
}
fn accumulated_checksum(cursor: &Cursor) -> u64 {
    cursor.accumulated_checksum()
}
fn padding(cursor: &Cursor) -> bool {
    cursor.padding()
}
fn next_min_scn(cursor: &Cursor) -> u64 {
    cursor.next_min_scn()
}
fn is_last_ffi(cursor: &Cursor) -> bool {
    cursor.is_last().unwrap_or(false)
}
