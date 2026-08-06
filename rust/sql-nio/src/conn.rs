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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum ReadState {
    Login,
    Command,
    ChangeUserAuth { expected_seq: u8 },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum AuthOutcome {
    NoChange,
    AuthOk,
    AuthSwitchSent { expected_seq: u8 },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct RequestContinuation {
    pub(crate) generation: u64,
    pub(crate) auth_outcome: AuthOutcome,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum FinalResponseState {
    Idle,
    Staged { generation: u64 },
    Prepared(RequestContinuation),
    Committed(RequestContinuation),
    CompletionQueued(RequestContinuation),
}

pub(crate) struct ResponseWriter {
    pub(crate) active_generation: u64,
    pub(crate) next_mysql_seq: u8,
    pub(crate) batch: Vec<u8>,
}

impl ResponseWriter {
    pub(crate) fn new() -> Self {
        Self {
            active_generation: 0,
            next_mysql_seq: 0,
            batch: Vec::new(),
        }
    }

    pub(crate) fn begin(&mut self, generation: u64, next_mysql_seq: u8) -> bool {
        if generation == 0 || self.active_generation != 0 || !self.batch.is_empty() {
            return false;
        }
        self.active_generation = generation;
        self.next_mysql_seq = next_mysql_seq;
        true
    }

    pub(crate) fn is_active(&self, generation: u64) -> bool {
        generation != 0 && self.active_generation == generation
    }

    pub(crate) fn clear_batch(&mut self) {
        self.batch.clear();
        if self.batch.capacity() > RETAINED_RESPONSE_CAPACITY.saturating_mul(4) {
            self.batch.shrink_to(RETAINED_RESPONSE_CAPACITY);
        }
    }

    pub(crate) fn finish(&mut self, generation: u64) -> bool {
        let matched = self.is_active(generation);
        self.clear_batch();
        self.active_generation = 0;
        self.next_mysql_seq = 0;
        matched
    }

    pub(crate) fn repair_for_completion(&mut self, generation: u64) {
        self.clear_batch();
        self.active_generation = generation;
        self.next_mysql_seq = 0;
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct QueuedCompletion {
    pub(crate) token: Token,
    pub(crate) generation: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct FinishOutcome {
    pub(crate) buffered_input: bool,
    pub(crate) readable_edge: bool,
}

pub(crate) struct PacketBody {
    pub(crate) bytes: Vec<u8>,
    pub(crate) logical_len: usize,
}

impl PacketBody {
    pub(crate) fn new(mut bytes: Vec<u8>) -> Result<Self, ()> {
        let logical_len = bytes.len();
        bytes.try_reserve_exact(1).map_err(|_| ())?;
        bytes.push(0);
        Ok(Self { bytes, logical_len })
    }

    pub(crate) fn writable_view(&mut self) -> (*mut c_char, usize) {
        (self.bytes.as_mut_ptr() as *mut c_char, self.logical_len)
    }

    pub(crate) fn into_cleared_bytes(mut self) -> Vec<u8> {
        self.bytes.clear();
        self.bytes
    }
}

pub(crate) struct ActiveRequestBody {
    pub(crate) generation: u64,
    pub(crate) body: PacketBody,
    pub(crate) callback_active: bool,
    pub(crate) worker_released: bool,
}

impl ActiveRequestBody {
    pub(crate) fn new(generation: u64, bytes: Vec<u8>) -> Result<Self, ()> {
        Ok(Self {
            generation,
            body: PacketBody::new(bytes)?,
            callback_active: true,
            worker_released: false,
        })
    }

    pub(crate) fn writable_view(&mut self) -> (*mut c_char, usize) {
        self.body.writable_view()
    }
}

pub(crate) struct MidRequestBody {
    pub(crate) generation: u64,
    pub(crate) lease_id: u64,
    pub(crate) body: PacketBody,
}

#[derive(Default)]
pub(crate) struct MidReadSignal {
    pub(crate) epoch: u64,
    pub(crate) interrupted_generation: u64,
}

pub(crate) struct ConnInner {
    pub(crate) sock: ConnStream,
    pub(crate) tls: Option<Box<TlsSession>>,
    pub(crate) expected_login_seq: u8,
    pub(crate) tls_arm_pending: bool,
    pub(crate) inbuf: Vec<u8>,
    pub(crate) rpos: usize,
    pub(crate) drain_stopped_at_budget: bool,
    pub(crate) outbuf: Vec<u8>,
    pub(crate) out_pos: usize,
    pub(crate) want_write: bool,
    pub(crate) write_waiters: usize,
    pub(crate) final_response: FinalResponseState,
    pub(crate) response: ResponseWriter,
    pub(crate) read_state: ReadState,
    pub(crate) inner_buf: Vec<u8>,
    pub(crate) inner_rpos: usize,
    pub(crate) compressed_wire_bytes: u64,
    pub(crate) mid_request_body: Option<MidRequestBody>,
    pub(crate) next_mid_request_lease: u64,
    pub(crate) active_request_body: Option<ActiveRequestBody>,
    pub(crate) initial_body_spare: Vec<u8>,
    pub(crate) req_compressed: bool,
    pub(crate) next_comp_seq: u8,
    pub(crate) login: Option<ParsedLogin>,
}

impl ConnInner {
    pub(crate) fn compact_raw_input(&mut self) {
        compact_buf(&mut self.inbuf, &mut self.rpos);
    }

    pub(crate) fn compact_inner_input(&mut self) {
        compact_buf(&mut self.inner_buf, &mut self.inner_rpos);
    }

    pub(crate) fn consume_raw_input(&mut self, amount: usize) {
        consume_input(&mut self.inbuf, &mut self.rpos, amount);
    }

    pub(crate) fn consume_inner_input(&mut self, amount: usize) {
        consume_input(&mut self.inner_buf, &mut self.inner_rpos, amount);
    }

    pub(crate) fn output_is_empty(&self) -> bool {
        self.out_pos >= self.outbuf.len()
    }

    pub(crate) fn transport_is_idle(&self) -> bool {
        self.output_is_empty() && self.tls.as_ref().is_none_or(|t| !t.conn.wants_write())
    }

    pub(crate) fn tls_plaintext_buffered(&self) -> bool {
        self.tls.as_ref().is_some_and(|t| t.plaintext_pending > 0)
    }

    pub(crate) fn compact_output_for_append(&mut self) {
        compact_buf(&mut self.outbuf, &mut self.out_pos);
    }

    pub(crate) fn clear_transport_output(&mut self) {
        self.out_pos = 0;
        trim_empty_buf(&mut self.outbuf);
    }

    pub(crate) fn clear_output(&mut self) {
        self.clear_transport_output();
        self.response.clear_batch();
    }
}

pub(crate) struct Conn {
    pub(crate) token: Token,
    pub(crate) server_caps: u32,
    pub(crate) tls_config: Option<Arc<rustls::ServerConfig>>,
    pub(crate) mu: Mutex<ConnInner>,
    pub(crate) write_done: Condvar,
    pub(crate) mid_read_signal: Mutex<MidReadSignal>,
    pub(crate) mid_read_ready: Condvar,
    pub(crate) session_storage: CppSessionStorage,
    pub(crate) reg: Arc<Registry>,
    pub(crate) waker: Arc<Waker>,
    pub(crate) reactor_polling: Arc<AtomicBool>,
    pub(crate) closes: Arc<Mutex<Vec<Token>>>,
    pub(crate) ready: Arc<Mutex<Vec<Token>>>,
    pub(crate) commits: Arc<Mutex<Vec<QueuedCompletion>>>,
    pub(crate) completions: Arc<Mutex<Vec<QueuedCompletion>>>,
    pub(crate) err: AtomicBool,
    pub(crate) peer_closed: AtomicBool,
    pub(crate) need_shutdown: AtomicBool,
    pub(crate) sql_session_bound: AtomicBool,
    pub(crate) max_packet_size: AtomicUsize,
    pub(crate) raw_caps: AtomicU32,
    pub(crate) transport_caps: AtomicU32,
    pub(crate) session_constructed: AtomicBool,
    pub(crate) disconnect_notified: AtomicBool,
    pub(crate) close_notified: AtomicBool,
    pub(crate) close_requested: AtomicBool,
    pub(crate) request_gate: Mutex<()>,
    pub(crate) request_state: AtomicU8,
    pub(crate) request_generation: AtomicU64,
}

impl Conn {
    pub(crate) fn sess(&self) -> *mut c_void {
        self.session_storage.as_ptr()
    }

    pub(crate) fn request_is_busy(&self) -> bool {
        self.request_state.load(Ordering::Acquire) & REQUEST_BUSY != 0
    }

    pub(crate) fn defer_read_if_busy(&self) -> bool {
        if self.request_state.load(Ordering::Acquire) & REQUEST_BUSY == 0 {
            return false;
        }
        let previous = self
            .request_state
            .fetch_or(REQUEST_READ_READY, Ordering::AcqRel);
        if previous & REQUEST_BUSY != 0 {
            true
        } else {
            self.request_state
                .fetch_and(!REQUEST_READ_READY, Ordering::AcqRel);
            false
        }
    }

    pub(crate) fn begin_request(&self) {
        debug_assert_eq!(self.request_state.load(Ordering::Acquire), 0);
        self.request_state.store(REQUEST_BUSY, Ordering::Release);
    }

    pub(crate) fn finish_request(&self) -> bool {
        let previous = self
            .request_state
            .fetch_and(!(REQUEST_BUSY | REQUEST_READ_READY), Ordering::AcqRel);
        debug_assert_ne!(previous & REQUEST_BUSY, 0);
        previous & REQUEST_READ_READY != 0
    }

    pub(crate) fn abort_request(&self) {
        self.request_state.store(0, Ordering::Release);
    }

    pub(crate) fn signal_mid_read(&self) {
        let mut signal = self.mid_read_signal.lock().unwrap();
        signal.epoch = signal.epoch.wrapping_add(1);
        self.mid_read_ready.notify_all();
    }
}

pub(crate) fn request_close(conn: &Arc<Conn>) {
    let first = {
        let _gate = conn.request_gate.lock().unwrap();
        !conn.close_requested.swap(true, Ordering::AcqRel)
    };
    if first {
        conn.closes.lock().unwrap().push(conn.token);
    }
    conn.signal_mid_read();
    let _ = conn.waker.wake();
}

pub(crate) fn publish_shutdown(conn: &Arc<Conn>) {
    let _gate = conn.request_gate.lock().unwrap();
    conn.need_shutdown.store(true, Ordering::Release);
    conn.signal_mid_read();
}

pub(crate) fn mark_connection_error(conn: &Arc<Conn>) {
    conn.err.store(true, Ordering::Release);
    conn.write_done.notify_all();
    request_close(conn);
}

pub(crate) fn cancel_connection_io(conn: &Arc<Conn>) {
    let async_completed = {
        let mut g = conn.mu.lock().unwrap();
        conn.err.store(true, Ordering::Release);
        g.clear_output();
        take_final_completion_locked(&mut g)
    };
    conn.write_done.notify_all();
    if let Some(generation) = async_completed {
        conn.completions.lock().unwrap().push(QueuedCompletion {
            token: conn.token,
            generation,
        });
    }
    request_close(conn);
}

pub(crate) fn packet_limit(conn: &Arc<Conn>) -> usize {
    conn.max_packet_size
        .load(Ordering::Acquire)
        .clamp(1, MAX_LOGICAL_PAYLOAD)
}

pub(crate) fn mysql_wire_budget(payload_limit: usize) -> usize {
    let payload_limit = payload_limit.min(MAX_LOGICAL_PAYLOAD);
    let frame_count = payload_limit / MAX_PAYLOAD + 1;
    payload_limit.saturating_add(frame_count.saturating_mul(HEADER_SIZE))
}

pub(crate) fn raw_input_budget(conn: &Arc<Conn>, compressed: bool) -> usize {
    if compressed {
        COMPRESS_HEADER_SIZE.saturating_add(MAX_PAYLOAD)
    } else {
        mysql_wire_budget(packet_limit(conn))
    }
}

pub(crate) fn trim_empty_buf(buf: &mut Vec<u8>) {
    buf.clear();
    if buf.capacity() > RETAINED_INPUT_CAPACITY.saturating_mul(4) {
        buf.shrink_to(RETAINED_INPUT_CAPACITY);
    }
}

pub(crate) fn compact_buf(buf: &mut Vec<u8>, pos: &mut usize) {
    *pos = (*pos).min(buf.len());
    if *pos == buf.len() {
        *pos = 0;
        trim_empty_buf(buf);
    } else if *pos >= RETAINED_INPUT_CAPACITY && *pos >= buf.len() / 2 {
        let remain = buf.len() - *pos;
        buf.copy_within(*pos.., 0);
        buf.truncate(remain);
        *pos = 0;
        let retained = RETAINED_INPUT_CAPACITY.max(remain);
        if buf.capacity() > retained.saturating_mul(4) {
            buf.shrink_to(retained);
        }
    }
}

pub(crate) fn consume_input(buf: &mut Vec<u8>, pos: &mut usize, amount: usize) {
    let available = buf.len().saturating_sub(*pos);
    *pos += amount.min(available);
    compact_buf(buf, pos);
}

/// # Safety
/// `sess` is a live C++ session pointer.
#[no_mangle]
pub unsafe extern "C" fn nio_set_shutdown(sess: *mut c_void, generation: u64) -> c_int {
    if let Some(conn) = conn_of(sess) {
        let g = conn.mu.lock().unwrap();
        let still_owned = match g.final_response {
            FinalResponseState::Idle => true,
            FinalResponseState::Staged {
                generation: staged_generation,
            } => staged_generation == generation,
            FinalResponseState::Prepared(_)
            | FinalResponseState::Committed(_)
            | FinalResponseState::CompletionQueued(_) => false,
        };
        if still_owned && valid_request_generation(&conn, generation) {
            publish_shutdown(&conn);
            return 0;
        }
    }
    -1
}

/// # Safety
/// `sess` is a live C++ session pointer.
#[no_mangle]
pub unsafe extern "C" fn nio_shutdown(sess: *mut c_void) {
    if let Some(conn) = conn_of(sess) {
        publish_shutdown(&conn);
        let mut g = conn.mu.lock().unwrap();
        send_tls_close_notify_locked(&conn, &mut g);
        let _ = g.sock.shutdown(std::net::Shutdown::Read);
        drop(g);
        cancel_connection_io(&conn);
    }
}

/// # Safety
/// `sess` is a live C++ session pointer.
#[no_mangle]
pub unsafe extern "C" fn nio_bind_sql_session(sess: *mut c_void) {
    if let Some(conn) = conn_of(sess) {
        conn.max_packet_size
            .store(MAX_LOGICAL_PAYLOAD, Ordering::Release);
        conn.sql_session_bound.store(true, Ordering::Release);
    }
}

/// # Safety
/// `sess` is a live C++ session pointer.
#[no_mangle]
pub unsafe extern "C" fn nio_release_sql_session(sess: *mut c_void) -> c_int {
    if let Some(conn) = conn_of_live_or_retired(sess) {
        conn.sql_session_bound.store(false, Ordering::Release);
        let _ = conn.waker.wake();
        0
    } else {
        -1
    }
}
