// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
//! Bounded, nonblocking in-process byte streams for the existing NIO reactor.
//! Notifications use a condition variable; no OS socket or browser proxy is used.
use crate::*;
use std::collections::VecDeque;
use std::io::{self, ErrorKind};
use std::ops::BitOr;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub(crate) struct Token(pub usize);

#[derive(Clone, Copy)]
pub(crate) struct Interest(u8);
impl Interest {
    pub(crate) const READABLE: Self = Self(1);
    pub(crate) const WRITABLE: Self = Self(2);
}
impl BitOr for Interest {
    type Output = Self;
    fn bitor(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }
}

pub(crate) struct Event {
    token: Token,
    readable: bool,
    writable: bool,
    closed: bool,
}
impl Event {
    pub(crate) fn token(&self) -> Token {
        self.token
    }
    pub(crate) fn is_readable(&self) -> bool {
        self.readable
    }
    pub(crate) fn is_writable(&self) -> bool {
        self.writable
    }
    pub(crate) fn is_read_closed(&self) -> bool {
        self.closed
    }
    pub(crate) fn is_write_closed(&self) -> bool {
        self.closed
    }
    pub(crate) fn is_error(&self) -> bool {
        false
    }
}
pub(crate) struct Events(Vec<Event>);
impl Events {
    pub(crate) fn with_capacity(n: usize) -> Self {
        Self(Vec::with_capacity(n))
    }
    pub(crate) fn iter(&self) -> std::slice::Iter<'_, Event> {
        self.0.iter()
    }
}

struct PollEntries {
    streams: HashMap<Token, (Weak<Pipe>, Interest)>,
    wake: Option<Token>,
}
struct PollState {
    entries: Mutex<PollEntries>,
    changed: Condvar,
}
impl PollState {
    fn notify(&self) {
        // Writers must release Pipe::data before taking this lock. Poll scans
        // take the opposite locks only in the order entries -> data.
        let _guard = self.entries.lock().unwrap();
        self.changed.notify_all();
    }
}
#[derive(Clone)]
pub(crate) struct Registry(Arc<PollState>);
pub(crate) struct Poll {
    registry: Registry,
}
impl Poll {
    pub(crate) fn new() -> io::Result<Self> {
        Ok(Self {
            registry: Registry(Arc::new(PollState {
                entries: Mutex::new(PollEntries {
                    streams: HashMap::new(),
                    wake: None,
                }),
                changed: Condvar::new(),
            })),
        })
    }
    pub(crate) fn registry(&self) -> &Registry {
        &self.registry
    }
    pub(crate) fn poll(
        &mut self,
        events: &mut Events,
        timeout: Option<Duration>,
    ) -> io::Result<()> {
        events.0.clear();
        let deadline = timeout.map(|duration| Instant::now() + duration);
        let state = &self.registry.0;
        let mut entries = state.entries.lock().unwrap();
        loop {
            if let Some(token) = entries.wake.take() {
                events.0.push(Event {
                    token,
                    readable: false,
                    writable: false,
                    closed: false,
                });
            }
            for (&token, (pipe, interest)) in &entries.streams {
                if let Some(pipe) = pipe.upgrade() {
                    let mut data = pipe.data.lock().unwrap();
                    let closed = data.close_pending;
                    let readable = interest.0 & 1 != 0
                        && data.read_pending
                        && !data.server_read_closed
                        && (!data.input.is_empty() || data.client_closed);
                    let writable = interest.0 & 2 != 0
                        && data.write_pending
                        && data.output.len() < pipe.capacity
                        && !data.client_closed
                        && !data.server_write_closed;
                    if readable {
                        data.read_pending = false;
                    }
                    if writable {
                        data.write_pending = false;
                    }
                    data.close_pending = false;
                    if readable || writable || closed {
                        events.0.push(Event {
                            token,
                            readable,
                            writable,
                            closed,
                        });
                    }
                }
            }
            if !events.0.is_empty() {
                return Ok(());
            }
            if let Some(deadline) = deadline {
                let remaining = deadline.saturating_duration_since(Instant::now());
                if remaining.is_zero() {
                    return Ok(());
                }
                entries = state.changed.wait_timeout(entries, remaining).unwrap().0;
            } else {
                entries = state.changed.wait(entries).unwrap();
            }
        }
    }
}
impl Registry {
    pub(crate) fn try_clone(&self) -> io::Result<Self> {
        Ok(self.clone())
    }
    pub(crate) fn register(
        &self,
        stream: &mut ConnStream,
        token: Token,
        interest: Interest,
    ) -> io::Result<()> {
        let mut entries = self.0.entries.lock().unwrap();
        let mut data = stream.pipe.data.lock().unwrap();
        if data.registration.is_some() || entries.streams.contains_key(&token) {
            return Err(ErrorKind::AlreadyExists.into());
        }
        data.registration = Some((Arc::downgrade(&self.0), token));
        data.read_pending = true;
        data.write_pending = true;
        entries
            .streams
            .insert(token, (Arc::downgrade(&stream.pipe), interest));
        self.0.changed.notify_all();
        Ok(())
    }
    pub(crate) fn reregister(
        &self,
        stream: &mut ConnStream,
        token: Token,
        interest: Interest,
    ) -> io::Result<()> {
        let mut entries = self.0.entries.lock().unwrap();
        let entry = entries.streams.get_mut(&token).ok_or(ErrorKind::NotFound)?;
        if !Weak::ptr_eq(&entry.0, &Arc::downgrade(&stream.pipe)) {
            return Err(ErrorKind::InvalidInput.into());
        }
        entry.1 = interest;
        let mut data = stream.pipe.data.lock().unwrap();
        data.read_pending = true;
        data.write_pending = true;
        self.0.changed.notify_all();
        Ok(())
    }
    pub(crate) fn deregister(&self, stream: &mut ConnStream) -> io::Result<()> {
        let mut entries = self.0.entries.lock().unwrap();
        let mut data = stream.pipe.data.lock().unwrap();
        if let Some((owner, token)) = data.registration.as_ref() {
            if !Weak::ptr_eq(owner, &Arc::downgrade(&self.0)) {
                return Err(ErrorKind::InvalidInput.into());
            }
            entries.streams.remove(token);
            data.registration = None;
        }
        Ok(())
    }
}
pub(crate) struct Waker {
    registry: Registry,
    token: Token,
}
impl Waker {
    pub(crate) fn new(registry: &Registry, token: Token) -> io::Result<Self> {
        Ok(Self {
            registry: registry.clone(),
            token,
        })
    }
    pub(crate) fn wake(&self) -> io::Result<()> {
        self.registry.0.entries.lock().unwrap().wake = Some(self.token);
        self.registry.0.changed.notify_all();
        Ok(())
    }
}

