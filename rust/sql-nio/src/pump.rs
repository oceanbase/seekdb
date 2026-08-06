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

use crate::capability::session_client_capabilities;
use crate::ffi_check::checked_out_range;

pub(crate) fn deliver_to_cpp(
    cb: NioCallbacks,
    sess: *mut c_void,
    body: *mut c_char,
    body_len: usize,
    wire_bytes: u64,
    packet_kind: c_int,
    command_view: Option<&NioMysqlCommandView>,
    generation: u64,
) -> bool {
    if (packet_kind == NIO_PACKET_COMMAND) != command_view.is_some() {
        return false;
    }
    if let Some(on_readable) = cb.on_readable {
        on_readable(
            cb.ctx,
            sess,
            body,
            body_len as i64,
            wire_bytes,
            packet_kind,
            command_view.map_or(std::ptr::null(), |view| view as *const _),
            generation,
        ) == 0
    } else {
        false
    }
}

pub(crate) struct DecodedPacket {
    pub(crate) consumed: usize,
    pub(crate) seq: u8,
    pub(crate) wire_bytes: u64,
    pub(crate) body: Vec<u8>,
}

pub(crate) fn assemble_for_conn(
    conn: &Arc<Conn>,
    buf: &[u8],
    at: usize,
    scratch: &mut Vec<u8>,
) -> Result<Option<DecodedPacket>, ()> {
    let limit = packet_limit(conn);
    match assemble_command_limited_with_scratch(buf, at, limit, scratch) {
        AssembleStep::NeedMore => {
            if buf.len().saturating_sub(at) >= mysql_wire_budget(limit) {
                Err(())
            } else {
                Ok(None)
            }
        }
        AssembleStep::Bad => Err(()),
        AssembleStep::Packet {
            consumed,
            last_seq,
            body,
        } => Ok(Some(DecodedPacket {
            consumed,
            seq: last_seq,
            wire_bytes: consumed as u64,
            body,
        })),
    }
}

pub(crate) fn first_sequence_mismatch(buf: &[u8], at: usize, expected: u8) -> bool {
    buf.len().saturating_sub(at) >= HEADER_SIZE && buf[at + 3] != expected
}

#[derive(Clone, Copy)]
pub(crate) enum WireProtocol {
    Plain,
    Compressed,
}

impl WireProtocol {
    pub(crate) fn negotiated(conn: &Conn) -> Self {
        if conn.transport_caps.load(Ordering::Acquire) & CLIENT_COMPRESS != 0 {
            Self::Compressed
        } else {
            Self::Plain
        }
    }

    pub(crate) fn current_request(g: &ConnInner) -> Self {
        if g.req_compressed {
            Self::Compressed
        } else {
            Self::Plain
        }
    }

    pub(crate) fn is_compressed(self) -> bool {
        matches!(self, Self::Compressed)
    }

    pub(crate) fn read_packet_locked(
        self,
        g: &mut ConnInner,
        conn: &Arc<Conn>,
        expected_first_seq: Option<u8>,
    ) -> Result<Option<DecodedPacket>, ()> {
        let packet = match self {
            Self::Compressed => assemble_compressed_for_conn(g, conn, expected_first_seq),
            Self::Plain => {
                drain_socket(g, conn, false)?;
                let rpos = g.rpos;
                if let Some(expected) = expected_first_seq {
                    if first_sequence_mismatch(&g.inbuf, rpos, expected) {
                        return Err(());
                    }
                }
                let mut scratch = std::mem::take(&mut g.initial_body_spare);
                let assembled = assemble_for_conn(conn, &g.inbuf, rpos, &mut scratch);
                g.initial_body_spare = scratch;
                assembled
            }
        }?;
        if let Some(mut packet) = packet {
            self.consume_locked(g, packet.consumed);
            if self.is_compressed() {
                packet.wire_bytes = std::mem::take(&mut g.compressed_wire_bytes);
            }
            g.req_compressed = self.is_compressed();
            Ok(Some(packet))
        } else {
            Ok(None)
        }
    }

    pub(crate) fn consume_locked(self, g: &mut ConnInner, amount: usize) {
        match self {
            Self::Plain => g.consume_raw_input(amount),
            Self::Compressed => g.consume_inner_input(amount),
        }
    }
}

