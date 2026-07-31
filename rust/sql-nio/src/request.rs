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

pub(crate) fn next_request_ticket() -> Option<u64> {
    NEXT_REQUEST_TICKET
        .fetch_update(Ordering::AcqRel, Ordering::Acquire, |current| {
            (current < usize::MAX as u64).then(|| current + 1)
        })
        .ok()
}

pub(crate) fn begin_worker_request(conn: &Arc<Conn>, allow_current_peer_eof: bool) -> Option<u64> {
    let _gate = conn.request_gate.lock().unwrap();
    if conn.close_requested.load(Ordering::Acquire)
        || conn.need_shutdown.load(Ordering::Acquire)
        || conn.err.load(Ordering::Acquire)
        || conn.request_is_busy()
        || (!allow_current_peer_eof && conn.peer_closed.load(Ordering::Acquire))
    {
        return None;
    }
    let generation = next_request_ticket()?;
    conn.request_generation.store(generation, Ordering::Release);
    conn.begin_request();
    Some(generation)
}

pub(crate) fn install_active_request_body(
    conn: &Arc<Conn>,
    generation: u64,
    next_mysql_seq: u8,
    body: Vec<u8>,
) -> Option<(*mut c_char, usize)> {
    let active = ActiveRequestBody::new(generation, body).ok()?;
    let mut g = conn.mu.lock().unwrap();
    if !valid_request_generation(conn, generation) || g.active_request_body.is_some() {
        return None;
    }
    if !g.response.begin(generation, next_mysql_seq) {
        return None;
    }
    g.active_request_body = Some(active);
    Some(
        g.active_request_body
            .as_mut()
            .expect("active request body was just installed")
            .writable_view(),
    )
}

pub(crate) fn recycle_initial_request_body_locked(
    conn: &Conn,
    g: &mut ConnInner,
    active: ActiveRequestBody,
) -> Vec<u8> {
    let bytes = active.body.into_cleared_bytes();
    let reusable = bytes.capacity() <= RETAINED_INPUT_CAPACITY
        && !conn.err.load(Ordering::Acquire)
        && !conn.peer_closed.load(Ordering::Acquire)
        && !conn.need_shutdown.load(Ordering::Acquire)
        && !conn.close_requested.load(Ordering::Acquire);
    if reusable && bytes.capacity() > g.initial_body_spare.capacity() {
        std::mem::replace(&mut g.initial_body_spare, bytes)
    } else {
        bytes
    }
}

pub(crate) fn finish_active_request_callback(
    conn: &Arc<Conn>,
    generation: u64,
    delivered: bool,
) -> bool {
    let mut g = conn.mu.lock().unwrap();
    let matched = g
        .active_request_body
        .as_ref()
        .is_some_and(|active| active.generation == generation);
    let should_clear = if let Some(active) = g.active_request_body.as_mut() {
        if active.generation == generation {
            active.callback_active = false;
            !delivered || active.worker_released
        } else {
            false
        }
    } else {
        false
    };
    let released_body = if should_clear {
        g.active_request_body.take()
    } else {
        None
    };
    let released_body = released_body.map(|active| {
        if delivered {
            recycle_initial_request_body_locked(conn, &mut g, active)
        } else {
            active.body.into_cleared_bytes()
        }
    });
    let released_mid_body = if !delivered {
        g.login = None;
        if valid_request_generation(conn, generation) {
            let released = take_mid_request_body_for_generation_locked(&mut g, generation);
            let _ = g.response.finish(generation);
            conn.abort_request();
            released
        } else {
            None
        }
    } else {
        None
    };
    drop(g);
    drop(released_body);
    drop(released_mid_body);
    matched
}

pub(crate) fn release_active_request_body_locked(
    g: &mut ConnInner,
    generation: u64,
) -> Option<ActiveRequestBody> {
    let should_clear = if let Some(active) = g.active_request_body.as_mut() {
        if active.generation == generation {
            if active.callback_active {
                active.worker_released = true;
                false
            } else {
                true
            }
        } else {
            debug_assert_eq!(active.generation, generation);
            false
        }
    } else {
        false
    };
    if should_clear {
        g.active_request_body.take()
    } else {
        None
    }
}

