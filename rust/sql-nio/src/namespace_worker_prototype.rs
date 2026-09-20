//! Throwaway V13 process transport. Bounded framed child pipes prove the real
//! SQL/storage split first. This synchronous adapter is not the planned Mio IPC
//! reactor and does not establish a high-concurrency performance claim.
use std::cell::Cell;
use std::ffi::c_void;
use std::io::{self, Read, Write};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{self, Receiver};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const MAX_FRAME: usize = 256 * 1024;
const MAX_SQL_MESSAGE: usize = 64 * 1024 * 1024;
const FRAGMENT_HEADER: usize = 1 + 1 + 8 + 8;
const FRAGMENT_CHUNK: usize = MAX_FRAME - FRAGMENT_HEADER;

thread_local! {
    static RESPONSE_DEADLINE: Cell<i64> = const { Cell::new(0) };
}

// The native request thread scopes this around namespace result delivery. No
// deadline/cache is attached to connections or to the common NIO reactor.
#[no_mangle]
pub extern "C" fn namespace_proto_response_deadline(deadline_us: i64) -> i64 {
    RESPONSE_DEADLINE.with(|deadline| deadline.replace(deadline_us))
}

pub(crate) fn response_expired() -> bool {
    RESPONSE_DEADLINE.with(|deadline| {
        let deadline = deadline.get();
        deadline > 0
            && SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .is_ok_and(|now| now.as_micros() >= deadline as u128)
    })
}

fn read_physical_frame(input: &mut impl Read) -> io::Result<Vec<u8>> {
    let mut header = [0; 8];
    input.read_exact(&mut header)?;
    let len = u32::from_le_bytes(header[4..].try_into().unwrap()) as usize;
    if header[..4] != *b"NS13" || len == 0 || len > MAX_SQL_MESSAGE {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "invalid worker frame",
        ));
    }
    let mut first = [0];
    input.read_exact(&mut first)?;
    if len > MAX_FRAME { return Err(io::Error::new(io::ErrorKind::InvalidData, "oversized physical frame")); }
    let mut payload = vec![0; len];
    payload[0] = first[0];
    input.read_exact(&mut payload[1..])?;
    Ok(payload)
}

fn read_frame(input: &mut impl Read) -> io::Result<Vec<u8>> {
    let first = read_physical_frame(input)?;
    if first[0] != b'f' { return Ok(first); }
    if first.len() < FRAGMENT_HEADER { return Err(io::Error::new(io::ErrorKind::InvalidData, "short fragment")); }
    let original = first[1];
    let total = u64::from_le_bytes(first[2..10].try_into().unwrap()) as usize;
    let mut offset = u64::from_le_bytes(first[10..18].try_into().unwrap()) as usize;
    if total == 0 || total > MAX_SQL_MESSAGE || offset != 0 || first.len() - FRAGMENT_HEADER > FRAGMENT_CHUNK {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "invalid fragment"));
    }
    let mut result = Vec::with_capacity(total);
    result.extend_from_slice(&first[FRAGMENT_HEADER..]);
    offset = result.len();
    while offset < total {
        let part = read_physical_frame(input)?;
        if part.len() < FRAGMENT_HEADER || part[0] != b'f' || part[1] != original
            || u64::from_le_bytes(part[2..10].try_into().unwrap()) as usize != total
            || u64::from_le_bytes(part[10..18].try_into().unwrap()) as usize != offset
            || part.len() - FRAGMENT_HEADER > FRAGMENT_CHUNK {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "invalid fragment sequence"));
        }
        result.extend_from_slice(&part[FRAGMENT_HEADER..]);
        offset = result.len();
    }
    if offset != total { return Err(io::Error::new(io::ErrorKind::InvalidData, "incomplete fragment")); }
    result[0] = original;
    Ok(result)
}

fn write_frame(output: &mut impl Write, payload: &[u8]) -> io::Result<()> {
    if payload.is_empty() || payload.len() > MAX_SQL_MESSAGE {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid worker frame size",
        ));
    }
    if payload.len() <= MAX_FRAME {
        let mut header = [0; 8]; header[..4].copy_from_slice(b"NS13");
        header[4..].copy_from_slice(&(payload.len() as u32).to_le_bytes());
        output.write_all(&header)?; output.write_all(payload)?;
    } else {
        let original = payload[0];
        for (offset, chunk) in payload.chunks(FRAGMENT_CHUNK).enumerate() {
            let offset = offset * FRAGMENT_CHUNK;
            let mut fragment = Vec::with_capacity(FRAGMENT_HEADER + chunk.len());
            fragment.push(b'f'); fragment.push(original);
            fragment.extend_from_slice(&(payload.len() as u64).to_le_bytes());
            fragment.extend_from_slice(&(offset as u64).to_le_bytes());
            fragment.extend_from_slice(chunk);
            let mut header = [0; 8]; header[..4].copy_from_slice(b"NS13");
            header[4..].copy_from_slice(&(fragment.len() as u32).to_le_bytes());
            output.write_all(&header)?; output.write_all(&fragment)?;
        }
    }
    output.flush()
}