pub(crate) fn send_greeting(conn: &Arc<Conn>, greeting: &NioGreetingInfo) -> bool {
    let version: &[u8] = match usize::try_from(greeting.version_len) {
        Ok(len) if len <= greeting.version.len() => &greeting.version[..len],
        _ => b"5.7.25",
    };
    let out = handshake::build_greeting(
        greeting.sessid,
        &greeting.scramble,
        version,
        conn.server_caps,
        greeting.status_flags,
    );
    let mut g = conn.mu.lock().unwrap();
    debug_assert!(g.tls.is_none(), "greeting after TLS promotion");
    let mut pos = 0;
    while pos < out.len() {
        match g.sock.write(&out[pos..]) {
            Ok(0) => return false,
            Ok(k) => pos += k,
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => return false,
        }
    }
    true
}

pub(crate) fn pump(conn: &Arc<Conn>, cb: NioCallbacks) -> bool {
    if conn.request_is_busy() {
        return true;
    }
    if conn.close_requested.load(Ordering::Acquire)
        || conn.err.load(Ordering::Acquire)
        || conn.peer_closed.load(Ordering::Acquire)
        || conn.need_shutdown.load(Ordering::Acquire)
    {
        request_close(conn);
        return true;
    }
    let read_state = conn.mu.lock().unwrap().read_state;
    let handled = match read_state {
        ReadState::Command => decode_pump(conn, cb),
        ReadState::Login => connect_pump(conn, cb),
        ReadState::ChangeUserAuth { expected_seq } => auth_switch_pump(conn, cb, expected_seq),
    };
    discharge_tls_arm(conn);
    handled
}

pub(crate) fn auth_switch_pump(conn: &Arc<Conn>, cb: NioCallbacks, expected_seq: u8) -> bool {
    let peer_closed_before = conn.peer_closed.load(Ordering::Acquire);
    let protocol = WireProtocol::negotiated(conn);
    let parsed = {
        let mut g = conn.mu.lock().unwrap();
        match protocol.read_packet_locked(&mut g, conn, Some(expected_seq)) {
            Ok(packet) => packet,
            Err(()) => {
                mark_connection_error(conn);
                return true;
            }
        }
    };
    let current_peer_eof = !peer_closed_before && conn.peer_closed.load(Ordering::Acquire);
    let packet = match parsed {
        Some(packet) => packet,
        None => {
            if current_peer_eof {
                request_close(conn);
            }
            return true;
        }
    };
    deliver_decoded_packet(
        conn,
        cb,
        packet,
        NIO_PACKET_AUTH_SWITCH_RESPONSE,
        None,
        current_peer_eof,
    )
}

/// # Safety
/// `out` points to writable storage for one `NioLoginView`.
#[no_mangle]
pub unsafe extern "C" fn nio_get_login_view(
    sess: *mut c_void,
    generation: u64,
    out: *mut NioLoginView,
) -> c_int {
    if checked_out_range(out).is_none() {
        return -1;
    }
    let conn = match conn_of(sess) {
        Some(c) => c,
        None => return -1,
    };
    let g = conn.mu.lock().unwrap();
    if !valid_request_generation(&conn, generation)
        || !g.response.is_active(generation)
        || !g
            .active_request_body
            .as_ref()
            .is_some_and(|body| body.generation == generation)
    {
        return -1;
    }
    let login = match g.login.as_ref() {
        Some(login) => login,
        None => return -1,
    };
    let attr_count = match i32::try_from(login.attrs.len()) {
        Ok(count) => count,
        Err(_) => return -1,
    };
    unsafe {
        *out = NioLoginView {
            capabilities: session_client_capabilities(login.raw_caps),
            charset: login.charset,
            reserved: [0; 3],
            username: login.user.into(),
            auth_response: login.auth.into(),
            database: login.db.into(),
            auth_plugin_name: login.plugin.into(),
            attr_count,
            attrs: if login.attrs.is_empty() {
                std::ptr::null()
            } else {
                login.attrs.as_ptr()
            },
        };
    }
    0
}

