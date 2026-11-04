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

use crate::{
    crc32, zlib_compress_bound, DeflateError, Error, GzipEncoder, InflateError, StreamDecoder,
    StreamProgress,
};
use crate::zstd::{
    self, Decoder as ZstdDecoder, Directive, Encoder as ZstdEncoder, Error as ZstdError,
    ErrorKind,
};
use std::{mem::MaybeUninit, ptr::NonNull, slice};
use zstd_safe::WriteBuf;

#[cxx::bridge(namespace = "oceanbase::compression")]
mod ffi {
    #[derive(Debug)]
    enum Code {
        Ok,
        InvalidArgument,
        InvalidData,
        OutputTooSmall,
        OutOfMemory,
        AllocationFailed,
        InvalidState,
        ResourceLimit,
        Unsupported,
    }

    struct OperationResult {
        code: Code,
        size: usize,
    }

    struct BridgeProgress {
        code: Code,
        consumed: usize,
        produced: usize,
        drained: bool,
        stream_end: bool,
        finished: bool,
    }

    extern "Rust" {
        fn zlib_compress_bound(source_len: usize) -> usize;
        fn zlib_compress(input: &[u8], output: &mut [u8], level: i32) -> OperationResult;
        fn zlib_decompress(input: &[u8], output: &mut [u8]) -> OperationResult;
        fn crc32(input: &[u8]) -> u32;

        type GzipEncoder;
        fn new_gzip_encoder() -> Box<GzipEncoder>;
        fn gzip_process(
            stream: &mut GzipEncoder,
            input: &[u8],
            output: &mut [u8],
            finish: bool,
        ) -> BridgeProgress;
        fn gzip_reset(stream: &mut GzipEncoder);

        type StreamDecoder;
        fn new_stream_decoder() -> Box<StreamDecoder>;
        fn decoder_process(
            stream: &mut StreamDecoder,
            input: &[u8],
            output: &mut [u8],
        ) -> BridgeProgress;

        fn zstd_compress_bound(source_len: usize) -> usize;
        /// Output must be one writable allocation, exclusively held during the
        /// call and disjoint from input. Its bytes may be uninitialized.
        unsafe fn zstd_compress(
            input: &[u8],
            output: *mut u8,
            output_capacity: usize,
        ) -> OperationResult;
        /// Output must be one writable allocation, exclusively held during the
        /// call and disjoint from input. Its bytes may be uninitialized.
        unsafe fn zstd_decompress(
            input: &[u8],
            output: *mut u8,
            output_capacity: usize,
        ) -> OperationResult;

        type ZstdEncoder;
        fn new_zstd_encoder() -> Box<ZstdEncoder>;
        /// Output must be one writable allocation, exclusively held during the
        /// call and disjoint from input. Its bytes may be uninitialized.
        unsafe fn zstd_encode(
            stream: &mut ZstdEncoder,
            input: &[u8],
            output: *mut u8,
            output_capacity: usize,
            finish: bool,
        ) -> BridgeProgress;
        fn zstd_encoder_reset(stream: &mut ZstdEncoder) -> Code;

        type ZstdDecoder;
        fn new_zstd_decoder() -> Box<ZstdDecoder>;
        /// Output must be one writable allocation, exclusively held during the
        /// call and disjoint from input. Its bytes may be uninitialized.
        unsafe fn zstd_decode(
            stream: &mut ZstdDecoder,
            input: &[u8],
            output: *mut u8,
            output_capacity: usize,
        ) -> BridgeProgress;
        fn zstd_decoder_finish(stream: &mut ZstdDecoder) -> Code;
    }
}

fn code(error: Error) -> ffi::Code {
    match error {
        Error::InvalidCompressionLevel => ffi::Code::InvalidArgument,
        Error::OutputTooSmall => ffi::Code::OutputTooSmall,
        Error::Deflate(DeflateError::MemError) | Error::Inflate(InflateError::MemError) => {
            ffi::Code::OutOfMemory
        }
        Error::InvalidData | Error::Inflate(_) | Error::Deflate(_) => ffi::Code::InvalidData,
    }
}

fn operation(result: Result<usize, Error>) -> ffi::OperationResult {
    match result {
        Ok(size) => ffi::OperationResult {
            code: ffi::Code::Ok,
            size,
        },
        Err(error) => ffi::OperationResult {
            code: code(error),
            size: 0,
        },
    }
}

pub fn zlib_compress(input: &[u8], output: &mut [u8], level: i32) -> ffi::OperationResult {
    operation(crate::zlib_compress(input, output, level))
}

