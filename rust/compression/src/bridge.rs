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

use crate::zlib::{
    self, crc32, DeflateError, Error, GzipEncoder, InflateError, StreamDecoder, StreamProgress,
};
use crate::zstd::{
    self, Decoder as ZstdDecoder, Directive, Encoder as ZstdEncoder, Error as ZstdError, ErrorKind,
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
        SizeOverflow,
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
        fn compress_bound(algorithm: u8, source_len: i64) -> OperationResult;
        /// Input and output must satisfy the buffer contracts documented on
        /// borrowed_buffers. Their lengths must be nonnegative.
        unsafe fn compress(
            algorithm: u8,
            input: *const u8,
            input_size: i64,
            output: *mut u8,
            output_capacity: i64,
        ) -> OperationResult;
        /// Input and output must satisfy the buffer contracts documented on
        /// borrowed_buffers. Their lengths must be nonnegative.
        unsafe fn decompress(
            algorithm: u8,
            input: *const u8,
            input_size: i64,
            output: *mut u8,
            output_capacity: i64,
        ) -> OperationResult;
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

#[repr(u8)]
enum Algorithm {
    None = 1,
    Zstd = 2,
    Zlib = 3,
}

impl TryFrom<u8> for Algorithm {
    type Error = ffi::Code;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::None),
            2 => Ok(Self::Zstd),
            3 => Ok(Self::Zlib),
            _ => Err(ffi::Code::Unsupported),
        }
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

fn operation_code(result: Result<usize, ffi::Code>) -> ffi::OperationResult {
    match result {
        Ok(size) => ffi::OperationResult {
            code: ffi::Code::Ok,
            size,
        },
        Err(code) => ffi::OperationResult { code, size: 0 },
    }
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

struct BufferError;

impl<'a> BorrowedOutput<'a> {
    /// # Safety
    /// Unless validation rejects the arguments, `output` must designate one
    /// writable allocation of at least `capacity` bytes for the duration of the
    /// call. That range must be exclusively borrowed, including against other
    /// C++ accesses. No initialized output bytes are required on entry.
    unsafe fn new(input: &[u8], output: *mut u8, capacity: usize) -> Result<Self, BufferError> {
        let invalid = || BufferError;
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

impl From<BufferError> for ZstdError {
    fn from(_: BufferError) -> Self {
        Self::new(ErrorKind::InvalidArgument)
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

fn buffer_len(len: i64) -> Result<usize, ffi::Code> {
    let len = usize::try_from(len).map_err(|_| ffi::Code::InvalidArgument)?;
    if len > isize::MAX as usize {
        Err(ffi::Code::InvalidArgument)
    } else {
        Ok(len)
    }
}

fn compress_bound(algorithm: u8, source_len: i64) -> ffi::OperationResult {
    let result = buffer_len(source_len).and_then(|source_len| {
        let bound = match Algorithm::try_from(algorithm)? {
            Algorithm::None => source_len,
            Algorithm::Zstd => zstd::compress_bound(source_len),
            Algorithm::Zlib => zlib::zlib_compress_bound(source_len),
        };
        if bound < source_len || bound as u128 > i64::MAX as u128 {
            Err(ffi::Code::SizeOverflow)
        } else {
            Ok(bound)
        }
    });
    operation_code(result)
}

/// # Safety
/// A non-null input must designate one readable allocation of input_size
/// bytes. Output must satisfy BorrowedOutput::new's writable allocation and
/// exclusive borrowing contract. Both ranges must remain live for this call.
unsafe fn borrowed_buffers<'a>(
    input: *const u8,
    input_size: i64,
    output: *mut u8,
    output_capacity: i64,
) -> Result<(&'a [u8], BorrowedOutput<'a>), ffi::Code> {
    let input_size = buffer_len(input_size)?;
    let output_capacity = buffer_len(output_capacity)?;
    if input.is_null() && input_size != 0 {
        return Err(ffi::Code::InvalidArgument);
    }
    let input_start = input as usize;
    input_start
        .checked_add(input_size)
        .ok_or(ffi::Code::InvalidArgument)?;
    let input_pointer = NonNull::new(input.cast_mut()).unwrap_or_else(NonNull::dangling);
    // SAFETY: the caller supplies a readable allocation, and the range was
    // checked for null, overflow and excessive length above.
    let input = unsafe { slice::from_raw_parts(input_pointer.as_ptr(), input_size) };
    // SAFETY: the caller supplies the output allocation; this checks its range
    // and verifies that it does not overlap input.
    let output = unsafe { BorrowedOutput::new(input, output, output_capacity) }
        .map_err(|_| ffi::Code::InvalidArgument)?;
    Ok((input, output))
}

fn copy_input(input: &[u8], output: &mut BorrowedOutput<'_>) -> Result<usize, ffi::Code> {
    if input.len() > output.storage.len() {
        return Err(ffi::Code::OutputTooSmall);
    }
    // SAFETY: BorrowedOutput validated capacity, exclusive access and lack of
    // overlap. The input slice is readable for its full length.
    unsafe {
        std::ptr::copy_nonoverlapping(
            input.as_ptr(),
            output.storage.as_mut_ptr().cast::<u8>(),
            input.len(),
        );
    }
    output.initialized = input.len();
    Ok(input.len())
}

/// # Safety
/// The pointers must satisfy borrowed_buffers' contracts.
unsafe fn compress(
    algorithm: u8,
    input: *const u8,
    input_size: i64,
    output: *mut u8,
    output_capacity: i64,
) -> ffi::OperationResult {
    let result = (|| {
        let algorithm = Algorithm::try_from(algorithm)?;
        // SAFETY: forwarded from this function's caller contract.
        let (input, mut output) =
            unsafe { borrowed_buffers(input, input_size, output, output_capacity) }?;
        match algorithm {
            Algorithm::None => copy_input(input, &mut output),
            Algorithm::Zstd => zstd::compress(input, &mut output).map_err(zstd_code),
            Algorithm::Zlib => {
                zlib::zlib_compress_uninit(input, output.storage, zlib::DEFAULT_COMPRESSION_LEVEL)
                    .map_err(code)
            }
        }
    })();
    operation_code(result)
}

/// # Safety
/// The pointers must satisfy borrowed_buffers' contracts.
unsafe fn decompress(
    algorithm: u8,
    input: *const u8,
    input_size: i64,
    output: *mut u8,
    output_capacity: i64,
) -> ffi::OperationResult {
    let result = (|| {
        let algorithm = Algorithm::try_from(algorithm)?;
        // SAFETY: forwarded from this function's caller contract.
        let (input, mut output) =
            unsafe { borrowed_buffers(input, input_size, output, output_capacity) }?;
        match algorithm {
            Algorithm::None => copy_input(input, &mut output),
            Algorithm::Zstd => zstd::decompress(input, &mut output).map_err(zstd_code),
            Algorithm::Zlib => zlib::zlib_decompress_uninit(input, output.storage).map_err(code),
        }
    })();
    operation_code(result)
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
            .map_err(ZstdError::from)
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
            .map_err(ZstdError::from)
            .and_then(|mut output| stream.process(input, &mut output)),
    )
}

fn zstd_decoder_finish(stream: &mut ZstdDecoder) -> ffi::Code {
    stream.finish().map_or_else(zstd_code, |()| ffi::Code::Ok)
}

#[cfg(test)]
mod tests;