pub(crate) fn next_mid_request_lease_id(g: &mut ConnInner) -> u64 {
    g.next_mid_request_lease = g.next_mid_request_lease.wrapping_add(1);
    if g.next_mid_request_lease == 0 {
        g.next_mid_request_lease = 1;
    }
    g.next_mid_request_lease
}

pub(crate) fn take_mid_request_body_for_generation_locked(
    g: &mut ConnInner,
    generation: u64,
) -> Option<MidRequestBody> {
    let matched = g
        .mid_request_body
        .as_ref()
        .is_some_and(|active| active.generation == generation);
    if matched {
        g.mid_request_body.take()
    } else {
        debug_assert!(g.mid_request_body.is_none());
        None
    }
}

pub(crate) fn deliver_decoded_packet(
    conn: &Arc<Conn>,
    cb: NioCallbacks,
    packet: DecodedPacket,
    packet_kind: c_int,
    command_view: Option<NioMysqlCommandView>,
    current_peer_eof: bool,
) -> bool {
    let generation = match begin_worker_request(conn, current_peer_eof) {
        Some(generation) => generation,
        None => {
            request_close(conn);
            return true;
        }
    };
    if current_peer_eof {
        request_close(conn);
    }
    let next_mysql_seq = packet.seq.wrapping_add(1);
    let (body, body_len) =
        match install_active_request_body(conn, generation, next_mysql_seq, packet.body) {
            Some(view) => view,
            None => {
                finish_active_request_callback(conn, generation, false);
                mark_connection_error(conn);
                return true;
            }
        };
    let delivered = deliver_to_cpp(
        cb,
        conn.sess(),
        body,
        body_len,
        packet.wire_bytes,
        packet_kind,
        command_view.as_ref(),
        generation,
    );
    let matched = finish_active_request_callback(conn, generation, delivered);
    if !delivered || !matched {
        mark_connection_error(conn);
    }
    true
}

pub(crate) fn valid_request_generation(conn: &Conn, generation: u64) -> bool {
    generation != 0
        && conn.request_is_busy()
        && conn.request_generation.load(Ordering::Acquire) == generation
}

pub(crate) fn valid_mid_read_generation_locked(
    g: &ConnInner,
    conn: &Conn,
    generation: u64,
) -> bool {
    g.final_response == FinalResponseState::Idle && valid_request_generation(conn, generation)
}

pub(crate) fn mid_read_failed(conn: &Conn) -> bool {
    conn.err.load(Ordering::Acquire)
        || conn.peer_closed.load(Ordering::Acquire)
        || conn.need_shutdown.load(Ordering::Acquire)
        || conn.close_requested.load(Ordering::Acquire)
}

pub(crate) fn flush_buffer_locked(conn: &Arc<Conn>, g: &mut ConnInner) -> bool {
    if g.tls.is_some() {
        flush_tls_locked(conn, g)
    } else {
        flush_plain_locked(conn, g)
    }
}

pub(crate) fn flush_plain_locked(conn: &Arc<Conn>, g: &mut ConnInner) -> bool {
    while !g.output_is_empty() {
        let result = {
            let start = g.out_pos;
            let sock = &mut g.sock;
            let outbuf = &g.outbuf;
            sock.write(&outbuf[start..])
        };
        match result {
            Ok(0) => break,
            Ok(n) => g.out_pos += n,
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => return fail_flush_locked(conn, g),
        }
    }
    let drained = g.output_is_empty();
    if drained {
        g.clear_transport_output();
    }
    drained
}

pub(crate) fn fail_flush_locked(conn: &Arc<Conn>, g: &mut ConnInner) -> bool {
    conn.err.store(true, Ordering::Release);
    g.clear_output();
    conn.write_done.notify_all();
    request_close(conn);
    true
}

pub(crate) fn flush_request(conn: &Arc<Conn>, generation: u64) -> Option<bool> {
    let mut g = conn.mu.lock().unwrap();
    valid_request_generation(conn, generation).then(|| flush_buffer_locked(conn, &mut g))
}