struct Received {
    replies: Option<Receiver<io::Result<Vec<u8>>>>,
    last: Vec<u8>,
}
#[derive(Clone, Copy)]
struct Dispatch {
    callback: unsafe extern "C" fn(*mut c_void, *const u8, usize),
    context: usize,
}
impl Dispatch {
    fn deliver(self, frame: &io::Result<Vec<u8>>) {
        let (data, len) = match frame {
            Ok(bytes) => (bytes.as_ptr(), bytes.len()),
            Err(_) => (std::ptr::null(), 0),
        };
        unsafe { (self.callback)(self.context as *mut c_void, data, len) };
    }
}
struct Worker {
    child: Mutex<Child>,
    input: Mutex<ChildStdin>,
    received: Mutex<Received>,
    reader: Mutex<Option<JoinHandle<()>>>,
    dispatch: Arc<Mutex<Option<Dispatch>>>,
}

impl Worker {
    fn interrupt(&self) {
        // Kill before waiting for the receive lock: EOF wakes a blocked receive.
        let mut child = self.child.lock().unwrap();
        eprintln!("namespace worker interrupt pid={} state={:?}", child.id(), child.try_wait());
        let _ = child.kill();
        let _ = child.wait();
        drop(child);
        // Dropping the receiver also wakes a reader blocked on the bounded queue.
        self.received.lock().unwrap().replies.take();
    }
}
impl Worker {
    fn stop(&self) {
        self.interrupt();
        let reader = self.reader.lock().unwrap().take();
        if let Some(reader) = reader {
            let _ = reader.join();
        }
    }
}
impl Drop for Worker {
    fn drop(&mut self) {
        self.stop();
    }
}

fn spawn(namespace: u64, generation: u64) -> io::Result<Worker> {
    let instance_base = std::env::current_dir()?;
    let base = instance_base
        .join("run")
        .join(format!("namespace-worker-{namespace}-{generation}"));
    std::fs::create_dir_all(&base)?;
    let stderr = std::fs::File::create(base.join("process.out"))?;
    let mut command = Command::new(std::env::current_exe()?);
    command
        .arg("--namespace-sql-worker-prototype")
        .arg(format!("@{namespace}"))
        .current_dir(base)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(stderr);
    let wallet = instance_base.join("wallet");
    if wallet.is_dir() {
        command.env("SEEKDB_SQL_NIO_WALLET_DIR", wallet);
    }
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        let max_fd = unsafe { libc::sysconf(libc::_SC_OPEN_MAX) };
        if max_fd < 0 {
            return Err(io::Error::last_os_error());
        }
        // Engine C++ files are not all CLOEXEC. Touch only the child's copy:
        // stdio survives, all other descriptors close on successful exec. Keep
        // the spawn error pipe open until exec so launch failures still report.
        // The pre_exec closure performs only async-signal-safe fcntl operations.
        // ponytail: bounded POSIX fd sweep; optimize spawn after measuring it.
        unsafe {
            command.pre_exec(move || {
                for fd in 3..max_fd {
                    loop {
                        if libc::fcntl(fd as libc::c_int, libc::F_SETFD, libc::FD_CLOEXEC) != -1 {
                            break;
                        }
                        let error = io::Error::last_os_error();
                        match error.raw_os_error() {
                            Some(libc::EBADF) => break,
                            Some(libc::EINTR) => continue,
                            _ => return Err(error),
                        }
                    }
                }
                Ok(())
            });
        }
    }
    let mut child = command.spawn()?;
    let input = child.stdin.take().unwrap();
    let mut output = child.stdout.take().unwrap();
    let (tx, rx) = mpsc::sync_channel(1);
    let dispatch = Arc::new(Mutex::new(None::<Dispatch>));
    let reader_dispatch = Arc::clone(&dispatch);
    let reader = match thread::Builder::new()
        .name("ns-proto-read".into())
        .spawn(move || loop {
            let frame = read_frame(&mut output);
            let failed = frame.is_err();
            if let Err(error) = &frame {
                eprintln!("namespace worker pipe read failed: {error}");
            }
            let target = reader_dispatch.lock().unwrap();
            if let Some(callback) = *target {
                drop(target);
                callback.deliver(&frame);
            } else if tx.send(frame).is_err() {
                break;
            }
            if failed {
                break;
            }
        }) {
        Ok(reader) => reader,
        Err(err) => {
            let _ = child.kill();
            let _ = child.wait();
            return Err(err);
        }
    };
    Ok(Worker {
        child: Mutex::new(child),
        input: Mutex::new(input),
        received: Mutex::new(Received {
            replies: Some(rx),
            last: Vec::new(),
        }),
        reader: Mutex::new(Some(reader)),
        dispatch,
    })
}

