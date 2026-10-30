// Copyright (c) 2025 OceanBase.
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

use crate::*;

pub(crate) const LOCAL_RUN_DIR: &str = "run";
#[cfg(all(unix, feature = "native-network"))]
pub(crate) const UNIX_SOCKET_NAME: &str = "sql.sock";
#[cfg(all(windows, feature = "native-network"))]
pub(crate) const PIPE_DISCOVERY_NAME: &str = "sql.pipe";

#[cfg_attr(not(windows), allow(dead_code))]
pub(crate) fn pipe_bare_name(pid: u32, secs: u64) -> String {
    format!("{pid}-{secs}")
}

pub(crate) struct LocalEndpointGuard {
    path: PathBuf,
    removed: AtomicBool,
}

impl LocalEndpointGuard {
    #[cfg(feature = "native-network")]
    #[cfg_attr(not(unix), allow(dead_code))]
    pub(crate) fn new(path: PathBuf) -> Self {
        Self {
            path,
            removed: AtomicBool::new(false),
        }
    }

    fn remove_once(&self) {
        if !self.removed.swap(true, Ordering::AcqRel) {
            let _ = std::fs::remove_file(&self.path);
        }
    }
}

impl Drop for LocalEndpointGuard {
    fn drop(&mut self) {
        self.remove_once();
    }
}

pub struct NioReactor {
    pub(crate) wakers: Vec<Arc<Waker>>,
    pub(crate) stop: Arc<AtomicBool>,
    pub(crate) joins: Vec<JoinHandle<()>>,
    #[cfg(feature = "native-network")]
    reactor_diagnostics: Arc<ReactorDiagnostics>,
    local_endpoint: Option<LocalEndpointGuard>,
    keepalive: Arc<TcpKeepaliveState>,
    bound_tcp_port: u32,
    #[cfg(feature = "memory-transport")]
    pub(crate) memory_peers: Vec<PeerHandoff>,
    #[cfg(feature = "memory-transport")]
    pub(crate) next_memory_peer: AtomicUsize,
}

pub(crate) struct TcpKeepaliveState {
    config: Mutex<TcpKeepaliveConfig>,
    revision: AtomicU64,
}

impl TcpKeepaliveState {
    pub(crate) fn new() -> Self {
        Self {
            config: Mutex::new(TcpKeepaliveConfig::default()),
            revision: AtomicU64::new(0),
        }
    }

    fn update(&self, config: TcpKeepaliveConfig) {
        let mut current = self.config.lock().unwrap();
        *current = config;
        self.revision.fetch_add(1, Ordering::Release);
    }

    pub(crate) fn snapshot(&self) -> (u64, TcpKeepaliveConfig) {
        let current = self.config.lock().unwrap();
        let revision = self.revision.load(Ordering::Acquire);
        (revision, *current)
    }
}

const CONN_TOKEN_SLOT_BITS: u32 = 20;
const CONN_TOKEN_SLOT_COUNT: usize = 1usize << CONN_TOKEN_SLOT_BITS;
const CONN_TOKEN_SLOT_MASK: usize = CONN_TOKEN_SLOT_COUNT - 1;
pub(crate) const CONN_TOKEN_SLOT_CAPACITY: usize = CONN_TOKEN_SLOT_COUNT - FIRST_CONN;
pub(crate) const CONN_TOKEN_MAX_GENERATION: usize = usize::MAX >> CONN_TOKEN_SLOT_BITS;

pub(crate) fn encode_conn_token(slot: usize, generation: usize) -> Option<Token> {
    if slot >= CONN_TOKEN_SLOT_CAPACITY || generation == 0 || generation > CONN_TOKEN_MAX_GENERATION
    {
        None
    } else {
        Some(Token(
            (generation << CONN_TOKEN_SLOT_BITS) | (slot + FIRST_CONN),
        ))
    }
}

pub(crate) fn decode_conn_token(token: Token) -> Option<usize> {
    let generation = token.0 >> CONN_TOKEN_SLOT_BITS;
    let encoded_slot = token.0 & CONN_TOKEN_SLOT_MASK;
    if generation == 0 || encoded_slot < FIRST_CONN {
        None
    } else {
        let slot = encoded_slot - FIRST_CONN;
        (slot < CONN_TOKEN_SLOT_CAPACITY).then_some(slot)
    }
}

pub(crate) struct ConnSlab<V> {
    entries: Slab<(Token, V)>,
    pub(crate) last_generation: usize,
}

impl<V> ConnSlab<V> {
    pub(crate) fn new() -> Self {
        Self {
            entries: Slab::new(),
            last_generation: 0,
        }
    }

    pub(crate) fn next_token(&mut self) -> Option<Token> {
        let generation = self.last_generation.checked_add(1)?;
        let token = encode_conn_token(self.entries.vacant_key(), generation)?;
        self.last_generation = generation;
        Some(token)
    }

    pub(crate) fn insert(&mut self, token: Token, value: V) {
        let slot = decode_conn_token(token).expect("invalid generated connection token");
        assert_eq!(
            slot,
            self.entries.vacant_key(),
            "connection slab reservation changed before insertion"
        );
        let inserted_slot = self.entries.insert((token, value));
        assert_eq!(slot, inserted_slot, "connection slab inserted wrong slot");
    }

    pub(crate) fn get(&self, token: &Token) -> Option<&V> {
        let slot = decode_conn_token(*token)?;
        self.entries
            .get(slot)
            .and_then(|(current, value)| (current == token).then_some(value))
    }

    pub(crate) fn remove(&mut self, token: &Token) -> Option<V> {
        let slot = decode_conn_token(*token)?;
        if self
            .entries
            .get(slot)
            .is_some_and(|(current, _)| current == token)
        {
            Some(self.entries.remove(slot).1)
        } else {
            None
        }
    }

    fn values(&self) -> impl Iterator<Item = &V> {
        self.entries.iter().map(|(_, (_, value))| value)
    }

