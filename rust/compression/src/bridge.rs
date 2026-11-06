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
    StreamProgress, GZIP_COMPRESSION_LEVEL,
};

pub struct GzipEncoderHandle(Result<GzipEncoder, Error>);

pub struct StreamDecoderHandle(Result<StreamDecoder, Error>);

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

        type GzipEncoderHandle;
        fn new_gzip_encoder() -> Box<GzipEncoderHandle>;
        fn gzip_creation_status(stream: &GzipEncoderHandle) -> Code;
        fn gzip_process(
            stream: &mut GzipEncoderHandle,
            input: &[u8],
            output: &mut [u8],
            finish: bool,
        ) -> BridgeProgress;
        fn gzip_reset(stream: &mut GzipEncoderHandle);

        type StreamDecoderHandle;
        fn new_stream_decoder() -> Box<StreamDecoderHandle>;
        fn decoder_creation_status(stream: &StreamDecoderHandle) -> Code;
        fn decoder_process(
            stream: &mut StreamDecoderHandle,
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

fn new_gzip_encoder() -> Box<GzipEncoderHandle> {
    Box::new(GzipEncoderHandle(GzipEncoder::new(GZIP_COMPRESSION_LEVEL)))
}

fn new_stream_decoder() -> Box<StreamDecoderHandle> {
    Box::new(StreamDecoderHandle(StreamDecoder::new()))
}

fn gzip_creation_status(stream: &GzipEncoderHandle) -> ffi::Code {
    stream
        .0
        .as_ref()
        .map_or_else(|error| code(*error), |_| ffi::Code::Ok)
}

fn decoder_creation_status(stream: &StreamDecoderHandle) -> ffi::Code {
    stream
        .0
        .as_ref()
        .map_or_else(|error| code(*error), |_| ffi::Code::Ok)
}

fn gzip_process(
    stream: &mut GzipEncoderHandle,
    input: &[u8],
    output: &mut [u8],
    finish: bool,
) -> ffi::BridgeProgress {
    progress(match &mut stream.0 {
        Ok(stream) => stream.process(input, output, finish),
        Err(error) => Err(*error),
    })
}

fn gzip_reset(stream: &mut GzipEncoderHandle) {
    if let Ok(stream) = &mut stream.0 {
        stream.reset();
    }
}

fn decoder_process(
    stream: &mut StreamDecoderHandle,
    input: &[u8],
    output: &mut [u8],
) -> ffi::BridgeProgress {
    progress(match &mut stream.0 {
        Ok(stream) => stream.process(input, output),
        Err(error) => Err(*error),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn failed_stream_handles_preserve_creation_errors() {
        for (deflate_error, inflate_error, expected_code) in [
            (
                DeflateError::MemError,
                InflateError::MemError,
                ffi::Code::OutOfMemory,
            ),
            (
                DeflateError::StreamError,
                InflateError::StreamError,
                ffi::Code::InvalidData,
            ),
        ] {
            let mut encoder = GzipEncoderHandle(Err(deflate_error.into()));
            let mut decoder = StreamDecoderHandle(Err(inflate_error.into()));
            assert!(gzip_creation_status(&encoder) == expected_code);
            assert!(decoder_creation_status(&decoder) == expected_code);

            let mut output = [0xab; 32];
            let compressed = gzip_process(&mut encoder, b"input", &mut output, true);
            let decoded = decoder_process(&mut decoder, b"input", &mut output);
            for progress in [compressed, decoded] {
                assert!(progress.code == expected_code);
                assert_eq!(0, progress.consumed);
                assert_eq!(0, progress.produced);
                assert!(!progress.drained);
                assert!(!progress.stream_end);
            }
            assert_eq!([0xab; 32], output);
            gzip_reset(&mut encoder);
            assert!(gzip_creation_status(&encoder) == expected_code);
        }
        assert!(gzip_creation_status(&new_gzip_encoder()) == ffi::Code::Ok);
        assert!(decoder_creation_status(&new_stream_decoder()) == ffi::Code::Ok);
    }
}