/// # Safety
/// `sess` is a live C++ session pointer; out-params are valid pointers.
pub(crate) unsafe fn read_buffered_packet(
    sess: *mut c_void,
    generation: u64,
    body: *mut *mut c_char,
    body_len: *mut i64,
    packet_lease: *mut u64,
) -> c_int {
    if body.is_null() || body_len.is_null() || packet_lease.is_null() {
        return -1;
    }
    let conn = match conn_of(sess) {
        Some(c) => c,
        None => return -1,
    };
    let mut g = conn.mu.lock().unwrap();
    if conn.err.load(Ordering::Acquire) || !valid_mid_read_generation_locked(&g, &conn, generation)
    {
        return -1;
    }
    if !g.response.is_active(generation) {
        return -1;
    }
    unsafe {
        *body = std::ptr::null_mut();
        *body_len = 0;
        *packet_lease = 0;
    }
    if g.mid_request_body.is_some() {
        return -1;
    }
    let protocol = WireProtocol::current_request(&g);
    let expected_seq = g.response.next_mysql_seq;
    let parsed = match protocol.read_packet_locked(&mut g, &conn, Some(expected_seq)) {
        Ok(packet) => packet,
        Err(()) => {
            mark_connection_error(&conn);
            return -1;
        }
    };
    if conn.peer_closed.load(Ordering::Acquire) {
        request_close(&conn);
    }
    match parsed {
        Some(packet) => {
            let seq = packet.seq;
            let packet_body = match PacketBody::new(packet.body) {
                Ok(body) => body,
                Err(()) => {
                    mark_connection_error(&conn);
                    return -1;
                }
            };
            let lease_id = next_mid_request_lease_id(&mut g);
            g.response.next_mysql_seq = seq.wrapping_add(1);
            g.mid_request_body = Some(MidRequestBody {
                generation,
                lease_id,
                body: packet_body,
            });
            let (body_ptr, logical_len) = g
                .mid_request_body
                .as_mut()
                .expect("mid-request body was just installed")
                .body
                .writable_view();
            unsafe {
                *body = body_ptr;
                *body_len = logical_len as i64;
                *packet_lease = lease_id;
            }
            1
        }
        None => {
            if conn.err.load(Ordering::Acquire) || conn.peer_closed.load(Ordering::Acquire) {
                -1
            } else {
                0
            }
        }
    }
}

/// # Safety
/// `sess` and `generation` identify a live request; out-params are valid
/// pointers for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn nio_wait_one_packet(
    sess: *mut c_void,
    generation: u64,
    timeout_us: i64,
    body: *mut *mut c_char,
    body_len: *mut i64,
    packet_lease: *mut u64,
) -> c_int {
    if body.is_null() || body_len.is_null() || packet_lease.is_null() {
        return -1;
    }
    let conn = match conn_of(sess) {
        Some(conn) => conn,
        None => return -1,
    };
    {
        let g = conn.mu.lock().unwrap();
        if mid_read_failed(&conn) || !valid_mid_read_generation_locked(&g, &conn, generation) {
            return -1;
        }
        if !g.response.is_active(generation) {
            return -1;
        }
        unsafe {
            *body = std::ptr::null_mut();
            *body_len = 0;
            *packet_lease = 0;
        }
    }
    let deadline = if timeout_us < 0 {
        None
    } else {
        match Instant::now().checked_add(Duration::from_micros(timeout_us as u64)) {
            Some(deadline) => Some(deadline),
            None => return -1,
        }
    };
    let mut observed_epoch = conn.mid_read_signal.lock().unwrap().epoch;

    loop {
        if mid_read_failed(&conn) {
            return -1;
        }
        let rc = unsafe { read_buffered_packet(sess, generation, body, body_len, packet_lease) };
        discharge_tls_arm(&conn);
        if rc != 0 {
            if rc == 1 {
                let mut signal = conn.mid_read_signal.lock().unwrap();
                if signal.interrupted_generation == generation {
                    signal.interrupted_generation = 0;
                }
            }
            return rc;
        }

        let mut signal = conn.mid_read_signal.lock().unwrap();
        if signal.interrupted_generation == generation {
            signal.interrupted_generation = 0;
            return 0;
        }
        if mid_read_failed(&conn) || !valid_request_generation(&conn, generation) {
            return -1;
        }
        let remaining = if let Some(deadline) = deadline {
            let now = Instant::now();
            if now >= deadline {
                return 0;
            }
            Some(deadline.saturating_duration_since(now))
        } else {
            None
        };
        if signal.epoch != observed_epoch {
            observed_epoch = signal.epoch;
            drop(signal);
            continue;
        }
        if let Some(remaining) = remaining {
            let (next_signal, _) = conn.mid_read_ready.wait_timeout(signal, remaining).unwrap();
            observed_epoch = next_signal.epoch;
            drop(next_signal);
        } else {
            let next_signal = conn.mid_read_ready.wait(signal).unwrap();
            observed_epoch = next_signal.epoch;
            drop(next_signal);
        }
    }
}

