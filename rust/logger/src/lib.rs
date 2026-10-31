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

//! Runtime logging for C++ and Rust through spdlog-rs 0.5.3.
//!
//! C++ supplies already formatted bytes. Rust owns the bounded async pool and
//! file backend; there are no reverse C++ calls. Native Rust code can use
//! `log::info!("rows={}", rows)` without passing a context or logger.
#![deny(improper_ctypes_definitions)]

mod bridge;
mod facade;
mod files;
mod policy;
mod record;
pub use policy::*;

use files::{Limits, Output};
use spdlog::error::{SendToChannelError, SendToChannelErrorDropped};
use spdlog::prelude::*;
use spdlog::sink::{AsyncPoolSink, GetSinkProp, OverflowPolicy, Sink, SinkProp};
use std::cell::Cell;
use std::ffi::{c_char, CStr};
use std::path::PathBuf;
use std::sync::{Arc, Condvar, Mutex, RwLock};
use std::time::Duration;

pub const CAPACITY: usize = 8192;
const ROUTES: usize = 4;
const FLUSH_INTERVAL: Duration = Duration::from_millis(20);
const OK: i32 = 0;
const ERROR: i32 = -1;
const FULL: i32 = -2;
const NOT_INIT: i32 = -3;
static BACKEND: RwLock<Option<Backend>> = RwLock::new(None);
static EARLY: RwLock<Option<Arc<Router>>> = RwLock::new(None);
thread_local! { static ADMISSION: Cell<i32> = const { Cell::new(OK) }; }

struct Files {
    outputs: [Output; ROUTES],
    limits: Limits,
    info: Option<String>,
    flush_failed: bool,
}

struct Router {
    prop: SinkProp,
    files: Mutex<Files>,
    completed_flushes: Mutex<(u64, bool)>,
    flushed: Condvar,
}

impl Router {
    fn write(&self, route: usize, bytes: &[u8], asynchronous: bool) -> spdlog::Result<()> {
        let mut files = self.files.lock().unwrap();
        let Files {
            outputs,
            limits,
            info,
            ..
        } = &mut *files;
        let result = outputs[route].write(
            bytes,
            limits,
            if route == 0 { info.as_deref() } else { None },
            asynchronous,
        );
        if asynchronous && result.is_err() {
            files.flush_failed = true;
        }
        result.map_err(spdlog::Error::WriteRecord)
    }

    fn flush_files(&self, files: &mut Files) -> spdlog::Result<()> {
        let mut first_error = None;
        for output in &mut files.outputs {
            if let Err(error) = output.flush() {
                files.flush_failed = true;
                first_error.get_or_insert(error);
            }
        }
        first_error.map_or(Ok(()), |error| Err(spdlog::Error::FlushBuffer(error)))
    }
}

impl GetSinkProp for Router {
    fn prop(&self) -> &SinkProp {
        &self.prop
    }
}

impl Sink for Router {
    fn log(&self, record: &spdlog::Record) -> spdlog::Result<()> {
        // Names are internal routing metadata. No metadata is added to the
        // C++ payload on disk. A separate lossless path handles binary bytes.
        let name = record.logger_name().unwrap_or("c0");
        let route = (name.as_bytes()[1] - b'0') as usize;
        if name.starts_with('b') {
            let bytes: Vec<u8> = record.payload().chars().map(|ch| ch as u8).collect();
            self.write(route, &bytes, true)
        } else {
            self.write(route, record.payload().as_bytes(), true)
        }
    }

    fn flush(&self) -> spdlog::Result<()> {
        let mut files = self.files.lock().unwrap();
        self.flush_files(&mut files)
    }
}

// Periodic flush tasks and explicit drain barriers share the FIFO worker, but
// only the latter may wake a caller waiting for its own queued barrier.
struct FlushBarrier(Arc<Router>);

impl GetSinkProp for FlushBarrier {
    fn prop(&self) -> &SinkProp {
        &self.0.prop
    }
}

impl Sink for FlushBarrier {
    fn log(&self, record: &spdlog::Record) -> spdlog::Result<()> {
        self.0.log(record)
    }

    fn flush(&self) -> spdlog::Result<()> {
        let mut files = self.0.files.lock().unwrap();
        let result = self.0.flush_files(&mut files);
        let failed = std::mem::take(&mut files.flush_failed);
        let mut completed = self.0.completed_flushes.lock().unwrap();
        completed.0 += 1;
        completed.1 = failed;
        self.0.flushed.notify_all();
        result
    }
}

struct Backend {
    loggers: [Arc<Logger>; ROUTES],
    binary_loggers: [Arc<Logger>; ROUTES],
    control: AsyncPoolSink,
    router: Arc<Router>,
    flush_serial: Mutex<()>,
    periodic: Arc<Logger>,
}

fn make_router(outputs: [Output; ROUTES], limits: Limits, info: Option<String>) -> Arc<Router> {
    Arc::new(Router {
        prop: SinkProp::default(),
        files: Mutex::new(Files {
            outputs,
            limits,
            info,
            flush_failed: false,
        }),
        completed_flushes: Mutex::new((0, false)),
        flushed: Condvar::new(),
    })
}

