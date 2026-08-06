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

use std::collections::TryReserveError;
use std::io::Read;

pub const COMPRESS_HEADER_SIZE: usize = 7;

const MIN_COMPRESS_LEN: usize = 50;

const MAX_COMPRESS_BODY_LEN: usize = 0xFF_FFFF;

fn reserve_frame_output(out: &mut Vec<u8>, additional: usize) -> Result<(), TryReserveError> {
    out.try_reserve_exact(additional)
}

pub fn frame_into(out: &mut Vec<u8>, payload: &[u8], seq: &mut u8) -> Result<(), TryReserveError> {
    frame_into_impl(out, payload, seq, reserve_frame_output, deflate_if_smaller)
}

fn frame_into_impl<R, D>(
    out: &mut Vec<u8>,
    payload: &[u8],
    seq: &mut u8,
    mut reserve_out: R,
    mut deflate: D,
) -> Result<(), TryReserveError>
where
    R: FnMut(&mut Vec<u8>, usize) -> Result<(), TryReserveError>,
    D: FnMut(&[u8]) -> Option<Vec<u8>>,
{
    let original_len = out.len();
    let mut next_seq = *seq;

    for chunk in payload.chunks(MAX_COMPRESS_BODY_LEN) {
        let deflated = deflate(chunk).filter(|body| body.len() < chunk.len());
        let (body, uncomp_len) = if let Some(body) = deflated.as_deref() {
            (body, chunk.len())
        } else {
            (chunk, 0usize)
        };
        let cl = body.len();
        if let Err(err) = reserve_out(out, COMPRESS_HEADER_SIZE + cl) {
            out.truncate(original_len);
            return Err(err);
        }
        out.extend_from_slice(&[
            (cl & 0xff) as u8,
            ((cl >> 8) & 0xff) as u8,
            ((cl >> 16) & 0xff) as u8,
            next_seq,
        ]);
        next_seq = next_seq.wrapping_add(1);
        out.extend_from_slice(&[
            (uncomp_len & 0xff) as u8,
            ((uncomp_len >> 8) & 0xff) as u8,
            ((uncomp_len >> 16) & 0xff) as u8,
        ]);
        out.extend_from_slice(body);
    }
    *seq = next_seq;
    Ok(())
}

fn deflate_if_smaller(chunk: &[u8]) -> Option<Vec<u8>> {
    deflate_if_smaller_with_reserve(chunk, |scratch, additional| {
        scratch.try_reserve_exact(additional)
    })
}

fn deflate_if_smaller_with_reserve<R>(chunk: &[u8], reserve_scratch: R) -> Option<Vec<u8>>
where
    R: FnOnce(&mut Vec<u8>, usize) -> Result<(), TryReserveError>,
{
    if chunk.len() < MIN_COMPRESS_LEN {
        return None;
    }

    let mut scratch = Vec::new();
    reserve_scratch(&mut scratch, chunk.len().saturating_sub(1)).ok()?;

    let mut compressor = flate2::Compress::new(flate2::Compression::default(), true);
    let status = compressor
        .compress_vec(chunk, &mut scratch, flate2::FlushCompress::Finish)
        .ok()?;
    if status == flate2::Status::StreamEnd
        && compressor.total_in() == chunk.len() as u64
        && scratch.len() < chunk.len()
    {
        Some(scratch)
    } else {
        None
    }
}

pub enum DeframeStep {
    NeedMore,
    Bad,
    Packet {
        consumed: usize,
        seq: u8,
        plain: Vec<u8>,
    },
}

pub fn deframe_step_limited(buf: &[u8], at: usize, max_plain: usize) -> DeframeStep {
    if at > buf.len() {
        return DeframeStep::Bad;
    }
    let avail = buf.len() - at;
    if avail < COMPRESS_HEADER_SIZE {
        return DeframeStep::NeedMore;
    }
    let h = &buf[at..];
    let comp_len = (h[0] as usize) | ((h[1] as usize) << 8) | ((h[2] as usize) << 16);
    let seq = h[3];
    let uncomp_len = (h[4] as usize) | ((h[5] as usize) << 8) | ((h[6] as usize) << 16);
    if comp_len == 0 {
        return DeframeStep::Bad;
    }
    let plain_len = if uncomp_len == 0 {
        comp_len
    } else {
        uncomp_len
    };
    if plain_len > max_plain {
        return DeframeStep::Bad;
    }
    if avail < COMPRESS_HEADER_SIZE + comp_len {
        return DeframeStep::NeedMore;
    }
    let pstart = at + COMPRESS_HEADER_SIZE;
    let payload = &buf[pstart..pstart + comp_len];
    let plain = if uncomp_len == 0 {
        let mut out = Vec::new();
        if out.try_reserve_exact(comp_len).is_err() {
            return DeframeStep::Bad;
        }
        out.extend_from_slice(payload);
        out
    } else {
        let mut dec = flate2::read::ZlibDecoder::new(payload);
        let mut out = Vec::new();
        let output_cap = match uncomp_len.checked_add(1) {
            Some(cap) => cap,
            None => return DeframeStep::Bad,
        };
        if out.try_reserve_exact(output_cap).is_err() {
            return DeframeStep::Bad;
        }
        let decoded = dec
            .by_ref()
            .take(uncomp_len.saturating_add(1) as u64)
            .read_to_end(&mut out);
        if decoded.is_err() || out.len() != uncomp_len || dec.total_in() != payload.len() as u64 {
            return DeframeStep::Bad;
        }
        out
    };
    DeframeStep::Packet {
        consumed: COMPRESS_HEADER_SIZE + comp_len,
        seq,
        plain,
    }
}