    pub(crate) fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

#[derive(Clone)]
pub(crate) struct PeerHandoff {
    pub(crate) incoming: Arc<Mutex<Vec<ConnStream>>>,
    pub(crate) waker: Arc<Waker>,
}

pub(crate) const HANDOFF_CAP: usize = 1024;

pub(crate) struct EventLoop {
    pub(crate) poll: Poll,
    #[cfg(feature = "native-network")]
    pub(crate) listener: Option<TcpListener>,
    #[cfg(all(unix, feature = "native-network"))]
    pub(crate) unix_listener: Option<UnixListener>,
    #[cfg(all(windows, feature = "native-network"))]
    pub(crate) pipe_name: Arc<Vec<u16>>,
    #[cfg(all(windows, feature = "native-network"))]
    pub(crate) pending_pipe: Option<NamedPipe>,
    #[cfg(all(windows, feature = "native-network"))]
    pub(crate) pipe_startup: Option<std::sync::mpsc::Sender<bool>>,
    pub(crate) incoming: Arc<Mutex<Vec<ConnStream>>>,
    pub(crate) peers: Vec<PeerHandoff>,
    #[cfg(feature = "native-network")]
    pub(crate) next_accept_target: usize,
    #[cfg(feature = "native-network")]
    pub(crate) accept_drain_id: u64,
    pub(crate) poll_id: u64,
    #[cfg(feature = "native-network")]
    pub(crate) listener_event_count: u64,
    #[cfg(feature = "native-network")]
    pub(crate) accept_calls: u64,
    #[cfg(feature = "native-network")]
    pub(crate) accepted_total: u64,
    #[cfg(feature = "native-network")]
    pub(crate) reactor_diagnostics: Arc<ReactorDiagnostics>,
    pub(crate) cxx_connect_attempts: Arc<AtomicU64>,
    pub(crate) cxx_connect_returns: Arc<AtomicU64>,
    pub(crate) cxx_connect_rejects: Arc<AtomicU64>,
    pub(crate) reg: Arc<Registry>,
    pub(crate) waker: Arc<Waker>,
    pub(crate) reactor_polling: Arc<AtomicBool>,
    pub(crate) closes: Arc<Mutex<Vec<Token>>>,
    pub(crate) ready: Arc<Mutex<Vec<Token>>>,
    pub(crate) ready_batch: Vec<Token>,
    pub(crate) commits: Arc<Mutex<Vec<QueuedCompletion>>>,
    pub(crate) commit_batch: Vec<QueuedCompletion>,
    pub(crate) completions: Arc<Mutex<Vec<QueuedCompletion>>>,
    pub(crate) stop: Arc<AtomicBool>,
    pub(crate) handler: Handler,
    pub(crate) session_size: usize,
    pub(crate) tls_config: Option<Arc<rustls::ServerConfig>>,
    pub(crate) keepalive: Arc<TcpKeepaliveState>,
    pub(crate) keepalive_revision: u64,
    pub(crate) conns: ConnSlab<Arc<Conn>>,
    pub(crate) retired: HashMap<Token, Arc<Conn>>,
}

const MAX_TRANSIENT_TCP_ACCEPT_ERRORS: u32 = 64;
const ACCEPT_ERROR_KIND_OTHER: i32 = 0;
const ACCEPT_ERROR_KIND_NOT_FOUND: i32 = 1;
const ACCEPT_ERROR_KIND_PERMISSION_DENIED: i32 = 2;
const ACCEPT_ERROR_KIND_CONNECTION_REFUSED: i32 = 3;
const ACCEPT_ERROR_KIND_CONNECTION_RESET: i32 = 4;
const ACCEPT_ERROR_KIND_HOST_UNREACHABLE: i32 = 5;
const ACCEPT_ERROR_KIND_NETWORK_UNREACHABLE: i32 = 6;
const ACCEPT_ERROR_KIND_CONNECTION_ABORTED: i32 = 7;
const ACCEPT_ERROR_KIND_NOT_CONNECTED: i32 = 8;
const ACCEPT_ERROR_KIND_ADDR_IN_USE: i32 = 9;
const ACCEPT_ERROR_KIND_ADDR_NOT_AVAILABLE: i32 = 10;
const ACCEPT_ERROR_KIND_BROKEN_PIPE: i32 = 11;
const ACCEPT_ERROR_KIND_ALREADY_EXISTS: i32 = 12;
const ACCEPT_ERROR_KIND_WOULD_BLOCK: i32 = 13;
const ACCEPT_ERROR_KIND_INVALID_INPUT: i32 = 14;
const ACCEPT_ERROR_KIND_INVALID_DATA: i32 = 15;
const ACCEPT_ERROR_KIND_TIMED_OUT: i32 = 16;
const ACCEPT_ERROR_KIND_WRITE_ZERO: i32 = 17;
const ACCEPT_ERROR_KIND_INTERRUPTED: i32 = 18;
const ACCEPT_ERROR_KIND_UNSUPPORTED: i32 = 19;
const ACCEPT_ERROR_KIND_UNEXPECTED_EOF: i32 = 20;
const ACCEPT_ERROR_KIND_OUT_OF_MEMORY: i32 = 21;
const ACCEPT_OUTCOME_WOULD_BLOCK: i32 = 0;
const ACCEPT_OUTCOME_FATAL: i32 = 1;
const ACCEPT_OUTCOME_REARMED: i32 = 2;
const ACCEPT_OUTCOME_REARM_FAILED: i32 = 3;
const REACTOR_POLL_TIMEOUT: i32 = 0;
const REACTOR_POLL_EVENTS: i32 = 1;
const REACTOR_POLL_FAILED: i32 = 2;
const REACTOR_POLL_SKIPPED: i32 = 3;
#[cfg(feature = "native-network")]
const REACTOR_POLL_FINAL: i32 = 4;

#[cfg(feature = "native-network")]
struct ReactorDiagnostics {
    poll_id: AtomicU64,
    event_count: AtomicU64,
    listener_events: AtomicU64,
    accept_calls: AtomicU64,
    accepted: AtomicU64,
    cxx_connect_attempts: AtomicU64,
    cxx_connect_returns: AtomicU64,
    cxx_connect_rejects: AtomicU64,
    listener_event_flags: AtomicU32,
}

#[cfg(feature = "native-network")]
impl ReactorDiagnostics {
    fn new() -> Self {
        Self {
            poll_id: AtomicU64::new(0),
            event_count: AtomicU64::new(0),
            listener_events: AtomicU64::new(0),
            accept_calls: AtomicU64::new(0),
            accepted: AtomicU64::new(0),
            cxx_connect_attempts: AtomicU64::new(0),
            cxx_connect_returns: AtomicU64::new(0),
            cxx_connect_rejects: AtomicU64::new(0),
            listener_event_flags: AtomicU32::new(0),
        }
    }
}

fn accept_error_kind_code(kind: std::io::ErrorKind) -> i32 {
    match kind {
        std::io::ErrorKind::NotFound => ACCEPT_ERROR_KIND_NOT_FOUND,
        std::io::ErrorKind::PermissionDenied => ACCEPT_ERROR_KIND_PERMISSION_DENIED,
        std::io::ErrorKind::ConnectionRefused => ACCEPT_ERROR_KIND_CONNECTION_REFUSED,
        std::io::ErrorKind::ConnectionReset => ACCEPT_ERROR_KIND_CONNECTION_RESET,
        std::io::ErrorKind::HostUnreachable => ACCEPT_ERROR_KIND_HOST_UNREACHABLE,
        std::io::ErrorKind::NetworkUnreachable => ACCEPT_ERROR_KIND_NETWORK_UNREACHABLE,
        std::io::ErrorKind::ConnectionAborted => ACCEPT_ERROR_KIND_CONNECTION_ABORTED,
        std::io::ErrorKind::NotConnected => ACCEPT_ERROR_KIND_NOT_CONNECTED,
        std::io::ErrorKind::AddrInUse => ACCEPT_ERROR_KIND_ADDR_IN_USE,
        std::io::ErrorKind::AddrNotAvailable => ACCEPT_ERROR_KIND_ADDR_NOT_AVAILABLE,
        std::io::ErrorKind::BrokenPipe => ACCEPT_ERROR_KIND_BROKEN_PIPE,
        std::io::ErrorKind::AlreadyExists => ACCEPT_ERROR_KIND_ALREADY_EXISTS,
        std::io::ErrorKind::WouldBlock => ACCEPT_ERROR_KIND_WOULD_BLOCK,
        std::io::ErrorKind::InvalidInput => ACCEPT_ERROR_KIND_INVALID_INPUT,
        std::io::ErrorKind::InvalidData => ACCEPT_ERROR_KIND_INVALID_DATA,
        std::io::ErrorKind::TimedOut => ACCEPT_ERROR_KIND_TIMED_OUT,
        std::io::ErrorKind::WriteZero => ACCEPT_ERROR_KIND_WRITE_ZERO,
        std::io::ErrorKind::Interrupted => ACCEPT_ERROR_KIND_INTERRUPTED,
        std::io::ErrorKind::Unsupported => ACCEPT_ERROR_KIND_UNSUPPORTED,
        std::io::ErrorKind::UnexpectedEof => ACCEPT_ERROR_KIND_UNEXPECTED_EOF,
        std::io::ErrorKind::OutOfMemory => ACCEPT_ERROR_KIND_OUT_OF_MEMORY,
        _ => ACCEPT_ERROR_KIND_OTHER,
    }
}

fn should_log_accept_sample(drain_id: u64) -> bool {
    drain_id < 16 || drain_id.is_power_of_two()
}

#[cfg(feature = "native-network")]
fn should_log_reactor_sample(poll_id: u64) -> bool {
    poll_id < 16 || poll_id.is_power_of_two()
}

#[cfg(windows)]
fn is_transient_tcp_accept_error(error: &std::io::Error) -> bool {
    matches!(
        error.kind(),
        std::io::ErrorKind::Interrupted
            | std::io::ErrorKind::ConnectionAborted
            | std::io::ErrorKind::ConnectionReset
    )
}

#[cfg(not(windows))]
fn is_transient_tcp_accept_error(error: &std::io::Error) -> bool {
    error.kind() == std::io::ErrorKind::Interrupted
}

impl EventLoop {
    pub(crate) fn run(mut self) {
        let mut events = Events::with_capacity(512);
        let mut stopping = false;
        loop {
            let poll_id = self.poll_id;
            self.poll_id = self.poll_id.wrapping_add(1);
            if !stopping && self.stop.load(Ordering::Acquire) {
                self.begin_shutdown();
                stopping = true;
            }
            if stopping && self.conns.is_empty() && self.retired.is_empty() {
                break;
            }
            #[cfg(all(windows, feature = "native-network"))]
            if self.pending_pipe.is_none() && !stopping {
                self.rearm_pending();
            }
            #[cfg(all(windows, feature = "native-network"))]
            if let Some(startup) = self.pipe_startup.take() {
                let _ = startup.send(self.pending_pipe.is_some());
            }
            self.reactor_polling.store(true, Ordering::Release);
            let skip_poll = !stopping
                && (!self.ready.lock().unwrap().is_empty()
                    || !self.commits.lock().unwrap().is_empty());
            let poll_failed = if skip_poll {
                false
            } else {
                self.poll
                    .poll(&mut events, Some(Duration::from_millis(1000)))
                    .is_err()
            };
            let poll_result = if skip_poll {
                REACTOR_POLL_SKIPPED
            } else if poll_failed {
                REACTOR_POLL_FAILED
            } else if events.iter().next().is_some() {
                REACTOR_POLL_EVENTS
            } else {
                REACTOR_POLL_TIMEOUT
            };
            self.reactor_polling.store(false, Ordering::Release);
            if !stopping {
                self.refresh_tcp_keepalive();
            }
            if !stopping && self.stop.load(Ordering::Acquire) {
                self.begin_shutdown();
                stopping = true;
            }
            if poll_failed && !stopping {
                self.publish_reactor_state(poll_result, poll_id, 0, 0);
                self.log_reactor_state(poll_result, poll_id, 0, 0);
                continue;
            }
            let mut event_count: u64 = 0;
            let mut listener_event_flags: u32 = 0;
            if !skip_poll {
                for event in events.iter() {
                    event_count += 1;
                    if stopping {
                        continue;
                    }
                    match event.token() {
                        WAKER => {}
                        #[cfg(feature = "native-network")]
                        LISTENER => {
                            self.listener_event_count += 1;
                            self.accept_calls += 1;
                            if event.is_readable() {
                                listener_event_flags |= 1;
                            }
                            if event.is_writable() {
                                listener_event_flags |= 1 << 1;
                            }
                            if event.is_error() {
                                listener_event_flags |= 1 << 2;
                            }
                            if event.is_read_closed() {
                                listener_event_flags |= 1 << 3;
                            }
                            if event.is_write_closed() {
                                listener_event_flags |= 1 << 4;
                            }
                            self.accept_all();
                        }
                        #[cfg(all(any(unix, windows), feature = "native-network"))]
                        LOCAL_LISTENER => self.accept_local(),
                        token => {
                            let transport_closed = event.is_error()
                                || event.is_read_closed()
                                || event.is_write_closed();
                            if event.is_writable() {
                                self.on_writable(token);
                            }
                            if event.is_readable() {
                                self.on_readable_event(token);
                            }
                            if transport_closed {
                                self.on_transport_closed(token, event.is_error());
                            }
                        }
                    }
                }
            }
            if !stopping {
                self.drain_incoming();
            }
            self.drain_commits();
            self.drain_completions();
            self.reap_closes();
            if !stopping {
                self.drain_ready();
            }
            self.reap_retired();
            self.publish_reactor_state(poll_result, poll_id, event_count, listener_event_flags);
            self.log_reactor_state(poll_result, poll_id, event_count, listener_event_flags);
        }
    }

