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

use super::*;
use std::fs;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::time::{Duration, Instant};

struct TempDir(PathBuf);
impl TempDir {
    fn new() -> Self {
        static NEXT: AtomicUsize = AtomicUsize::new(0);
        let path = std::env::temp_dir().join(format!(
            "logger-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }
    fn path(&self, route: usize) -> PathBuf {
        self.0.join(format!("route{route}.log"))
    }
    fn backend(&self, capacity: usize, size: u64, count: u64) -> Backend {
        let outputs = std::array::from_fn(|i| Output::new(self.path(i), false).unwrap());
        Backend::from_router(make_router(outputs, Limits { size, count }, None), capacity).unwrap()
    }
}
impl Drop for TempDir {
    fn drop(&mut self) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = fs::set_permissions(&self.0, fs::Permissions::from_mode(0o755));
        }
        fs::remove_dir_all(&self.0).unwrap();
    }
}

// Use the same file setup and startup path as the C++ adapter.
fn start_files(paths: &[std::ffi::CString]) {
    for (route, path) in paths.iter().enumerate() {
        assert_eq!(unsafe { set_file(route, path.as_ptr(), false) }, OK);
    }
    file_limit(0, 0, 1);
    file_limit(1, 0, 1);
    assert_eq!(start(), OK);
}

#[test]
fn cpp_bytes_survive_all_routes_and_both_modes() {
    let dir = TempDir::new();
    let backend = dir.backend(CAPACITY, 0, 0);
    let binary: Vec<u8> = (0..=255).chain(b"\0tail\n".iter().copied()).collect();
    for route in 0..ROUTES {
        assert_eq!(
            backend.submit(route, "[header] normal utf8 中文\n".as_bytes(), false),
            OK
        );
        assert_eq!(backend.submit(route, &binary, true), OK);
        assert_eq!(backend.submit(route, &[b'x'; 65536], true), OK);
    }
    assert_eq!(backend.flush(), OK);
    for route in 0..ROUTES {
        let mut expected = "[header] normal utf8 中文\n".as_bytes().to_vec();
        expected.extend(&binary);
        expected.extend(&[b'x'; 65536]);
        assert_eq!(fs::read(dir.path(route)).unwrap(), expected);
    }
}

#[test]
fn full_queue_drops_without_waiting_and_preserves_accepted_records() {
    let dir = TempDir::new();
    let backend = dir.backend(1, 0, 0);
    // Stop disk processing while filling the public spdlog async queue.
    let guard = backend.router.files.lock().unwrap();
    let start = Instant::now();
    let results: Vec<_> = (0..1000)
        .map(|i| backend.submit(0, format!("record-{i}\n").as_bytes(), true))
        .collect();
    assert!(start.elapsed() < Duration::from_secs(2));
    let accepted = results.iter().filter(|&&r| r == OK).count() as u64;
    assert!((1..=2).contains(&accepted)); // one queued, at most one in the worker
    assert!(results.iter().all(|&r| r == OK || r == FULL));
    drop(guard);
    assert_eq!(backend.flush(), OK);
    let expected: String = results
        .iter()
        .enumerate()
        .filter(|(_, result)| **result == OK)
        .map(|(i, _)| format!("record-{i}\n"))
        .collect();
    assert_eq!(fs::read_to_string(dir.path(0)).unwrap(), expected);
}

#[test]
fn concurrent_flushes_and_producers_do_not_confuse_barriers() {
    let dir = TempDir::new();
    let backend = dir.backend(CAPACITY, 0, 0);
    let accepted = AtomicU64::new(0);
    std::thread::scope(|scope| {
        for route in 0..ROUTES {
            let backend = &backend;
            let accepted = &accepted;
            scope.spawn(move || {
                for _ in 0..1000 {
                    if backend.submit(route, b"record\n", true) == OK {
                        accepted.fetch_add(1, Ordering::Relaxed);
                    }
                }
            });
        }
        for _ in 0..4 {
            let backend = &backend;
            scope.spawn(move || {
                for _ in 0..10 {
                    assert_eq!(backend.flush(), OK);
                }
            });
        }
    });
    assert_eq!(backend.flush(), OK);
    let mut written = 0;
    for route in 0..ROUTES {
        let bytes = fs::read(dir.path(route)).unwrap();
        assert!(bytes.chunks(7).all(|record| record == b"record\n"));
        written += bytes.len() as u64 / 7;
    }
    assert_eq!(written, accepted.load(Ordering::Relaxed));
}

#[test]
fn drop_drains_the_queue_and_releases_files() {
    let dir = TempDir::new();
    let backend = dir.backend(CAPACITY, 0, 0);
    for _ in 0..1000 {
        assert_eq!(backend.submit(0, b"record\n", true), OK);
    }
    drop(backend);
    assert_eq!(fs::read(dir.path(0)).unwrap(), b"record\n".repeat(1000));
    // A fresh backend is independent and appends to the same files.
    let second = dir.backend(1, 0, 0);
    assert_eq!(second.submit(0, b"second\n", true), OK);
    drop(second);
    assert_eq!(fs::read(dir.path(0)).unwrap().len(), 7007);
}

#[test]
fn low_rate_tail_flushes_without_explicit_flush() {
    let dir = TempDir::new();
    let backend = dir.backend(CAPACITY, 0, 0);
    assert_eq!(backend.submit(0, b"only-record\n", true), OK);
    let deadline = Instant::now() + Duration::from_secs(2);
    while fs::read(dir.path(0)).unwrap() != b"only-record\n" {
        assert!(
            Instant::now() < deadline,
            "tail waited for another log or explicit flush"
        );
        std::thread::sleep(Duration::from_millis(5));
    }
}

#[cfg(unix)]
#[test]
fn periodic_flush_preserves_explicit_barriers_and_io_failures() {
    let dir = TempDir::new();
    let outputs = std::array::from_fn(|i| {
        Output::new(
            if i == 0 {
                PathBuf::from("/dev/full")
            } else {
                dir.path(i)
            },
            false,
        )
        .unwrap()
    });
    let backend = Backend::from_router(
        make_router(outputs, Limits { size: 0, count: 0 }, None),
        CAPACITY,
    )
    .unwrap();
    backend.periodic.set_flush_period(None);
    assert_eq!(backend.submit(0, b"async\n", true), OK);
    // Invoke the same sink operation as the library timer after its record.
    let deadline = Instant::now() + Duration::from_secs(2);
    while !backend.router.files.lock().unwrap().outputs[0].buffered() {
        assert!(
            Instant::now() < deadline,
            "worker did not consume the record"
        );
        std::thread::yield_now();
    }
    assert!(backend.router.flush().is_err());
    assert_eq!(backend.router.completed_flushes.lock().unwrap().0, 0);
    assert_eq!(backend.flush(), ERROR);
    assert_eq!(backend.flush(), OK);
}

#[test]
fn buffered_bytes_preserve_sync_order_and_external_replacement() {
    let dir = TempDir::new();
    let mut output = Output::new(dir.path(0), false).unwrap();
    let limits = Limits { size: 0, count: 0 };
    output
        .write(b"async-before\n", &limits, None, true)
        .unwrap();
    assert!(fs::read(dir.path(0)).unwrap().is_empty());
    output.write(b"sync-after\n", &limits, None, false).unwrap();
    assert_eq!(
        fs::read(dir.path(0)).unwrap(),
        b"async-before\nsync-after\n"
    );
    output.write(b"old-tail\n", &limits, None, true).unwrap();
    let moved = dir.0.join("moved.log");
    fs::rename(dir.path(0), &moved).unwrap();
    fs::write(dir.path(0), b"new-head\n").unwrap();
    output.check().unwrap();
    output.write(b"new-tail\n", &limits, None, true).unwrap();
    output.flush().unwrap();
    assert_eq!(
        fs::read(moved).unwrap(),
        b"async-before\nsync-after\nold-tail\n"
    );
    assert_eq!(fs::read(dir.path(0)).unwrap(), b"new-head\nnew-tail\n");
}

#[test]
fn rotation_names_retention_and_external_replacement() {
    let dir = TempDir::new();
    let backend = dir.backend(CAPACITY, 12, 3);
    for _ in 0..40 {
        assert_eq!(backend.submit(0, b"abcdef\n", true), OK);
    }
    assert_eq!(backend.flush(), OK);
    let files: Vec<_> = fs::read_dir(&dir.0)
        .unwrap()
        .map(|e| e.unwrap().file_name())
        .collect();
    let archive: Vec<_> = files
        .iter()
        .filter_map(|s| s.to_str())
        .filter(|s| s.starts_with("route0.log."))
        .collect();
    assert_eq!(archive.len(), 2);
    assert!(archive
        .iter()
        .all(|name| name.len() == "route0.log.".len() + 17));
    for name in &archive {
        assert_eq!(fs::read(dir.0.join(name)).unwrap(), b"abcdef\nabcdef\n");
    }
    assert!(fs::read(dir.path(0)).unwrap().is_empty());

    let removed = dir.0.join("moved.log");
    fs::rename(dir.path(0), &removed).unwrap();
    {
        let mut files = backend.router.files.lock().unwrap();
        files.limits.size = 0;
        files.outputs[0].check().unwrap();
    }
    assert_eq!(backend.submit(0, b"after-move\n", false), OK);
    assert_eq!(fs::read(dir.path(0)).unwrap(), b"after-move\n");
    fs::remove_file(dir.path(0)).unwrap();
    backend.router.files.lock().unwrap().outputs[0]
        .check()
        .unwrap();
    assert_eq!(backend.submit(0, b"after-delete\n", false), OK);
    assert_eq!(fs::read(dir.path(0)).unwrap(), b"after-delete\n");
}

#[cfg(unix)]
#[test]
fn io_errors_are_returned_without_recursive_logging() {
    let dir = TempDir::new();
    let outputs = std::array::from_fn(|i| {
        Output::new(
            if i == 0 {
                PathBuf::from("/dev/full")
            } else {
                dir.path(i)
            },
            false,
        )
        .unwrap()
    });
    let backend = Backend::from_router(
        make_router(outputs, Limits { size: 0, count: 0 }, None),
        CAPACITY,
    )
    .unwrap();
    assert_eq!(backend.submit(0, b"sync\n", false), ERROR);
    assert_eq!(backend.submit(0, b"async\n", true), OK);
    assert_eq!(backend.flush(), ERROR);
    // A failed batch is discarded. A second barrier completes without
    // retrying it or waiting forever.
    assert_eq!(backend.flush(), OK);
}

#[cfg(unix)]
#[test]
fn failed_rotation_keeps_written_bytes_and_recovers() {
    use std::os::unix::fs::PermissionsExt;
    if unsafe { libc::geteuid() } == 0 {
        return;
    }
    let dir = TempDir::new();
    let backend = dir.backend(CAPACITY, 1, 3);
    fs::set_permissions(&dir.0, fs::Permissions::from_mode(0o555)).unwrap();
    assert_eq!(backend.submit(0, b"still-written\n", true), OK);
    assert_eq!(backend.flush(), OK);
    assert_eq!(fs::read(dir.path(0)).unwrap(), b"still-written\n");
    fs::set_permissions(&dir.0, fs::Permissions::from_mode(0o755)).unwrap();
    assert_eq!(backend.submit(0, b"recovered\n", false), OK);
    let archives: Vec<_> = fs::read_dir(&dir.0)
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .filter(|path| {
            path.file_name()
                .unwrap()
                .to_string_lossy()
                .starts_with("route0.log.")
        })
        .collect();
    assert_eq!(archives.len(), 1);
    assert_eq!(
        fs::read(&archives[0]).unwrap(),
        b"still-written\nrecovered\n"
    );
    assert!(fs::read(dir.path(0)).unwrap().is_empty());
}

#[test]
fn lifecycle_validation_and_concurrent_shutdown() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    let dir = TempDir::new();
    let paths: Vec<_> = (0..ROUTES)
        .map(|i| std::ffi::CString::new(dir.path(i).to_str().unwrap()).unwrap())
        .collect();
    unsafe {
        assert_eq!(set_file(0, std::ptr::null(), false), ERROR);
        assert_eq!(set_file(ROUTES, paths[0].as_ptr(), false), ERROR);
        assert_eq!(submit_complete(0, b"x", true), NOT_INIT);
        start_files(&paths);
        assert_eq!(start(), ERROR);
        for _ in 0..100 {
            assert_eq!(submit_complete(0, b"line\n", true), OK);
        }
        assert_eq!(set_instance_level(2, 0), OK);
        log::info!("native rows={}", 42);
        assert_eq!(set_instance_level(0, 0), OK);
        let evaluated = Cell::new(false);
        if log::log_enabled!(log::Level::Info) {
            log::info!("hidden={}", {
                evaluated.set(true);
                1
            });
        }
        assert!(!evaluated.get());
        assert_eq!(set_instance_level(5, 0), OK);
        assert!(log::log_enabled!(log::Level::Trace));
        assert!(!log::log_enabled!(log::Level::Debug));
        assert_eq!(set_instance_level(2, 0), OK);
        assert_eq!(shutdown(), OK);
        assert_eq!(shutdown(), OK);
        assert_eq!(submit_complete(0, b"x", true), NOT_INIT);
        let content = fs::read_to_string(dir.path(0)).unwrap();
        assert_eq!(content.matches("line\n").count(), 100);
        assert!(content.contains("native rows=42"));
        start_files(&paths);
        assert_eq!(submit_complete(0, b"reinit\n", true), OK);
        assert_eq!(flush(), OK);
        let accepted = AtomicU64::new(0);
        let barrier = std::sync::Barrier::new(9);
        std::thread::scope(|scope| {
            for _ in 0..8 {
                let accepted = &accepted;
                let barrier = &barrier;
                scope.spawn(move || {
                    barrier.wait();
                    for _ in 0..1000 {
                        let result = submit_complete(0, b"race\n", true);
                        assert!(matches!(result, OK | FULL | NOT_INIT));
                        if result == OK {
                            accepted.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                });
            }
            barrier.wait();
            std::thread::sleep(Duration::from_millis(1));
            assert_eq!(shutdown(), OK);
        });
        assert_eq!(
            fs::read_to_string(dir.path(0))
                .unwrap()
                .matches("race\n")
                .count() as u64,
            accepted.load(Ordering::Relaxed)
        );
        struct RecursiveFormatting;
        impl std::fmt::Display for RecursiveFormatting {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                log::info!("recursive-native-must-be-suppressed");
                f.write_str("outer-native")
            }
        }
        start_files(&paths);
        log::info!("{}", RecursiveFormatting);
        assert_eq!(shutdown(), OK);
        let text = fs::read_to_string(dir.path(0)).unwrap();
        assert!(text.contains("outer-native"));
        assert!(!text.contains("recursive-native-must-be-suppressed"));
        struct StopsDuringFormatting;
        impl std::fmt::Display for StopsDuringFormatting {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                assert_eq!(shutdown(), OK);
                f.write_str("uncommitted-native-after-shutdown")
            }
        }
        start_files(&paths);
        log::info!("{}", StopsDuringFormatting);
        assert_eq!(flush(), NOT_INIT);
        assert!(!fs::read_to_string(dir.path(0))
            .unwrap()
            .contains("uncommitted-native-after-shutdown"));
    }
}