pub fn zlib_decompress(input: &[u8], output: &mut [u8]) -> ffi::OperationResult {
    operation(crate::zlib_decompress(input, output))
}

fn progress(result: Result<StreamProgress, Error>) -> ffi::BridgeProgress {
    match result {
        Ok(progress) => ffi::BridgeProgress {
            code: ffi::Code::Ok,
            consumed: progress.consumed,
            produced: progress.produced,
            drained: progress.drained,
            stream_end: progress.stream_end,
            finished: progress.stream_end,
        },
        Err(error) => ffi::BridgeProgress {
            code: code(error),
            consumed: 0,
            produced: 0,
            drained: false,
            stream_end: false,
            finished: false,
        },
    }
}

fn new_gzip_encoder() -> Box<GzipEncoder> {
    Box::new(GzipEncoder::default())
}

fn new_stream_decoder() -> Box<StreamDecoder> {
    Box::new(StreamDecoder::new())
}

fn gzip_process(
    stream: &mut GzipEncoder,
    input: &[u8],
    output: &mut [u8],
    finish: bool,
) -> ffi::BridgeProgress {
    progress(stream.process(input, output, finish))
}

fn gzip_reset(stream: &mut GzipEncoder) {
    stream.reset();
}

fn decoder_process(
    stream: &mut StreamDecoder,
    input: &[u8],
    output: &mut [u8],
) -> ffi::BridgeProgress {
    progress(stream.process(input, output))
}

/// A C++ allocation whose initialized prefix is tracked without reading its
/// remaining capacity or creating a Rust byte slice over uninitialized memory.
struct BorrowedOutput<'a> {
    storage: &'a mut [MaybeUninit<u8>],
    initialized: usize,
}

impl<'a> BorrowedOutput<'a> {
    /// # Safety
    /// Unless validation rejects the arguments, `output` must designate one
    /// writable allocation of at least `capacity` bytes for the duration of the
    /// call. That range must be exclusively borrowed, including against other
    /// C++ accesses. No initialized output bytes are required on entry.
    unsafe fn new(input: &[u8], output: *mut u8, capacity: usize) -> Result<Self, ZstdError> {
        let invalid = || ZstdError::new(ErrorKind::InvalidArgument);
        if capacity > isize::MAX as usize || (output.is_null() && capacity != 0) {
            return Err(invalid());
        }
        let output_start = output as usize;
        let output_end = output_start.checked_add(capacity).ok_or_else(invalid)?;
        let input_start = input.as_ptr() as usize;
        let input_end = input_start.checked_add(input.len()).ok_or_else(invalid)?;
        if capacity != 0
            && !input.is_empty()
            && output_start < input_end
            && input_start < output_end
        {
            return Err(invalid());
        }
        let pointer =
            NonNull::new(output.cast::<MaybeUninit<u8>>()).unwrap_or_else(NonNull::dangling);
        // SAFETY: the caller supplies a writable, exclusively borrowed
        // allocation. Bounds and overlap were checked above. MaybeUninit does
        // not require the allocation's bytes to be initialized.
        let storage = unsafe { slice::from_raw_parts_mut(pointer.as_ptr(), capacity) };
        Ok(Self {
            storage,
            initialized: 0,
        })
    }
}

// SAFETY: storage exclusively owns a writable range for its entire lifetime.
// Only filled_until, called after native initialization, advances the prefix
// exposed by as_slice. The remaining storage is never read as bytes.
unsafe impl WriteBuf for BorrowedOutput<'_> {
    fn as_slice(&self) -> &[u8] {
        // SAFETY: initialized is advanced only after those bytes were written,
        // and never exceeds the capacity of the single borrowed allocation.
        unsafe { slice::from_raw_parts(self.storage.as_ptr().cast(), self.initialized) }
    }

    fn capacity(&self) -> usize {
        self.storage.len()
    }

    fn as_mut_ptr(&mut self) -> *mut u8 {
        self.storage.as_mut_ptr().cast()
    }

    unsafe fn filled_until(&mut self, n: usize) {
        // The WriteBuf caller guarantees initialization of this prefix.
        assert!(n <= self.capacity());
        self.initialized = n;
    }
}

