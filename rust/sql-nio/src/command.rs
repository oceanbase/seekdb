// Copyright (c) 2025 OceanBase.
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

use crate::capability::{CLIENT_CONNECT_ATTRS, CLIENT_PLUGIN_AUTH, CLIENT_SECURE_CONNECTION};
use crate::codec::read_lenenc;
use crate::{
    NioMysqlCommandField, NioMysqlCommandView, NIO_MYSQL_CHANGE_USER_HAS_ATTRS,
    NIO_MYSQL_CHANGE_USER_HAS_CHARSET, NIO_MYSQL_CHANGE_USER_HAS_PLUGIN,
    NIO_MYSQL_CHANGE_USER_SECURE_AUTH, NIO_MYSQL_COMMAND_LAYOUT_BYTES,
    NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER, NIO_MYSQL_COMMAND_LAYOUT_EMPTY,
    NIO_MYSQL_COMMAND_LAYOUT_EXECUTE, NIO_MYSQL_COMMAND_LAYOUT_FETCH,
    NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST, NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA,
    NIO_MYSQL_COMMAND_LAYOUT_U16, NIO_MYSQL_COMMAND_LAYOUT_U32, NIO_MYSQL_COMMAND_LAYOUT_U8,
};

pub const COM_QUIT: u8 = 0x01;

pub const COM_INIT_DB: u8 = 0x02;

pub const COM_QUERY: u8 = 0x03;

pub const COM_FIELD_LIST: u8 = 0x04;
pub const COM_REFRESH: u8 = 0x07;
pub const COM_STATISTICS: u8 = 0x09;
pub const COM_PROCESS_INFO: u8 = 0x0a;
pub const COM_PROCESS_KILL: u8 = 0x0c;
pub const COM_DEBUG: u8 = 0x0d;
pub const COM_PING: u8 = 0x0e;

pub const COM_CHANGE_USER: u8 = 0x11;

pub const COM_STMT_PREPARE: u8 = 0x16;
pub const COM_STMT_EXECUTE: u8 = 0x17;
pub const COM_STMT_SEND_LONG_DATA: u8 = 0x18;
pub const COM_STMT_CLOSE: u8 = 0x19;
pub const COM_STMT_RESET: u8 = 0x1a;
pub const COM_SET_OPTION: u8 = 0x1b;
pub const COM_STMT_FETCH: u8 = 0x1c;
pub const COM_RESET_CONNECTION: u8 = 0x1f;

const COM_END: u8 = 0x20;

const COM_INTERNAL_DELETE_SESSION: u8 = 0x40;
const COM_INTERNAL_AUTH_SWITCH_RESPONSE: u8 = 0x43;

struct Cursor<'a> {
    bytes: &'a [u8],
    pos: usize,
    end: usize,
}

impl<'a> Cursor<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self {
            bytes,
            pos: 0,
            end: bytes.len(),
        }
    }

    fn advance(&mut self, len: usize) -> bool {
        match self.pos.checked_add(len) {
            Some(next) if next <= self.end => {
                self.pos = next;
                true
            }
            _ => false,
        }
    }

    fn byte(&mut self) -> Option<u8> {
        let value = *self.bytes.get(self.pos)?;
        self.pos += 1;
        Some(value)
    }

    fn bytes(&mut self, len: usize) -> Option<(usize, usize)> {
        let start = self.pos;
        self.advance(len).then_some((start, len))
    }

    fn nul_terminated_bytes(&mut self) -> Option<(usize, usize)> {
        let tail = self.bytes.get(self.pos..self.end)?;
        match tail.iter().position(|&byte| byte == 0) {
            Some(len) => {
                let start = self.pos;
                self.advance(len + 1).then_some((start, len))
            }
            None => None,
        }
    }

    fn lenenc(&mut self) -> Option<usize> {
        let (value, encoded_len) = read_lenenc(&self.bytes[..self.end], self.pos)?;
        if !self.advance(encoded_len) {
            return None;
        }
        usize::try_from(value).ok()
    }

    fn skip_lenenc_bytes(&mut self) -> bool {
        self.lenenc().is_some_and(|len| self.advance(len))
    }

    fn skip_attrs_block(&mut self) -> bool {
        let total = match self.lenenc() {
            Some(total) => total,
            None => return false,
        };
        let attrs_end = match self.pos.checked_add(total) {
            Some(end) if end <= self.end => end,
            _ => return false,
        };
        let outer_end = self.end;
        self.end = attrs_end;
        while self.pos < self.end {
            if !self.skip_lenenc_bytes() || !self.skip_lenenc_bytes() {
                self.end = outer_end;
                return false;
            }
        }
        self.end = outer_end;
        true
    }
}

fn field(off: usize, len: usize) -> Option<NioMysqlCommandField> {
    Some(NioMysqlCommandField {
        off: i32::try_from(off).ok()?,
        len: i32::try_from(len).ok()?,
    })
}

fn view(command: u8, layout: u32) -> NioMysqlCommandView {
    NioMysqlCommandView {
        command: command as u32,
        layout,
        scalar0: 0,
        scalar1: 0,
        fields: [NioMysqlCommandField::default(); 4],
        scalar2: 0,
    }
}

fn payload_field(range: (usize, usize)) -> Option<NioMysqlCommandField> {
    field(range.0.checked_add(1)?, range.1)
}

