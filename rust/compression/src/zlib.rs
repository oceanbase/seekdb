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

//! Zlib and gzip operations backed by zlib-rs.
//!
//! One-shot operations borrow caller-owned buffers. Streaming objects own
//! their compressor state and can be reset without retaining input or output
//! slices between calls.

use std::{fmt, mem::MaybeUninit};
use zlib_rs::{Deflate, DeflateConfig, DeflateFlush, Inflate, InflateFlush, Status};

pub const DEFAULT_COMPRESSION_LEVEL: i32 = -1;
pub const GZIP_COMPRESSION_LEVEL: i32 = 5;
const WINDOW_BITS: u8 = 15;
const GZIP_WINDOW_BITS: i32 = 16 + WINDOW_BITS as i32;
const AUTO_WINDOW_BITS: u8 = 32 + WINDOW_BITS;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeflateError {
    StreamError,
    DataError,
    MemError,
}

impl DeflateError {
    fn as_str(self) -> &'static str {
        match self {
            Self::StreamError => "stream error",
            Self::DataError => "data error",
            Self::MemError => "memory error",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InflateError {
    NeedDict { dict_id: u32 },
    StreamError,
    DataError,
    MemError,
}

impl InflateError {
    fn as_str(self) -> &'static str {
        match self {
            Self::NeedDict { .. } => "need dictionary",
            Self::StreamError => "stream error",
            Self::DataError => "data error",
            Self::MemError => "memory error",
        }
    }
}

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

impl From<zlib_rs::DeflateError> for Error {
    fn from(error: zlib_rs::DeflateError) -> Self {
        Self::Deflate(match error {
            zlib_rs::DeflateError::StreamError => DeflateError::StreamError,
            zlib_rs::DeflateError::DataError => DeflateError::DataError,
            zlib_rs::DeflateError::MemError => DeflateError::MemError,
        })
    }
}

impl From<zlib_rs::InflateError> for Error {
    fn from(error: zlib_rs::InflateError) -> Self {
        Self::Inflate(match error {
            zlib_rs::InflateError::NeedDict { dict_id } => InflateError::NeedDict { dict_id },
            zlib_rs::InflateError::StreamError => InflateError::StreamError,
            zlib_rs::InflateError::DataError => InflateError::DataError,
            zlib_rs::InflateError::MemError => InflateError::MemError,
        })
    }
}

pub fn zlib_compress_bound(source_len: usize) -> usize {
    zlib_rs::compress_bound(source_len)
}

pub fn zlib_compress(input: &[u8], output: &mut [u8], level: i32) -> Result<usize, Error> {
    // SAFETY: initialized bytes remain valid when viewed as MaybeUninit, and
    // the compressor never exposes uninitialized bytes as initialized output.
    zlib_compress_uninit(
        input,
        unsafe { &mut *(output as *mut [u8] as *mut [MaybeUninit<u8>]) },
        level,
    )
}

pub(crate) fn zlib_compress_uninit(
    input: &[u8],
    output: &mut [MaybeUninit<u8>],
    level: i32,
) -> Result<usize, Error> {
    if !(-1..=9).contains(&level) {
        return Err(Error::InvalidCompressionLevel);
    }
    let config = DeflateConfig {
        level,
        ..DeflateConfig::default()
    };
    let mut encoder = Deflate::new_with_config(config);
    let mut consumed = 0;
    let mut produced = 0;
    loop {
        let before_in = consumed;
        let before_out = produced;
        let status = encoder
            .compress_uninit(
                &input[consumed..],
                &mut output[produced..],
                DeflateFlush::Finish,
            )
            .map_err(|error| match error {
                zlib_rs::DeflateError::DataError => Error::InvalidData,
                other => other.into(),
            })?;
        consumed = encoder.total_in() as usize;
        produced = encoder.total_out() as usize;
        if status == Status::StreamEnd {
            return Ok(produced);
        }
        if produced == output.len() {
            return Err(Error::OutputTooSmall);
        }
        if consumed == before_in && produced == before_out {
            return Err(Error::InvalidData);
        }
    }
}

/// Decodes one zlib stream, allowing bytes after its end.
pub fn zlib_decompress(input: &[u8], output: &mut [u8]) -> Result<usize, Error> {
    // SAFETY: initialized bytes remain valid when viewed as MaybeUninit, and
    // only the initialized prefix returned by the decoder is exposed.
    zlib_decompress_uninit(input, unsafe {
        &mut *(output as *mut [u8] as *mut [MaybeUninit<u8>])
    })
}

pub(crate) fn zlib_decompress_uninit(
    input: &[u8],
    output: &mut [MaybeUninit<u8>],
) -> Result<usize, Error> {
    let mut decoder = Inflate::new(true, WINDOW_BITS);
    let mut consumed = 0;
    let mut produced = 0;
    loop {
        let before_in = consumed;
        let before_out = produced;
        let status = decoder
            .decompress_uninit(
                &input[consumed..],
                &mut output[produced..],
                InflateFlush::Finish,
            )
            .map_err(|error| match error {
                zlib_rs::InflateError::NeedDict { .. } | zlib_rs::InflateError::DataError => {
                    Error::InvalidData
                }
                other => other.into(),
            })?;
        consumed = decoder.total_in() as usize;
        produced = decoder.total_out() as usize;
        if status == Status::StreamEnd {
            return Ok(produced);
        }
        if produced == output.len() {
            return Err(Error::OutputTooSmall);
        }
        if consumed == input.len() || (consumed == before_in && produced == before_out) {
            return Err(Error::InvalidData);
        }
    }
}

/// Decodes exactly one complete zlib stream, rejecting trailing input.
pub fn zlib_decompress_complete(input: &[u8], output: &mut [u8]) -> Result<usize, Error> {
    let mut decoder = Inflate::new(true, WINDOW_BITS);
    let mut consumed = 0;
    let mut produced = 0;
    loop {
        let before_in = consumed;
        let before_out = produced;
        let status = decoder.decompress(
            &input[consumed..],
            &mut output[produced..],
            InflateFlush::Finish,
        )?;
        consumed = decoder.total_in() as usize;
        produced = decoder.total_out() as usize;
        if status == Status::StreamEnd {
            return if consumed == input.len() {
                Ok(produced)
            } else {
                Err(Error::InvalidData)
            };
        }
        if produced == output.len() {
            return Err(Error::OutputTooSmall);
        }
        if consumed == input.len() || (consumed == before_in && produced == before_out) {
            return Err(Error::InvalidData);
        }
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
        Ok(Self::with_valid_level(level))
    }

    fn with_valid_level(level: i32) -> Self {
        Self {
            stream: Deflate::new_with_config(DeflateConfig {
                level,
                window_bits: GZIP_WINDOW_BITS,
                ..DeflateConfig::default()
            }),
        }
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
        Self::with_valid_level(GZIP_COMPRESSION_LEVEL)
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
        if self.needs_reset && input.is_empty() {
            return Ok(StreamProgress {
                consumed: 0,
                produced: 0,
                drained: !output.is_empty(),
                stream_end: true,
            });
        }
        if self.needs_reset {
            // Inflate::reset(true) selects zlib-only mode rather than autodetection.
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