static FFI_TEST_GATE: Mutex<()> = Mutex::new(());

#[cfg(unix)]
#[test]
fn shutdown_reports_drain_failures_without_replaying_past_errors() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    let dir = TempDir::new();
    let paths: Vec<_> = (0..ROUTES)
        .map(|i| {
            std::ffi::CString::new(if i == 0 {
                "/dev/full".to_owned()
            } else {
                dir.path(i).to_str().unwrap().to_owned()
            })
            .unwrap()
        })
        .collect();
    unsafe {
        start_files(&paths);
        // This synchronous error has already been returned to its caller.
        assert_eq!(submit_complete(0, b"sync\n", false), ERROR);
        assert_eq!(shutdown(), OK);

        start_files(&paths);
        with_backend(|backend| {
            backend.periodic.set_flush_period(None);
            assert_eq!(backend.submit(0, b"past-async\n", true), OK);
            let deadline = Instant::now() + Duration::from_secs(2);
            while !backend.router.files.lock().unwrap().outputs[0].buffered() {
                assert!(
                    Instant::now() < deadline,
                    "worker did not consume the record"
                );
                std::thread::yield_now();
            }
            // Simulate a timer flush failure with no explicit caller to
            // receive it. There is no remaining data when shutdown starts.
            assert!(backend.router.flush().is_err());
            OK
        });
        assert_eq!(shutdown(), OK);

        start_files(&paths);
        with_backend(|backend| {
            backend.periodic.set_flush_period(None);
            OK
        });
        // An accepted asynchronous record still fails during drain.
        assert_eq!(submit_complete(0, b"async\n", true), OK);
        assert_eq!(shutdown(), ERROR);
        assert_eq!(shutdown(), OK);

        start_files(&paths);
        assert_eq!(submit_complete(0, b"reported\n", true), OK);
        assert_eq!(flush(), ERROR);
        let recovered = std::ffi::CString::new(dir.path(0).to_str().unwrap()).unwrap();
        assert_eq!(set_file(0, recovered.as_ptr(), false), OK);
        assert_eq!(submit_complete(0, b"recovered\n", true), OK);
        assert_eq!(shutdown(), OK);
        assert_eq!(fs::read(dir.path(0)).unwrap(), b"recovered\n");
    }
}