struct PipeData {
    input: VecDeque<u8>,
    output: VecDeque<u8>,
    client_closed: bool,
    server_read_closed: bool,
    server_write_closed: bool,
    // Edge notifications persist until delivered (or explicitly rearmed).
    // An in-flight request with unread pipelined bytes must not spin the reactor.
    read_pending: bool,
    write_pending: bool,
    close_pending: bool,
    registration: Option<(Weak<PollState>, Token)>,
}
struct Pipe {
    data: Mutex<PipeData>,
    capacity: usize,
}
impl Pipe {
    fn notify(registration: Option<(Weak<PollState>, Token)>) {
        if let Some((owner, _)) = registration {
            if let Some(owner) = owner.upgrade() {
                owner.notify();
            }
        }
    }
    fn read(&self, server: bool, output: &mut [u8]) -> io::Result<usize> {
        if output.is_empty() {
            return Ok(0);
        }
        let mut data = self.data.lock().unwrap();
        let closed = if server {
            data.client_closed || data.server_read_closed
        } else {
            data.server_write_closed
        };
        let buffer = if server {
            &mut data.input
        } else {
            &mut data.output
        };
        if buffer.is_empty() {
            return if closed {
                Ok(0)
            } else {
                Err(ErrorKind::WouldBlock.into())
            };
        }
        let count = output.len().min(buffer.len());
        for byte in &mut output[..count] {
            *byte = buffer.pop_front().unwrap();
        }
        if !server {
            data.write_pending = true;
        }
        let registration = data.registration.clone();
        drop(data);
        Self::notify(registration);
        Ok(count)
    }
    fn write(&self, server: bool, input: &[u8]) -> io::Result<usize> {
        if input.is_empty() {
            return Ok(0);
        }
        let mut data = self.data.lock().unwrap();
        if data.client_closed
            || (if server {
                data.server_write_closed
            } else {
                data.server_read_closed
            })
        {
            return Err(ErrorKind::BrokenPipe.into());
        }
        let buffer = if server {
            &mut data.output
        } else {
            &mut data.input
        };
        let count = input.len().min(self.capacity - buffer.len());
        if count == 0 {
            return Err(ErrorKind::WouldBlock.into());
        }
        buffer
            .try_reserve(count)
            .map_err(|_| io::Error::from(ErrorKind::OutOfMemory))?;
        buffer.extend(&input[..count]);
        if !server {
            data.read_pending = true;
        }
        let registration = data.registration.clone();
        drop(data);
        Self::notify(registration);
        Ok(count)
    }
    fn close(&self, server: bool) {
        let mut data = self.data.lock().unwrap();
        if server {
            data.server_read_closed = true;
            data.server_write_closed = true;
        } else {
            data.client_closed = true;
            data.close_pending = true;
            data.read_pending = true;
        }
        let registration = data.registration.clone();
        drop(data);
        Self::notify(registration);
    }
}
pub(crate) struct ConnStream {
    pipe: Arc<Pipe>,
}
impl ConnStream {
    pub(crate) fn is_local(&self) -> bool {
        true
    }
    pub(crate) fn apply_tcp_keepalive(&self, _: TcpKeepaliveConfig) -> io::Result<()> {
        Ok(())
    }
    pub(crate) fn shutdown(&self, how: std::net::Shutdown) -> io::Result<()> {
        let mut data = self.pipe.data.lock().unwrap();
        if matches!(how, std::net::Shutdown::Read | std::net::Shutdown::Both) {
            data.server_read_closed = true;
        }
        if matches!(how, std::net::Shutdown::Write | std::net::Shutdown::Both) {
            data.server_write_closed = true;
        }
        let registration = data.registration.clone();
        drop(data);
        Pipe::notify(registration);
        Ok(())
    }
}
impl Read for ConnStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.pipe.read(true, buf)
    }
}
impl Write for ConnStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.pipe.write(true, buf)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}
impl Drop for ConnStream {
    fn drop(&mut self) {
        self.pipe.close(true);
    }
}
pub(crate) fn raw_fd(_: &ConnStream) -> c_int {
    -1
}
#[derive(Clone, Copy, Default)]
#[allow(dead_code)] // Local byte streams, like Unix sockets, do not use TCP keepalive.
pub(crate) struct TcpKeepaliveConfig {
    pub enabled: bool,
    pub idle: u32,
    pub interval: u32,
    pub count: u32,
}