pub(crate) fn connect_pump(conn: &Arc<Conn>, cb: NioCallbacks) -> bool {
    let peer_closed_before = conn.peer_closed.load(Ordering::Acquire);
    let (parsed, tls_active) = {
        let mut g = conn.mu.lock().unwrap();
        let expected_seq = g.expected_login_seq;
        match WireProtocol::Plain.read_packet_locked(&mut g, conn, Some(expected_seq)) {
            Ok(packet) => (packet, g.tls.is_some()),
            Err(()) => {
                mark_connection_error(conn);
                return true;
            }
        }
    };
    let current_peer_eof = !peer_closed_before && conn.peer_closed.load(Ordering::Acquire);
    let packet = match parsed {
        Some(packet) => packet,
        None => {
            if current_peer_eof {
                request_close(conn);
            }
            return true;
        }
    };
    if packet.body.len() < 2 {
        conn.err.store(true, Ordering::Release);
        return true;
    }
    let caps_lo = (packet.body[0] as u32) | ((packet.body[1] as u32) << 8);
    if caps_lo & CLIENT_SSL != 0 && !tls_active {
        return promote_to_tls(conn, &packet);
    }
    let parsed_login = match parse_login(&packet.body, conn.server_caps) {
        Some(pl) => pl,
        None => {
            conn.err.store(true, Ordering::Release);
            return true;
        }
    };
    conn.raw_caps
        .store(parsed_login.raw_caps, Ordering::Release);
    conn.transport_caps
        .store(parsed_login.transport_caps, Ordering::Release);
    conn.mu.lock().unwrap().login = Some(parsed_login);
    deliver_decoded_packet(conn, cb, packet, NIO_PACKET_LOGIN, None, current_peer_eof)
}

pub(crate) fn refuse_login(conn: &Arc<Conn>, msg: &[u8]) -> bool {
    let mut payload = [0u8; 128];
    if let Some(n) = response::encode_error_payload(1043, b"08S01", msg, &mut payload) {
        let mut out = Vec::with_capacity(HEADER_SIZE + n);
        packet::write_header(&mut out, n, 2);
        out.extend_from_slice(&payload[..n]);
        let mut g = conn.mu.lock().unwrap();
        if append_response_locked(&mut g, &out).is_ok() {
            let _ = flush_buffer_locked(conn, &mut g);
        }
    }
    conn.err.store(true, Ordering::Release);
    request_close(conn);
    true
}

pub(crate) fn decode_pump(conn: &Arc<Conn>, cb: NioCallbacks) -> bool {
    let protocol = WireProtocol::negotiated(conn);
    let peer_closed_before = conn.peer_closed.load(Ordering::Acquire);
    let parsed = {
        let mut g = conn.mu.lock().unwrap();
        match protocol.read_packet_locked(&mut g, conn, Some(0)) {
            Ok(packet) => packet,
            Err(()) => {
                mark_connection_error(conn);
                return true;
            }
        }
    };
    let current_peer_eof = !peer_closed_before && conn.peer_closed.load(Ordering::Acquire);
    let packet = match parsed {
        Some(packet) => packet,
        None => {
            if current_peer_eof {
                request_close(conn);
            }
            return true;
        }
    };
    let client_caps = conn.raw_caps.load(Ordering::Acquire);
    let command_view = match command::parse_body(&packet.body, client_caps) {
        Some(view) => view,
        None => {
            mark_connection_error(conn);
            return true;
        }
    };
    deliver_decoded_packet(
        conn,
        cb,
        packet,
        NIO_PACKET_COMMAND,
        Some(command_view),
        current_peer_eof,
    )
}

#[cfg(unix)]
fn read_raw_fd(inbuf: &mut Vec<u8>, fd: c_int, read_len: usize) -> std::io::Result<usize> {
    if inbuf.spare_capacity_mut().is_empty() {
        inbuf
            .try_reserve(read_len)
            .map_err(|_| std::io::Error::from(std::io::ErrorKind::OutOfMemory))?;
    }
    let old_len = inbuf.len();
    let writable_len = read_len.min(inbuf.spare_capacity_mut().len());
    let dst = inbuf.spare_capacity_mut().as_mut_ptr().cast::<c_void>();
    let n = unsafe { libc::read(fd, dst, writable_len) };
    if n < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        let n = n as usize;
        debug_assert!(n <= writable_len);
        unsafe { inbuf.set_len(old_len + n) };
        Ok(n)
    }
}

