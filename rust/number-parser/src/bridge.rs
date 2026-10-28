/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#[cxx::bridge(namespace = "oceanbase::number_parser")]
mod ffi {
    enum Status {
        Ok,
        Invalid,
        OutOfRange,
    }

    extern "Rust" {
        fn parse_f64(input: &[u8], value: &mut f64, consumed: &mut usize) -> Status;
        fn parse_f32(input: &[u8], value: &mut f32, consumed: &mut usize) -> Status;
        fn parse_u32(input: &[u8], value: &mut u32, consumed: &mut usize) -> Status;
    }
}

use ffi::Status;

fn status(kind: Option<crate::ErrorKind>) -> Status {
    match kind {
        None => Status::Ok,
        Some(crate::ErrorKind::Invalid) => Status::Invalid,
        Some(crate::ErrorKind::OutOfRange) => Status::OutOfRange,
    }
}

pub(crate) fn parse_f64(input: &[u8], value: &mut f64, consumed: &mut usize) -> Status {
    match crate::parse_f64(input) {
        Ok(parsed) => {
            *value = parsed.value;
            *consumed = parsed.consumed;
            Status::Ok
        }
        Err(error) => {
            *value = 0.0;
            *consumed = error.consumed;
            status(Some(error.kind))
        }
    }
}

pub(crate) fn parse_f32(input: &[u8], value: &mut f32, consumed: &mut usize) -> Status {
    match crate::parse_f32(input) {
        Ok(parsed) => {
            *value = parsed.value;
            *consumed = parsed.consumed;
            Status::Ok
        }
        Err(error) => {
            *value = 0.0;
            *consumed = error.consumed;
            status(Some(error.kind))
        }
    }
}

pub(crate) fn parse_u32(input: &[u8], value: &mut u32, consumed: &mut usize) -> Status {
    match crate::parse_u32(input) {
        Ok(parsed) => {
            *value = parsed.value;
            *consumed = parsed.consumed;
            Status::Ok
        }
        Err(error) => {
            *value = 0;
            *consumed = error.consumed;
            status(Some(error.kind))
        }
    }
}
