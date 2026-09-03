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

use crate::cert::PeerCertificateInfo;
use crate::*;

pub(crate) const TLS_SEND_LIMIT: usize = 256 * 1024;

pub(crate) const TLS_HANDSHAKE_MAX_BYTES: usize = 64 * 1024;

pub(crate) struct TlsSession {
    pub(crate) conn: rustls::ServerConnection,
    pub(crate) plaintext_pending: usize,
    pub(crate) handshake_bytes: usize,
    pub(crate) close_notify_sent: bool,
    pub(crate) peer_cert_info: Option<PeerCertificateInfo>,
    pub(crate) cipher_name: Option<Vec<u8>>,
}

impl TlsSession {
    pub(crate) fn new(cfg: &Arc<rustls::ServerConfig>) -> Result<Box<Self>, rustls::Error> {
        let mut conn = rustls::ServerConnection::new(cfg.clone())?;
        conn.set_buffer_limit(Some(TLS_SEND_LIMIT));
        Ok(Box::new(Self {
            conn,
            plaintext_pending: 0,
            handshake_bytes: 0,
            close_notify_sent: false,
            peer_cert_info: None,
            cipher_name: None,
        }))
    }

    pub(crate) fn ensure_peer_cert_info(&mut self) {
        if self.peer_cert_info.is_none() {
            self.peer_cert_info = self
                .conn
                .peer_certificates()
                .and_then(|certs| certs.first())
                .map(|cert| PeerCertificateInfo::parse(cert.as_ref()));
        }
    }
}

pub(crate) fn tls_cipher_name(suite: rustls::CipherSuite) -> Option<&'static [u8]> {
    use rustls::CipherSuite::*;
    Some(match suite {
        TLS13_AES_128_GCM_SHA256 => b"TLS_AES_128_GCM_SHA256",
        TLS13_AES_256_GCM_SHA384 => b"TLS_AES_256_GCM_SHA384",
        TLS13_CHACHA20_POLY1305_SHA256 => b"TLS_CHACHA20_POLY1305_SHA256",
        TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA => b"ECDHE-ECDSA-AES128-SHA",
        TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA => b"ECDHE-ECDSA-AES256-SHA",
        TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA => b"ECDHE-RSA-AES128-SHA",
        TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA => b"ECDHE-RSA-AES256-SHA",
        TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256 => b"ECDHE-ECDSA-AES128-SHA256",
        TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384 => b"ECDHE-ECDSA-AES256-SHA384",
        TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256 => b"ECDHE-RSA-AES128-SHA256",
        TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384 => b"ECDHE-RSA-AES256-SHA384",
        TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 => b"ECDHE-ECDSA-AES128-GCM-SHA256",
        TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 => b"ECDHE-ECDSA-AES256-GCM-SHA384",
        TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 => b"ECDHE-RSA-AES128-GCM-SHA256",
        TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 => b"ECDHE-RSA-AES256-GCM-SHA384",
        TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 => b"ECDHE-RSA-CHACHA20-POLY1305",
        TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 => b"ECDHE-ECDSA-CHACHA20-POLY1305",
        _ => return None,
    })
}

pub(crate) fn flush_tls_locked(conn: &Arc<Conn>, g: &mut ConnInner) -> bool {
    let mut fatal = false;
    {
        let ConnInner {
            tls,
            sock,
            outbuf,
            out_pos,
            ..
        } = &mut *g;
        let tls = tls
            .as_mut()
            .expect("flush_tls_locked requires a TLS session");
        while *out_pos < outbuf.len() {
            match tls.conn.writer().write(&outbuf[*out_pos..]) {
                Ok(0) => break,
                Ok(n) => *out_pos += n,
                Err(_) => {
                    fatal = true;
                    break;
                }
            }
        }
        while !fatal && tls.conn.wants_write() {
            match tls.conn.write_tls(sock) {
                Ok(0) => break,
                Ok(_) => {}
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(_) => {
                    fatal = true;
                    break;
                }
            }
        }
    }
    if fatal {
        return fail_flush_locked(conn, g);
    }
    if g.output_is_empty() {
        g.clear_transport_output();
    }
    g.transport_is_idle()
}

pub(crate) fn send_tls_close_notify_locked(conn: &Arc<Conn>, g: &mut ConnInner) {
    if g.tls.is_none() {
        return;
    }
    if !conn.err.load(Ordering::Acquire) {
        let tls = g.tls.as_mut().expect("checked above");
        if !tls.close_notify_sent {
            tls.close_notify_sent = true;
            tls.conn.send_close_notify();
        }
    }
    let ConnInner { tls, sock, .. } = &mut *g;
    let tls = tls.as_mut().expect("checked above");
    while tls.conn.wants_write() {
        match tls.conn.write_tls(sock) {
            Ok(0) => break,
            Ok(_) => {}
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => break,
        }
    }
}

pub(crate) fn discharge_tls_arm(conn: &Arc<Conn>) {
    let pending = {
        let mut g = conn.mu.lock().unwrap();
        std::mem::take(&mut g.tls_arm_pending)
    };
    if pending && arm_writable(conn, None) {
        let _ = conn.waker.wake();
    }
}

