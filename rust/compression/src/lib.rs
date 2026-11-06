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

//! Compression and checksum primitives backed by libz-sys.
//!
//! One-shot operations borrow caller-owned buffers. Streaming objects own
//! their compressor state and can be reset without retaining input or output
//! slices between calls.

mod bridge;

use std::fmt;
use std::mem::size_of;
use std::ptr;

pub const DEFAULT_COMPRESSION_LEVEL: i32 = -1;
pub const GZIP_COMPRESSION_LEVEL: i32 = 5;
const WINDOW_BITS: i32 = 15;
const GZIP_WINDOW_BITS: i32 = 16 + WINDOW_BITS;
const AUTO_WINDOW_BITS: i32 = 32 + WINDOW_BITS;
const DEFAULT_MEM_LEVEL: i32 = 8;

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

pub fn zlib_compress_bound(source_len: usize) -> usize {
    // SAFETY: compressBound only inspects the supplied length.
    unsafe { libz_sys::compressBound(source_len as libz_sys::uLong) as usize }
}

pub fn zlib_compress(input: &[u8], output: &mut [u8], level: i32) -> Result<usize, Error> {
    if !(-1..=9).contains(&level) {
        return Err(Error::InvalidCompressionLevel);
    }
    let mut output_len = output.len() as libz_sys::uLongf;
    // SAFETY: both buffers remain alive and exclusively borrowed for the call;
    // output_len is initialized to the actual output capacity.
    let code = unsafe {
        libz_sys::compress2(
            output.as_mut_ptr(),
            &mut output_len,
            input.as_ptr(),
            input.len() as libz_sys::uLong,
            level,
        )
    };
    match code {
        libz_sys::Z_OK => Ok(output_len as usize),
        libz_sys::Z_BUF_ERROR => Err(Error::OutputTooSmall),
        libz_sys::Z_MEM_ERROR => Err(DeflateError::MemError.into()),
        libz_sys::Z_STREAM_ERROR => Err(DeflateError::StreamError.into()),
        _ => Err(Error::InvalidData),
    }
}

pub fn zlib_decompress(input: &[u8], output: &mut [u8]) -> Result<usize, Error> {
    let mut output_len = output.len() as libz_sys::uLongf;
    // SAFETY: both buffers remain alive and exclusively borrowed for the call;
    // output_len is initialized to the actual output capacity.
    let code = unsafe {
        libz_sys::uncompress(
            output.as_mut_ptr(),
            &mut output_len,
            input.as_ptr(),
            input.len() as libz_sys::uLong,
        )
    };
    match code {
        libz_sys::Z_OK => Ok(output_len as usize),
        libz_sys::Z_BUF_ERROR => Err(Error::OutputTooSmall),
        libz_sys::Z_MEM_ERROR => Err(InflateError::MemError.into()),
        libz_sys::Z_STREAM_ERROR => Err(InflateError::StreamError.into()),
        _ => Err(Error::InvalidData),
    }
}

pub fn crc32(input: &[u8]) -> u32 {
    let mut checksum = 0;
    for chunk in input.chunks(libz_sys::uInt::MAX as usize) {
        // SAFETY: each chunk pointer and length remain valid for the call.
        checksum =
            unsafe { libz_sys::crc32(checksum, chunk.as_ptr(), chunk.len() as libz_sys::uInt) };
    }
    checksum as u32
}

unsafe extern "C" fn allocate(
    _opaque: libz_sys::voidpf,
    items: libz_sys::uInt,
    size: libz_sys::uInt,
) -> libz_sys::voidpf {
    // SAFETY: zlib provides allocation sizes and does not retain Rust references.
    unsafe { libc::calloc(items as usize, size as usize) }
}

unsafe extern "C" fn deallocate(_opaque: libz_sys::voidpf, address: libz_sys::voidpf) {
    // SAFETY: address was returned by allocate or is null, as permitted by free.
    unsafe { libc::free(address) }
}

fn empty_stream() -> libz_sys::z_stream {
    libz_sys::z_stream {
        next_in: ptr::null_mut(),
        avail_in: 0,
        total_in: 0,
        next_out: ptr::null_mut(),
        avail_out: 0,
        total_out: 0,
        msg: ptr::null_mut(),
        state: ptr::null_mut(),
        zalloc: allocate,
        zfree: deallocate,
        opaque: ptr::null_mut(),
        data_type: 0,
        adler: 0,
        reserved: 0,
    }
}

#[derive(Clone, Copy)]
enum StreamKind {
    Deflate,
    Inflate,
}

struct ZStream {
    raw: Box<libz_sys::z_stream>,
    kind: StreamKind,
}

impl ZStream {
    fn new_deflate(level: i32) -> Result<Self, DeflateError> {
        let mut raw = Box::new(empty_stream());
        // SAFETY: raw is initialized, the version comes from the linked zlib,
        // and the supplied size matches libz-sys's z_stream layout.
        let code = unsafe {
            libz_sys::deflateInit2_(
                &mut *raw,
                level,
                libz_sys::Z_DEFLATED,
                GZIP_WINDOW_BITS,
                DEFAULT_MEM_LEVEL,
                libz_sys::Z_DEFAULT_STRATEGY,
                libz_sys::zlibVersion(),
                size_of::<libz_sys::z_stream>() as i32,
            )
        };
        match code {
            libz_sys::Z_OK => Ok(Self {
                raw,
                kind: StreamKind::Deflate,
            }),
            libz_sys::Z_MEM_ERROR => Err(DeflateError::MemError),
            libz_sys::Z_DATA_ERROR => Err(DeflateError::DataError),
            _ => Err(DeflateError::StreamError),
        }
    }

