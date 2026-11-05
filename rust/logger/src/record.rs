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

//! Complete records are owned here. C++ borrows only the body region between
//! begin/grow and commit/cancel; metadata and context never escape that borrow.
use crate::policy::{self, with_context, TRACE};
use std::alloc::{alloc, dealloc, realloc, Layout};
use std::ffi::{c_char, CStr};
use std::fmt::{self, Write};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

// The real DB diagnostic workload includes many 1–2 KiB object records.
// Starting at 2 KiB avoids formatting those C++ objects twice; the allocation
// is still released per record, rather than retained as a per-thread buffer.
const BASE_SIZE: usize = 2 * 1024;
const MAX_SIZE: usize = 64 * 1024;
const CANARY: u64 = 0xDEADBEEFBAADF00D;
const GUARD_SIZE: usize = 8;
pub const BACKTRACE: u32 = 1;

pub use crate::bridge::ffi::{RecordBuffer as Buffer, RecordMetadata as Metadata};
#[repr(C, align(16))]
struct Record {
    started: Instant,
    capacity: usize,
    header: usize,
    route: usize,
    trace_mode: bool,
    flags: u32,
}
impl Record {
    fn layout(capacity: usize) -> Layout {
        Layout::from_size_align(std::mem::size_of::<Self>() + capacity, 16).unwrap()
    }
    unsafe fn bytes(&mut self) -> &mut [u8] {
        std::slice::from_raw_parts_mut((self as *mut Self).add(1).cast::<u8>(), self.capacity)
    }
    unsafe fn view(&mut self) -> Buffer {
        Buffer {
            record: (self as *mut Self).cast(),
            data: (self as *mut Self)
                .add(1)
                .cast::<u8>()
                .add(self.header)
                .cast(),
            capacity: self.capacity - self.header - GUARD_SIZE,
            flags: self.flags,
        }
    }
}

pub struct SliceWriter<'a> {
    bytes: &'a mut [u8],
    pub pos: usize,
}
impl<'a> SliceWriter<'a> {
    pub fn new(bytes: &'a mut [u8]) -> Self {
        Self { bytes, pos: 0 }
    }
}
impl Write for SliceWriter<'_> {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        if text.len() > self.bytes.len().saturating_sub(self.pos) {
            return Err(fmt::Error);
        }
        self.bytes[self.pos..self.pos + text.len()].copy_from_slice(text.as_bytes());
        self.pos += text.len();
        Ok(())
    }
}
unsafe fn text<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        ""
    } else {
        CStr::from_ptr(ptr).to_str().unwrap_or("?")
    }
}
pub fn route(module: &str, level: i32, event: bool) -> usize {
    if level <= 2 && event {
        3
    } else if level > 1 && module.starts_with("[FLT") {
        1
    } else {
        0
    }
}

fn timestamp(value: i64) -> i64 {
    if value != 0 {
        value
    } else {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_micros()
            .min(i64::MAX as u128) as i64
    }
}

#[derive(Clone, Copy)]
struct TimestampCache {
    second: i64,
    bytes: [u8; 32],
    len: usize,
}
thread_local! {
    // Like the former C++ local-time cache, this is derived data, refreshed
    // whenever the second changes (including clock rollback). No heap buffer
    // is retained per thread, and microseconds are always taken from this record.
    static TIMESTAMP_CACHE: std::cell::Cell<TimestampCache> = const {
        std::cell::Cell::new(TimestampCache { second: i64::MIN, bytes: [0; 32], len: 0 })
    };
}
fn write_timestamp(writer: &mut impl Write, time: i64) -> fmt::Result {
    let second = time.div_euclid(1_000_000);
    TIMESTAMP_CACHE.with(|slot| {
        let mut cached = slot.get();
        if cached.second != second {
            let date = chrono::DateTime::from_timestamp(second, 0).ok_or(fmt::Error)?;
            let date = date.with_timezone(&chrono::Local);
            let mut prefix = SliceWriter::new(&mut cached.bytes);
            // write_to avoids DelayedFormat's Display temporary String.
            date.format("%Y-%m-%d %H:%M:%S").write_to(&mut prefix)?;
            cached.len = prefix.pos;
            cached.second = second;
            slot.set(cached);
        }
        writer
            .write_str(std::str::from_utf8(&cached.bytes[..cached.len]).map_err(|_| fmt::Error)?)?;
        write!(writer, ".{:06}", time.rem_euclid(1_000_000))
    })
}
fn write_trace(writer: &mut impl Write, words: [u64; 4]) -> fmt::Result {
    // The IPv6 flag is bit 17 of the high 32 bits of word zero. IPv6 textual
    // IDs omit the IPv4 field and retain the existing lowercase last words.
    if words[0] & (1_u64 << 49) != 0 {
        write!(
            writer,
            "Z{:X}-{:016X}-{:x}-{:x}",
            words[0] >> 32,
            words[1],
            words[2],
            words[3]
        )
    } else {
        write!(
            writer,
            "Y{:X}-{:016X}-{:x}-{:x}",
            words[0], words[1], words[2], words[3]
        )
    }
}