    pub(crate) fn begin_shutdown(&mut self) {
        #[cfg(feature = "memory-transport")]
        self.incoming.lock().unwrap().clear();
        let conns: Vec<Arc<Conn>> = self.conns.values().cloned().collect();
        let mut completed = Vec::new();
        for conn in conns {
            let had_async_write = {
                let mut g = conn.mu.lock().unwrap();
                conn.err.store(true, Ordering::Release);
                g.clear_output();
                take_final_completion_locked(&mut g)
            };
            conn.write_done.notify_all();
            if let Some(generation) = had_async_write {
                completed.push(QueuedCompletion {
                    token: conn.token,
                    generation,
                });
            }
            self.notify_disconnect(&conn);
            request_close(&conn);
        }
        self.completions.lock().unwrap().extend(completed);
    }

    pub(crate) fn make_conn(&self, token: Token, stream: impl Into<ConnStream>) -> Arc<Conn> {
        let stream = stream.into();
        let (_, config) = self.keepalive.snapshot();
        if let Err(err) = stream.apply_tcp_keepalive(config) {
            eprintln!(
                "sql-nio: applying TCP keepalive to new fd {} failed: {err}",
                raw_fd(&stream)
            );
        }
        Arc::new(Conn {
            token,
            server_caps: capability::server_capabilities(self.tls_config.is_some()),
            tls_config: self.tls_config.clone(),
            mu: Mutex::new(ConnInner {
                sock: stream,
                tls: None,
                expected_login_seq: 1,
                tls_arm_pending: false,
                inbuf: Vec::new(),
                rpos: 0,
                drain_stopped_at_budget: false,
                outbuf: Vec::new(),
                out_pos: 0,
                want_write: false,
                write_waiters: 0,
                final_response: FinalResponseState::Idle,
                response: ResponseWriter::new(),
                read_state: ReadState::Login,
                inner_buf: Vec::new(),
                inner_rpos: 0,
                compressed_wire_bytes: 0,
                mid_request_body: None,
                next_mid_request_lease: 0,
                active_request_body: None,
                initial_body_spare: Vec::new(),
                req_compressed: false,
                next_comp_seq: 0,
                login: None,
            }),
            write_done: Condvar::new(),
            mid_read_signal: Mutex::new(MidReadSignal::default()),
            mid_read_ready: Condvar::new(),
            session_storage: CppSessionStorage::new(self.session_size),
            reg: self.reg.clone(),
            waker: self.waker.clone(),
            reactor_polling: self.reactor_polling.clone(),
            closes: self.closes.clone(),
            ready: self.ready.clone(),
            commits: self.commits.clone(),
            completions: self.completions.clone(),
            err: AtomicBool::new(false),
            peer_closed: AtomicBool::new(false),
            need_shutdown: AtomicBool::new(false),
            sql_session_bound: AtomicBool::new(false),
            max_packet_size: AtomicUsize::new(MAX_LOGIN_BODY_LEN),
            raw_caps: AtomicU32::new(0),
            transport_caps: AtomicU32::new(0),
            session_constructed: AtomicBool::new(false),
            disconnect_notified: AtomicBool::new(false),
            close_notified: AtomicBool::new(false),
            close_requested: AtomicBool::new(false),
            request_gate: Mutex::new(()),
            request_state: AtomicU8::new(0),
            request_generation: AtomicU64::new(0),
        })
    }

