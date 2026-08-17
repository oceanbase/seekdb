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
#[cfg(unix)]
pub(crate) const UNIX_SOCKET_NAME: &str = "sql.sock";
#[cfg(windows)]
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

pub struct Reactor {
    pub(crate) wakers: Vec<Arc<Waker>>,
    stop: Arc<AtomicBool>,
    pub(crate) joins: Vec<JoinHandle<()>>,
    local_endpoint: Option<LocalEndpointGuard>,
    keepalive: Arc<TcpKeepaliveState>,
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

pub(crate) struct PeerHandoff {
    incoming: Arc<Mutex<Vec<ConnStream>>>,
    waker: Arc<Waker>,
}

const HANDOFF_CAP: usize = 1024;

pub(crate) struct EventLoop {
    pub(crate) poll: Poll,
    pub(crate) listener: Option<TcpListener>,
    #[cfg(unix)]
    pub(crate) unix_listener: Option<UnixListener>,
    #[cfg(windows)]
    pub(crate) pipe_name: Arc<Vec<u16>>,
    #[cfg(windows)]
    pub(crate) pending_pipe: Option<NamedPipe>,
    #[cfg(windows)]
    pub(crate) pipe_startup: Option<std::sync::mpsc::Sender<bool>>,
    pub(crate) incoming: Arc<Mutex<Vec<ConnStream>>>,
    pub(crate) peers: Vec<PeerHandoff>,
    pub(crate) next_accept_target: usize,
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
    pub(crate) cb: NioCallbacks,
    pub(crate) session_size: usize,
    pub(crate) tls_config: Option<Arc<rustls::ServerConfig>>,
    pub(crate) keepalive: Arc<TcpKeepaliveState>,
    pub(crate) keepalive_revision: u64,
    pub(crate) conns: ConnSlab<Arc<Conn>>,
    pub(crate) retired: HashMap<Token, Arc<Conn>>,
}

impl EventLoop {
    pub(crate) fn run(mut self) {
        let mut events = Events::with_capacity(512);
        let mut stopping = false;
        loop {
            if !stopping && self.stop.load(Ordering::Acquire) {
                self.begin_shutdown();
                stopping = true;
            }
            if stopping && self.conns.is_empty() && self.retired.is_empty() {
                break;
            }
            #[cfg(windows)]
            if self.pending_pipe.is_none() && !stopping {
                self.rearm_pending();
            }
            #[cfg(windows)]
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
            self.reactor_polling.store(false, Ordering::Release);
            if !stopping {
                self.refresh_tcp_keepalive();
            }
            if !stopping && self.stop.load(Ordering::Acquire) {
                self.begin_shutdown();
                stopping = true;
            }
            if poll_failed && !stopping {
                continue;
            }
            if !skip_poll {
                for event in events.iter() {
                    if stopping {
                        continue;
                    }
                    match event.token() {
                        WAKER => {}
                        LISTENER => self.accept_all(),
                        #[cfg(any(unix, windows))]
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
        }
    }

    pub(crate) fn begin_shutdown(&mut self) {
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
        let rejected = match self.cb.on_connect {
            Some(on_connect) => {
                let rc = on_connect(self.cb.ctx, sess, fd, is_unix, &mut greeting);
                conn.session_constructed.store(true, Ordering::Release);
                rc != 0
            }
            None => true,
        };
        if rejected || conn.err.load(Ordering::Acquire) || !send_greeting(conn, &greeting) {
            self.abort_admission(conn, preregistered);
            return false;
        }
        if preregistered {
            return true;
        }
        let registered = {
            let mut g = conn.mu.lock().unwrap();
            self.reg
                .register(&mut g.sock, conn.token, Interest::READABLE)
                .is_ok()
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

    pub(crate) fn accept_all(&mut self) {
        loop {
            let listener = match self.listener.as_ref() {
                Some(listener) => listener,
                None => return,
            };
            let (sock, _addr) = match listener.accept() {
                Ok(v) => v,
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(_) => break,
            };
            if sock.set_nodelay(true).is_err() {
                continue;
            }
            self.distribute(ConnStream::from(sock));
        }
    }

    #[cfg(unix)]
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

    #[cfg(windows)]
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

    #[cfg(windows)]
    fn rearm_pending(&mut self) {
        let result =
            create_pipe_instance(&self.pipe_name).and_then(|pipe| self.try_arm_pending(pipe));
        if result.is_err() {
            self.pending_pipe = None;
        }
    }

    #[cfg(windows)]
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
        let handled = pump(conn, self.cb);
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
            #[cfg(unix)]
            unsafe {
                libc::shutdown(raw_fd(&g.sock), libc::SHUT_RDWR);
            }
            #[cfg(not(unix))]
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
            if let Some(on_disconnect) = self.cb.on_disconnect {
                on_disconnect(self.cb.ctx, conn.sess());
            }
        }
    }

    fn notify_close(&self, conn: &Arc<Conn>, err: c_int) {
        if conn.session_constructed.load(Ordering::Acquire)
            && !conn.close_notified.swap(true, Ordering::AcqRel)
        {
            if let Some(on_close) = self.cb.on_close {
                on_close(self.cb.ctx, conn.sess(), err);
            }
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

#[cfg(unix)]
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

#[cfg(not(unix))]
fn bind_tcp_listener(addr: SocketAddr) -> std::io::Result<TcpListener> {
    TcpListener::bind(addr)
}

pub(crate) const NIO_START_OK: i32 = 0;
pub(crate) const NIO_START_EINVAL: i32 = 1;
pub(crate) const NIO_START_EABI: i32 = 2;
pub(crate) const NIO_START_EADDR: i32 = 3;
pub(crate) const NIO_START_ECALLBACKS: i32 = 4;
pub(crate) const NIO_START_EIO: i32 = 5;
pub(crate) const NIO_START_ETLS: i32 = 6;
pub(crate) const NIO_TLS_MIN_TLSV1_3: u8 = 4;

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
/// `addr` is a valid C string; `cb` points to a valid callbacks struct;
/// `out_err` is null or points to writable i32 storage.
#[no_mangle]
pub unsafe extern "C" fn nio_start(
    addr: *const c_char,
    abi_version: u32,
    cb: *const NioCallbacks,
    callbacks_size: usize,
    session_size: usize,
    thread_count: usize,
    tls: *const NioTlsConfig,
    tls_size: usize,
    out_err: *mut i32,
    disable_tcp: c_int,
) -> *mut Reactor {
    unsafe {
        nio_start_in_dir(
            addr,
            abi_version,
            cb,
            callbacks_size,
            session_size,
            thread_count,
            tls,
            tls_size,
            out_err,
            disable_tcp,
            Path::new(LOCAL_RUN_DIR),
        )
    }
}

#[allow(clippy::too_many_arguments)]
pub(crate) unsafe fn nio_start_in_dir(
    addr: *const c_char,
    abi_version: u32,
    cb: *const NioCallbacks,
    callbacks_size: usize,
    session_size: usize,
    thread_count: usize,
    tls: *const NioTlsConfig,
    tls_size: usize,
    out_err: *mut i32,
    disable_tcp: c_int,
    local_run_dir: &Path,
) -> *mut Reactor {
    if abi_version != NIO_ABI_VERSION {
        write_start_err(out_err, NIO_START_EABI);
        return std::ptr::null_mut();
    }
    if addr.is_null()
        || cb.is_null()
        || callbacks_size != std::mem::size_of::<NioCallbacks>()
        || session_size == 0
        || !(1..=MAX_IO_THREADS).contains(&thread_count)
        || !matches!(disable_tcp, 0 | 1)
        || (tls.is_null() && tls_size != 0)
        || (!tls.is_null() && tls_size != std::mem::size_of::<NioTlsConfig>())
    {
        write_start_err(out_err, NIO_START_EINVAL);
        return std::ptr::null_mut();
    }
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
    let cb = unsafe { *cb };
    if cb.ctx.is_null()
        || cb.on_connect.is_none()
        || cb.on_readable.is_none()
        || cb.on_disconnect.is_none()
        || cb.on_close.is_none()
    {
        write_start_err(out_err, NIO_START_ECALLBACKS);
        return std::ptr::null_mut();
    }

    let started = (|| -> std::io::Result<Reactor> {
        let stop = Arc::new(AtomicBool::new(false));
        let keepalive = Arc::new(TcpKeepaliveState::new());
        let mut listener = if disable_tcp == 0 {
            Some(bind_tcp_listener(addr)?)
        } else {
            None
        };
        #[cfg(unix)]
        let (mut unix_listener, local_endpoint) = {
            let socket_path = local_run_dir.join(UNIX_SOCKET_NAME);
            let _ = std::fs::create_dir_all(local_run_dir);
            let _ = std::fs::remove_file(&socket_path);
            match UnixListener::bind(&socket_path) {
                Ok(l) => {
                    let guard = LocalEndpointGuard::new(socket_path);
                    (Some(l), Some(guard))
                }
                Err(err) if disable_tcp != 0 => return Err(err),
                Err(err) => {
                    eprintln!(
                        "sql-nio: local endpoint {} unavailable ({err}); serving TCP only",
                        socket_path.display(),
                    );
                    (None, None)
                }
            }
        };
        #[cfg(windows)]
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
                Err(err) if disable_tcp != 0 => return Err(err),
                Err(err) => {
                    eprintln!(
                        "sql-nio: pipe discovery staging unavailable ({err}); serving TCP only",
                    );
                    None
                }
            };
            (Arc::new(wide), staged)
        };
        #[cfg(not(any(unix, windows)))]
        let local_endpoint: Option<LocalEndpointGuard> = {
            let _ = local_run_dir;
            if disable_tcp != 0 {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::Unsupported,
                    "local-only SQL-NIO has no transport on this platform",
                ));
            }
            None
        };
        let mut ios = Vec::with_capacity(thread_count);
        let mut handoffs = Vec::with_capacity(thread_count);
        #[cfg(windows)]
        let (pipe_startup_tx, pipe_startup_rx) = std::sync::mpsc::channel();
        for index in 0..thread_count {
            let poll = Poll::new()?;
            if index == 0 {
                if let Some(listener) = listener.as_mut() {
                    poll.registry()
                        .register(listener, LISTENER, Interest::READABLE)?;
                }
                #[cfg(unix)]
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
                listener: None,
                #[cfg(unix)]
                unix_listener: if index == 0 {
                    unix_listener.take()
                } else {
                    None
                },
                #[cfg(windows)]
                pipe_name: pipe_name.clone(),
                #[cfg(windows)]
                pending_pipe: None,
                #[cfg(windows)]
                pipe_startup: Some(pipe_startup_tx.clone()),
                incoming: incoming.clone(),
                peers: Vec::new(),
                next_accept_target: 0,
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
                cb,
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
        #[cfg(windows)]
        drop(pipe_startup_tx);
        ios[0].1.listener = listener;
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
        #[cfg(windows)]
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
                if disable_tcp != 0 {
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
            }
            match pending_discovery.take() {
                Some((staged, discovery_path)) => {
                    match std::fs::rename(&staged.path, &discovery_path) {
                        Ok(()) => {
                            staged.removed.store(true, Ordering::Release);
                            Some(LocalEndpointGuard::new(discovery_path))
                        }
                        Err(err) if disable_tcp != 0 => {
                            stop.store(true, Ordering::Release);
                            for waker in &wakers {
                                let _ = waker.wake();
                            }
                            for join in joins.drain(..) {
                                let _ = join.join();
                            }
                            return Err(err);
                        }
                        Err(err) => {
                            eprintln!(
                                "sql-nio: pipe discovery publication failed ({err}); serving TCP only",
                            );
                            None
                        }
                    }
                }
                None => None,
            }
        };
        Ok(Reactor {
            wakers,
            stop,
            joins,
            local_endpoint,
            keepalive,
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

/// # Safety
/// `reactor` is null or a live handle.
#[no_mangle]
pub unsafe extern "C" fn nio_update_tcp_keepalive_params(
    reactor: *mut Reactor,
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
#[no_mangle]
pub unsafe extern "C" fn nio_stop(reactor: *mut Reactor) {
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
#[no_mangle]
pub unsafe extern "C" fn nio_wait_destroy(reactor: *mut Reactor) {
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
}