unsafe fn header(meta: &Metadata, route: usize, writer: &mut SliceWriter<'_>) -> fmt::Result {
    if meta.with_head == 0 {
        return Ok(());
    }
    let time = timestamp(meta.timestamp_us);
    let file = text(meta.file).rsplit('/').next().unwrap_or("");
    let module = text(meta.module);
    let function = text(meta.function);
    let name = text(meta.thread_name);
    let trace = TRACE.with(std::cell::Cell::get);
    let level = policy::NAMES.get(meta.level as usize).ok_or(fmt::Error)?;
    if route == 3 {
        write_timestamp(writer, time)?;
        write!(
            writer,
            "|{}|{}|{}|{}|{}|{}|",
            level,
            module,
            if meta.event.is_null() {
                "none"
            } else {
                text(meta.event)
            },
            meta.error,
            meta.thread_id,
            name
        )?;
        write_trace(writer, trace)?;
        write!(writer, "|{}|{}:{}|", function, file, meta.line)
    } else if route == 1 {
        writer.write_str("[")?;
        write_timestamp(writer, time)?;
        write!(writer, "] [{}][{}][", meta.thread_id, name)?;
        write_trace(writer, trace)?;
        writer.write_str("] ")
    } else {
        writer.write_str("[")?;
        write_timestamp(writer, time)?;
        write!(
            writer,
            "] {:<5} {}{} ({}:{}) [{}][{}][",
            level, module, function, file, meta.line, meta.thread_id, name
        )?;
        write_trace(writer, trace)?;
        let cost = with_context(|ctx| ctx.borrow().last_cost);
        write!(writer, "] [lt={}] ", cost)?;
        // The legacy format places errcode immediately after [lt=...] and
        // only then writes a space. Replace that last space when applicable.
        if [0, 1, 3, 4].contains(&meta.level) {
            writer.pos -= 1;
            write!(writer, "[errcode={}] ", meta.error)?;
        }
        Ok(())
    }
}

pub unsafe fn begin(meta: *const Metadata, out: *mut Buffer) -> i32 {
    if meta.is_null() || out.is_null() {
        return crate::ERROR;
    }
    out.write(Buffer::default());
    let mut metadata = *meta;
    metadata.timestamp_us = timestamp(metadata.timestamp_us);
    let meta = &metadata;
    if !(0..7).contains(&meta.level)
        || meta.module.is_null()
        || meta.file.is_null()
        || meta.function.is_null()
    {
        return crate::ERROR;
    }
    if policy::option(policy::STOP_APPEND) != 0 {
        return crate::NOT_INIT;
    }
    if !policy::context_available() || with_context(|ctx| ctx.borrow().disabled) {
        return 1;
    }
    let start = Instant::now();
    let mut memory = alloc(Record::layout(BASE_SIZE)).cast::<Record>();
    if memory.is_null() {
        return crate::ERROR;
    }
    let trace_mode = with_context(|ctx| {
        let mut ctx = ctx.borrow_mut();
        ctx.disabled = true;
        ctx.trace_mode
    });
    memory.write(Record {
        started: start,
        capacity: BASE_SIZE,
        header: 0,
        route: route(text(meta.module), meta.level, !meta.event.is_null()),
        trace_mode,
        flags: if meta.level == 3 { BACKTRACE } else { 0 },
    });
    let mut header_len = initialize_header(memory, meta);
    if header_len.is_none() {
        let larger = realloc(
            memory.cast(),
            Record::layout(BASE_SIZE),
            Record::layout(MAX_SIZE).size(),
        )
        .cast::<Record>();
        if larger.is_null() {
            release(memory);
            return crate::ERROR;
        }
        memory = larger;
        (*memory).capacity = MAX_SIZE;
        header_len = initialize_header(memory, meta);
    }
    let Some(header_len) = header_len else {
        release(memory);
        return crate::ERROR;
    };
    let record = &mut *memory;
    record.header = header_len;
    let capacity = record.capacity;
    let header = record.header;
    let bytes = record.bytes();
    std::ptr::write_unaligned(
        bytes.as_mut_ptr().add(capacity - GUARD_SIZE).cast::<u64>(),
        CANARY,
    );
    bytes[header] = 0;
    out.write(record.view());
    0
}