// Opaque pointers below are process-local C ABI handles, never wire fields.
#[no_mangle]
pub unsafe extern "C" fn namespace_proto_spawn(
    namespace: u64,
    generation: u64,
    pid: *mut u32,
) -> *mut c_void {
    if pid.is_null() {
        return std::ptr::null_mut();
    }
    match spawn(namespace, generation) {
        Ok(worker) => {
            *pid = worker.child.lock().unwrap().id();
            Box::into_raw(Box::new(worker)).cast()
        }
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn namespace_proto_send(
    worker: *mut c_void,
    data: *const u8,
    len: usize,
) -> i32 {
    if worker.is_null() || data.is_null() || len == 0 || len > MAX_SQL_MESSAGE {
        return -1;
    }
    let worker = &*worker.cast::<Worker>();
    match write_frame(
        &mut *worker.input.lock().unwrap(),
        std::slice::from_raw_parts(data, len),
    ) {
        Ok(()) => 0,
        Err(_) => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn namespace_proto_receive(
    worker: *mut c_void,
    data: *mut *const u8,
    len: *mut usize,
    timeout_ms: u64,
) -> i32 {
    if worker.is_null() || data.is_null() || len.is_null() {
        return -1;
    }
    let worker = &*worker.cast::<Worker>();
    // Exactly one C++ dispatcher consumes this stream. Returned bytes survive
    // until its next receive; send/interrupt never modify the returned buffer.
    let mut received = worker.received.lock().unwrap();
    let reply = match received.replies.as_ref() {
        Some(rx) => rx.recv_timeout(Duration::from_millis(timeout_ms)),
        None => return -1,
    };
    match reply {
        Ok(Ok(frame)) => {
            received.last = frame;
            *data = received.last.as_ptr();
            *len = received.last.len();
            0
        }
        Err(mpsc::RecvTimeoutError::Timeout) => 1,
        _ => -1,
    }
}

// After Ready, the existing pipe reader directly invokes the bounded C++
// dispatcher. No second relay thread, and no per-connection transport thread.
#[no_mangle]
pub unsafe extern "C" fn namespace_proto_dispatch(
    worker: *mut c_void,
    callback: Option<unsafe extern "C" fn(*mut c_void, *const u8, usize)>,
    context: *mut c_void,
) -> i32 {
    let (Some(worker), Some(callback)) = (worker.cast::<Worker>().as_ref(), callback) else {
        return -1;
    };
    let callback = Dispatch {
        callback,
        context: context as usize,
    };
    let queued = {
        let mut target = worker.dispatch.lock().unwrap();
        if target.is_some() {
            return -1;
        }
        *target = Some(callback);
        let received = worker.received.lock().unwrap();
        received.replies.as_ref().and_then(|rx| rx.try_recv().ok())
    };
    if let Some(frame) = queued {
        callback.deliver(&frame);
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn namespace_proto_interrupt(worker: *mut c_void) {
    if !worker.is_null() {
        eprintln!("namespace_proto_interrupt");
        (&*worker.cast::<Worker>()).interrupt();
    }
}

#[no_mangle]
pub unsafe extern "C" fn namespace_proto_stop(worker: *mut c_void) {
    if !worker.is_null() {
        eprintln!("namespace_proto_stop");
        // Join callbacks through a shared borrow before creating exclusive Box
        // ownership. A callback may still be executing interrupt(&Worker).
        (&*worker.cast::<Worker>()).stop();
        drop(Box::from_raw(worker.cast::<Worker>()));
    }
}

#[no_mangle]
pub unsafe extern "C" fn namespace_proto_worker_read(
    callback: Option<unsafe extern "C" fn(*mut c_void, *const u8, usize)>,
    context: *mut c_void,
) -> i32 {
    let Some(callback) = callback else {
        return -1;
    };
    match read_frame(&mut io::stdin().lock()) {
        Ok(frame) => {
            callback(context, frame.as_ptr(), frame.len());
            0
        }
        _ => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn namespace_proto_worker_write(data: *const u8, len: usize) -> i32 {
    if data.is_null() || len == 0 || len > MAX_SQL_MESSAGE {
        return -1;
    }
    match write_frame(
        &mut io::stdout().lock(),
        std::slice::from_raw_parts(data, len),
    ) {
        Ok(()) => 0,
        Err(_) => -1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn large_sql_keeps_result_frames_bounded() {
        let mut sql = vec![b'x'; MAX_FRAME + 4096];
        sql[0] = b'I';
        let mut wire = Vec::new();
        write_frame(&mut wire, &sql).unwrap();
        assert_eq!(read_frame(&mut Cursor::new(&wire)).unwrap(), sql);
        sql[0] = b'r';
        let mut wire = Vec::new();
        write_frame(&mut wire, &sql).unwrap();
        assert_eq!(read_frame(&mut Cursor::new(&wire)).unwrap(), sql);
        let mut header = b"NS13".to_vec();
        header.extend_from_slice(&((MAX_SQL_MESSAGE + 1) as u32).to_le_bytes());
        assert!(read_frame(&mut Cursor::new(header)).is_err());
    }
}
