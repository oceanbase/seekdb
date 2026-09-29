// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

// Private compatibility parser, not a general JSON API. In particular, the old
// in-situ parser accepts unvalidated UTF-8 bytes, stops at raw NUL, and permits
// 101 container levels. Keep strings as bytes, with borrowing where possible.
use std::borrow::Cow;

use crate::{number, push, reserve, ParseError, Result};

pub(super) enum Value<'a> {
    Other,
    Number(f32),
    String(Cow<'a, [u8]>),
    Array(Vec<Value<'a>>),
    Object(Vec<(Cow<'a, [u8]>, Value<'a>)>),
}

impl Value<'_> {
    pub(super) fn field(&self, name: &[u8]) -> Result<&Self> {
        let Self::Object(fields) = self else {
            return Err(ParseError::InvalidArgument);
        };
        fields
            .iter()
            .rev()
            .find(|(key, _)| key.as_ref() == name)
            .map(|(_, value)| value)
            .ok_or(ParseError::MissingField)
    }

    pub(super) fn array(&self) -> Result<&[Self]> {
        match self {
            Self::Array(values) => Ok(values),
            _ => Err(ParseError::InvalidArgument),
        }
    }
}

pub(super) fn parse(input: &[u8]) -> Result<Value<'_>> {
    let end = input
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(input.len());
    let mut parser = Parser {
        input: &input[..end],
        pos: 0,
    };
    let root = parser.value(0)?;
    parser.whitespace();
    if parser.pos != parser.input.len() {
        return Err(ParseError::InvalidJson);
    }
    Ok(root)
}

struct Parser<'a> {
    input: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn peek(&self) -> Option<u8> {
        self.input.get(self.pos).copied()
    }

    fn consume(&mut self, byte: u8) -> bool {
        if self.peek() == Some(byte) {
            self.pos += 1;
            true
        } else {
            false
        }
    }

    fn whitespace(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\n' | b'\r' | b'\t')) {
            self.pos += 1;
        }
    }

    fn value(&mut self, depth: usize) -> Result<Value<'a>> {
        self.whitespace();
        match self.peek() {
            Some(b'{' | b'[') if depth > 100 => Err(ParseError::InvalidJson),
            Some(b'{') => {
                self.pos += 1;
                self.whitespace();
                let mut fields = Vec::new();
                if !self.consume(b'}') {
                    loop {
                        self.whitespace();
                        let key = self.string()?;
                        self.whitespace();
                        if !self.consume(b':') {
                            return Err(ParseError::InvalidJson);
                        }
                        let value = self.value(depth + 1)?;
                        push(&mut fields, (key, value))?;
                        self.whitespace();
                        if self.consume(b'}') {
                            break;
                        }
                        if !self.consume(b',') {
                            return Err(ParseError::InvalidJson);
                        }
                    }
                }
                Ok(Value::Object(fields))
            }
            Some(b'[') => {
                self.pos += 1;
                self.whitespace();
                let mut values = Vec::new();
                if !self.consume(b']') {
                    loop {
                        push(&mut values, self.value(depth + 1)?)?;
                        self.whitespace();
                        if self.consume(b']') {
                            break;
                        }
                        if !self.consume(b',') {
                            return Err(ParseError::InvalidJson);
                        }
                    }
                }
                Ok(Value::Array(values))
            }
            Some(b'"') => Ok(Value::String(self.string()?)),
            Some(b'-' | b'0'..=b'9') => {
                let (value, length) = number::parse(&self.input[self.pos..])?;
                self.pos += length;
                Ok(Value::Number(value))
            }
            Some(b't') => self.literal(b"true"),
            Some(b'f') => self.literal(b"false"),
            Some(b'n') => self.literal(b"null"),
            _ => Err(ParseError::InvalidJson),
        }
    }

    fn literal(&mut self, literal: &[u8]) -> Result<Value<'a>> {
        if self.input[self.pos..].starts_with(literal) {
            self.pos += literal.len();
            Ok(Value::Other)
        } else {
            Err(ParseError::InvalidJson)
        }
    }

    fn string(&mut self) -> Result<Cow<'a, [u8]>> {
        if !self.consume(b'"') {
            return Err(ParseError::InvalidJson);
        }
        let start = self.pos;
        while let Some(byte) = self.peek() {
            match byte {
                b'"' => {
                    let value = &self.input[start..self.pos];
                    self.pos += 1;
                    return Ok(Cow::Borrowed(value));
                }
                b'\\' => return self.escaped_string(start),
                0..=31 => return Err(ParseError::InvalidJson),
                _ => self.pos += 1,
            }
        }
        Err(ParseError::InvalidJson)
    }

    fn escaped_string(&mut self, start: usize) -> Result<Cow<'a, [u8]>> {
        let mut bytes = Vec::new();
        reserve(&mut bytes, self.pos - start)?;
        bytes.extend_from_slice(&self.input[start..self.pos]);
        while let Some(byte) = self.peek() {
            self.pos += 1;
            match byte {
                b'"' => return Ok(Cow::Owned(bytes)),
                0..=31 => return Err(ParseError::InvalidJson),
                b'\\' => {
                    let escape = self.peek().ok_or(ParseError::InvalidJson)?;
                    self.pos += 1;
                    match escape {
                        b'"' | b'\\' | b'/' => push(&mut bytes, escape)?,
                        b'b' => push(&mut bytes, 8)?,
                        b'f' => push(&mut bytes, 12)?,
                        b'n' => push(&mut bytes, b'\n')?,
                        b'r' => push(&mut bytes, b'\r')?,
                        b't' => push(&mut bytes, b'\t')?,
                        b'u' => {
                            let mut codepoint = self.hex4()?;
                            if (0xd800..=0xdbff).contains(&codepoint) {
                                if !self.consume(b'\\') || !self.consume(b'u') {
                                    return Err(ParseError::InvalidJson);
                                }
                                let low = self.hex4()?;
                                if !(0xdc00..=0xdfff).contains(&low) {
                                    return Err(ParseError::InvalidJson);
                                }
                                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
                            }
                            let ch = char::from_u32(codepoint).ok_or(ParseError::InvalidJson)?;
                            let mut utf8 = [0; 4];
                            let encoded = ch.encode_utf8(&mut utf8).as_bytes();
                            reserve(&mut bytes, encoded.len())?;
                            bytes.extend_from_slice(encoded);
                        }
                        _ => return Err(ParseError::InvalidJson),
                    }
                }
                _ => push(&mut bytes, byte)?,
            }
        }
        Err(ParseError::InvalidJson)
    }

    fn hex4(&mut self) -> Result<u32> {
        let mut result = 0;
        for _ in 0..4 {
            let digit = match self.peek() {
                Some(byte @ b'0'..=b'9') => byte - b'0',
                Some(byte @ b'a'..=b'f') => byte - b'a' + 10,
                Some(byte @ b'A'..=b'F') => byte - b'A' + 10,
                _ => return Err(ParseError::InvalidJson),
            };
            self.pos += 1;
            result = result * 16 + u32::from(digit);
        }
        Ok(result)
    }
}
