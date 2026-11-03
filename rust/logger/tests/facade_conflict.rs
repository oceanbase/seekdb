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

//! A separate process is required because log's global receiver is set once.
use std::sync::atomic::{AtomicUsize, Ordering};

struct ExistingLogger;
static EXISTING: ExistingLogger = ExistingLogger;
static RECEIVED: AtomicUsize = AtomicUsize::new(0);

impl log::Log for ExistingLogger {
    fn enabled(&self, _: &log::Metadata<'_>) -> bool {
        true
    }
    fn log(&self, _: &log::Record<'_>) {
        RECEIVED.fetch_add(1, Ordering::Relaxed);
    }
    fn flush(&self) {}
}

#[test]
fn initialization_fails_without_replacing_an_existing_receiver() {
    log::set_logger(&EXISTING).unwrap();
    log::set_max_level(log::LevelFilter::Trace);
    assert_eq!(logger::start(), -1);
    assert_eq!(logger::option_value(logger::INITIALIZED, 0, 0), 0);
    log::info!("the pre-existing receiver remains installed");
    assert_eq!(RECEIVED.load(Ordering::Relaxed), 1);
    assert_eq!(logger::shutdown(), 0);
}