/// # Safety
/// `sess` is a live C++ session pointer.
#[no_mangle]
pub unsafe extern "C" fn nio_interrupt_read(sess: *mut c_void, generation: u64) -> c_int {
    let conn = match conn_of(sess) {
        Some(conn) => conn,
        None => return -1,
    };
    let g = conn.mu.lock().unwrap();
    let current = conn.request_generation.load(Ordering::Acquire);
    let target = if generation == 0 { current } else { generation };
    if target == 0
        || target != current
        || !conn.request_is_busy()
        || g.final_response != FinalResponseState::Idle
        || mid_read_failed(&conn)
    {
        return -1;
    }

    let mut signal = conn.mid_read_signal.lock().unwrap();
    if target != conn.request_generation.load(Ordering::Acquire)
        || !conn.request_is_busy()
        || mid_read_failed(&conn)
    {
        return -1;
    }
    signal.interrupted_generation = target;
    signal.epoch = signal.epoch.wrapping_add(1);
    conn.mid_read_ready.notify_all();
    0
}

/// # Safety
/// `sess` and `generation` identify a live request; C++ no longer accesses the
/// packet body after this call begins.
#[no_mangle]
pub unsafe extern "C" fn nio_release_read_packet(
    sess: *mut c_void,
    generation: u64,
    packet_lease: u64,
) -> c_int {
    if packet_lease == 0 {
        return -1;
    }
    let conn = match conn_of(sess) {
        Some(conn) => conn,
        None => return -1,
    };
    if !valid_request_generation(&conn, generation) {
        return -1;
    }
    let released = {
        let mut g = conn.mu.lock().unwrap();
        if !valid_request_generation(&conn, generation) {
            return -1;
        }
        let matched = g.mid_request_body.as_ref().is_some_and(|active| {
            active.generation == generation && active.lease_id == packet_lease
        });
        if matched {
            g.mid_request_body.take()
        } else {
            return -1;
        }
    };
    drop(released);
    0
}

pub(crate) const NIO_AUTH_NO_CHANGE: c_int = 0;
pub(crate) const NIO_AUTH_OK: c_int = 1;
pub(crate) const NIO_AUTH_SWITCH_SENT: c_int = 2;