#[test]
fn ecosystem_logs_share_session_modules_trace_and_lifecycle() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    let dir = TempDir::new();
    let paths: Vec<_> = (0..ROUTES)
        .map(|i| std::ffi::CString::new(dir.path(i).to_str().unwrap()).unwrap())
        .collect();
    unsafe {
        option_value(policy::PERF, 0, 1);
        option_value(policy::IGNORE_REQUEST_LEVEL, 0, 1);
        context_map(std::ptr::null());
        start_files(&paths);
        option_value(policy::ASYNC, 0, 1);
        assert_eq!(set_instance_level(2, 0), OK);
        assert_eq!(register_module(8, -1, c"SQL".as_ptr()), OK);
        assert_eq!(register_module(8, 3, c"ENG".as_ptr()), OK);
        let mut session = std::ptr::null_mut();
        assert_eq!(map_set(&mut session, -1, -1, 0), OK);
        assert_eq!(map_set(&mut session, 8, 3, 6), OK);
        context_map(session);
        map_free(session); // active Rust snapshot must remain valid
        set_trace_id([1_u64, 2, 3, 4].as_ptr());
        assert!(log::log_enabled!(target: "SQL.ENG", log::Level::Debug));
        assert!(!log::log_enabled!(target: "SQL", log::Level::Debug));
        log::debug!(target: "SQL.ENG", "session-child-enabled");
        struct MustNotFormat;
        impl std::fmt::Display for MustNotFormat {
            fn fmt(&self, _: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                panic!("a rejected Rust log must not format its body")
            }
        }
        log::info!(target: "SQL", "{MustNotFormat}");
        log::debug!(target: "third_party::net", "{MustNotFormat}");
        let evaluated = Cell::new(false);
        if log::log_enabled!(target: "SQL", log::Level::Debug) {
            log::debug!(target: "SQL", "{}", {
                evaluated.set(true);
                1
            });
        }
        assert!(!evaluated.get());
        std::thread::spawn(|| {
            assert!(!log::log_enabled!(target: "SQL.ENG", log::Level::Debug));
            log::debug!(target: "SQL.ENG", "other-thread-must-be-filtered");
        })
        .join()
        .unwrap();
        context_map(std::ptr::null());
        assert!(!log::log_enabled!(target: "SQL.ENG", log::Level::Debug));
        log::debug!(target: "SQL.ENG", "after-session-must-be-filtered");
        set_trace_id(std::ptr::null());

        // Exercise a real dependency's log call, without modifying its source.
        assert_eq!(set_instance_level(6, 0), OK);
        let poll = mio::Poll::new().unwrap();
        let mut listener = mio::net::TcpListener::bind("127.0.0.1:0".parse().unwrap()).unwrap();
        poll.registry()
            .register(&mut listener, mio::Token(17), mio::Interest::READABLE)
            .unwrap();
        assert_eq!(set_instance_level(0, 0), OK);
        poll.registry()
            .reregister(&mut listener, mio::Token(18), mio::Interest::READABLE)
            .unwrap();
        log::logger().flush();
        let text = fs::read_to_string(dir.path(0)).unwrap();
        assert!(text.contains("[SQL.ENG]") && text.contains("session-child-enabled"));
        assert!(text.contains("Y1-0000000000000002-3-4"));
        assert!(text.contains("[mio::poll]") && text.contains("registering event source"));
        assert!(!text.contains("reregistering event source"));
        assert!(!text.contains("must-be-filtered"));
        assert_eq!(shutdown(), OK);
        log::info!("{MustNotFormat}");
        start_files(&paths);
        assert_eq!(set_instance_level(2, 0), OK);
        log::info!(target: "foreign::component", "facade-after-reinit");
        assert_eq!(shutdown(), OK);
        assert!(fs::read_to_string(dir.path(0))
            .unwrap()
            .contains("facade-after-reinit"));
        option_value(policy::ASYNC, 1, 1);
    }
}