    pub(crate) fn admit(
        &mut self,
        conn: &Arc<Conn>,
        fd: c_int,
        is_unix: c_int,
        preregistered: bool,
    ) -> bool {
        let sess = conn.sess();
        insert_conn_mapping(conn);
        let mut greeting = NioGreetingInfo::zeroed();
        self.cxx_connect_attempts.fetch_add(1, Ordering::Relaxed);
        let rc = unsafe {
            ob_sql_sock_handler_on_connect(self.handler.0, sess, fd, is_unix, &mut greeting)
        };
        self.cxx_connect_returns.fetch_add(1, Ordering::Relaxed);
        if rc != 0 {
            self.cxx_connect_rejects.fetch_add(1, Ordering::Relaxed);
        }
        conn.session_constructed.store(true, Ordering::Release);
        let rejected = rc != 0;
        if rejected || conn.err.load(Ordering::Acquire) || !send_greeting(conn, &greeting) {
            self.abort_admission(conn, preregistered);
            return false;
        }
        let registered = {
            let mut g = conn.mu.lock().unwrap();
            let interest = if g.want_write {
                Interest::READABLE | Interest::WRITABLE
            } else {
                Interest::READABLE
            };
            if preregistered {
                self.reg
                    .reregister(&mut g.sock, conn.token, interest)
                    .is_ok()
            } else {
                self.reg.register(&mut g.sock, conn.token, interest).is_ok()
            }
        };
        if !registered {
            self.abort_admission(conn, true);
        }
        registered
    }

    fn abort_admission(&mut self, conn: &Arc<Conn>, registered: bool) {
        conn.err.store(true, Ordering::Release);
        conn.write_done.notify_all();
        if registered {
            let mut g = conn.mu.lock().unwrap();
            let _ = conn.reg.deregister(&mut g.sock);
        }
        self.notify_disconnect(conn);
        self.notify_close(conn, -1);
        self.retire_or_release(conn.clone());
    }

    #[cfg(feature = "native-network")]
    fn log_reactor_state(
        &self,
        poll_result: i32,
        poll_id: u64,
        event_count: u64,
        listener_event_flags: u32,
    ) {
        if self.listener.is_none() || !should_log_reactor_sample(poll_id) {
            return;
        }
        unsafe {
            ob_sql_nio_log_reactor_state(
                poll_result,
                poll_id,
                event_count,
                self.listener_event_count,
                self.accept_calls,
                self.accepted_total,
                self.cxx_connect_attempts.load(Ordering::Acquire),
                self.cxx_connect_returns.load(Ordering::Acquire),
                self.cxx_connect_rejects.load(Ordering::Acquire),
                listener_event_flags,
            );
        }
    }

    #[cfg(feature = "native-network")]
    fn publish_reactor_state(
        &self,
        _poll_result: i32,
        poll_id: u64,
        event_count: u64,
        listener_event_flags: u32,
    ) {
        if self.listener.is_none() {
            return;
        }
        self.reactor_diagnostics
            .poll_id
            .store(poll_id, Ordering::Release);
        self.reactor_diagnostics
            .event_count
            .store(event_count, Ordering::Release);
        self.reactor_diagnostics
            .listener_events
            .store(self.listener_event_count, Ordering::Release);
        self.reactor_diagnostics
            .accept_calls
            .store(self.accept_calls, Ordering::Release);
        self.reactor_diagnostics
            .accepted
            .store(self.accepted_total, Ordering::Release);
        self.reactor_diagnostics
            .cxx_connect_attempts
            .store(self.cxx_connect_attempts.load(Ordering::Acquire), Ordering::Release);
        self.reactor_diagnostics
            .cxx_connect_returns
            .store(self.cxx_connect_returns.load(Ordering::Acquire), Ordering::Release);
        self.reactor_diagnostics
            .cxx_connect_rejects
            .store(self.cxx_connect_rejects.load(Ordering::Acquire), Ordering::Release);
        self.reactor_diagnostics
            .listener_event_flags
            .store(listener_event_flags, Ordering::Release);
    }

    #[cfg(not(feature = "native-network"))]
    fn publish_reactor_state(
        &self,
        _poll_result: i32,
        _poll_id: u64,
        _event_count: u64,
        _listener_event_flags: u32,
    ) {
    }

    #[cfg(not(feature = "native-network"))]
    fn log_reactor_state(
        &self,
        _poll_result: i32,
        _poll_id: u64,
        _event_count: u64,
        _listener_event_flags: u32,
    ) {
    }

    #[cfg(feature = "native-network")]
    pub(crate) fn accept_all(&mut self) {
        let drain_id = self.accept_drain_id;
        self.accept_drain_id = self.accept_drain_id.wrapping_add(1);
        let mut accepted = 0;
        let mut first_error_kind = None;
        let mut first_error_raw = -1;
        let mut transient_errors = 0;
        let mut consecutive_errors = 0;
        loop {
            let listener = match self.listener.as_ref() {
                Some(listener) => listener,
                None => return,
            };
            let (sock, _addr) = match listener.accept() {
                Ok(v) => {
                    accepted += 1;
                    self.accepted_total += 1;
                    consecutive_errors = 0;
                    v
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    if first_error_kind.is_some() || should_log_accept_sample(drain_id) {
                        unsafe {
                            ob_sql_nio_log_accept_state(
                                first_error_kind.unwrap_or(0),
                                first_error_raw,
                                drain_id,
                                accepted,
                                transient_errors,
                                consecutive_errors,
                                ACCEPT_OUTCOME_WOULD_BLOCK,
                            );
                        }
                    }
                    break;
                }
                Err(e) => {
                    if first_error_kind.is_none() {
                        first_error_kind = Some(accept_error_kind_code(e.kind()));
                        first_error_raw = e.raw_os_error().unwrap_or(-1);
                    }
                    if !is_transient_tcp_accept_error(&e) {
                        unsafe {
                            ob_sql_nio_log_accept_state(
                                first_error_kind.unwrap(),
                                first_error_raw,
                                drain_id,
                                accepted,
                                transient_errors,
                                consecutive_errors,
                                ACCEPT_OUTCOME_FATAL,
                            );
                        }
                        break;
                    }
                    transient_errors += 1;
                    consecutive_errors += 1;
                    if consecutive_errors >= MAX_TRANSIENT_TCP_ACCEPT_ERRORS {
                        let rearmed = match self.listener.as_mut() {
                            Some(listener) => self
                                .reg
                                .reregister(listener, LISTENER, Interest::READABLE)
                                .is_ok(),
                            None => false,
                        };
                        unsafe {
                            ob_sql_nio_log_accept_state(
                                first_error_kind.unwrap(),
                                first_error_raw,
                                drain_id,
                                accepted,
                                transient_errors,
                                consecutive_errors,
                                if rearmed {
                                    ACCEPT_OUTCOME_REARMED
                                } else {
                                    ACCEPT_OUTCOME_REARM_FAILED
                                },
                            );
                        }
                        break;
                    }
                    continue;
                }
            };
            if sock.set_nodelay(true).is_err() {
                continue;
            }
            self.distribute(ConnStream::from(sock));
        }
    }

