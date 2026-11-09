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

//! Standard zstd frames and incremental sessions backed by bundled libzstd.
//!
//! Contexts allocate lazily and own all native workspace. No caller buffer is
//! retained between calls. A failed operation requires an explicit reset.
//! Calls rejected before native processing leave the existing session intact.
//! On a codec error, any bytes written to the supplied output must be discarded.

use std::fmt;
use zstd_safe::{
    CCtx, CParameter, DCtx, DParameter, InBuffer, OutBuffer, ResetDirective, WriteBuf,
};

pub const DEFAULT_COMPRESSION_LEVEL: i32 = 1;
pub const DEFAULT_WINDOW_LOG_MAX: u32 = 27;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ErrorKind {
    InvalidArgument,
    OutputTooSmall,
    InvalidData,
    TruncatedInput,
    DictionaryRequired,
    AllocationFailed,
    InvalidState,
    ResourceLimit,
    Unsupported,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Error {
    pub kind: ErrorKind,
    native_code: Option<usize>,
}

impl Error {
    pub(crate) fn new(kind: ErrorKind) -> Self {
        Self {
            kind,
            native_code: None,
        }
    }

    pub fn native_code(self) -> Option<usize> {
        self.native_code
    }

    fn native(code: usize) -> Self {
        use zstd_sys::ZSTD_ErrorCode::*;
        // SAFETY: the native function only decodes a scalar error value.
        let kind = match unsafe { zstd_sys::ZSTD_getErrorCode(code) } {
            ZSTD_error_memory_allocation => ErrorKind::AllocationFailed,
            ZSTD_error_dstSize_tooSmall | ZSTD_error_noForwardProgress_destFull => {
                ErrorKind::OutputTooSmall
            }
            ZSTD_error_frameParameter_windowTooLarge | ZSTD_error_workSpace_tooSmall => {
                ErrorKind::ResourceLimit
            }
            ZSTD_error_dictionary_wrong => ErrorKind::DictionaryRequired,
            ZSTD_error_parameter_unsupported
            | ZSTD_error_parameter_combination_unsupported
            | ZSTD_error_frameParameter_unsupported
            | ZSTD_error_version_unsupported => ErrorKind::Unsupported,
            ZSTD_error_parameter_outOfBound
            | ZSTD_error_dstBuffer_null
            | ZSTD_error_dstBuffer_wrong
            | ZSTD_error_srcBuffer_wrong => ErrorKind::InvalidArgument,
            ZSTD_error_stage_wrong | ZSTD_error_init_missing => ErrorKind::InvalidState,
            ZSTD_error_srcSize_wrong | ZSTD_error_noForwardProgress_inputEmpty => {
                ErrorKind::TruncatedInput
            }
            _ => ErrorKind::InvalidData,
        };
        Self {
            kind,
            native_code: Some(code),
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.native_code {
            Some(code) => write!(
                formatter,
                "{:?}: {}",
                self.kind,
                zstd_safe::get_error_name(code)
            ),
            None => write!(formatter, "{:?}", self.kind),
        }
    }
}

impl std::error::Error for Error {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Directive {
    Continue,
    Finish,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Progress {
    pub consumed: usize,
    pub produced: usize,
    pub finished: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum EncodeState {
    Ready,
    Active,
    Finishing { remaining_input: usize },
    Finished,
    Failed,
}

pub struct Encoder {
    context: Option<CCtx<'static>>,
    level: i32,
    state: EncodeState,
}

impl Encoder {
    pub fn new(level: i32) -> Result<Self, Error> {
        if !(zstd_safe::min_c_level()..=zstd_safe::max_c_level()).contains(&level) {
            return Err(Error::new(ErrorKind::InvalidArgument));
        }
        Ok(Self {
            context: None,
            level,
            state: EncodeState::Ready,
        })
    }

    fn context(&mut self) -> Result<&mut CCtx<'static>, Error> {
        if self.context.is_none() {
            let mut context =
                CCtx::try_create().ok_or_else(|| Error::new(ErrorKind::AllocationFailed))?;
            context
                .set_parameter(CParameter::CompressionLevel(self.level))
                .map_err(Error::native)?;
            self.context = Some(context);
        }
        Ok(self.context.as_mut().unwrap())
    }

    pub fn encode_frame<C: WriteBuf + ?Sized>(
        &mut self,
        input: &[u8],
        output: &mut C,
    ) -> Result<usize, Error> {
        if !matches!(self.state, EncodeState::Ready | EncodeState::Finished) {
            return Err(Error::new(ErrorKind::InvalidState));
        }
        let result = self.context().and_then(|context| {
            context
                .reset(ResetDirective::SessionOnly)
                .map_err(Error::native)?;
            context.compress2(output, input).map_err(Error::native)
        });
        self.state = if result.is_ok() {
            EncodeState::Finished
        } else {
            EncodeState::Failed
        };
        result
    }

    /// Processes one chunk. During Finish, subsequent calls supply only the
    /// unconsumed tail of the final input until `finished` becomes true.
    pub fn process<C: WriteBuf + ?Sized>(
        &mut self,
        input: &[u8],
        output: &mut C,
        directive: Directive,
    ) -> Result<Progress, Error> {
        match self.state {
            EncodeState::Finished | EncodeState::Failed => {
                return Err(Error::new(ErrorKind::InvalidState));
            }
            EncodeState::Finishing { remaining_input }
                if directive != Directive::Finish || input.len() != remaining_input =>
            {
                return Err(Error::new(ErrorKind::InvalidState));
            }
            _ => {}
        }
        if directive == Directive::Finish {
            self.state = EncodeState::Finishing {
                remaining_input: input.len(),
            };
        }
        if output.capacity() == 0 {
            return Ok(Progress::default());
        }
        let mut source = InBuffer::around(input);
        let mut destination = OutBuffer::around(output);
        let end = match directive {
            Directive::Continue => zstd_sys::ZSTD_EndDirective::ZSTD_e_continue,
            Directive::Finish => zstd_sys::ZSTD_EndDirective::ZSTD_e_end,
        };
        let result = self.context().and_then(|context| {
            context
                .compress_stream2(&mut destination, &mut source, end)
                .map_err(Error::native)
        });
        match result {
            Ok(remaining) => {
                let finished = directive == Directive::Finish && remaining == 0;
                self.state = if finished {
                    EncodeState::Finished
                } else if directive == Directive::Finish {
                    EncodeState::Finishing {
                        remaining_input: input.len() - source.pos,
                    }
                } else {
                    EncodeState::Active
                };
                Ok(Progress {
                    consumed: source.pos,
                    produced: destination.pos(),
                    finished,
                })
            }
            Err(error) => {
                self.state = EncodeState::Failed;
                Err(error)
            }
        }
    }

    pub fn reset(&mut self) -> Result<(), Error> {
        if let Some(context) = self.context.as_mut() {
            if let Err(code) = context.reset(ResetDirective::SessionOnly) {
                self.context = None;
                self.state = EncodeState::Failed;
                return Err(Error::native(code));
            }
        }
        self.state = EncodeState::Ready;
        Ok(())
    }

    /// Reports native workspace bytes, excluding the caller's buffers and
    /// allocator overhead. This does not enforce an allocation quota.
    pub fn memory_usage(&self) -> usize {
        self.context.as_ref().map_or(0, CCtx::sizeof)
    }
}

impl Default for Encoder {
    fn default() -> Self {
        Self {
            context: None,
            level: DEFAULT_COMPRESSION_LEVEL,
            state: EncodeState::Ready,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum DecodeState {
    Ready,
    Active,
    FrameBoundary,
    Finished,
    Failed,
}

pub struct Decoder {
    context: Option<DCtx<'static>>,
    window_log_max: u32,
    state: DecodeState,
}

impl Decoder {
    pub fn new(window_log_max: u32) -> Result<Self, Error> {
        // SAFETY: this query takes only an enum and returns scalar bounds. It
        // does not allocate or access a caller-provided pointer.
        let bounds = unsafe {
            zstd_sys::ZSTD_dParam_getBounds(zstd_sys::ZSTD_dParameter::ZSTD_d_windowLogMax)
        };
        if bounds.error != 0 {
            return Err(Error::native(bounds.error));
        }
        if window_log_max != 0
            && !(bounds.lowerBound as u32..=bounds.upperBound as u32).contains(&window_log_max)
        {
            return Err(Error::new(ErrorKind::InvalidArgument));
        }
        Ok(Self {
            context: None,
            window_log_max,
            state: DecodeState::Ready,
        })
    }

    fn context(&mut self) -> Result<&mut DCtx<'static>, Error> {
        if self.context.is_none() {
            let mut context =
                DCtx::try_create().ok_or_else(|| Error::new(ErrorKind::AllocationFailed))?;
            context
                .set_parameter(DParameter::WindowLogMax(self.window_log_max))
                .map_err(Error::native)?;
            self.context = Some(context);
        }
        Ok(self.context.as_mut().unwrap())
    }

    pub fn decode_frame<C: WriteBuf + ?Sized>(
        &mut self,
        input: &[u8],
        output: &mut C,
    ) -> Result<usize, Error> {
        if !matches!(
            self.state,
            DecodeState::Ready | DecodeState::FrameBoundary | DecodeState::Finished
        ) {
            return Err(Error::new(ErrorKind::InvalidState));
        }
        let result = if input.is_empty() {
            Err(Error::new(ErrorKind::TruncatedInput))
        } else {
            self.context().and_then(|context| {
                context
                    .reset(ResetDirective::SessionOnly)
                    .map_err(Error::native)?;
                context.decompress(output, input).map_err(Error::native)
            })
        };
        self.state = if result.is_ok() {
            DecodeState::FrameBoundary
        } else {
            DecodeState::Failed
        };
        result
    }

    /// Processes at most one frame boundary. Unconsumed bytes may start a
    /// following frame and must be supplied again by the caller.
    pub fn process<C: WriteBuf + ?Sized>(
        &mut self,
        input: &[u8],
        output: &mut C,
    ) -> Result<Progress, Error> {
        match self.state {
            DecodeState::Failed | DecodeState::Finished => {
                return Err(Error::new(ErrorKind::InvalidState));
            }
            DecodeState::FrameBoundary if input.is_empty() => {
                return Ok(Progress {
                    finished: true,
                    ..Progress::default()
                });
            }
            DecodeState::Ready if input.is_empty() => return Ok(Progress::default()),
            _ => {}
        }
        if !input.is_empty() {
            self.state = DecodeState::Active;
        }
        if output.capacity() == 0 {
            return Ok(Progress::default());
        }
        let mut source = InBuffer::around(input);
        let mut destination = OutBuffer::around(output);
        let result = self.context().and_then(|context| {
            context
                .decompress_stream(&mut destination, &mut source)
                .map_err(Error::native)
        });
        match result {
            Ok(remaining) => {
                let finished = remaining == 0;
                self.state = if finished {
                    DecodeState::FrameBoundary
                } else {
                    DecodeState::Active
                };
                Ok(Progress {
                    consumed: source.pos,
                    produced: destination.pos(),
                    finished,
                })
            }
            Err(error) => {
                self.state = DecodeState::Failed;
                Err(error)
            }
        }
    }

    /// Validates physical EOF after the caller has drained buffered output.
    pub fn finish(&mut self) -> Result<(), Error> {
        match self.state {
            DecodeState::FrameBoundary | DecodeState::Finished => {
                self.state = DecodeState::Finished;
                Ok(())
            }
            DecodeState::Ready | DecodeState::Active => {
                self.state = DecodeState::Failed;
                Err(Error::new(ErrorKind::TruncatedInput))
            }
            DecodeState::Failed => Err(Error::new(ErrorKind::InvalidState)),
        }
    }

    pub fn reset(&mut self) -> Result<(), Error> {
        if let Some(context) = self.context.as_mut() {
            if let Err(code) = context.reset(ResetDirective::SessionOnly) {
                self.context = None;
                self.state = DecodeState::Failed;
                return Err(Error::native(code));
            }
        }
        self.state = DecodeState::Ready;
        Ok(())
    }

    pub fn memory_usage(&self) -> usize {
        self.context.as_ref().map_or(0, DCtx::sizeof)
    }
}

impl Default for Decoder {
    fn default() -> Self {
        Self {
            context: None,
            window_log_max: DEFAULT_WINDOW_LOG_MAX,
            state: DecodeState::Ready,
        }
    }
}

pub fn compress_bound(source_len: usize) -> usize {
    zstd_safe::compress_bound(source_len)
}

pub fn compress<C: WriteBuf + ?Sized>(input: &[u8], output: &mut C) -> Result<usize, Error> {
    Encoder::default().encode_frame(input, output)
}

pub fn decompress<C: WriteBuf + ?Sized>(input: &[u8], output: &mut C) -> Result<usize, Error> {
    Decoder::default().decode_frame(input, output)
}

#[cfg(test)]
mod tests;