#[test]
fn core_policy_maps_are_owned_and_restore_request_overrides() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    unsafe {
        option_value(policy::INFO_AS_WDIAG, 1, 1);
        assert_eq!(register_module(8, -1, c"SQL".as_ptr()), OK);
        assert_eq!(register_module(8, 3, c"ENG".as_ptr()), OK);
        assert_eq!(
            parse_settings(
                c"ALL.*:ERROR, SQL.ENG:DEBUG".as_ptr(),
                26,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                0,
                0
            ),
            OK
        );
        assert_eq!(get_level(-1, -1, 0), 0);
        assert_eq!(get_level(8, 3, 0), 6);
        assert_eq!(get_level(8, 2, 0), 0);
        let mut map = std::ptr::null_mut();
        assert_eq!(map_set(&mut map, -1, -1, 4), OK);
        assert_eq!(map_set(&mut map, 8, 3, 5), OK);
        let copy = map_clone(map);
        context_map(map);
        map_free(map);
        assert_eq!(get_level(8, 3, 1), 5); // retained after wrapper death
        assert_eq!(get_level(8, 2, 1), 4);
        let mut changed = copy;
        map_set(&mut changed, 8, 3, 2);
        assert_eq!(get_level(8, 3, 1), 5); // immutable prior snapshot
        context_map(changed);
        assert_eq!(get_level(8, 3, 1), 2);
        map_set(&mut changed, 8, 3, 6);
        assert_eq!(get_level(8, 3, 1), 6); // mutation of active handle
        context_map(std::ptr::null());
        context_value(0, 7, 1);
        map_free(changed);
        assert_eq!(get_level(8, 3, 1), 6);
        context_value(0, 1, 1);
        assert_eq!(get_level(8, 3, 1), 1);
        assert_eq!(std::thread::spawn(|| get_level(8, 3, 1)).join().unwrap(), 6);
        context_value(0, 7, 1);
        assert_ne!(
            parse_settings(
                c"ALL.*:DEBUG, UNKNOWN.*:INFO".as_ptr(),
                27,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                0,
                0
            ),
            OK
        );
        assert_eq!(get_level(-1, -1, 0), 0); // no partial application
        assert_eq!(
            parse_settings(
                c"PERF".as_ptr(),
                4,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                0,
                0
            ),
            OK
        );
        assert_eq!(current_level(8, 3), 0);
        assert_eq!(
            parse_settings(
                c"INFO".as_ptr(),
                4,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                0,
                0
            ),
            OK
        );
        assert_eq!(get_level(-1, -1, 1), 4);
        set_instance_level(2, 0);
    }
}