impl Backend {
    fn from_router(router: Arc<Router>, capacity: usize) -> spdlog::Result<Self> {
        let pool = spdlog::ThreadPool::builder()
            .capacity(capacity.try_into().unwrap())
            .on_thread_spawn(|| {
                #[cfg(any(target_os = "linux", target_os = "android"))]
                unsafe {
                    libc::pthread_setname_np(libc::pthread_self(), c"OB_PLOG".as_ptr());
                }
                #[cfg(target_os = "macos")]
                unsafe {
                    libc::pthread_setname_np(c"OB_PLOG".as_ptr());
                }
            })
            .build_arc()?;
        let handler = move |error: spdlog::Error| {
            if matches!(
                error,
                spdlog::Error::SendToChannel(
                    SendToChannelError::Full,
                    SendToChannelErrorDropped::Record(_)
                )
            ) {
                ADMISSION.set(FULL);
            } else {
                ADMISSION.set(ERROR);
            }
        };
        let sink = AsyncPoolSink::builder()
            .sink(router.clone())
            .thread_pool(pool.clone())
            .overflow_policy(OverflowPolicy::DropIncoming)
            .error_handler(handler)
            .build_arc()?;
        let logger = |name: String| -> spdlog::Result<Arc<Logger>> {
            let logger = Logger::builder()
                .name(name)
                .sink(sink.clone())
                .level_filter(LevelFilter::All)
                .build_arc()?;
            logger.set_error_handler(handler);
            Ok(logger)
        };
        let loggers = (0..ROUTES)
            .map(|i| logger(format!("c{i}")))
            .collect::<spdlog::Result<Vec<_>>>()?;
        let binary_loggers = (0..ROUTES)
            .map(|i| logger(format!("b{i}")))
            .collect::<spdlog::Result<Vec<_>>>()?;
        let control = AsyncPoolSink::builder()
            .sink(Arc::new(FlushBarrier(router.clone())))
            .thread_pool(pool.clone())
            .overflow_policy(OverflowPolicy::Block)
            // The barrier reports file failures to its waiting caller.
            .error_handler(|_| {})
            .build()?;
        // One library timer covers all routes. A full queue may skip a timer
        // tick; subsequent ticks retry. The Router retains unreported failures
        // for the next explicit flush, without logging recursively.
        let periodic_sink = AsyncPoolSink::builder()
            .sink(router.clone())
            .thread_pool(pool)
            .overflow_policy(OverflowPolicy::DropIncoming)
            .error_handler(|_| {})
            .build_arc()?;
        let periodic = Logger::builder().sink(periodic_sink).build_arc()?;
        periodic.set_error_handler(|_| {});
        periodic.set_flush_period(Some(FLUSH_INTERVAL));
        Ok(Self {
            loggers: loggers.try_into().ok().unwrap(),
            binary_loggers: binary_loggers.try_into().ok().unwrap(),
            control,
            router,
            flush_serial: Mutex::new(()),
            periodic,
        })
    }

    fn submit(&self, route: usize, bytes: &[u8], asynchronous: bool) -> i32 {
        if !asynchronous {
            return match self.router.write(route, bytes, false) {
                Ok(()) => OK,
                Err(_) => ERROR,
            };
        }
        ADMISSION.set(OK);
        match std::str::from_utf8(bytes) {
            Ok(text) => spdlog::info!(logger: &self.loggers[route], "{}", text),
            Err(_) => {
                // Latin-1 is only an internal reversible transport through
                // spdlog's UTF-8 Record. It is decoded by Router, not emitted.
                let text: String = bytes.iter().map(|&b| char::from(b)).collect();
                spdlog::info!(logger: &self.binary_loggers[route], "{}", text);
            }
        }
        ADMISSION.get()
    }

    fn flush(&self) -> i32 {
        let _serial = self.flush_serial.lock().unwrap();
        let mut completed = self.router.completed_flushes.lock().unwrap();
        let target = completed.0 + 1;
        if self.control.flush().is_err() {
            return ERROR;
        }
        while completed.0 < target {
            completed = self.router.flushed.wait(completed).unwrap();
        }
        if completed.1 {
            ERROR
        } else {
            OK
        }
    }
}

impl Drop for Backend {
    fn drop(&mut self) {
        // BACKEND's write lock has quiesced producers before this runs.
        // Stop the library timer before closing the shared async pool.
        self.periodic.set_flush_period(None);
        // spdlog joins the sole worker after processing the remaining queue.
        let _ = self.control.flush_on_exit();
    }
}

fn with_backend(f: impl FnOnce(&Backend) -> i32) -> i32 {
    let backend = BACKEND.read().unwrap();
    backend.as_ref().map_or(NOT_INIT, f)
}

