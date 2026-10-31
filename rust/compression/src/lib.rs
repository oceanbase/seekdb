// Copyright (c) 2026 OceanBase.
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

//! Seekdb compression and checksum primitives backed entirely by zlib-rs.
//!
//! One-shot operations borrow caller-owned buffers. Streaming objects own
//! their compressor state and can be reset without retaining input or output
//! slices between calls.

mod bridge;

use std::fmt;
use zlib_rs::{
    compress_slice, decompress_slice, Deflate, DeflateConfig, DeflateError, DeflateFlush, Inflate,
    InflateConfig, InflateError, InflateFlush, ReturnCode, Status,
};

pub const DEFAULT_COMPRESSION_LEVEL: i32 = -1;
pub const GZIP_COMPRESSION_LEVEL: i32 = 5;
const WINDOW_BITS: u8 = 15;
const GZIP_WINDOW_BITS: i32 = 16 + WINDOW_BITS as i32;
const AUTO_WINDOW_BITS: u8 = 32 + WINDOW_BITS;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidCompressionLevel,
    OutputTooSmall,
    InvalidData,
    Deflate(DeflateError),
    Inflate(InflateError),
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidCompressionLevel => formatter.write_str("invalid compression level"),
            Self::OutputTooSmall => formatter.write_str("output buffer is too small"),
            Self::InvalidData => formatter.write_str("invalid or truncated compressed data"),
            Self::Deflate(error) => write!(formatter, "deflate failed: {}", error.as_str()),
            Self::Inflate(error) => write!(formatter, "inflate failed: {}", error.as_str()),
        }
    }
}

impl std::error::Error for Error {}

impl From<DeflateError> for Error {
    fn from(error: DeflateError) -> Self {
        Self::Deflate(error)
    }
}

impl From<InflateError> for Error {
    fn from(error: InflateError) -> Self {
        Self::Inflate(error)
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct StreamProgress {
    pub consumed: usize,
    pub produced: usize,
    pub drained: bool,
    pub stream_end: bool,
}

pub fn zlib_compress_bound(source_len: usize) -> usize {
    zlib_rs::compress_bound(source_len)
}

pub fn zlib_compress(input: &[u8], output: &mut [u8], level: i32) -> Result<usize, Error> {
    if !(-1..=9).contains(&level) {
        return Err(Error::InvalidCompressionLevel);
    }
    let config = DeflateConfig {
        level,
        ..DeflateConfig::default()
    };
    let (compressed, code) = compress_slice(output, input, config);
    let size = compressed.len();
    match code {
        ReturnCode::Ok => Ok(size),
        ReturnCode::BufError => Err(Error::OutputTooSmall),
        ReturnCode::MemError => Err(Error::Deflate(DeflateError::MemError)),
        ReturnCode::StreamError => Err(Error::Deflate(DeflateError::StreamError)),
        _ => Err(Error::InvalidData),
    }
}

pub fn zlib_decompress(input: &[u8], output: &mut [u8]) -> Result<usize, Error> {
    let (decompressed, code) = decompress_slice(output, input, InflateConfig::default());
    let size = decompressed.len();
    match code {
        ReturnCode::Ok => Ok(size),
        ReturnCode::BufError => Err(Error::OutputTooSmall),
        ReturnCode::MemError => Err(Error::Inflate(InflateError::MemError)),
        ReturnCode::StreamError => Err(Error::Inflate(InflateError::StreamError)),
        _ => Err(Error::InvalidData),
    }
}

pub fn crc32(input: &[u8]) -> u32 {
    zlib_rs::crc32::crc32(0, input)
}

pub struct GzipEncoder {
    stream: Deflate,
}

impl GzipEncoder {
    pub fn new(level: i32) -> Result<Self, Error> {
        if !(-1..=9).contains(&level) {
            return Err(Error::InvalidCompressionLevel);
        }
        Ok(Self {
            stream: Deflate::new_with_config(DeflateConfig {
                level,
                window_bits: GZIP_WINDOW_BITS,
                ..DeflateConfig::default()
            }),
        })
    }

    pub fn process(
        &mut self,
        input: &[u8],
        output: &mut [u8],
        finish: bool,
    ) -> Result<StreamProgress, Error> {
        let before_in = self.stream.total_in();
        let before_out = self.stream.total_out();
        let status = self.stream.compress(
            input,
            output,
            if finish {
                DeflateFlush::Finish
            } else {
                DeflateFlush::NoFlush
            },
        )?;
        let consumed = (self.stream.total_in() - before_in) as usize;
        let produced = (self.stream.total_out() - before_out) as usize;
        Ok(StreamProgress {
            consumed,
            produced,
            drained: consumed == input.len() && produced < output.len(),
            stream_end: status == Status::StreamEnd,
        })
    }

    pub fn reset(&mut self) {
        self.stream.reset();
    }
}

impl Default for GzipEncoder {
    fn default() -> Self {
        Self::new(GZIP_COMPRESSION_LEVEL).expect("the fixed gzip compression level is valid")
    }
}

pub struct StreamDecoder {
    stream: Inflate,
    needs_reset: bool,
}

impl StreamDecoder {
    pub fn new() -> Self {
        Self {
            stream: Inflate::new(true, AUTO_WINDOW_BITS),
            needs_reset: false,
        }
    }

    pub fn process(&mut self, input: &[u8], output: &mut [u8]) -> Result<StreamProgress, Error> {
        if self.needs_reset {
            self.stream = Inflate::new(true, AUTO_WINDOW_BITS);
            self.needs_reset = false;
        }
        let before_in = self.stream.total_in();
        let before_out = self.stream.total_out();
        let status = self
            .stream
            .decompress(input, output, InflateFlush::NoFlush)?;
        let consumed = (self.stream.total_in() - before_in) as usize;
        let produced = (self.stream.total_out() - before_out) as usize;
        let stream_end = status == Status::StreamEnd;
        self.needs_reset = stream_end;
        Ok(StreamProgress {
            consumed,
            produced,
            drained: consumed == input.len() && produced < output.len(),
            stream_end,
        })
    }
}

impl Default for StreamDecoder {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests;