unsafe fn initialize_header(memory: *mut Record, meta: &Metadata) -> Option<usize> {
    let record = &mut *memory;
    let (route, capacity) = (record.route, record.capacity);
    let mut writer = SliceWriter::new(&mut record.bytes()[..capacity - GUARD_SIZE - 2]);
    if header(meta, route, &mut writer).is_ok() {
        Some(writer.pos)
    } else {
        None
    }
}

pub unsafe fn grow(buffer: *mut Buffer) -> i32 {
    if buffer.is_null() || (*buffer).record.is_null() {
        return crate::ERROR;
    }
    let old = (*buffer).record.cast::<Record>();
    if std::ptr::read_unaligned(
        (*old)
            .bytes()
            .as_ptr()
            .add((*old).capacity - GUARD_SIZE)
            .cast::<u64>(),
    ) != CANARY
    {
        std::process::abort();
    }
    if (*old).capacity >= MAX_SIZE {
        return crate::ERROR;
    }
    let memory = realloc(
        old.cast(),
        Record::layout((*old).capacity),
        Record::layout(MAX_SIZE).size(),
    )
    .cast::<Record>();
    if memory.is_null() {
        return crate::ERROR;
    }
    (*memory).capacity = MAX_SIZE;
    std::ptr::write_unaligned(
        (*memory)
            .bytes()
            .as_mut_ptr()
            .add(MAX_SIZE - GUARD_SIZE)
            .cast::<u64>(),
        CANARY,
    );
    *buffer = (*memory).view();
    0
}

unsafe fn release(record: *mut Record) {
    let elapsed = (*record)
        .started
        .elapsed()
        .as_micros()
        .min(u64::MAX as u128) as u64;
    if elapsed > 500_000 {
        eprintln!("LOGGER COST TOO MUCH TIME, cost: {elapsed} us");
    }
    with_context(|ctx| {
        let mut ctx = ctx.borrow_mut();
        ctx.disabled = false;
        ctx.last_cost = elapsed;
    });
    dealloc(record.cast(), Record::layout((*record).capacity));
}
pub unsafe fn cancel(buffer: *mut Buffer) {
    if !buffer.is_null() && !(*buffer).record.is_null() {
        let record = (*buffer).record.cast::<Record>();
        release(record);
        *buffer = Buffer::default();
    }
}

fn asynchronous() -> bool {
    policy::option(policy::ASYNC) != 0
        && policy::option(policy::INITIALIZED) != 0
        && policy::option(policy::STOPPED) == 0
}
pub fn flush_trace(route: usize) -> i32 {
    // Detach while submitting so a filesystem error cannot recursively borrow
    // the context. Reuse its allocation after the queue has taken ownership.
    let mut bytes = with_context(|ctx| std::mem::take(&mut ctx.borrow_mut().trace_buffer));
    let result = if bytes.is_empty() {
        0
    } else {
        crate::submit_complete(route, &bytes, asynchronous())
    };
    bytes.clear();
    with_context(|ctx| ctx.borrow_mut().trace_buffer = bytes);
    result
}
pub unsafe fn trace_flush(module: *const c_char, level: i32) -> i32 {
    flush_trace(route(text(module), level, false))
}

