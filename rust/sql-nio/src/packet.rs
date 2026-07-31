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

pub const HEADER_SIZE: usize = 4;

pub const MAX_PAYLOAD: usize = 0xff_ffff;

pub const MAX_LOGICAL_PAYLOAD: usize = 1024 * 1024 * 1024;

pub enum AssembleStep {
    NeedMore,
    Bad,
    Packet {
        consumed: usize,
        last_seq: u8,
        body: Vec<u8>,
    },
}

pub fn write_header(out: &mut Vec<u8>, len: usize, seq: u8) {
    out.push((len & 0xff) as u8);
    out.push(((len >> 8) & 0xff) as u8);
    out.push(((len >> 16) & 0xff) as u8);
    out.push(seq);
}

pub(crate) fn assemble_command_limited_with_scratch(
    buf: &[u8],
    at: usize,
    max_payload: usize,
    scratch: &mut Vec<u8>,
) -> AssembleStep {
    if at > buf.len() {
        return AssembleStep::Bad;
    }

    let max_payload = max_payload.min(MAX_LOGICAL_PAYLOAD);

    let mut scan = at;
    let mut total = 0usize;
    let mut last_seq: Option<u8> = None;
    loop {
        if buf.len() - scan < HEADER_SIZE {
            return AssembleStep::NeedMore;
        }
        let h = &buf[scan..];
        let len = (h[0] as usize) | ((h[1] as usize) << 8) | ((h[2] as usize) << 16);
        let seq = h[3];
        if let Some(prev) = last_seq {
            if seq != prev.wrapping_add(1) {
                return AssembleStep::Bad;
            }
        }
        total = match total.checked_add(len) {
            Some(n) if n <= max_payload => n,
            _ => return AssembleStep::Bad,
        };
        if buf.len() - scan < HEADER_SIZE + len {
            return AssembleStep::NeedMore;
        }
        scan += HEADER_SIZE + len;
        last_seq = Some(seq);
        if len < MAX_PAYLOAD {
            break;
        }
    }

    let storage_len = match total.checked_add(1) {
        Some(storage_len) => storage_len,
        None => return AssembleStep::Bad,
    };
    scratch.clear();
    if scratch.try_reserve_exact(storage_len).is_err() {
        return AssembleStep::Bad;
    }
    let mut copy = at;
    while copy < scan {
        let h = &buf[copy..];
        let len = (h[0] as usize) | ((h[1] as usize) << 8) | ((h[2] as usize) << 16);
        let start = copy + HEADER_SIZE;
        scratch.extend_from_slice(&buf[start..start + len]);
        copy = start + len;
    }
    AssembleStep::Packet {
        consumed: scan - at,
        last_seq: last_seq.expect("a complete packet has a header"),
        body: std::mem::take(scratch),
    }
}
