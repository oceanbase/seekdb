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

#[cxx::bridge(namespace = "oceanbase::compression")]
mod ffi {
    enum Code {
        Ok,
        InvalidArgument,
        InvalidData,
        OutputTooSmall,
        OutOfMemory,
    }

    struct OperationResult {
        code: Code,
        size: u64,
    }

    struct BridgeProgress {
        code: Code,
        consumed: u64,
        produced: u64,
        drained: bool,
        stream_end: bool,
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
            size: size as u64,
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
            consumed: progress.consumed as u64,
            produced: progress.produced as u64,
            drained: progress.drained,
            stream_end: progress.stream_end,
        },
        Err(error) => ffi::BridgeProgress {
            code: code(error),
            consumed: 0,
            produced: 0,
            drained: false,
            stream_end: false,
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