pub struct MemoryClient {
    pipe: Arc<Pipe>,
}
impl Drop for MemoryClient {
    fn drop(&mut self) {
        self.pipe.close(false);
    }
}

/// # Safety
/// The reactor remains live during this call; its destruction cannot race it.
#[no_mangle]
pub unsafe extern "C" fn nio_memory_connect(
    reactor: *const reactor::Reactor,
    capacity: usize,
) -> *mut MemoryClient {
    let Some(reactor) = (unsafe { reactor.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if capacity == 0 || capacity > 16 * 1024 * 1024 || reactor.stop.load(Ordering::Acquire) {
        return std::ptr::null_mut();
    }
    let peer = &reactor.memory_peers
        [reactor.next_memory_peer.fetch_add(1, Ordering::Relaxed) % reactor.memory_peers.len()];
    let mut incoming = peer.incoming.lock().unwrap();
    if incoming.len() >= reactor::HANDOFF_CAP
        || reactor.stop.load(Ordering::Acquire)
        || incoming.try_reserve(1).is_err()
    {
        return std::ptr::null_mut();
    }
    let pipe = Arc::new(Pipe {
        capacity,
        data: Mutex::new(PipeData {
            input: VecDeque::new(),
            output: VecDeque::new(),
            client_closed: false,
            server_read_closed: false,
            server_write_closed: false,
            registration: None,
            read_pending: true,
            write_pending: true,
            close_pending: false,
        }),
    });
    incoming.push(ConnStream { pipe: pipe.clone() });
    drop(incoming);
    let _ = peer.waker.wake();
    Box::into_raw(Box::new(MemoryClient { pipe }))
}

fn io_result(result: io::Result<usize>) -> i64 {
    match result {
        Ok(n) => n as i64,
        Err(e) if e.kind() == ErrorKind::WouldBlock => -2,
        Err(_) => -1,
    }
}

/// # Safety
/// `client` remains live during the call and `buffer` is readable for `length` bytes.
#[no_mangle]
pub unsafe extern "C" fn nio_memory_write(
    client: *const MemoryClient,
    buffer: *const c_char,
    length: i64,
) -> i64 {
    let Some(client) = (unsafe { client.as_ref() }) else {
        return -1;
    };
    let Some(length) = crate::ffi_check::checked_bytes_len(buffer, length) else {
        return -1;
    };
    if length == 0 {
        return 0;
    }
    io_result(client.pipe.write(false, unsafe {
        std::slice::from_raw_parts(buffer.cast(), length)
    }))
}
/// # Safety
/// `client` remains live during the call and `buffer` is writable for `length` bytes.
#[no_mangle]
pub unsafe extern "C" fn nio_memory_read(
    client: *const MemoryClient,
    buffer: *mut c_char,
    length: i64,
) -> i64 {
    let Some(client) = (unsafe { client.as_ref() }) else {
        return -1;
    };
    let Some(length) = crate::ffi_check::checked_bytes_len(buffer, length) else {
        return -1;
    };
    if length == 0 {
        return 0;
    }
    io_result(client.pipe.read(false, unsafe {
        std::slice::from_raw_parts_mut(buffer.cast(), length)
    }))
}
/// # Safety
/// `client` is null or is a live client destroyed once, with no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn nio_memory_close(client: *mut MemoryClient) {
    if !client.is_null() {
        drop(unsafe { Box::from_raw(client) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn readiness_is_consumed_and_rearmed_without_losing_new_input() {
        let pipe = Arc::new(Pipe {
            capacity: 7,
            data: Mutex::new(PipeData {
                input: VecDeque::new(),
                output: VecDeque::new(),
                client_closed: false,
                server_read_closed: false,
                server_write_closed: false,
                read_pending: true,
                write_pending: true,
                close_pending: false,
                registration: None,
            }),
        });
        let mut stream = ConnStream { pipe: pipe.clone() };
        let mut poll = Poll::new().unwrap();
        let mut events = Events::with_capacity(4);
        let token = Token(3);
        poll.registry()
            .register(&mut stream, token, Interest::READABLE | Interest::WRITABLE)
            .unwrap();
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert_eq!(events.0.len(), 1);
        assert!(events.0[0].writable && !events.0[0].readable);
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert!(events.0.is_empty()); // Writable capacity alone must not spin.
        pipe.write(false, b"abc").unwrap();
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert_eq!(events.0.len(), 1);
        assert!(events.0[0].readable);
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert!(events.0.is_empty()); // Leave pipelined data unread while the worker owns a request.
        pipe.write(false, b"d").unwrap();
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert_eq!(events.0.len(), 1); // New input wakes a mid-request reader.
        let other = Poll::new().unwrap();
        assert!(other.registry().deregister(&mut stream).is_err());
        poll.registry()
            .reregister(&mut stream, token, Interest::READABLE)
            .unwrap();
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert_eq!(events.0.len(), 1); // Rearming redelivers existing readiness.
        let mut bytes = [0; 7];
        assert_eq!(stream.read(&mut bytes).unwrap(), 4);
        assert_eq!(&bytes[..4], b"abcd");
        stream.shutdown(std::net::Shutdown::Read).unwrap();
        assert_eq!(
            pipe.write(false, b"x").unwrap_err().kind(),
            ErrorKind::BrokenPipe
        );
        assert_eq!(stream.write(b"last").unwrap(), 4); // Read shutdown permits the final response.
        stream.shutdown(std::net::Shutdown::Both).unwrap();
        assert_eq!(pipe.read(false, &mut bytes).unwrap(), 4);
        assert_eq!(&bytes[..4], b"last");
        assert_eq!(pipe.read(false, &mut bytes).unwrap(), 0); // EOF while ConnStream still exists.
        pipe.close(false);
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert_eq!(events.0.len(), 1);
        assert!(events.0[0].closed);
        poll.poll(&mut events, Some(Duration::ZERO)).unwrap();
        assert!(events.0.is_empty());
        poll.registry().deregister(&mut stream).unwrap();
    }
}
