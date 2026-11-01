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

//! Receives the ecosystem `log` facade on the calling thread, before enqueueing.
//! All filtering and request context stay in the same core used by C++.
use crate::{policy, record};
use log::{Level, Log, Metadata, Record};
use std::ffi::CString;
use std::sync::OnceLock;

struct CoreLogger;
static LOGGER: CoreLogger = CoreLogger;

pub(crate) fn install() -> bool {
    static INSTALLED: OnceLock<bool> = OnceLock::new();
    *INSTALLED.get_or_init(|| {
        if log::set_logger(&LOGGER).is_err() {
            eprintln!("logger: another logger already owns the log facade");
            return false;
        }
        // A process-wide threshold cannot represent session overrides or the
        // seekdb TRACE/DEBUG ordering. Only the core chooses the active level.
        log::set_max_level(log::LevelFilter::Trace);
        true
    })
}

fn core_level(level: Level) -> i32 {
    match level {
        Level::Error => 3, // EDIAG
        Level::Warn => 4,  // WDIAG
        Level::Info => 2,
        Level::Debug => 6,
        Level::Trace => 5,
    }
}

impl Log for CoreLogger {
    fn enabled(&self, meta: &Metadata<'_>) -> bool {
        if policy::option(policy::STOP_APPEND) != 0 || !policy::context_available() {
            return false;
        }
        if policy::with_context(|ctx| ctx.borrow().disabled) {
            return false;
        }
        core_level(meta.level()) <= policy::target_level(meta.target()) as i32
    }

    fn log(&self, entry: &Record<'_>) {
        // log's ordinary macros do not call Log::enabled. Filter here before
        // allocating or evaluating Display, including session/module rules.
        if !self.enabled(entry.metadata()) {
            return;
        }
        emit(entry);
    }

    fn flush(&self) {
        let _ = crate::flush();
    }
}

fn emit(entry: &Record<'_>) {
    let Ok(module) = CString::new(format!("[{}]", entry.target())) else {
        return;
    };
    let Ok(file) = CString::new(entry.file().unwrap_or("")) else {
        return;
    };
    let current = std::thread::current();
    let Ok(name) = CString::new(current.name().unwrap_or("")) else {
        return;
    };
    #[cfg(any(target_os = "linux", target_os = "android"))]
    let thread_id = unsafe { libc::syscall(libc::SYS_gettid) as i64 };
    #[cfg(target_os = "macos")]
    let thread_id = {
        let mut id = 0;
        unsafe { libc::pthread_threadid_np(libc::pthread_self(), &mut id) };
        id as i64
    };
    #[cfg(windows)]
    let thread_id = {
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn GetCurrentThreadId() -> u32;
        }
        unsafe { GetCurrentThreadId() as i64 }
    };
    #[cfg(all(
        unix,
        not(any(target_os = "linux", target_os = "android", target_os = "macos"))
    ))]
    let thread_id = unsafe { libc::pthread_self() as usize as i64 };
    let meta = record::Metadata {
        module: module.as_ptr(),
        file: file.as_ptr(),
        function: c"".as_ptr(),
        event: std::ptr::null(),
        thread_name: name.as_ptr(),
        timestamp_us: 0,
        thread_id,
        line: entry.line().unwrap_or(0).min(i32::MAX as u32) as i32,
        level: core_level(entry.level()),
        error: 0,
        with_head: 1,
    };
    struct Pending(record::Buffer);
    impl Drop for Pending {
        fn drop(&mut self) {
            unsafe { record::cancel(&mut self.0) };
        }
    }
    let mut pending = Pending(record::Buffer::default());
    let buffer = &mut pending.0;
    unsafe {
        if record::begin(&meta, buffer) != crate::OK {
            return;
        }
        // Do not hold a lifecycle/configuration lock while running arbitrary
        // Display code. The core suppresses recursive formatting.
        let text = entry.args().to_string();
        if text.len() >= buffer.capacity {
            let _ = record::grow(buffer);
        }
        let size = text.len().min(buffer.capacity.saturating_sub(2));
        std::ptr::copy_nonoverlapping(text.as_ptr(), buffer.data.cast(), size);
        let backtrace = if buffer.flags & record::BACKTRACE != 0 {
            CString::new(std::backtrace::Backtrace::force_capture().to_string()).ok()
        } else {
            None
        };
        let _ = record::commit(
            buffer,
            size,
            backtrace
                .as_ref()
                .map_or(std::ptr::null(), |bt| bt.as_ptr()),
        );
    }
}