    #[cfg(all(unix, feature = "native-network"))]
    pub(crate) fn accept_local(&mut self) {
        loop {
            let listener = match self.unix_listener.as_ref() {
                Some(listener) => listener,
                None => return,
            };
            let (sock, _addr) = match listener.accept() {
                Ok(v) => v,
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(_) => break,
            };
            self.distribute(ConnStream::from(sock));
        }
    }

    #[cfg(all(windows, feature = "native-network"))]
    pub(crate) fn accept_local(&mut self) {
        let pending = match self.pending_pipe.take() {
            Some(p) => p,
            None => return,
        };
        if !matches!(pending.take_error(), Ok(None)) {
            self.rearm_pending();
            return;
        }
        let token = match self.conns.next_token() {
            Some(token) => token,
            None => {
                self.rearm_pending();
                return;
            }
        };
        let conn = self.make_conn(token, Stream::Pipe(pending));
        let promoted = {
            let mut g = conn.mu.lock().unwrap();
            self.reg
                .reregister(&mut g.sock, token, Interest::READABLE)
                .is_ok()
        };
        if !promoted {
            conn.err.store(true, Ordering::Release);
            conn.write_done.notify_all();
            let mut g = conn.mu.lock().unwrap();
            let _ = self.reg.deregister(&mut g.sock);
            drop(g);
            self.rearm_pending();
            return;
        }
        if self.admit(&conn, -1, 1, true) {
            self.conns.insert(token, conn);
        }
        self.rearm_pending();
    }

    #[cfg(all(windows, feature = "native-network"))]
    fn rearm_pending(&mut self) {
        let result =
            create_pipe_instance(&self.pipe_name).and_then(|pipe| self.try_arm_pending(pipe));
        if result.is_err() {
            self.pending_pipe = None;
        }
    }

    #[cfg(all(windows, feature = "native-network"))]
    fn try_arm_pending(&mut self, mut pipe: NamedPipe) -> std::io::Result<()> {
        self.reg.register(
            &mut pipe,
            LOCAL_LISTENER,
            Interest::READABLE | Interest::WRITABLE,
        )?;
        match pipe.connect() {
            Ok(()) => {}
            Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(err) => {
                let _ = self.reg.deregister(&mut pipe);
                return Err(err);
            }
        }
        self.pending_pipe = Some(pipe);
        Ok(())
    }

    #[cfg(feature = "native-network")]
    fn distribute(&mut self, stream: ConnStream) {
        let fanout = self.peers.len() + 1;
        let target = self.next_accept_target;
        self.next_accept_target = (self.next_accept_target + 1) % fanout;
        let mut stream = Some(stream);
        if target != 0 {
            for slot in 0..self.peers.len() {
                let idx = (target - 1 + slot) % self.peers.len();
                let peer = &self.peers[idx];
                let pushed = {
                    let mut queue = peer.incoming.lock().unwrap();
                    if queue.len() < HANDOFF_CAP {
                        queue.push(stream.take().expect("stream handed off once"));
                        true
                    } else {
                        false
                    }
                };
                if pushed {
                    let _ = peer.waker.wake();
                    break;
                }
            }
        }
        if let Some(stream) = stream.take() {
            self.admit_incoming(stream);
        }
    }

    fn admit_incoming(&mut self, stream: ConnStream) {
        let token = match self.conns.next_token() {
            Some(token) => token,
            None => return,
        };
        let fd = raw_fd(&stream);
        let is_unix = stream.is_local() as c_int;
        let conn = self.make_conn(token, stream);
        if self.admit(&conn, fd, is_unix, false) {
            self.conns.insert(token, conn);
        }
    }

    fn drain_incoming(&mut self) {
        loop {
            if self.stop.load(Ordering::Acquire) {
                break;
            }
            let sock = match self.incoming.lock().unwrap().pop() {
                Some(sock) => sock,
                None => break,
            };
            self.admit_incoming(sock);
        }
    }

    fn refresh_tcp_keepalive(&mut self) {
        let (revision, config) = self.keepalive.snapshot();
        if revision == self.keepalive_revision {
            return;
        }
        for conn in self.conns.values() {
            let guard = conn.mu.lock().unwrap();
            if let Err(err) = guard.sock.apply_tcp_keepalive(config) {
                eprintln!(
                    "sql-nio: refreshing TCP keepalive on fd {} failed: {err}",
                    raw_fd(&guard.sock)
                );
            }
        }
        self.keepalive_revision = revision;
    }

    pub(crate) fn on_readable_event(&self, token: Token) {
        let conn = match self.conns.get(&token) {
            Some(conn) => conn,
            None => return,
        };
        if conn.defer_read_if_busy() {
            conn.signal_mid_read();
            return;
        }
        let handled = pump(conn, self.handler);
        if !handled {
            mark_connection_error(conn);
        }
        if conn.err.load(Ordering::Acquire) || conn.peer_closed.load(Ordering::Acquire) {
            self.notify_disconnect(conn);
            request_close(conn);
        }
    }

    pub(crate) fn on_transport_closed(&self, token: Token, _is_error: bool) {
        let conn = match self.conns.get(&token) {
            Some(conn) => conn,
            None => return,
        };
        conn.peer_closed.store(true, Ordering::Release);
        cancel_connection_io(conn);
        self.notify_disconnect(conn);
    }

    fn drive_writable(&self, token: Token, expected_commit: Option<u64>, arm_on_block: bool) {
        let conn = match self.conns.get(&token) {
            Some(conn) => conn,
            None => return,
        };
        let (wake_blocking_writer, finished, deferred_completion, failed) = {
            let mut g = conn.mu.lock().unwrap();
            if expected_commit.is_some_and(|generation| {
                !valid_request_generation(conn, generation)
                    || !matches!(
                        g.final_response,
                        FinalResponseState::Committed(continuation)
                            if continuation.generation == generation
                    )
                    || g.output_is_empty()
            }) {
                return;
            }

            let drained = flush_buffer_locked(conn, &mut g);
            let mut failed = conn.err.load(Ordering::Acquire);
            if drained && g.want_write {
                g.want_write = false;
                if !failed
                    && conn
                        .reg
                        .reregister(&mut g.sock, token, Interest::READABLE)
                        .is_err()
                {
                    failed = true;
                    conn.err.store(true, Ordering::Release);
                    g.clear_output();
                }
            } else if !drained && arm_on_block && !g.want_write {
                if !failed
                    && conn
                        .reg
                        .reregister(&mut g.sock, token, Interest::READABLE | Interest::WRITABLE)
                        .is_ok()
                {
                    g.want_write = true;
                } else {
                    failed = true;
                    conn.err.store(true, Ordering::Release);
                    g.want_write = false;
                    g.clear_output();
                }
            }

            let completed = take_final_completion_locked(&mut g);
            let finished = completed
                .and_then(|generation| finish_worker_request_locked(conn, &mut g, generation));
            let deferred_completion = completed.filter(|_| finished.is_none());
            let wake_blocking_writer = g.transport_is_idle() && g.write_waiters > 0;
            (wake_blocking_writer, finished, deferred_completion, failed)
        };
        if wake_blocking_writer {
            conn.write_done.notify_all();
        }
        if let Some(finished) = finished {
            publish_finish(conn, finished);
        }
        if let Some(generation) = deferred_completion {
            conn.completions.lock().unwrap().push(QueuedCompletion {
                token: conn.token,
                generation,
            });
        }
        if failed {
            conn.write_done.notify_all();
            request_close(conn);
        }
    }

