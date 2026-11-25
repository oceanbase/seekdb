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

use crate::{parse, Event, EventHandler, ParseError, ParseMode, ParseOptions};
use std::panic::{catch_unwind, AssertUnwindSafe};

const XML_PARSE_RECOVER: u64 = 1 << 1;
const XML_PARSE_SYNTAX_CHECK: u64 = 1 << 3;
const XML_PARSE_CONTENT_ALLOW_XML_DECL: u64 = 1 << 4;
const XML_PARSE_NOT_ENTITY_REPLACE: u64 = 1 << 6;

const XML_PARSE_SYNTAX_ERROR: i32 = 1;
const XML_PARSE_INTERNAL_ERROR: i32 = 2;
const XML_PARSE_ARGUMENT_ERROR: i32 = 3;

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
        /// Returns zero, a callback error, or a positive adapter status mapped
        /// by map_parse_status in ob_rust_xml_parser.cpp.
        unsafe fn xml_parse(
            input: *const u8,
            input_len: usize,
            content: bool,
            options: u64,
            context: *mut u8,
        ) -> i32;
    }
}

fn parse_options(bits: u64) -> ParseOptions {
    ParseOptions {
        recover: bits & XML_PARSE_RECOVER != 0,
        syntax_check: bits & XML_PARSE_SYNTAX_CHECK != 0,
        content_allow_xml_decl: bits & XML_PARSE_CONTENT_ALLOW_XML_DECL != 0,
        preserve_entity_references: bits & XML_PARSE_NOT_ENTITY_REPLACE != 0,
    }
}

struct BridgeHandler {
    context: *mut u8,
}

impl EventHandler for BridgeHandler {
    type Error = i32;

    fn on_event(&mut self, event: Event<'_>) -> Result<(), Self::Error> {
        let context = self.context;
        let status = unsafe {
            match event {
                Event::StartDocument => ffi::xml_on_start_document(context),
                Event::EndDocument => ffi::xml_on_end_document(context),
                Event::Declaration(value) => {
                    ffi::xml_on_declaration(context, value.as_ptr(), value.len())
                }
                Event::StartElement {
                    name,
                    attributes,
                    empty,
                } => {
                    let views = attributes
                        .iter()
                        .map(|attribute| ffi::XmlAttributeView {
                            name: attribute.name.as_ptr(),
                            name_len: attribute.name.len(),
                            value: attribute.value.as_ptr(),
                            value_len: attribute.value.len(),
                        })
                        .collect::<Vec<_>>();
                    ffi::xml_on_start_element(
                        context,
                        name.as_ptr(),
                        name.len(),
                        views.as_ptr(),
                        views.len(),
                        empty,
                    )
                }
                Event::EndElement { empty, .. } => ffi::xml_on_end_element(context, empty),
                Event::Text(value) => ffi::xml_on_text(context, value.as_ptr(), value.len()),
                Event::CData(value) => ffi::xml_on_cdata(context, value.as_ptr(), value.len()),
                Event::Comment(value) => ffi::xml_on_comment(context, value.as_ptr(), value.len()),
                Event::ProcessingInstruction { target, value } => {
                    ffi::xml_on_processing_instruction(
                        context,
                        target.as_ptr(),
                        target.len(),
                        value.as_ptr(),
                        value.len(),
                    )
                }
            }
        };
        if status == 0 {
            Ok(())
        } else {
            Err(status)
        }
    }
}

/// Parse XML and synchronously invoke the C++ tree builder.
///
/// # Safety
/// `input` must be readable for `input_len` bytes (or null only when empty),
/// and `context` must satisfy the C++ callback contract until this returns.
unsafe fn xml_parse(
    input: *const u8,
    input_len: usize,
    content: bool,
    options: u64,
    context: *mut u8,
) -> i32 {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if context.is_null() || (input.is_null() && input_len != 0) {
            return XML_PARSE_ARGUMENT_ERROR;
        }
        let input = if input_len == 0 {
            &[][..]
        } else {
            // SAFETY: the CXX caller guarantees the readable input buffer.
            unsafe { std::slice::from_raw_parts(input, input_len) }
        };
        let options = parse_options(options);
        let mut handler = BridgeHandler { context };
        match parse(
            input,
            if content {
                ParseMode::Content
            } else {
                ParseMode::Document
            },
            options,
            &mut handler,
        ) {
            Ok(()) => 0,
            Err(ParseError::Handler(status)) => status,
            Err(ParseError::Xml(_)) => XML_PARSE_SYNTAX_ERROR,
        }
    }));
    result.unwrap_or(XML_PARSE_INTERNAL_ERROR)
}