pub(crate) fn promote_to_tls(conn: &Arc<Conn>, packet: &DecodedPacket) -> bool {
    let cfg = match conn.tls_config.as_ref() {
        Some(cfg) => cfg,
        None => {
            return refuse_login(conn, b"TLS is not enabled on this server");
        }
    };
    if packet.body.len() != 32 {
        return refuse_login(conn, b"malformed SSLRequest");
    }
    let mut g = conn.mu.lock().unwrap();
    debug_assert!(g.tls.is_none(), "SSLRequest on an established session");
    let mut tls = match TlsSession::new(cfg) {
        Ok(tls) => tls,
        Err(_) => {
            drop(g);
            return refuse_login(conn, b"TLS session setup failed");
        }
    };
    {
        let mut rest = &g.inbuf[g.rpos..];
        tls.handshake_bytes = rest.len();
        while !rest.is_empty() {
            match tls.conn.read_tls(&mut rest) {
                Ok(0) => break,
                Ok(_) => {}
                Err(_) => {
                    drop(g);
                    mark_connection_error(conn);
                    return true;
                }
            }
        }
    }
    g.inbuf.clear();
    g.rpos = 0;
    g.tls = Some(tls);
    g.expected_login_seq = 2;
    if process_tls_packets(&mut g).is_err() {
        drop(g);
        mark_connection_error(conn);
        return true;
    }
    true
}

#[cfg(unix)]
pub(crate) fn read_socket_raw(g: &mut ConnInner, buf: &mut [u8]) -> std::io::Result<usize> {
    let fd = raw_fd(&g.sock);
    let n = unsafe { libc::read(fd, buf.as_mut_ptr() as *mut c_void, buf.len()) };
    if n < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(n as usize)
    }
}

#[cfg(not(unix))]
pub(crate) fn read_socket_raw(g: &mut ConnInner, buf: &mut [u8]) -> std::io::Result<usize> {
    g.sock.read(buf)
}

pub(crate) fn read_tls_input(g: &mut ConnInner, read_len: usize) -> std::io::Result<usize> {
    let mut sock_eof = false;
    loop {
        {
            let ConnInner { tls, inbuf, .. } = &mut *g;
            let tls = tls.as_mut().expect("read_tls_input requires a TLS session");
            inbuf
                .try_reserve(read_len)
                .map_err(|_| std::io::Error::from(std::io::ErrorKind::OutOfMemory))?;
            let old_len = inbuf.len();
            inbuf.resize(old_len + read_len, 0);
            match tls.conn.reader().read(&mut inbuf[old_len..]) {
                Ok(n) => {
                    inbuf.truncate(old_len + n);
                    if n > 0 {
                        tls.plaintext_pending = tls.plaintext_pending.saturating_sub(n);
                        return Ok(n);
                    }
                    return Ok(0);
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    inbuf.truncate(old_len);
                    if sock_eof {
                        return Ok(0);
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::UnexpectedEof => {
                    inbuf.truncate(old_len);
                    return Ok(0);
                }
                Err(_) => {
                    inbuf.truncate(old_len);
                    return Err(std::io::Error::from(std::io::ErrorKind::InvalidData));
                }
            }
        }
        let mut cipher = [0u8; 16 * 1024];
        match read_socket_raw(g, &mut cipher) {
            Ok(0) => sock_eof = true,
            Ok(n) => {
                let tls = g.tls.as_mut().expect("session checked above");
                let mut fed = &cipher[..n];
                while !fed.is_empty() {
                    match tls.conn.read_tls(&mut fed) {
                        Ok(0) => break,
                        Ok(_) => {}
                        Err(_) => {
                            return Err(std::io::Error::from(std::io::ErrorKind::InvalidData))
                        }
                    }
                }
                if tls.conn.is_handshaking() {
                    tls.handshake_bytes = tls.handshake_bytes.saturating_add(n);
                    if tls.handshake_bytes > TLS_HANDSHAKE_MAX_BYTES {
                        return Err(std::io::Error::from(std::io::ErrorKind::InvalidData));
                    }
                }
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                let peer_closed = process_tls_packets(g)?;
                if !peer_closed && !g.tls_plaintext_buffered() {
                    return Err(std::io::Error::from(std::io::ErrorKind::WouldBlock));
                }
                continue;
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => return Err(std::io::Error::from(std::io::ErrorKind::InvalidData)),
        }
        let _ = process_tls_packets(g)?;
    }
}

pub(crate) fn process_tls_packets(g: &mut ConnInner) -> std::io::Result<bool> {
    let peer_closed = {
        let tls = g.tls.as_mut().expect("session checked by caller");
        let state = tls
            .conn
            .process_new_packets()
            .map_err(|_| std::io::Error::from(std::io::ErrorKind::InvalidData))?;
        tls.plaintext_pending = state.plaintext_bytes_to_read();
        state.peer_has_closed()
    };
    let wants_write_after = {
        let ConnInner { tls, sock, .. } = &mut *g;
        let tls = tls.as_mut().expect("session unchanged");
        loop {
            if !tls.conn.wants_write() {
                break;
            }
            match tls.conn.write_tls(sock) {
                Ok(0) => break,
                Ok(_) => {}
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(_) => return Err(std::io::Error::from(std::io::ErrorKind::InvalidData)),
            }
        }
        tls.conn.wants_write()
    };
    if wants_write_after {
        g.tls_arm_pending = true;
    }
    Ok(peer_closed)
}

#[cfg(test)]
mod tests {
    use super::tls_cipher_name;

    #[test]
    fn exposes_sql_cipher_names() {
        assert_eq!(
            tls_cipher_name(rustls::CipherSuite::TLS13_AES_256_GCM_SHA384),
            Some(&b"TLS_AES_256_GCM_SHA384"[..])
        );
        assert_eq!(
            tls_cipher_name(rustls::CipherSuite::TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256),
            Some(&b"ECDHE-RSA-AES128-GCM-SHA256"[..])
        );
    }
}
