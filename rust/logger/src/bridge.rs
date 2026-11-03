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

//! Generated C++ interface. All calls are C++ -> Rust; no callbacks.
//! Pointer arguments borrow bytes only for the call. Record body views expire
//! on grow/commit/cancel, and must stay on the creating thread. Map/context
//! snapshots and the read-only trace view retain the core's existing contracts.
use crate::policy::{
    context_map, dba_enabled, find_module, get_level, level_name, map_clone, map_free, map_level,
    map_set, parse_level, parse_settings, register_module, set_instance_level, set_trace_id,
    trace_field, trace_id, trace_seed, trace_sequence, MapHandle as LevelMap,
};
use crate::record::{begin, cancel, commit, grow, trace_flush};
use crate::{check_files, file_limit, flush, set_file, set_new_file_info, shutdown, start};

#[cxx::bridge(namespace = "oceanbase::logger")]
pub mod ffi {
    #[derive(Clone, Copy)]
    struct RecordMetadata {
        module: *const c_char,
        file: *const c_char,
        function: *const c_char,
        event: *const c_char,
        thread_name: *const c_char,
        timestamp_us: i64,
        thread_id: i64,
        line: i32,
        level: i32,
        error: i32,
        with_head: i32,
    }
    #[derive(Default)]
    struct RecordBuffer {
        record: *mut u8,
        data: *mut c_char,
        capacity: usize,
        flags: u32,
    }
    #[repr(u8)]
    enum OptionKey {
        Async,
        StopAppend,
        Perf,
        IgnoreRequestLevel,
        ForceCheck,
        InfoAsWdiag,
        Stopped,
        Initialized,
        AlertLevel,
    }
    #[repr(u8)]
    enum ContextKey {
        Level,
        Disabled,
        Guard,
        TraceMode,
        LastCost,
    }
    #[repr(u32)]
    enum RecordFlag {
        Backtrace = 1,
    }

    extern "Rust" {
        type LevelMap;
        fn flush() -> i32;
        fn shutdown() -> i32;
        unsafe fn set_new_file_info(info: *const c_char) -> i32;
        fn check_files() -> i32;
        unsafe fn set_file(route: usize, path: *const c_char, redirect: bool) -> i32;
        unsafe fn map_clone(map: *const LevelMap) -> *mut LevelMap;
        unsafe fn map_free(map: *mut LevelMap);
        unsafe fn map_level(map: *const LevelMap, parent: i32, sub: i32) -> i32;
        unsafe fn map_set(map: *mut *mut LevelMap, parent: i32, sub: i32, level: i32) -> i32;
        unsafe fn context_map(map: *const LevelMap);
        fn context_value(key: ContextKey, value: i64, write: i32) -> i64;
        fn option(key: OptionKey, value: i64, write: i32) -> i64;
        fn get_level(parent: i32, sub: i32, request: i32) -> i32;
        fn dba_enabled(level: i32, force: i32) -> i32;
        fn set_instance_level(level: i32, version: i64) -> i32;
        unsafe fn register_module(parent: i32, sub: i32, name: *const c_char) -> i32;
        unsafe fn find_module(
            name: *const c_char,
            sub_name: *const c_char,
            parent: *mut u64,
            sub: *mut u64,
        ) -> i32;
        unsafe fn parse_level(name: *const c_char, level: *mut i8, alert: i32) -> i32;
        unsafe fn parse_settings(
            text: *const c_char,
            len: i32,
            valid: *mut i32,
            map: *mut *mut LevelMap,
            version: i64,
            check: i32,
        ) -> i32;
        fn level_name(level: i32) -> *const c_char;
        fn trace_id() -> *const u64;
        unsafe fn set_trace_id(words: *const u64);
        fn trace_sequence() -> u64;
        fn trace_seed(seed: u64);
        fn trace_field(field: usize, value: u64);
        unsafe fn begin(meta: *const RecordMetadata, out: *mut RecordBuffer) -> i32;
        unsafe fn grow(buffer: *mut RecordBuffer) -> i32;
        unsafe fn commit(
            buffer: *mut RecordBuffer,
            body_len: usize,
            backtrace: *const c_char,
        ) -> i32;
        unsafe fn cancel(buffer: *mut RecordBuffer);
        unsafe fn trace_flush(module: *const c_char, level: i32) -> i32;
        fn start() -> i32;
        fn file_limit(key: usize, value: u64, write: i32) -> u64;
    }
}

fn option(key: ffi::OptionKey, value: i64, write: i32) -> i64 {
    crate::option_value(key.repr as usize, value, write)
}
fn context_value(key: ffi::ContextKey, value: i64, write: i32) -> i64 {
    crate::context_value(key.repr as usize, value, write)
}