pub unsafe fn commit(buffer: *mut Buffer, body_len: usize, backtrace: *const c_char) -> i32 {
    if buffer.is_null() || (*buffer).record.is_null() {
        return crate::ERROR;
    }
    let initial = (*buffer).record.cast::<Record>();
    if (*initial).flags & BACKTRACE != 0 && !backtrace.is_null() {
        let tail = CStr::from_ptr(backtrace)
            .to_bytes()
            .len()
            .saturating_add(17);
        if (*initial).header.saturating_add(tail + GUARD_SIZE + 2) > (*initial).capacity {
            let _ = grow(buffer);
        }
    }
    let memory = (*buffer).record.cast::<Record>();
    let record = &mut *memory;
    let capacity = record.capacity;
    let header = record.header;
    let (flags, trace_mode, route) = (record.flags, record.trace_mode, record.route);
    let bytes = record.bytes();
    if std::ptr::read_unaligned(bytes.as_ptr().add(capacity - GUARD_SIZE).cast::<u64>()) != CANARY {
        std::process::abort();
    }
    let end = capacity - GUARD_SIZE - 2;
    let mut len = header + body_len.min(end - header);
    // The platform stack text is a value supplied actively by the C++ adapter;
    // the core decides whether to append it and owns the complete tail format.
    if flags & BACKTRACE != 0 && !backtrace.is_null() {
        if len > 0 && bytes[len - 1] == b'\n' {
            len -= 1;
        }
        let suffix = b" BACKTRACE:".as_slice();
        let bt = CStr::from_ptr(backtrace).to_bytes();
        let available = end.saturating_sub(header);
        let tail_len = (suffix.len() + bt.len()).min(available);
        len = len.min(end - tail_len);
        for part in [suffix, bt] {
            let n = part.len().min(end.saturating_sub(len));
            bytes[len..len + n].copy_from_slice(&part[..n]);
            len += n;
        }
    }
    if len == 0 || bytes[len - 1] != b'\n' {
        bytes[len] = b'\n';
        len += 1;
    }
    let result = if trace_mode {
        let spill = with_context(|ctx| ctx.borrow().trace_buffer.len() + len > MAX_SIZE);
        let result = if spill { flush_trace(route) } else { 0 };
        with_context(|ctx| {
            let mut ctx = ctx.borrow_mut();
            if ctx.trace_buffer.capacity() == 0 {
                ctx.trace_buffer.reserve_exact(MAX_SIZE);
            }
            ctx.trace_buffer.extend_from_slice(&bytes[..len]);
        });
        result
    } else {
        crate::submit_complete(route, &bytes[..len], asynchronous())
    };
    release(memory);
    *buffer = Buffer::default();
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cached_timestamps_preserve_fraction_and_refresh_on_clock_changes() {
        for time in [
            946684800123456_i64,
            946684800999999,
            946684801000001,
            946684800000000,
            -1,
            -999999,
            -1000001,
            0,
            i64::MAX,
        ] {
            let date = chrono::DateTime::from_timestamp(
                time.div_euclid(1_000_000),
                (time.rem_euclid(1_000_000) * 1000) as u32,
            );
            let mut result = String::new();
            if let Some(date) = date {
                write_timestamp(&mut result, time).unwrap();
                assert_eq!(
                    result,
                    date.with_timezone(&chrono::Local)
                        .format("%Y-%m-%d %H:%M:%S%.6f")
                        .to_string()
                );
            } else {
                assert!(write_timestamp(&mut result, time).is_err());
            }
        }
    }
    #[test]
    fn legacy_headers_match_all_levels_routes_and_ip_families() {
        let date = chrono::DateTime::from_timestamp(946684800, 123456000)
            .unwrap()
            .with_timezone(&chrono::Local);
        let time = date.format("%Y-%m-%d %H:%M:%S%.6f").to_string();
        with_context(|ctx| ctx.borrow_mut().last_cost = 9);
        for (words, trace) in [
            ([0x123_u64, 2, 0xab, 0xcd], "Y123-0000000000000002-ab-cd"),
            (
                [0x2_0000_0000_0123, 2, 0xab, 0xcd],
                "Z20000-0000000000000002-ab-cd",
            ),
        ] {
            TRACE.with(|id| id.set(words));
            for (level, name) in ["ERROR", "WARN", "INFO", "EDIAG", "WDIAG", "TRACE", "DEBUG"]
                .into_iter()
                .enumerate()
            {
                for (module, event) in [
                    (c"[SQL]", std::ptr::null()),
                    (c"[FLT]", std::ptr::null()),
                    (c"[SQL]", c"event".as_ptr()),
                ] {
                    let meta = Metadata {
                        module: module.as_ptr(),
                        file: c"/a/file.cpp".as_ptr(),
                        function: c"fn".as_ptr(),
                        event,
                        thread_name: c"worker".as_ptr(),
                        timestamp_us: 946684800123456,
                        thread_id: 17,
                        line: 42,
                        level: level as i32,
                        error: -4002,
                        with_head: 1,
                    };
                    let route = route(module.to_str().unwrap(), level as i32, !event.is_null());
                    let expected = if !event.is_null() && level <= 2 {
                        format!("{time}|{name}|[SQL]|event|-4002|17|worker|{trace}|fn|file.cpp:42|")
                    } else if module == c"[FLT]" && level > 1 {
                        format!("[{time}] [17][worker][{trace}] ")
                    } else {
                        format!(
                            "[{time}] {name:<5} {}fn (file.cpp:42) [17][worker][{trace}] [lt=9]{} ",
                            module.to_str().unwrap(),
                            if [0, 1, 3, 4].contains(&level) {
                                "[errcode=-4002]"
                            } else {
                                ""
                            }
                        )
                    };
                    let mut bytes = [0_u8; 1024];
                    let mut writer = SliceWriter::new(&mut bytes);
                    unsafe {
                        header(&meta, route, &mut writer).unwrap();
                    }
                    let len = writer.pos;
                    assert_eq!(&bytes[..len], expected.as_bytes());
                }
            }
        }
        TRACE.with(|id| id.set([0; 4]));
    }
}