fn zstd_code(error: ZstdError) -> ffi::Code {
    match error.kind {
        ErrorKind::InvalidArgument => ffi::Code::InvalidArgument,
        ErrorKind::OutputTooSmall => ffi::Code::OutputTooSmall,
        ErrorKind::InvalidData | ErrorKind::TruncatedInput => ffi::Code::InvalidData,
        ErrorKind::AllocationFailed => ffi::Code::AllocationFailed,
        ErrorKind::InvalidState => ffi::Code::InvalidState,
        ErrorKind::ResourceLimit => ffi::Code::ResourceLimit,
        ErrorKind::DictionaryRequired | ErrorKind::Unsupported => ffi::Code::Unsupported,
    }
}

fn zstd_operation(result: Result<usize, ZstdError>) -> ffi::OperationResult {
    match result {
        Ok(size) => ffi::OperationResult {
            code: ffi::Code::Ok,
            size,
        },
        Err(error) => ffi::OperationResult {
            code: zstd_code(error),
            size: 0,
        },
    }
}

fn zstd_progress(result: Result<zstd::Progress, ZstdError>) -> ffi::BridgeProgress {
    match result {
        Ok(progress) => ffi::BridgeProgress {
            code: ffi::Code::Ok,
            consumed: progress.consumed,
            produced: progress.produced,
            drained: false,
            stream_end: progress.finished,
            finished: progress.finished,
        },
        Err(error) => ffi::BridgeProgress {
            code: zstd_code(error),
            consumed: 0,
            produced: 0,
            drained: false,
            stream_end: false,
            finished: false,
        },
    }
}

fn zstd_compress_bound(source_len: usize) -> usize {
    zstd::compress_bound(source_len)
}

/// # Safety
/// Output must satisfy BorrowedOutput::new's writable allocation and exclusive
/// borrowing contract for this call. It need not contain initialized bytes.
unsafe fn zstd_compress(
    input: &[u8],
    output: *mut u8,
    output_capacity: usize,
) -> ffi::OperationResult {
    // SAFETY: the CXX caller supplies the documented writable allocation.
    zstd_operation(
        unsafe { BorrowedOutput::new(input, output, output_capacity) }
            .and_then(|mut output| zstd::compress(input, &mut output)),
    )
}

/// # Safety
/// Output must satisfy BorrowedOutput::new's writable allocation and exclusive
/// borrowing contract for this call. It need not contain initialized bytes.
unsafe fn zstd_decompress(
    input: &[u8],
    output: *mut u8,
    output_capacity: usize,
) -> ffi::OperationResult {
    // SAFETY: the CXX caller supplies the documented writable allocation.
    zstd_operation(
        unsafe { BorrowedOutput::new(input, output, output_capacity) }
            .and_then(|mut output| zstd::decompress(input, &mut output)),
    )
}

fn new_zstd_encoder() -> Box<ZstdEncoder> {
    Box::new(ZstdEncoder::default())
}

/// # Safety
/// Output must satisfy BorrowedOutput::new's writable allocation and exclusive
/// borrowing contract for this call. It need not contain initialized bytes.
unsafe fn zstd_encode(
    stream: &mut ZstdEncoder,
    input: &[u8],
    output: *mut u8,
    output_capacity: usize,
    finish: bool,
) -> ffi::BridgeProgress {
    let directive = if finish {
        Directive::Finish
    } else {
        Directive::Continue
    };
    // SAFETY: the CXX caller supplies the documented writable allocation.
    zstd_progress(
        unsafe { BorrowedOutput::new(input, output, output_capacity) }
            .and_then(|mut output| stream.process(input, &mut output, directive)),
    )
}

fn zstd_encoder_reset(stream: &mut ZstdEncoder) -> ffi::Code {
    stream.reset().map_or_else(zstd_code, |()| ffi::Code::Ok)
}

fn new_zstd_decoder() -> Box<ZstdDecoder> {
    Box::new(ZstdDecoder::default())
}

/// # Safety
/// Output must satisfy BorrowedOutput::new's writable allocation and exclusive
/// borrowing contract for this call. It need not contain initialized bytes.
unsafe fn zstd_decode(
    stream: &mut ZstdDecoder,
    input: &[u8],
    output: *mut u8,
    output_capacity: usize,
) -> ffi::BridgeProgress {
    // SAFETY: the CXX caller supplies the documented writable allocation.
    zstd_progress(
        unsafe { BorrowedOutput::new(input, output, output_capacity) }
            .and_then(|mut output| stream.process(input, &mut output)),
    )
}

fn zstd_decoder_finish(stream: &mut ZstdDecoder) -> ffi::Code {
    stream.finish().map_or_else(zstd_code, |()| ffi::Code::Ok)
}

#[cfg(test)]
mod tests;