fn parse_change_user(payload: &[u8], caps: u32) -> Option<NioMysqlCommandView> {
    let mut cursor = Cursor::new(payload);
    let username = cursor.nul_terminated_bytes()?;
    let secure_auth = caps & CLIENT_SECURE_CONNECTION != 0;
    let auth_response = if secure_auth {
        let auth_len = cursor.byte()? as usize;
        cursor.bytes(auth_len)?
    } else {
        cursor.nul_terminated_bytes()?
    };
    let database = cursor.nul_terminated_bytes()?;

    let mut parsed = view(COM_CHANGE_USER, NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER);
    parsed.scalar0 = -1;
    parsed.fields[0] = payload_field(username)?;
    parsed.fields[1] = payload_field(auth_response)?;
    parsed.fields[2] = payload_field(database)?;
    if secure_auth {
        parsed.scalar1 |= NIO_MYSQL_CHANGE_USER_SECURE_AUTH;
    }

    if cursor.pos == cursor.end {
        return Some(parsed);
    }

    let charset = cursor.bytes(2)?;
    parsed.scalar0 = i64::from(u16_le(&payload[charset.0..charset.0 + 2])?);
    parsed.scalar1 |= NIO_MYSQL_CHANGE_USER_HAS_CHARSET;

    if caps & CLIENT_PLUGIN_AUTH != 0 && cursor.pos < cursor.end {
        parsed.fields[3] = payload_field(cursor.nul_terminated_bytes()?)?;
        parsed.scalar1 |= NIO_MYSQL_CHANGE_USER_HAS_PLUGIN;
    }

    if caps & CLIENT_CONNECT_ATTRS != 0 && cursor.pos < cursor.end {
        if !cursor.skip_attrs_block() {
            return None;
        }
        parsed.scalar1 |= NIO_MYSQL_CHANGE_USER_HAS_ATTRS;
    }

    Some(parsed)
}

fn bytes_view(command: u8, payload_len: usize, layout: u32) -> Option<NioMysqlCommandView> {
    let mut parsed = view(command, layout);
    parsed.fields[0] = field(1, payload_len)?;
    Some(parsed)
}

fn u16_le(bytes: &[u8]) -> Option<u16> {
    Some(u16::from_le_bytes(bytes.try_into().ok()?))
}

fn u32_le(bytes: &[u8]) -> Option<u32> {
    Some(u32::from_le_bytes(bytes.try_into().ok()?))
}

fn i32_le(bytes: &[u8]) -> Option<i32> {
    Some(i32::from_le_bytes(bytes.try_into().ok()?))
}

pub fn parse_body(body: &[u8], client_caps: u32) -> Option<NioMysqlCommandView> {
    let (&cmd, payload) = body.split_first()?;
    match cmd {
        COM_INTERNAL_DELETE_SESSION..=COM_INTERNAL_AUTH_SWITCH_RESPONSE => None,
        COM_QUERY | COM_INIT_DB | COM_STMT_PREPARE => {
            bytes_view(cmd, payload.len(), NIO_MYSQL_COMMAND_LAYOUT_BYTES)
        }
        COM_QUIT | COM_STATISTICS | COM_PROCESS_INFO | COM_DEBUG | COM_PING
        | COM_RESET_CONNECTION => {
            if !payload.is_empty() {
                return None;
            }
            Some(view(cmd, NIO_MYSQL_COMMAND_LAYOUT_EMPTY))
        }
        COM_REFRESH => {
            if payload.len() != 1 {
                return None;
            }
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_U8);
            parsed.scalar0 = i64::from(payload[0]);
            Some(parsed)
        }
        COM_FIELD_LIST => {
            let nul = payload.iter().position(|&byte| byte == 0)?;
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST);
            parsed.fields[0] = field(1, nul)?;
            let wildcard_off = nul.checked_add(2)?;
            let wildcard_len = payload.len().checked_sub(nul.checked_add(1)?)?;
            parsed.fields[1] = field(wildcard_off, wildcard_len)?;
            Some(parsed)
        }
        COM_PROCESS_KILL | COM_STMT_CLOSE | COM_STMT_RESET => {
            if payload.len() != 4 {
                return None;
            }
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_U32);
            parsed.scalar0 = u32_le(payload)? as i64;
            Some(parsed)
        }
        COM_SET_OPTION => {
            if payload.len() != 2 {
                return None;
            }
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_U16);
            parsed.scalar0 = u16_le(payload)? as i64;
            Some(parsed)
        }
        COM_STMT_FETCH => {
            if payload.len() < 8 {
                return None;
            }
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_FETCH);
            parsed.scalar0 = u32_le(&payload[..4])? as i64;
            parsed.scalar1 = i32_le(&payload[4..8])? as i64;
            parsed.fields[0] = field(9, payload.len() - 8)?;
            Some(parsed)
        }
        COM_STMT_SEND_LONG_DATA => {
            if payload.len() < 6 {
                return None;
            }
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA);
            parsed.scalar0 = i32_le(&payload[..4])? as i64;
            parsed.scalar1 = u16_le(&payload[4..6])? as i64;
            parsed.fields[0] = field(7, payload.len() - 6)?;
            Some(parsed)
        }
        COM_CHANGE_USER => parse_change_user(payload, client_caps),
        COM_STMT_EXECUTE => {
            if payload.len() < 9 {
                return None;
            }
            let mut parsed = view(cmd, NIO_MYSQL_COMMAND_LAYOUT_EXECUTE);
            parsed.scalar0 = i64::from(u32_le(&payload[..4])?);
            parsed.scalar1 = i64::from(u32_le(&payload[5..9])?);
            parsed.scalar2 = i64::from(payload[4]);
            parsed.fields[0] = field(10, payload.len() - 9)?;
            Some(parsed)
        }
        cmd if cmd < COM_END => bytes_view(cmd, payload.len(), NIO_MYSQL_COMMAND_LAYOUT_BYTES),
        _ => None,
    }
}