    pub(crate) fn on_writable(&self, token: Token) {
        self.drive_writable(token, None, false);
    }

    pub(crate) fn reap_closes(&mut self) {
        let tokens: Vec<Token> = std::mem::take(&mut *self.closes.lock().unwrap());
        for t in tokens {
            let request_is_busy = match self.conns.get(&t) {
                Some(conn) => {
                    self.notify_disconnect(conn);
                    conn.request_is_busy()
                }
                None => continue,
            };
            if request_is_busy {
                self.closes.lock().unwrap().push(t);
                continue;
            }
            self.close(t, 0);
        }
    }

    pub(crate) fn drain_commits(&mut self) {
        let mut commits = std::mem::take(&mut self.commit_batch);
        {
            let mut pending = self.commits.lock().unwrap();
            std::mem::swap(&mut commits, &mut *pending);
        }
        for commit in commits.drain(..) {
            self.drive_writable(commit.token, Some(commit.generation), true);
        }
        self.commit_batch = commits;
    }

    pub(crate) fn drain_completions(&mut self) {
        let completions: Vec<QueuedCompletion> =
            std::mem::take(&mut *self.completions.lock().unwrap());
        for completion in completions {
            let conn = match self.conns.get(&completion.token) {
                Some(conn) => conn,
                None => continue,
            };
            finish_worker_request(conn, completion.generation);
        }
    }

    pub(crate) fn drain_ready(&mut self) {
        let mut tokens = std::mem::take(&mut self.ready_batch);
        {
            let mut pending = self.ready.lock().unwrap();
            std::mem::swap(&mut tokens, &mut *pending);
        }
        for t in tokens.drain(..) {
            self.on_readable_event(t);
        }
        self.ready_batch = tokens;
    }

    pub(crate) fn close(&mut self, token: Token, err: c_int) {
        let conn = match self.conns.remove(&token) {
            Some(c) => c,
            None => return,
        };
        {
            let mut g = conn.mu.lock().unwrap();
            send_tls_close_notify_locked(&conn, &mut g);
            let _ = conn.reg.deregister(&mut g.sock);
            #[cfg(all(unix, feature = "native-network"))]
            unsafe {
                libc::shutdown(raw_fd(&g.sock), libc::SHUT_RDWR);
            }
            #[cfg(any(not(unix), feature = "memory-transport"))]
            let _ = g.sock.shutdown(std::net::Shutdown::Both);
        }
        self.notify_disconnect(&conn);
        self.notify_close(&conn, err);
        conn.write_done.notify_all();
        conn.signal_mid_read();
        self.retire_or_release(conn);
    }

    fn notify_disconnect(&self, conn: &Arc<Conn>) {
        if conn.session_constructed.load(Ordering::Acquire)
            && !conn.disconnect_notified.swap(true, Ordering::AcqRel)
        {
            unsafe { ob_sql_sock_handler_on_disconnect(self.handler.0, conn.sess()) };
        }
    }

    fn notify_close(&self, conn: &Arc<Conn>, err: c_int) {
        if conn.session_constructed.load(Ordering::Acquire)
            && !conn.close_notified.swap(true, Ordering::AcqRel)
        {
            unsafe { ob_sql_sock_handler_on_close(self.handler.0, conn.sess(), err) };
        }
    }

    fn retire_or_release(&mut self, conn: Arc<Conn>) {
        if !conn.sql_session_bound.load(Ordering::Acquire) {
            remove_conn_mapping(&conn);
        } else {
            self.retired.insert(conn.token, conn);
        }
    }

