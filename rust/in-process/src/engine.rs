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

use std::env;
use std::fs;
use std::io::{self, Read, Write};
use std::os::unix::net::UnixStream;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Mutex, PoisonError};
use std::thread;
use std::time::Duration;

use crate::bridge::run_server;

const IDLE: u8 = 0;
const STARTING: u8 = 1;
const RUNNING: u8 = 2;
const FAILED: u8 = 3;
const OB_INVALID_ARGUMENT: i32 = -4002;
const ENGINE_STACK_SIZE: usize = 8 * 1024 * 1024;
const READINESS_ATTEMPTS: u32 = 1500;
const READINESS_INTERVAL: Duration = Duration::from_millis(200);
const STOPPED_DURING_STARTUP: &str =
    "the engine stopped during startup; see the log directory under the database directory";
const NOT_READY: &str = "the engine did not accept SQL connections within 300 seconds; \
                         see the log directory under the database directory";

pub(crate) enum StartError {
    InvalidArgument(&'static str),
    Internal(&'static str),
}

struct Engine {
    started: bool,
    directory: String,
}

static ENGINE: Mutex<Engine> = Mutex::new(Engine {
    started: false,
    directory: String::new(),
});
static STATE: AtomicU8 = AtomicU8::new(IDLE);
static FAILED_WITH_INVALID_ARGUMENT: AtomicBool = AtomicBool::new(false);

pub(crate) fn start(
    directory: Option<&str>,
    startup: Vec<String>,
    initial: Vec<String>,
) -> Result<String, StartError> {
    let Some(directory) = directory.filter(|directory| directory.starts_with('/')) else {
        return Err(StartError::InvalidArgument(
            "the directory must be an absolute path",
        ));
    };
    let mut engine = ENGINE.lock().unwrap_or_else(PoisonError::into_inner);
    let Some(canonical) = canonical_directory(directory) else {
        return Err(StartError::InvalidArgument("cannot resolve the directory"));
    };
    if engine.started && engine.directory != canonical {
        return Err(StartError::Internal(
            "another directory is already open in this process",
        ));
    }
    if engine.started && STATE.load(Ordering::Acquire) != RUNNING {
        return Err(StartError::Internal(
            "the engine is not running in this process; start a new process to open a database again",
        ));
    }
    if !engine.started {
        let thread_directory = canonical.clone();
        thread::Builder::new()
            .stack_size(ENGINE_STACK_SIZE)
            .spawn(move || engine_thread(&thread_directory, startup, initial))
            .map_err(|_| StartError::Internal("cannot start the engine thread"))?;
        engine.started = true;
        engine.directory = canonical;
        wait_until_ready(&engine.directory)?;
    }
    Ok(format!("{}/run/sql.sock", engine.directory))
}

fn engine_thread(directory: &str, startup: Vec<String>, initial: Vec<String>) {
    STATE.store(STARTING, Ordering::Release);
    if let Ok(parameters) = prepare_server(directory, startup, initial) {
        let result = run_server(directory, &parameters, report_running);
        FAILED_WITH_INVALID_ARGUMENT.store(result == OB_INVALID_ARGUMENT, Ordering::Release);
    }
    STATE.store(FAILED, Ordering::Release);
}

fn prepare_server(
    directory: &str,
    mut parameters: Vec<String>,
    initial: Vec<String>,
) -> io::Result<Vec<String>> {
    fs::create_dir_all(directory)?;
    env::set_current_dir(directory)?;
    for path in ["run", "etc", "log"] {
        fs::create_dir_all(path)?;
    }
    if first_init() {
        parameters.extend(initial);
    }
    Ok(parameters)
}

fn first_init() -> bool {
    file_io::read_dir("store/sstable")
        .map_or(true, |mut entries| !matches!(entries.next(), Some(Ok(_))))
}

fn report_running() {
    STATE.store(RUNNING, Ordering::Release);
}

fn wait_until_ready(directory: &str) -> Result<(), StartError> {
    for _ in 0..READINESS_ATTEMPTS {
        match STATE.load(Ordering::Acquire) {
            FAILED if FAILED_WITH_INVALID_ARGUMENT.load(Ordering::Acquire) => {
                return Err(StartError::InvalidArgument(STOPPED_DURING_STARTUP));
            }
            FAILED => return Err(StartError::Internal(STOPPED_DURING_STARTUP)),
            RUNNING if socket_ready(directory) => return Ok(()),
            _ => {}
        }
        thread::sleep(READINESS_INTERVAL);
    }
    Err(StartError::Internal(NOT_READY))
}

fn socket_ready(directory: &str) -> bool {
    static FAILED_ATTEMPTS: AtomicU32 = AtomicU32::new(0);
    let mut greeting = [0u8; 5];
    let mut received = 0;
    let result = read_greeting(&mut greeting, &mut received);
    let ready = result.is_ok() && greeting[3] == 0 && greeting[4] == 10;
    if !ready {
        let attempt = FAILED_ATTEMPTS.fetch_add(1, Ordering::Relaxed) + 1;
        if attempt % 50 == 1 {
            let reason = result.map_or_else(
                |error| error.to_string(),
                |()| "not a MySQL greeting".to_owned(),
            );
            eprintln!("SeekDB socket readiness: {received} bytes, {reason}");
            if let Ok(mut log) = fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(format!("{directory}/log/in_process.log"))
            {
                let _ = writeln!(
                    log,
                    "socket readiness attempt {attempt}: {received} bytes, {reason}"
                );
            }
        }
    }
    ready
}

fn read_greeting(greeting: &mut [u8; 5], received: &mut usize) -> io::Result<()> {
    let mut stream = UnixStream::connect("run/sql.sock")?;
    stream.set_read_timeout(Some(Duration::from_secs(1)))?;
    while *received < greeting.len() {
        match stream.read(&mut greeting[*received..])? {
            0 => return Err(io::ErrorKind::UnexpectedEof.into()),
            count => *received += count,
        }
    }
    Ok(())
}

fn canonical_directory(directory: &str) -> Option<String> {
    let mut parent = directory.to_owned();
    let mut suffix = Vec::new();
    let resolved = loop {
        if let Ok(path) = fs::canonicalize(&parent) {
            break path.into_os_string().into_string().ok()?;
        }
        let slash = parent.rfind('/')?;
        if parent == "/" {
            return None;
        }
        suffix.push(parent[slash + 1..].to_owned());
        parent = if slash == 0 {
            "/".to_owned()
        } else {
            parent[..slash].to_owned()
        };
    };
    let mut result = resolved;
    for part in suffix.iter().rev() {
        match part.as_str() {
            "" | "." => {}
            ".." => {
                let slash = result.rfind('/').unwrap_or(0);
                result = if slash == 0 {
                    "/".to_owned()
                } else {
                    result[..slash].to_owned()
                };
            }
            _ => {
                if result != "/" {
                    result.push('/');
                }
                result.push_str(part);
            }
        }
    }
    Some(result)
}