// Startup has the same Rust file owner without allocating the async queue.
fn early_router() -> Result<Arc<Router>, i32> {
    let mut early = EARLY.write().unwrap();
    if early.is_none() {
        let outputs = (0..ROUTES)
            .map(|_| Output::new(PathBuf::new(), false))
            .collect::<std::io::Result<Vec<_>>>()
            .map_err(|_| ERROR)?;
        *early = Some(make_router(
            outputs.try_into().ok().unwrap(),
            Limits {
                size: 256 * 1024 * 1024,
                count: 0,
            },
            None,
        ));
    }
    Ok(early.as_ref().unwrap().clone())
}
fn with_router(f: impl FnOnce(&Router) -> i32) -> i32 {
    // Keep the admission/lifecycle gate through file operations. Shutdown
    // cannot detach a router while a producer or configuration call uses it.
    let backend = BACKEND.read().unwrap();
    if let Some(backend) = backend.as_ref() {
        f(&backend.router)
    } else {
        early_router().map_or_else(|error| error, |router| f(&router))
    }
}
fn submit_complete(route: usize, bytes: &[u8], asynchronous: bool) -> i32 {
    if policy::option(policy::STOP_APPEND) != 0 {
        return NOT_INIT;
    }
    if asynchronous {
        return with_backend(|backend| {
            if policy::option(policy::FORCE_CHECK) != 0
                && check_files_unlocked(&backend.router) != OK
            {
                return ERROR;
            }
            backend.submit(route, bytes, true)
        });
    }
    with_router(|router| {
        if policy::option(policy::FORCE_CHECK) != 0 && check_files_unlocked(router) != OK {
            return ERROR;
        }
        if router.write(route, bytes, false).is_ok() {
            OK
        } else {
            ERROR
        }
    })
}
pub fn start() -> i32 {
    if !facade::install() {
        return ERROR;
    }
    let mut backend = BACKEND.write().unwrap();
    if backend.is_some() {
        return ERROR;
    }
    let router = match early_router() {
        Ok(router) => router,
        Err(error) => return error,
    };
    match Backend::from_router(router, CAPACITY) {
        Ok(value) => {
            *backend = Some(value);
            policy::set_option(policy::INITIALIZED, 1);
            policy::set_option(policy::STOPPED, 0);
            policy::set_option(policy::STOP_APPEND, 0);
            OK
        }
        Err(_) => ERROR,
    }
}
pub fn file_limit(key: usize, value: u64, write: i32) -> u64 {
    let mut result = 0;
    with_router(|router| {
        let mut files = router.files.lock().unwrap();
        let target = match key {
            0 => &mut files.limits.size,
            1 => &mut files.limits.count,
            _ => return ERROR,
        };
        result = *target;
        if write != 0 {
            *target = value;
        }
        OK
    });
    result
}

unsafe fn owned_text(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        None
    } else {
        Some(CStr::from_ptr(ptr).to_string_lossy().into_owned())
    }
}

pub fn flush() -> i32 {
    with_backend(Backend::flush)
}

pub fn shutdown() -> i32 {
    // Hold the gate until draining/joining finishes: init cannot race shutdown.
    let mut backend = BACKEND.write().unwrap();
    policy::set_option(policy::STOP_APPEND, 1);
    policy::set_option(policy::STOPPED, 1);
    policy::set_option(policy::INITIALIZED, 0);
    EARLY.write().unwrap().take();
    if let Some(value) = backend.take() {
        // Report failures from draining the remaining work, not errors from
        // records already processed before the drain begins.
        value.router.files.lock().unwrap().flush_failed = false;
        let result = value.flush();
        drop(value);
        result
    } else {
        OK
    }
}

pub unsafe fn set_new_file_info(info: *const c_char) -> i32 {
    with_router(|router| {
        router.files.lock().unwrap().info = owned_text(info);
        OK
    })
}

pub fn check_files() -> i32 {
    with_router(check_files_unlocked)
}
fn check_files_unlocked(router: &Router) -> i32 {
    let mut files = router.files.lock().unwrap();
    for output in &mut files.outputs {
        let result = output.check();
        if result.is_err() {
            return ERROR;
        }
    }
    OK
}

pub unsafe fn set_file(route: usize, path: *const c_char, redirect: bool) -> i32 {
    if route >= ROUTES || path.is_null() {
        return ERROR;
    }
    #[cfg(unix)]
    let path = {
        use std::os::unix::ffi::OsStrExt;
        PathBuf::from(std::ffi::OsStr::from_bytes(CStr::from_ptr(path).to_bytes()))
    };
    #[cfg(windows)]
    let path = match CStr::from_ptr(path).to_str() {
        Ok(p) => PathBuf::from(p),
        Err(_) => return ERROR,
    };
    with_router(|router| {
        let mut files = router.files.lock().unwrap();
        let result = files.outputs[route].flush();
        if result.is_err() {
            files.flush_failed = true;
            return ERROR;
        }
        match Output::new(path, route == 0 && redirect) {
            Ok(output) => {
                files.outputs[route] = output;
                OK
            }
            Err(_) => ERROR,
        }
    })
}

#[cfg(test)]
mod tests;
