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

//! Streaming XML parsing with seekdb's SQL compatibility rules.
//!
//! The native API accepts UTF-8 and BOM-marked UTF-16 memory input and emits
//! synchronous events.
//! Entity expansion, XML newline and attribute normalization, fragment rules,
//! and recover/syntax options live here rather than in the C++ adapter.

mod bridge;
mod name;

use quick_xml::escape::unescape;
use quick_xml::events::{BytesDecl, BytesStart, Event as XmlEvent};
use quick_xml::Reader;
use std::borrow::Cow;
use std::fmt;
use std::str::Utf8Error;

use name::{is_name_char, is_name_start_char};

const MAX_DEPTH: usize = 257;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ParseOptions {
    pub recover: bool,
    pub syntax_check: bool,
    pub content_allow_xml_decl: bool,
    pub preserve_entity_references: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParseMode {
    Document,
    Content,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Attribute<'a> {
    pub name: &'a [u8],
    pub value: Cow<'a, [u8]>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Event<'a> {
    StartDocument,
    EndDocument,
    Declaration(Cow<'a, [u8]>),
    StartElement {
        name: &'a [u8],
        attributes: Vec<Attribute<'a>>,
        empty: bool,
    },
    EndElement {
        name: &'a [u8],
        empty: bool,
    },
    Text(Cow<'a, [u8]>),
    CData(&'a [u8]),
    Comment(&'a [u8]),
    ProcessingInstruction {
        target: &'a [u8],
        value: &'a [u8],
    },
}

pub trait EventHandler {
    type Error;

    fn on_event(&mut self, event: Event<'_>) -> Result<(), Self::Error>;
}

#[derive(Debug)]
pub enum Error {
    Xml(quick_xml::Error),
    InvalidUtf8(Utf8Error),
    Syntax(&'static str),
    DepthLimit,
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Xml(error) => write!(formatter, "{error}"),
            Self::InvalidUtf8(error) => write!(formatter, "invalid UTF-8 XML input: {error}"),
            Self::Syntax(message) => formatter.write_str(message),
            Self::DepthLimit => formatter.write_str("XML nesting depth exceeds seekdb's limit"),
        }
    }
}

impl std::error::Error for Error {}

impl From<quick_xml::Error> for Error {
    fn from(error: quick_xml::Error) -> Self {
        Self::Xml(error)
    }
}

impl From<Utf8Error> for Error {
    fn from(error: Utf8Error) -> Self {
        Self::InvalidUtf8(error)
    }
}

#[derive(Debug)]
pub enum ParseError<E> {
    Xml(Error),
    Handler(E),
}

impl<E> From<Error> for ParseError<E> {
    fn from(error: Error) -> Self {
        Self::Xml(error)
    }
}

impl<E> From<quick_xml::Error> for ParseError<E> {
    fn from(error: quick_xml::Error) -> Self {
        Self::Xml(Error::Xml(error))
    }
}

impl<E> From<quick_xml::events::attributes::AttrError> for ParseError<E> {
    fn from(error: quick_xml::events::attributes::AttrError) -> Self {
        Self::Xml(Error::Xml(error.into()))
    }
}

impl<E> From<quick_xml::escape::EscapeError> for ParseError<E> {
    fn from(error: quick_xml::escape::EscapeError) -> Self {
        Self::Xml(Error::Xml(error.into()))
    }
}

impl<E> From<Utf8Error> for ParseError<E> {
    fn from(error: Utf8Error) -> Self {
        Self::Xml(Error::InvalidUtf8(error))
    }
}

type ParseResult<T, E> = Result<T, ParseError<E>>;

struct Parser<'a, H> {
    mode: ParseMode,
    options: ParseOptions,
    handler: &'a mut H,
    depth: usize,
    root_seen: bool,
    root_closed: bool,
    event_seen: bool,
}

impl<'a, H: EventHandler> Parser<'a, H> {
    fn emit(&mut self, event: Event<'_>) -> ParseResult<(), H::Error> {
        if self.options.syntax_check {
            Ok(())
        } else {
            self.handler.on_event(event).map_err(ParseError::Handler)
        }
    }

    fn start_element(
        &mut self,
        element: &BytesStart<'_>,
        empty: bool,
    ) -> ParseResult<(), H::Error> {
        if self.depth >= MAX_DEPTH {
            return Err(Error::DepthLimit.into());
        }
        if self.mode == ParseMode::Document && self.depth == 0 {
            if self.root_seen {
                return Err(Error::Syntax("an XML document has exactly one root element").into());
            }
            self.root_seen = true;
        }
        let name = element.name().0;
        validate_qname(name)?;
        validate_attribute_separators(element.attributes_raw())?;
        let mut attributes = Vec::new();
        for attribute in element.attributes() {
            let attribute = attribute?;
            validate_qname(attribute.key.0)?;
            let value = attribute_value(&attribute.value, self.options.preserve_entity_references)?;
            attributes.push(Attribute {
                name: attribute.key.0,
                value: Cow::Owned(value.into_bytes()),
            });
        }
        self.event_seen = true;
        self.emit(Event::StartElement {
            name,
            attributes,
            empty,
        })?;
        if empty {
            self.emit(Event::EndElement { name, empty: true })?;
            if self.depth == 0 {
                self.root_closed = true;
            }
        } else {
            self.depth += 1;
        }
        Ok(())
    }

    fn declaration(&mut self, declaration: &BytesDecl<'_>) -> ParseResult<(), H::Error> {
        if self.event_seen
            || (self.mode == ParseMode::Content && !self.options.content_allow_xml_decl)
        {
            return Err(Error::Syntax("misplaced XML declaration").into());
        }
        let fields = BytesStart::from_content(std::str::from_utf8(declaration)?, 3);
        validate_attribute_separators(fields.attributes_raw())?;
        let mut attributes = fields.attributes();
        let version = attributes
            .next()
            .transpose()?
            .ok_or(Error::Syntax("missing XML version"))?;
        if version.key.0 != b"version" {
            return Err(Error::Syntax("XML version must be the first declaration field").into());
        }
        if self.mode == ParseMode::Document && version.value.as_ref() != b"1.0" {
            return Err(Error::Syntax("unsupported XML version").into());
        }
        let mut optional_fields = [b"encoding".as_slice(), b"standalone".as_slice()].into_iter();
        for attribute in attributes {
            let attribute = attribute?;
            if !optional_fields.by_ref().any(|name| name == attribute.key.0) {
                return Err(Error::Syntax("invalid XML declaration field order").into());
            }
            if attribute.key.0 == b"standalone"
                && attribute.value.as_ref() != b"yes"
                && attribute.value.as_ref() != b"no"
            {
                return Err(Error::Syntax("invalid XML standalone value").into());
            }
        }
        let mut raw = Vec::with_capacity(declaration.len() + 4);
        raw.extend_from_slice(b"<?");
        raw.extend_from_slice(declaration);
        raw.extend_from_slice(b"?>");
        self.event_seen = true;
        self.emit(Event::Declaration(Cow::Owned(raw)))
    }

    fn text(&mut self, raw: &str) -> ParseResult<(), H::Error> {
        if raw.contains("]]>") {
            return Err(Error::Syntax("XML text contains the CDATA terminator").into());
        }
        if self.mode == ParseMode::Document && self.depth == 0 {
            if raw.bytes().all(is_xml_space) {
                return Ok(());
            }
            return Err(Error::Syntax("text is not allowed outside the document element").into());
        }
        let decoded = unescape(raw)?;
        validate_decoded_characters(&decoded)?;
        self.event_seen = true;
        if self.options.preserve_entity_references {
            self.emit(Event::Text(Cow::Borrowed(raw.as_bytes())))
        } else {
            match decoded {
                Cow::Borrowed(value) => self.emit(Event::Text(Cow::Borrowed(value.as_bytes()))),
                Cow::Owned(value) => self.emit(Event::Text(Cow::Owned(value.into_bytes()))),
            }
        }
    }

    fn finish_event(&mut self, event: XmlEvent<'_>) -> ParseResult<bool, H::Error> {
        match event {
            XmlEvent::Start(element) => self.start_element(&element, false)?,
            XmlEvent::Empty(element) => self.start_element(&element, true)?,
            XmlEvent::End(element) => {
                if self.depth == 0 {
                    return Err(Error::Syntax("unexpected closing element").into());
                }
                self.depth -= 1;
                if self.depth == 0 {
                    self.root_closed = true;
                }
                self.event_seen = true;
                self.emit(Event::EndElement {
                    name: element.name().0,
                    empty: false,
                })?;
            }
            // parse coalesces text and references before dispatching markup.
            XmlEvent::Text(_) | XmlEvent::GeneralRef(_) => {}
            XmlEvent::CData(value) => {
                if self.mode == ParseMode::Document && self.depth == 0 {
                    return Err(
                        Error::Syntax("CDATA is not allowed outside the document element").into(),
                    );
                }
                self.event_seen = true;
                self.emit(Event::CData(&value))?;
            }
            XmlEvent::Comment(value) => {
                self.event_seen = true;
                self.emit(Event::Comment(&value))?;
            }
            XmlEvent::Decl(declaration) => self.declaration(&declaration)?,
            XmlEvent::PI(instruction) => {
                let target = instruction.target();
                validate_qname(target)?;
                if target.contains(&b':') {
                    return Err(Error::Syntax(
                        "XML processing-instruction target must be an NCName",
                    )
                    .into());
                }
                if target.eq_ignore_ascii_case(b"xml") {
                    if self.mode == ParseMode::Content && self.options.content_allow_xml_decl {
                        return Ok(false);
                    }
                    return Err(Error::Syntax("reserved XML processing-instruction target").into());
                }
                let value = trim_xml_space_start(instruction.content());
                self.event_seen = true;
                self.emit(Event::ProcessingInstruction { target, value })?;
            }
            XmlEvent::DocType(_) => {
                return Err(Error::Syntax("DTD is not supported").into());
            }
            XmlEvent::Eof => return Ok(true),
        }
        Ok(false)
    }
}

/// Parse a document or fragment and synchronously deliver borrowed events.
pub fn parse<H: EventHandler>(
    input: &[u8],
    mode: ParseMode,
    options: ParseOptions,
    handler: &mut H,
) -> Result<(), ParseError<H::Error>> {
    let input = normalize_input(input, mode)?;
    let mut parser = Parser {
        mode,
        options,
        handler,
        depth: 0,
        root_seen: false,
        root_closed: false,
        event_seen: false,
    };
    parser.emit(Event::StartDocument)?;
    let result = (|| {
        let mut reader = Reader::from_str(input.as_ref());
        let config = reader.config_mut();
        config.check_comments = true;
        config.check_end_names = true;
        config.allow_unmatched_ends = false;
        config.expand_empty_elements = false;
        config.trim_text(false);
        let mut text_start = None;
        loop {
            let position = input.len() - reader.get_ref().len();
            if matches!(input.as_bytes().get(position), None | Some(b'<')) {
                if let Some(start) = text_start.take() {
                    parser.text(&input[start..position])?;
                }
            }
            let event_start = reader.buffer_position();
            let event = reader.read_event()?;
            if matches!(event, XmlEvent::Text(_) | XmlEvent::GeneralRef(_)) {
                let end = input.len() - reader.get_ref().len();
                let consumed = (reader.buffer_position() - event_start) as usize;
                text_start.get_or_insert(end - consumed);
            }
            if parser.finish_event(event)? {
                break;
            }
        }
        if parser.depth != 0 {
            return Err(Error::Syntax("unclosed XML element").into());
        }
        if mode == ParseMode::Document && (!parser.root_seen || !parser.root_closed) {
            return Err(Error::Syntax("XML document has no root element").into());
        }
        parser.emit(Event::EndDocument)?;
        Ok(())
    })();
    match result {
        Err(ParseError::Xml(_)) if options.recover => Ok(()),
        result => result,
    }
}

fn normalize_input(input: &[u8], mode: ParseMode) -> Result<Cow<'_, str>, Error> {
    let mut input = input;
    if mode == ParseMode::Document {
        while input.first().copied().map(is_xml_space).unwrap_or(false) {
            input = &input[1..];
        }
    }
    let decoded = if let Some(value) = input.strip_prefix(&[0xff, 0xfe]) {
        Cow::Owned(decode_utf16(value, true)?)
    } else if let Some(value) = input.strip_prefix(&[0xfe, 0xff]) {
        Cow::Owned(decode_utf16(value, false)?)
    } else {
        if input.starts_with(&[0xef, 0xbb, 0xbf]) {
            input = &input[3..];
        }
        Cow::Borrowed(std::str::from_utf8(input)?)
    };
    validate_raw_characters(&decoded)?;
    if decoded.contains('\r') {
        let mut normalized = String::with_capacity(input.len());
        let mut chars = decoded.chars().peekable();
        while let Some(ch) = chars.next() {
            if ch == '\r' {
                if chars.peek() == Some(&'\n') {
                    chars.next();
                }
                normalized.push('\n');
            } else {
                normalized.push(ch);
            }
        }
        Ok(Cow::Owned(normalized))
    } else {
        Ok(decoded)
    }
}

fn decode_utf16(input: &[u8], little_endian: bool) -> Result<String, Error> {
    if !input.len().is_multiple_of(2) {
        return Err(Error::Syntax("UTF-16 XML input has an odd byte length"));
    }
    let units = input.as_chunks::<2>().0.iter().map(|pair| {
        if little_endian {
            u16::from_le_bytes([pair[0], pair[1]])
        } else {
            u16::from_be_bytes([pair[0], pair[1]])
        }
    });
    let mut decoded = String::with_capacity(input.len());
    for unit in std::char::decode_utf16(units) {
        let ch =
            unit.map_err(|_| Error::Syntax("UTF-16 XML input contains an unpaired surrogate"))?;
        decoded.push(ch);
    }
    Ok(decoded)
}

fn is_xml_space(value: u8) -> bool {
    matches!(value, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

fn trim_xml_space_start(mut value: &[u8]) -> &[u8] {
    while value.first().copied().map(is_xml_space).unwrap_or(false) {
        value = &value[1..];
    }
    value
}

fn validate_raw_characters(value: &str) -> Result<(), Error> {
    if value.chars().all(|ch| {
        matches!(ch, '\u{9}' | '\u{a}' | '\u{d}')
            || ('\u{20}'..='\u{d7ff}').contains(&ch)
            || ('\u{e000}'..='\u{fffd}').contains(&ch)
            || ('\u{10000}'..='\u{10ffff}').contains(&ch)
    }) {
        Ok(())
    } else {
        Err(Error::Syntax("XML input contains a forbidden character"))
    }
}

fn validate_decoded_characters(value: &str) -> Result<(), Error> {
    if value.chars().all(|ch| {
        ('\u{20}'..='\u{d7ff}').contains(&ch)
            || ('\u{e000}'..='\u{fffd}').contains(&ch)
            || ('\u{10000}'..='\u{10ffff}').contains(&ch)
            || matches!(ch, '\u{9}' | '\u{a}' | '\u{d}')
    }) {
        Ok(())
    } else {
        Err(Error::Syntax(
            "XML reference expands to a forbidden character",
        ))
    }
}

fn attribute_value(value: &[u8], preserve: bool) -> Result<String, Error> {
    if value.contains(&b'<') {
        return Err(Error::Syntax("XML attribute contains a literal '<'"));
    }
    let normalized;
    let value = if value.iter().any(|ch| matches!(ch, b'\t' | b'\n' | b'\r')) {
        normalized = value
            .iter()
            .map(|ch| {
                if matches!(ch, b'\t' | b'\n' | b'\r') {
                    b' '
                } else {
                    *ch
                }
            })
            .collect::<Vec<_>>();
        Cow::Borrowed(normalized.as_slice())
    } else {
        Cow::Borrowed(value)
    };
    let value = std::str::from_utf8(&value)?;
    let decoded = unescape(value)
        .map_err(quick_xml::Error::from)
        .map_err(Error::from)?;
    validate_decoded_characters(&decoded)?;
    if preserve {
        Ok(escape_predefined(&decoded))
    } else {
        Ok(decoded.into_owned())
    }
}

fn validate_attribute_separators(raw: &[u8]) -> Result<(), Error> {
    // quick-xml checks attribute quoting but accepts adjacent quoted attributes.
    // XML requires S between attributes, including namespace declarations.
    let mut quote = None;
    for (index, &byte) in raw.iter().enumerate() {
        match quote {
            Some(open) if byte == open => {
                if raw.get(index + 1).is_some_and(|&next| !is_xml_space(next)) {
                    return Err(Error::Syntax(
                        "XML attributes must be separated by whitespace",
                    ));
                }
                quote = None;
            }
            None if matches!(byte, b'\'' | b'"') => quote = Some(byte),
            _ => {}
        }
    }
    Ok(())
}

fn escape_predefined(value: &str) -> String {
    let mut escaped = String::with_capacity(value.len());
    for ch in value.chars() {
        match ch {
            '&' => escaped.push_str("&amp;"),
            '<' => escaped.push_str("&lt;"),
            '>' => escaped.push_str("&gt;"),
            '"' => escaped.push_str("&quot;"),
            '\'' => escaped.push_str("&apos;"),
            _ => escaped.push(ch),
        }
    }
    escaped
}

fn validate_qname(name: &[u8]) -> Result<(), Error> {
    let name = std::str::from_utf8(name)?;
    let mut parts = name.split(':');
    let first = parts.next().unwrap_or_default();
    let second = parts.next();
    if first.is_empty() || second == Some("") || parts.next().is_some() {
        return Err(Error::Syntax("invalid qualified XML name"));
    }
    for part in first.chars().chain(second.into_iter().flat_map(str::chars)) {
        if !is_name_char(part) {
            return Err(Error::Syntax("invalid qualified XML name"));
        }
    }
    if first
        .chars()
        .next()
        .is_some_and(|ch| !is_name_start_char(ch))
        || second
            .and_then(|part| part.chars().next())
            .is_some_and(|ch| !is_name_start_char(ch))
    {
        return Err(Error::Syntax("invalid qualified XML name"));
    }
    Ok(())
}

#[cfg(test)]
mod tests;