/// # Safety
/// `connection_handle` is null or is a live acquired handle and must not be
/// released until this call returns.
#[no_mangle]
pub unsafe extern "C" fn nio_prepare_commit(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    auth_outcome: c_int,
) -> c_int {
    let conn = match unsafe { connection_handle.as_ref() } {
        Some(handle) if !handle.conn.close_notified.load(Ordering::Acquire) => &handle.conn,
        None => return -1,
        Some(_) => return -1,
    };
    let mut g = conn.mu.lock().unwrap();
    if !valid_request_generation(conn, generation) {
        return -1;
    }
    let mut invalid_completion = false;
    match g.final_response {
        FinalResponseState::Idle => {}
        FinalResponseState::Staged {
            generation: staged_generation,
        } if staged_generation == generation => {}
        FinalResponseState::Prepared(continuation)
        | FinalResponseState::Committed(continuation)
        | FinalResponseState::CompletionQueued(continuation)
            if continuation.generation == generation =>
        {
            return -1;
        }
        FinalResponseState::Staged { .. }
        | FinalResponseState::Prepared(_)
        | FinalResponseState::Committed(_)
        | FinalResponseState::CompletionQueued(_) => invalid_completion = true,
    }
    if !g.response.is_active(generation) {
        invalid_completion = true;
        g.response.repair_for_completion(generation);
    }
    let connection_is_ending = conn.err.load(Ordering::Acquire)
        || conn.peer_closed.load(Ordering::Acquire)
        || conn.need_shutdown.load(Ordering::Acquire)
        || conn.close_requested.load(Ordering::Acquire);
    let missing_final_flush = g.final_response == FinalResponseState::Idle
        && (!connection_is_ending || !g.response.batch.is_empty());
    let staged_with_unpublished_bytes = matches!(
        g.final_response,
        FinalResponseState::Staged {
            generation: staged_generation
        } if staged_generation == generation
    ) && !g.response.batch.is_empty();
    invalid_completion |= missing_final_flush || staged_with_unpublished_bytes;
    let auth_outcome = match auth_outcome {
        NIO_AUTH_NO_CHANGE => AuthOutcome::NoChange,
        NIO_AUTH_OK => AuthOutcome::AuthOk,
        NIO_AUTH_SWITCH_SENT => AuthOutcome::AuthSwitchSent {
            expected_seq: g.response.next_mysql_seq,
        },
        _ => {
            conn.err.store(true, Ordering::Release);
            g.clear_output();
            invalid_completion = true;
            AuthOutcome::NoChange
        }
    };
    if invalid_completion {
        conn.err.store(true, Ordering::Release);
        g.clear_output();
    } else if g.final_response == FinalResponseState::Idle {
        g.response.clear_batch();
    }
    g.final_response = FinalResponseState::Prepared(RequestContinuation {
        generation,
        auth_outcome,
    });
    drop(g);
    if invalid_completion {
        conn.write_done.notify_all();
        request_close(conn);
    }
    0
}

/// # Safety
/// `connection_handle` is a live opaque response handle when this function is
/// entered and remains live through the immediate entry clone. The worker will
/// not touch either the packet object or its body after this call begins. The
/// clone keeps Conn alive even when this commit clears REQUEST_BUSY and a
/// concurrent close subsequently releases the connection-scoped owner before
/// this function returns. The delivery callback may still be returning on the
/// io thread; Rust retains the body for it. Empty responses complete inline;
/// non-empty output is handed to the owning reactor so socket writes stay
/// concentrated on the SQL-NIO threads.
#[no_mangle]
pub unsafe extern "C" fn nio_commit_request(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
) {
    let conn = match unsafe { connection_handle.as_ref() } {
        Some(handle) if !handle.conn.close_notified.load(Ordering::Acquire) => {
            Arc::clone(&handle.conn)
        }
        None => return,
        Some(_) => return,
    };
    let (committed, released_body, released_mid_body, released_login) = {
        let mut g = conn.mu.lock().unwrap();
        if !valid_request_generation(&conn, generation) {
            return;
        }
        match g.final_response {
            FinalResponseState::Prepared(continuation) if continuation.generation == generation => {
                let released_body =
                    release_active_request_body_locked(&mut g, continuation.generation)
                        .map(|active| recycle_initial_request_body_locked(&conn, &mut g, active));
                let released_mid_body =
                    take_mid_request_body_for_generation_locked(&mut g, continuation.generation);
                let released_login = g.login.take();
                g.final_response = FinalResponseState::Committed(continuation);

                let completed = take_final_completion_locked(&mut g);
                let finished = completed
                    .and_then(|generation| finish_worker_request_locked(&conn, &mut g, generation));
                (
                    Some((completed, finished)),
                    released_body,
                    released_mid_body,
                    released_login,
                )
            }
            FinalResponseState::Idle
            | FinalResponseState::Staged { .. }
            | FinalResponseState::Prepared(_)
            | FinalResponseState::Committed(_)
            | FinalResponseState::CompletionQueued(_) => (None, None, None, None),
        }
    };
    drop(released_body);
    drop(released_mid_body);
    drop(released_login);
    let (completed, finished) = match committed {
        Some(committed) => committed,
        None => return,
    };
    if let Some(finished) = finished {
        publish_finish(&conn, finished);
    } else if let Some(generation) = completed {
        finish_worker_request(&conn, generation);
    } else {
        let queue_was_empty = {
            let mut commits = conn.commits.lock().unwrap();
            let was_empty = commits.is_empty();
            commits.push(QueuedCompletion {
                token: conn.token,
                generation,
            });
            was_empty
        };
        if queue_was_empty && conn.reactor_polling.load(Ordering::Acquire) {
            let _ = conn.waker.wake();
        }
    }
}

