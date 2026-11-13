//! Independent diagnostic branch only: copy selected complete records before async admission.
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::{Mutex, OnceLock};

const LIMIT: u64 = 256 * 1024 * 1024;
static SINK: OnceLock<Mutex<Option<Capture>>> = OnceLock::new();

struct Capture {
    file: Option<File>,
    path: PathBuf,
    seen: u64,
    written: u64,
    bytes: u64,
    failed: bool,
}

fn sink() -> &'static Mutex<Option<Capture>> {
    SINK.get_or_init(|| Mutex::new(std::env::var_os("SEEK1402_DIAGNOSTIC_FILE").map(|path| {
        let path = PathBuf::from(path);
        let file = OpenOptions::new().write(true).create_new(true).open(&path).ok();
        Capture { failed: file.is_none(), file, path, seen: 0, written: 0, bytes: 0 }
    })))
}

pub fn copy(bytes: &[u8]) {
    if !bytes.windows(b"SEEK1402_DIAGNOSTIC".len()).any(|s| s == b"SEEK1402_DIAGNOSTIC") {
        return;
    }
    let mut guard = sink().lock().unwrap_or_else(|e| e.into_inner());
    let Some(capture) = guard.as_mut() else { return; };
    capture.seen += 1;
    if capture.failed || capture.bytes + bytes.len() as u64 > LIMIT {
        capture.failed = true;
        return;
    }
    match capture.file.as_mut().unwrap().write_all(bytes) {
        Ok(()) => {
            capture.bytes += bytes.len() as u64;
            capture.written += 1;
        }
        Err(_) => capture.failed = true,
    }
}

// The controller must stop the workload and request logger flush before sealing this snapshot.
// A missing/stale snapshot, failed=true, or seen!=written invalidates capture completeness.
pub fn snapshot() {
    let sink = sink();
    let mut guard = sink.lock().unwrap_or_else(|e| e.into_inner());
    let Some(capture) = guard.as_mut() else { return; };
    if let Some(file) = capture.file.as_mut() {
        if file.sync_data().is_err() { capture.failed = true; }
    }
    let body = format!(
        "{{\"seen\":{},\"written\":{},\"bytes\":{},\"failed\":{},\"limit\":{}}}\n",
        capture.seen, capture.written, capture.bytes, capture.failed, LIMIT
    );
    let status = capture.path.with_extension("status.json");
    let temporary = capture.path.with_extension("status.tmp");
    if fs::write(&temporary, body).and_then(|_| fs::rename(&temporary, &status)).is_err() {
        capture.failed = true;
    }
}
