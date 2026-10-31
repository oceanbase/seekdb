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

//! Synchronous CXX adapter for the seekdb XML tree builder.
//!
//! Input and event buffers are borrowed only for the duration of a callback.
//! The caller owns `context`; parsing never retains it. Callbacks run on the
//! calling thread and may abort parsing by returning a nonzero seekdb error.

use crate::parse_ffi;

#[cxx::bridge(namespace = "oceanbase::common")]
pub mod ffi {
    struct XmlAttributeView {
        name: *const u8,
        name_len: usize,
        value: *const u8,
        value_len: usize,
    }

    unsafe extern "C++" {
        include!("rust/xml-parser/include/callbacks.h");

        unsafe fn xml_on_start_document(context: *mut u8) -> i32;
        unsafe fn xml_on_end_document(context: *mut u8) -> i32;
        unsafe fn xml_on_declaration(context: *mut u8, value: *const u8, value_len: usize) -> i32;
        unsafe fn xml_on_start_element(
            context: *mut u8,
            name: *const u8,
            name_len: usize,
            attributes: *const XmlAttributeView,
            attribute_count: usize,
            empty: bool,
        ) -> i32;
        unsafe fn xml_on_end_element(context: *mut u8, empty: bool) -> i32;
        unsafe fn xml_on_text(context: *mut u8, value: *const u8, value_len: usize) -> i32;
        unsafe fn xml_on_cdata(context: *mut u8, value: *const u8, value_len: usize) -> i32;
        unsafe fn xml_on_comment(context: *mut u8, value: *const u8, value_len: usize) -> i32;
        unsafe fn xml_on_processing_instruction(
            context: *mut u8,
            target: *const u8,
            target_len: usize,
            value: *const u8,
            value_len: usize,
        ) -> i32;
    }

    extern "Rust" {
        /// Returns zero, a callback's seekdb error, XML_PARSE_SYNTAX_ERROR,
        /// or one of the other small positive adapter errors documented in
        /// ob_xml_parser.cpp.
        unsafe fn xml_parse(
            input: *const u8,
            input_len: usize,
            content: bool,
            options: u64,
            context: *mut u8,
        ) -> i32;
    }
}

/// Parse XML and synchronously invoke the C++ tree builder.
///
/// # Safety
/// `input` must be readable for `input_len` bytes (or null only when empty),
/// and `context` must satisfy the C++ callback contract until this returns.
pub unsafe fn xml_parse(
    input: *const u8,
    input_len: usize,
    content: bool,
    options: u64,
    context: *mut u8,
) -> i32 {
    // SAFETY: the caller upholds the pointer contract above.
    unsafe { parse_ffi(input, input_len, content, options, context) }
}