    pub(crate) fn reap_retired(&mut self) {
        let ready: Vec<Token> = self
            .retired
            .iter()
            .filter_map(|(&token, conn)| {
                (!conn.sql_session_bound.load(Ordering::Acquire)).then_some(token)
            })
            .collect();
        for token in ready {
            if let Some(conn) = self.retired.remove(&token) {
                remove_conn_mapping(&conn);
            }
        }
    }
}

#[cfg(all(unix, feature = "native-network"))]
fn bind_tcp_listener(addr: SocketAddr) -> std::io::Result<TcpListener> {
    use socket2::{Domain, Protocol, SockAddr, Socket, Type};

    let socket = Socket::new(Domain::for_address(addr), Type::STREAM, Some(Protocol::TCP))?;
    socket.set_reuse_address(true)?;
    if addr.is_ipv6() {
        socket.set_only_v6(true)?;
    }
    socket.set_nonblocking(true)?;
    socket.bind(&SockAddr::from(addr))?;
    socket.listen(1024)?;
    let listener: std::net::TcpListener = socket.into();
    Ok(TcpListener::from_std(listener))
}

#[cfg(all(not(unix), feature = "native-network"))]
fn bind_tcp_listener(addr: SocketAddr) -> std::io::Result<TcpListener> {
    TcpListener::bind(addr)
}

#[cfg(feature = "native-network")]
fn build_tls_server_config(
    tls: &NioTlsConfig,
) -> Result<Arc<rustls::ServerConfig>, Box<dyn std::error::Error>> {
    use rustls_pki_types::pem::PemObject;
    use rustls_pki_types::{CertificateDer, PrivateKeyDer};

    let path_of = |p: *const c_char| -> Result<&std::path::Path, Box<dyn std::error::Error>> {
        if p.is_null() {
            return Err("required TLS path is null".into());
        }
        Ok(Path::new(unsafe { CStr::from_ptr(p) }.to_str()?))
    };

    let certs: Vec<CertificateDer<'static>> =
        CertificateDer::pem_file_iter(path_of(tls.cert_file)?)?.collect::<Result<_, _>>()?;
    let key = PrivateKeyDer::from_pem_file(path_of(tls.key_file)?)?;
    let provider = Arc::new(rustls::crypto::ring::default_provider());
    // rustls supports TLS 1.2 and TLS 1.3.  TLSv1/TLSv1.1 remain accepted
    // configuration values for compatibility, but cannot lower rustls below
    // its TLS 1.2 floor.  TLSv1.3 is the only value that narrows the set.
    let builder = rustls::ServerConfig::builder_with_provider(provider.clone());
    let builder = if tls.min_tls_version == NIO_TLS_MIN_TLSV1_3 {
        builder.with_protocol_versions(&[&rustls::version::TLS13])?
    } else {
        builder.with_protocol_versions(&[&rustls::version::TLS13, &rustls::version::TLS12])?
    };
    let builder = if tls.ca_file.is_null() {
        builder.with_no_client_auth()
    } else {
        let mut roots = rustls::RootCertStore::empty();
        for cert in CertificateDer::pem_file_iter(path_of(tls.ca_file)?)? {
            roots.add(cert?)?;
        }
        let verifier =
            rustls::server::WebPkiClientVerifier::builder_with_provider(Arc::new(roots), provider)
                .allow_unauthenticated()
                .build()?;
        builder.with_client_cert_verifier(verifier)
    };
    Ok(Arc::new(builder.with_single_cert(certs, key)?))
}

fn write_start_err(out_err: *mut i32, reason: i32) {
    if !out_err.is_null() {
        unsafe { *out_err = reason };
    }
}

/// # Safety
/// `addr` is a valid C string; `handler` is the ObSqlSockHandler* the
/// ob_sql_sock_handler_on_* shims accept; `out_err` is null or points to
/// writable i32 storage.
pub unsafe fn nio_start(
    addr: *const c_char,
    handler: *mut ObSqlSockHandler,
    session_size: usize,
    thread_count: usize,
    tls: *const NioTlsConfig,
    out_err: *mut i32,
    disable_tcp: c_int,
) -> *mut NioReactor {
    unsafe {
        nio_start_in_dir(
            addr,
            handler,
            session_size,
            thread_count,
            tls,
            out_err,
            disable_tcp,
            Path::new(LOCAL_RUN_DIR),
        )
    }
}

#[allow(clippy::too_many_arguments)]
pub(crate) unsafe fn nio_start_in_dir(
    addr: *const c_char,
    handler: *mut ObSqlSockHandler,
    session_size: usize,
    thread_count: usize,
    tls: *const NioTlsConfig,
    out_err: *mut i32,
    disable_tcp: c_int,
    local_run_dir: &Path,
) -> *mut NioReactor {
    if addr.is_null()
        || session_size == 0
        || !(1..=MAX_IO_THREADS).contains(&thread_count)
        || !matches!(disable_tcp, 0 | 1)
    {
        write_start_err(out_err, NIO_START_EINVAL);
        return std::ptr::null_mut();
    }
    #[cfg(feature = "memory-transport")]
    let tls_config = {
        if !tls.is_null() || disable_tcp != 1 {
            write_start_err(
                out_err,
                if !tls.is_null() {
                    NIO_START_ETLS
                } else {
                    NIO_START_EINVAL
                },
            );
            return std::ptr::null_mut();
        }
        None
    };
    #[cfg(feature = "native-network")]
    let tls_config = match unsafe { tls.as_ref() } {
        None => None,
        Some(tls) => match build_tls_server_config(tls) {
            Ok(cfg) => Some(cfg),
            Err(err) => {
                eprintln!("sql-nio: TLS config rejected: {err}");
                write_start_err(out_err, NIO_START_ETLS);
                return std::ptr::null_mut();
            }
        },
    };
    let addr: SocketAddr = match unsafe { CStr::from_ptr(addr) }
        .to_str()
        .ok()
        .and_then(|s| s.parse().ok())
    {
        Some(a) => a,
        None => {
            write_start_err(out_err, NIO_START_EADDR);
            return std::ptr::null_mut();
        }
    };
    #[cfg(feature = "memory-transport")]
    let _ = (addr, local_run_dir); // Parsed for ABI consistency; no socket or file is opened.
    if handler.is_null() {
        write_start_err(out_err, NIO_START_ECALLBACKS);
        return std::ptr::null_mut();
    }
    let handler = Handler(handler);
    let cxx_connect_attempts = Arc::new(AtomicU64::new(0));
    let cxx_connect_returns = Arc::new(AtomicU64::new(0));
    let cxx_connect_rejects = Arc::new(AtomicU64::new(0));
    #[cfg(feature = "native-network")]
    let reactor_diagnostics = Arc::new(ReactorDiagnostics::new());

    let started = (|| -> std::io::Result<NioReactor> {
        let stop = Arc::new(AtomicBool::new(false));
        let keepalive = Arc::new(TcpKeepaliveState::new());
        #[cfg(feature = "native-network")]
        let mut listener = if disable_tcp == 0 {
            Some(bind_tcp_listener(addr)?)
        } else {
            None
        };
        #[cfg(feature = "memory-transport")]
        let (bound_tcp_port, local_endpoint) = (0, None);
        #[cfg(feature = "native-network")]
        let bound_tcp_port = if let Some(listener) = listener.as_ref() {
            u32::from(listener.local_addr()?.port())
        } else {
            0
        };
        #[cfg(all(unix, feature = "native-network"))]
        let (mut unix_listener, local_endpoint) = {
            let socket_path = local_run_dir.join(UNIX_SOCKET_NAME);
            let _ = std::fs::create_dir_all(local_run_dir);
            let _ = std::fs::remove_file(&socket_path);
            let l = UnixListener::bind(&socket_path)?;
            let guard = LocalEndpointGuard::new(socket_path);
            (Some(l), Some(guard))
        };
        #[cfg(all(windows, feature = "native-network"))]
        let (pipe_name, mut pending_discovery) = {
            let secs = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs())
                .unwrap_or(0);
            let bare = pipe_bare_name(std::process::id(), secs);
            let discovery_path = local_run_dir.join(PIPE_DISCOVERY_NAME);
            let staged_path = local_run_dir.join(format!(
                "{PIPE_DISCOVERY_NAME}.starting-{}",
                std::process::id()
            ));
            let _ = std::fs::create_dir_all(local_run_dir);
            let _ = std::fs::remove_file(&discovery_path);
            let _ = std::fs::remove_file(&staged_path);
            let wide: Vec<u16> = format!(r"\\.\pipe\{bare}")
                .encode_utf16()
                .chain(Some(0))
                .collect();
            let staged = match std::fs::write(&staged_path, bare.as_bytes()) {
                Ok(()) => Some((LocalEndpointGuard::new(staged_path), discovery_path)),
                Err(err) => return Err(err),
            };
            (Arc::new(wide), staged)
        };
        #[cfg(all(not(any(unix, windows)), feature = "native-network"))]
        let local_endpoint: Option<LocalEndpointGuard> = {
            let _ = local_run_dir;
            return Err(std::io::Error::new(
                std::io::ErrorKind::Unsupported,
                "local SQL-NIO endpoint is unsupported on this platform",
            ));
        };
        let mut ios = Vec::with_capacity(thread_count);
        let mut handoffs = Vec::with_capacity(thread_count);
        #[cfg(all(windows, feature = "native-network"))]
        let (pipe_startup_tx, pipe_startup_rx) = std::sync::mpsc::channel();
        for index in 0..thread_count {
            let poll = Poll::new()?;
            #[cfg(feature = "native-network")]
            if index == 0 {
                if let Some(listener) = listener.as_mut() {
                    poll.registry()
                        .register(listener, LISTENER, Interest::READABLE)?;
                }
                #[cfg(all(unix, feature = "native-network"))]
                if let Some(l) = unix_listener.as_mut() {
                    poll.registry()
                        .register(l, LOCAL_LISTENER, Interest::READABLE)?;
                }
            }
            let reg = Arc::new(poll.registry().try_clone()?);
            let waker = Arc::new(Waker::new(poll.registry(), WAKER)?);
            let reactor_polling = Arc::new(AtomicBool::new(false));
            let incoming = Arc::new(Mutex::new(Vec::new()));
            let io = EventLoop {
                poll,
                #[cfg(feature = "native-network")]
                listener: None,
                #[cfg(all(unix, feature = "native-network"))]
                unix_listener: if index == 0 {
                    unix_listener.take()
                } else {
                    None
                },
                #[cfg(all(windows, feature = "native-network"))]
                pipe_name: pipe_name.clone(),
                #[cfg(all(windows, feature = "native-network"))]
                pending_pipe: None,
                #[cfg(all(windows, feature = "native-network"))]
                pipe_startup: Some(pipe_startup_tx.clone()),
                incoming: incoming.clone(),
                peers: Vec::new(),
                #[cfg(feature = "native-network")]
                next_accept_target: 0,
                #[cfg(feature = "native-network")]
                accept_drain_id: 0,
                poll_id: 0,
                #[cfg(feature = "native-network")]
                listener_event_count: 0,
                #[cfg(feature = "native-network")]
                accept_calls: 0,
                #[cfg(feature = "native-network")]
                accepted_total: 0,
                #[cfg(feature = "native-network")]
                reactor_diagnostics: reactor_diagnostics.clone(),
                cxx_connect_attempts: cxx_connect_attempts.clone(),
                cxx_connect_returns: cxx_connect_returns.clone(),
                cxx_connect_rejects: cxx_connect_rejects.clone(),
                reg,
                waker: waker.clone(),
                reactor_polling,
                closes: Arc::new(Mutex::new(Vec::new())),
                ready: Arc::new(Mutex::new(Vec::new())),
                ready_batch: Vec::new(),
                commits: Arc::new(Mutex::new(Vec::new())),
                commit_batch: Vec::new(),
                completions: Arc::new(Mutex::new(Vec::new())),
                stop: stop.clone(),
                handler,
                session_size,
                tls_config: tls_config.clone(),
                keepalive: keepalive.clone(),
                keepalive_revision: 0,
                conns: ConnSlab::new(),
                retired: HashMap::new(),
            };
            handoffs.push(PeerHandoff {
                incoming,
                waker: waker.clone(),
            });
            ios.push((index, io, waker));
        }
        #[cfg(all(windows, feature = "native-network"))]
        drop(pipe_startup_tx);
        #[cfg(feature = "native-network")]
        {
            ios[0].1.listener = listener;
        }
        #[cfg(feature = "memory-transport")]
        let memory_peers = handoffs.clone();
        ios[0].1.peers = handoffs.split_off(1);

        let mut wakers = Vec::with_capacity(thread_count);
        let mut joins = Vec::with_capacity(thread_count);
        for (index, io, waker) in ios {
            wakers.push(waker);
            match thread::Builder::new()
                .name(format!("sql-nio-{index}"))
                .spawn(move || {
                    let outcome =
                        std::panic::catch_unwind(std::panic::AssertUnwindSafe(move || io.run()));
                    if outcome.is_err() {
                        std::process::abort();
                    }
                }) {
                Ok(join) => joins.push(join),
                Err(err) => {
                    stop.store(true, Ordering::Release);
                    for waker in &wakers {
                        let _ = waker.wake();
                    }
                    for join in joins {
                        let _ = join.join();
                    }
                    return Err(err);
                }
            }
        }
        #[cfg(all(windows, feature = "native-network"))]
        let local_endpoint = {
            let deadline = Instant::now() + Duration::from_secs(5);
            let mut armed_pipe_count = 0usize;
            for _ in 0..thread_count {
                let remaining = deadline.saturating_duration_since(Instant::now());
                match pipe_startup_rx.recv_timeout(remaining) {
                    Ok(true) => armed_pipe_count += 1,
                    Ok(false) => {}
                    Err(err) => {
                        stop.store(true, Ordering::Release);
                        for waker in &wakers {
                            let _ = waker.wake();
                        }
                        for join in joins.drain(..) {
                            let _ = join.join();
                        }
                        return Err(std::io::Error::new(
                            std::io::ErrorKind::TimedOut,
                            format!("named-pipe startup acknowledgement failed: {err}"),
                        ));
                    }
                }
            }
            if armed_pipe_count == 0 {
                pending_discovery.take();
                stop.store(true, Ordering::Release);
                for waker in &wakers {
                    let _ = waker.wake();
                }
                for join in joins.drain(..) {
                    let _ = join.join();
                }
                return Err(std::io::Error::new(
                    std::io::ErrorKind::AddrNotAvailable,
                    "no named-pipe instance could be armed",
                ));
            }
            match pending_discovery.take() {
                Some((staged, discovery_path)) => {
                    match std::fs::rename(&staged.path, &discovery_path) {
                        Ok(()) => {
                            staged.removed.store(true, Ordering::Release);
                            Some(LocalEndpointGuard::new(discovery_path))
                        }
                        Err(err) => {
                            stop.store(true, Ordering::Release);
                            for waker in &wakers {
                                let _ = waker.wake();
                            }
                            for join in joins.drain(..) {
                                let _ = join.join();
                            }
                            return Err(err);
                        }
                    }
                }
                None => None,
            }
        };
        Ok(NioReactor {
            wakers,
            stop,
            joins,
            #[cfg(feature = "native-network")]
            reactor_diagnostics,
            local_endpoint,
            keepalive,
            bound_tcp_port,
            #[cfg(feature = "memory-transport")]
            memory_peers,
            #[cfg(feature = "memory-transport")]
            next_memory_peer: AtomicUsize::new(0),
        })
    })();