#[test]
fn trace_words_fields_and_alert_sampling_share_rust_context() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    unsafe {
        let words = [0x1234_u64, 55, 0xab, 0xcd];
        set_trace_id(words.as_ptr());
        assert_eq!(*trace_id().add(1), 55);
        trace_field(0, 1);
        trace_field(1, 0x23);
        trace_field(2, 0x142);
        assert_eq!(*trace_id(), 0x4231_0000_0000_1234);
        assert_eq!(dba_enabled(0, 0), 1);
        for _ in 2..20 {
            assert_eq!(dba_enabled(0, 0), 0);
        }
        assert_eq!(dba_enabled(0, 0), -20);
        assert_eq!(dba_enabled(0, 1), 1);
        std::thread::spawn(|| assert_eq!(*trace_id(), 0))
            .join()
            .unwrap();
        set_trace_id(std::ptr::null());
        assert_eq!(dba_enabled(0, 0), 1);
    }
}

#[test]
fn complete_records_keep_text_binary_growth_cancel_and_trace_flush() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    let dir = TempDir::new();
    unsafe {
        shutdown();
        let path = std::ffi::CString::new(dir.path(0).to_str().unwrap()).unwrap();
        assert_eq!(set_file(0, path.as_ptr(), false), OK);
        assert_eq!(start(), OK);
        option_value(policy::ASYNC, 0, 1);
        set_trace_id([1_u64, 2, 0xab, 0xcd].as_ptr());
        context_value(4, 9, 1);
        let meta = record::Metadata {
            module: c"[SQL]".as_ptr(),
            file: c"/src/query.cpp".as_ptr(),
            function: c"query".as_ptr(),
            event: std::ptr::null(),
            thread_name: c"worker".as_ptr(),
            timestamp_us: 946684800123456,
            thread_id: 17,
            line: 42,
            level: 4,
            error: -4002,
            with_head: 1,
        };
        let mut buffer = record::Buffer::default();
        assert_eq!(record::begin(&meta, &mut buffer), OK);
        let mut recursive = record::Buffer::default();
        assert_eq!(record::begin(&meta, &mut recursive), 1);
        let body = b"body\0\xff";
        std::ptr::copy_nonoverlapping(body.as_ptr(), buffer.data.cast(), body.len());
        assert_eq!(
            record::commit(&mut buffer, body.len(), std::ptr::null()),
            OK
        );
        assert!(buffer.record.is_null());
        let date = chrono::DateTime::from_timestamp(946684800, 123456000)
            .unwrap()
            .with_timezone(&chrono::Local);
        let mut expected = format!("[{}] WDIAG [SQL]query (query.cpp:42) [17][worker][Y1-0000000000000002-ab-cd] [lt=9][errcode=-4002] ", date.format("%Y-%m-%d %H:%M:%S%.6f")).into_bytes();
        expected.extend_from_slice(body);
        expected.push(b'\n');
        assert_eq!(fs::read(dir.path(0)).unwrap(), expected); // sync visibility
        assert_eq!(record::begin(&meta, &mut buffer), OK);
        record::cancel(&mut buffer);
        assert_eq!(context_value(1, 0, 0), 0);
        assert_eq!(record::begin(&meta, &mut buffer), OK);
        let small = buffer.capacity;
        assert!(small < 2048);
        assert_eq!(record::grow(&mut buffer), OK);
        assert!(buffer.capacity > 60 * 1024);
        std::ptr::write_bytes(buffer.data.cast::<u8>(), b'x', buffer.capacity);
        assert_eq!(
            record::commit(&mut buffer, buffer.capacity, std::ptr::null()),
            OK
        );
        assert!(fs::read(dir.path(0)).unwrap().len() < expected.len() + 65536);
        let diagnostic = record::Metadata { level: 3, ..meta };
        let bt = std::ffi::CString::new("b".repeat(4096)).unwrap();
        assert_eq!(record::begin(&diagnostic, &mut buffer), OK);
        *buffer.data = b'd' as c_char;
        assert_eq!(record::commit(&mut buffer, 1, bt.as_ptr()), OK);
        assert!(fs::read(dir.path(0))
            .unwrap()
            .ends_with(&(" BACKTRACE:".to_owned() + &"b".repeat(4096) + "\n").into_bytes()));
        context_value(3, 1, 1);
        let before = fs::read(dir.path(0)).unwrap();
        assert_eq!(record::begin(&meta, &mut buffer), OK);
        *buffer.data = b't' as c_char;
        record::commit(&mut buffer, 1, std::ptr::null());
        assert_eq!(fs::read(dir.path(0)).unwrap(), before);
        assert_eq!(record::trace_flush(meta.module, meta.level), OK);
        assert!(fs::read(dir.path(0)).unwrap().ends_with(b"t\n"));
        context_value(3, 0, 1);
        context_value(4, 0, 1);
        set_trace_id(std::ptr::null());
        assert_eq!(shutdown(), OK);
        assert_eq!(record::begin(&meta, &mut buffer), NOT_INIT);
        option_value(policy::ASYNC, 1, 1);
    }
}

#[test]
fn foreign_tls_destructor_logging_is_suppressed_without_panicking() {
    let _serial = FFI_TEST_GATE.lock().unwrap();
    option_value(policy::STOP_APPEND, 0, 1);
    struct LateDrop;
    impl Drop for LateDrop {
        fn drop(&mut self) {
            assert_eq!(context_value(1, 0, 0), 1);
            assert_eq!(context_value(2, 0, 0), 1);
        }
    }
    thread_local! { static LATE: LateDrop = const { LateDrop }; }
    std::thread::spawn(|| {
        LATE.with(|_| ());
        context_value(0, 7, 1);
    })
    .join()
    .unwrap();
    option_value(policy::STOP_APPEND, 1, 1);
}