pub(crate) fn take_final_completion_locked(g: &mut ConnInner) -> Option<u64> {
    if g.transport_is_idle() {
        if let FinalResponseState::Committed(continuation) = g.final_response {
            g.final_response = FinalResponseState::CompletionQueued(continuation);
            return Some(continuation.generation);
        }
    }
    None
}

pub(crate) fn arm_writable(conn: &Arc<Conn>, generation: Option<u64>) -> bool {
    let (ok, async_completed, failed) = {
        let mut g = conn.mu.lock().unwrap();
        if generation.is_some_and(|generation| !valid_request_generation(conn, generation)) {
            return false;
        } else if conn.err.load(Ordering::Acquire) {
            g.clear_output();
            let completed = take_final_completion_locked(&mut g);
            (false, completed, true)
        } else if g.want_write {
            (true, None, false)
        } else if conn
            .reg
            .reregister(
                &mut g.sock,
                conn.token,
                Interest::READABLE | Interest::WRITABLE,
            )
            .is_ok()
        {
            g.want_write = true;
            (true, None, false)
        } else {
            conn.err.store(true, Ordering::Release);
            g.want_write = false;
            g.clear_output();
            let completed = take_final_completion_locked(&mut g);
            (false, completed, true)
        }
    };
    if failed {
        conn.write_done.notify_all();
        if let Some(generation) = async_completed {
            conn.completions.lock().unwrap().push(QueuedCompletion {
                token: conn.token,
                generation,
            });
        }
        request_close(conn);
        let _ = conn.waker.wake();
    }
    ok
}

pub(crate) fn finish_worker_request_locked(
    conn: &Arc<Conn>,
    g: &mut ConnInner,
    generation: u64,
) -> Option<FinishOutcome> {
    let continuation = match g.final_response {
        FinalResponseState::CompletionQueued(continuation)
            if continuation.generation == generation
                && conn.request_is_busy()
                && conn.request_generation.load(Ordering::Acquire) == generation =>
        {
            continuation
        }
        _ => return None,
    };
    if !g.response.finish(generation) {
        conn.err.store(true, Ordering::Release);
        g.clear_output();
    }
    g.final_response = FinalResponseState::Idle;
    let next_read = match continuation.auth_outcome {
        AuthOutcome::NoChange => g.read_state,
        AuthOutcome::AuthOk => ReadState::Command,
        AuthOutcome::AuthSwitchSent { expected_seq } => ReadState::ChangeUserAuth { expected_seq },
    };
    g.read_state = next_read;
    if !matches!(next_read, ReadState::ChangeUserAuth { .. }) {
        g.next_comp_seq = 0;
    }
    let readable_edge = conn.finish_request();
    Some(FinishOutcome {
        buffered_input: g.rpos < g.inbuf.len()
            || g.inner_rpos < g.inner_buf.len()
            || g.drain_stopped_at_budget
            || g.tls_plaintext_buffered(),
        readable_edge,
    })
}

pub(crate) fn publish_finish(conn: &Arc<Conn>, finished: FinishOutcome) {
    if conn.err.load(Ordering::Acquire)
        || conn.peer_closed.load(Ordering::Acquire)
        || conn.need_shutdown.load(Ordering::Acquire)
        || conn.close_requested.load(Ordering::Acquire)
    {
        request_close(conn);
    } else if finished.buffered_input || finished.readable_edge {
        let queue_was_empty = {
            let mut ready = conn.ready.lock().unwrap();
            let was_empty = ready.is_empty();
            ready.push(conn.token);
            was_empty
        };
        if queue_was_empty && conn.reactor_polling.load(Ordering::Acquire) {
            let _ = conn.waker.wake();
        }
    }
}

pub(crate) fn finish_worker_request(conn: &Arc<Conn>, generation: u64) {
    let finished = {
        let mut g = conn.mu.lock().unwrap();
        finish_worker_request_locked(conn, &mut g, generation)
    };
    if let Some(finished) = finished {
        publish_finish(conn, finished);
    }
}