    match started {
        Ok(e) => {
            write_start_err(out_err, NIO_START_OK);
            Box::into_raw(Box::new(e))
        }
        Err(_) => {
            write_start_err(out_err, NIO_START_EIO);
            std::ptr::null_mut()
        }
    }
}

impl NioReactor {
    #[cfg(feature = "native-network")]
    fn log_final_diagnostics(&self) {
        unsafe {
            ob_sql_nio_log_reactor_state(
                REACTOR_POLL_FINAL,
                self.reactor_diagnostics.poll_id.load(Ordering::Acquire),
                self.reactor_diagnostics.event_count.load(Ordering::Acquire),
                self.reactor_diagnostics
                    .listener_events
                    .load(Ordering::Acquire),
                self.reactor_diagnostics
                    .accept_calls
                    .load(Ordering::Acquire),
                self.reactor_diagnostics.accepted.load(Ordering::Acquire),
                self.reactor_diagnostics
                    .cxx_connect_attempts
                    .load(Ordering::Acquire),
                self.reactor_diagnostics
                    .cxx_connect_returns
                    .load(Ordering::Acquire),
                self.reactor_diagnostics
                    .cxx_connect_rejects
                    .load(Ordering::Acquire),
                self.reactor_diagnostics
                    .listener_event_flags
                    .load(Ordering::Acquire),
            );
        }
    }

    #[cfg(not(feature = "native-network"))]
    fn log_final_diagnostics(&self) {}
}

/// # Safety
/// `reactor` is null or a live handle.
pub unsafe fn nio_get_bound_tcp_port(reactor: *const NioReactor) -> u32 {
    unsafe { reactor.as_ref() }
        .map(|reactor| reactor.bound_tcp_port)
        .unwrap_or(0)
}

/// # Safety
/// `reactor` is null or a live handle.
pub unsafe fn nio_update_tcp_keepalive_params(
    reactor: *mut NioReactor,
    enabled: c_int,
    idle: u32,
    interval: u32,
    count: u32,
) -> c_int {
    let Some(reactor) = (unsafe { reactor.as_ref() }) else {
        return -1;
    };
    if !matches!(enabled, 0 | 1) || (enabled == 1 && (idle == 0 || interval == 0 || count == 0)) {
        return -1;
    }
    reactor.keepalive.update(TcpKeepaliveConfig {
        enabled: enabled == 1,
        idle,
        interval,
        count,
    });
    for waker in &reactor.wakers {
        let _ = waker.wake();
    }
    0
}

/// # Safety
/// `reactor` is null or a live handle.
pub unsafe fn nio_stop(reactor: *mut NioReactor) {
    if let Some(e) = unsafe { reactor.as_ref() } {
        if let Some(endpoint) = e.local_endpoint.as_ref() {
            endpoint.remove_once();
        }
        e.stop.store(true, Ordering::Release);
        for waker in &e.wakers {
            let _ = waker.wake();
        }
    }
}

/// # Safety
/// `reactor` is null or a live handle, used once.
pub unsafe fn nio_wait_destroy(reactor: *mut NioReactor) {
    if reactor.is_null() {
        return;
    }
    let mut e = unsafe { Box::from_raw(reactor) };
    e.stop.store(true, Ordering::Release);
    for waker in &e.wakers {
        let _ = waker.wake();
    }
    for join in e.joins.drain(..) {
        let _ = join.join();
    }
    e.log_final_diagnostics();
}