#[cfg(unix)]
pub(crate) fn read_raw_input(g: &mut ConnInner, read_len: usize) -> std::io::Result<usize> {
    let fd = raw_fd(&g.sock);
    read_raw_fd(&mut g.inbuf, fd, read_len)
}

#[cfg(not(unix))]
pub(crate) fn read_raw_input(g: &mut ConnInner, read_len: usize) -> std::io::Result<usize> {
    let mut tmp = [0u8; 16 * 1024];
    let n = g.sock.read(&mut tmp[..read_len])?;
    g.inbuf
        .try_reserve(n)
        .map_err(|_| std::io::Error::from(std::io::ErrorKind::OutOfMemory))?;
    g.inbuf.extend_from_slice(&tmp[..n]);
    Ok(n)
}

pub(crate) fn drain_socket(
    g: &mut ConnInner,
    conn: &Arc<Conn>,
    compressed: bool,
) -> Result<(), ()> {
    g.compact_raw_input();
    let budget = raw_input_budget(conn, compressed);
    g.drain_stopped_at_budget = false;
    loop {
        let live = g.inbuf.len().saturating_sub(g.rpos);
        let read_len = (16 * 1024).min(budget.saturating_sub(live));
        if read_len == 0 {
            g.drain_stopped_at_budget = true;
            break;
        }
        let read_result = if g.tls.is_some() {
            read_tls_input(g, read_len)
        } else {
            read_raw_input(g, read_len)
        };
        match read_result {
            Ok(0) => {
                conn.peer_closed.store(true, Ordering::Release);
                break;
            }
            Ok(_) => {}
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => return Err(()),
        }
    }
    Ok(())
}

pub(crate) fn deframe_compress_one(g: &mut ConnInner, conn: &Arc<Conn>) -> Result<bool, ()> {
    g.compact_raw_input();
    g.compact_inner_input();
    let inner_budget = mysql_wire_budget(packet_limit(conn)).saturating_add(MAX_PAYLOAD);
    let inner_live = g.inner_buf.len().saturating_sub(g.inner_rpos);
    let remaining = inner_budget.checked_sub(inner_live).ok_or(())?;
    match compress::deframe_step_limited(&g.inbuf, g.rpos, remaining) {
        DeframeStep::NeedMore => {
            if g.inbuf.len().saturating_sub(g.rpos) >= raw_input_budget(conn, true) {
                Err(())
            } else {
                Ok(false)
            }
        }
        DeframeStep::Bad => Err(()),
        DeframeStep::Packet {
            consumed,
            seq,
            plain,
        } => {
            if seq != g.next_comp_seq {
                return Err(());
            }
            let next_wire_bytes = g
                .compressed_wire_bytes
                .checked_add(u64::try_from(consumed).map_err(|_| ())?)
                .ok_or(())?;
            if g.inner_buf.is_empty() {
                g.inner_buf = plain;
            } else {
                if g.inner_buf.try_reserve(plain.len()).is_err() {
                    return Err(());
                }
                g.inner_buf.extend_from_slice(&plain);
            }
            g.next_comp_seq = seq.wrapping_add(1);
            g.consume_raw_input(consumed);
            g.compressed_wire_bytes = next_wire_bytes;
            Ok(true)
        }
    }
}

pub(crate) fn assemble_compressed_for_conn(
    g: &mut ConnInner,
    conn: &Arc<Conn>,
    expected_first_seq: Option<u8>,
) -> Result<Option<DecodedPacket>, ()> {
    loop {
        let rpos = g.inner_rpos;
        if let Some(expected) = expected_first_seq {
            if first_sequence_mismatch(&g.inner_buf, rpos, expected) {
                return Err(());
            }
        }
        let mut scratch = std::mem::take(&mut g.initial_body_spare);
        let assembled = assemble_for_conn(conn, &g.inner_buf, rpos, &mut scratch);
        g.initial_body_spare = scratch;
        if let Some(packet) = assembled? {
            return Ok(Some(packet));
        }
        if deframe_compress_one(g, conn)? {
            continue;
        }

        let before = g.inbuf.len().saturating_sub(g.rpos);
        drain_socket(g, conn, true)?;
        let after = g.inbuf.len().saturating_sub(g.rpos);
        if after == before {
            return Ok(None);
        }
    }
}