    fn new_inflate() -> Result<Self, InflateError> {
        let mut raw = Box::new(empty_stream());
        // SAFETY: raw is initialized, the version comes from the linked zlib,
        // and the supplied size matches libz-sys's z_stream layout.
        let code = unsafe {
            libz_sys::inflateInit2_(
                &mut *raw,
                AUTO_WINDOW_BITS,
                libz_sys::zlibVersion(),
                size_of::<libz_sys::z_stream>() as i32,
            )
        };
        match code {
            libz_sys::Z_OK => Ok(Self {
                raw,
                kind: StreamKind::Inflate,
            }),
            libz_sys::Z_MEM_ERROR => Err(InflateError::MemError),
            _ => Err(InflateError::StreamError),
        }
    }

    fn set_buffers(&mut self, input: &[u8], output: &mut [u8]) {
        self.raw.next_in = input.as_ptr().cast_mut();
        self.raw.avail_in = input.len().min(libz_sys::uInt::MAX as usize) as libz_sys::uInt;
        self.raw.next_out = output.as_mut_ptr();
        self.raw.avail_out = output.len().min(libz_sys::uInt::MAX as usize) as libz_sys::uInt;
    }

    fn progress(&self, input: &[u8], output: &[u8], stream_end: bool) -> StreamProgress {
        let consumed = input.len().min(libz_sys::uInt::MAX as usize) - self.raw.avail_in as usize;
        let produced = output.len().min(libz_sys::uInt::MAX as usize) - self.raw.avail_out as usize;
        StreamProgress {
            consumed,
            produced,
            drained: consumed == input.len() && produced < output.len(),
            stream_end,
        }
    }

    fn reset(&mut self) {
        // SAFETY: the stream is initialized and no input or output buffers are retained.
        let code = unsafe {
            match self.kind {
                StreamKind::Deflate => libz_sys::deflateReset(&mut *self.raw),
                StreamKind::Inflate => libz_sys::inflateReset(&mut *self.raw),
            }
        };
        assert_eq!(
            libz_sys::Z_OK,
            code,
            "failed to reset initialized zlib stream"
        );
    }
}

impl Drop for ZStream {
    fn drop(&mut self) {
        // SAFETY: the stream was initialized successfully and is dropped once.
        unsafe {
            match self.kind {
                StreamKind::Deflate => libz_sys::deflateEnd(&mut *self.raw),
                StreamKind::Inflate => libz_sys::inflateEnd(&mut *self.raw),
            };
        }
    }
}

pub struct GzipEncoder {
    stream: ZStream,
}

impl GzipEncoder {
    pub fn new(level: i32) -> Result<Self, Error> {
        if !(-1..=9).contains(&level) {
            return Err(Error::InvalidCompressionLevel);
        }
        Ok(Self {
            stream: ZStream::new_deflate(level)?,
        })
    }

    pub fn process(
        &mut self,
        input: &[u8],
        output: &mut [u8],
        finish: bool,
    ) -> Result<StreamProgress, Error> {
        self.stream.set_buffers(input, output);
        // SAFETY: the stream is initialized and its pointers reference the
        // live, exclusively borrowed buffers configured above.
        let code = unsafe {
            libz_sys::deflate(
                &mut *self.stream.raw,
                if finish {
                    libz_sys::Z_FINISH
                } else {
                    libz_sys::Z_NO_FLUSH
                },
            )
        };
        match code {
            libz_sys::Z_OK | libz_sys::Z_STREAM_END | libz_sys::Z_BUF_ERROR => Ok(self
                .stream
                .progress(input, output, code == libz_sys::Z_STREAM_END)),
            libz_sys::Z_STREAM_ERROR => Err(DeflateError::StreamError.into()),
            libz_sys::Z_DATA_ERROR => Err(DeflateError::DataError.into()),
            libz_sys::Z_MEM_ERROR => Err(DeflateError::MemError.into()),
            _ => Err(DeflateError::StreamError.into()),
        }
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
    stream: ZStream,
    needs_reset: bool,
}

impl StreamDecoder {
    pub fn new() -> Self {
        Self {
            stream: ZStream::new_inflate().expect("failed to initialize zlib inflate stream"),
            needs_reset: false,
        }
    }

    pub fn process(&mut self, input: &[u8], output: &mut [u8]) -> Result<StreamProgress, Error> {
        if self.needs_reset {
            self.stream.reset();
            self.needs_reset = false;
        }
        self.stream.set_buffers(input, output);
        // SAFETY: the stream is initialized and its pointers reference the
        // live, exclusively borrowed buffers configured above.
        let code = unsafe { libz_sys::inflate(&mut *self.stream.raw, libz_sys::Z_NO_FLUSH) };
        match code {
            libz_sys::Z_OK | libz_sys::Z_STREAM_END | libz_sys::Z_BUF_ERROR => {
                let stream_end = code == libz_sys::Z_STREAM_END;
                self.needs_reset = stream_end;
                Ok(self.stream.progress(input, output, stream_end))
            }
            libz_sys::Z_NEED_DICT => Err(InflateError::NeedDict {
                dict_id: self.stream.raw.adler as u32,
            }
            .into()),
            libz_sys::Z_DATA_ERROR => Err(InflateError::DataError.into()),
            libz_sys::Z_MEM_ERROR => Err(InflateError::MemError.into()),
            _ => Err(InflateError::StreamError.into()),
        }
    }
}

impl Default for StreamDecoder {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests;
