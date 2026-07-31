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

use crate::capability::{
    negotiate_client_capabilities, CLIENT_CONNECT_ATTRS, CLIENT_CONNECT_WITH_DB,
    CLIENT_PLUGIN_AUTH, CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA, CLIENT_PROTOCOL_41,
    CLIENT_SECURE_CONNECTION,
};
use crate::codec::read_lenenc;

pub(crate) const MAX_LOGIN_BODY_LEN: usize = 1024 * 1024;

const MAX_CONNECT_ATTRS_LEN: usize = 64 * 1024;

const MAX_CONNECT_ATTR_COUNT: usize = 1024;

fn read_login_cstr(b: &[u8], at: &mut usize) -> Option<(i32, i32)> {
    let start = *at;
    let rest = b.get(start..)?;
    let len = rest.iter().position(|&c| c == 0)?;
    let next = start.checked_add(len)?.checked_add(1)?;
    let off = i32::try_from(start).ok()?;
    let len = i32::try_from(len).ok()?;
    *at = next;
    Some((off, len))
}

fn take_field(b: &[u8], at: &mut usize, len: usize) -> Option<(i32, i32)> {
    let start = *at;
    let end = start.checked_add(len)?;
    if end > b.len() {
        return None;
    }
    let off = i32::try_from(start).ok()?;
    let len_i32 = i32::try_from(len).ok()?;
    *at = end;
    Some((off, len_i32))
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NioLoginAttr {
    pub key_off: i32,
    pub key_len: i32,
    pub value_off: i32,
    pub value_len: i32,
}

#[derive(Clone, Debug)]
pub struct ParsedLogin {
    pub raw_caps: u32,
    pub transport_caps: u32,
    pub charset: u8,
    pub user: (i32, i32),
    pub auth: (i32, i32),
    pub db: (i32, i32),
    pub plugin: (i32, i32),
    pub attrs: Vec<NioLoginAttr>,
}

pub fn parse_login(b: &[u8], server_caps: u32) -> Option<ParsedLogin> {
    const FIXED: usize = 32;
    if b.len() < FIXED || b.len() > MAX_LOGIN_BODY_LEN {
        return None;
    }
    let raw_caps = u32::from_le_bytes([b[0], b[1], b[2], b[3]]);
    if raw_caps & CLIENT_PROTOCOL_41 == 0 {
        return None;
    }
    let charset = b[8];
    let mut p = FIXED;

    let user = read_login_cstr(b, &mut p)?;

    let auth = if raw_caps & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA != 0 {
        let (len, inc) = read_lenenc(b, p)?;
        p = p.checked_add(inc)?;
        take_field(b, &mut p, usize::try_from(len).ok()?)?
    } else if raw_caps & CLIENT_SECURE_CONNECTION != 0 {
        let len = *b.get(p)? as usize;
        p += 1;
        take_field(b, &mut p, len)?
    } else {
        read_login_cstr(b, &mut p)?
    };

    let db = if raw_caps & CLIENT_CONNECT_WITH_DB != 0 {
        read_login_cstr(b, &mut p)?
    } else {
        (0, -1)
    };

    let plugin = if raw_caps & CLIENT_PLUGIN_AUTH != 0 {
        read_login_cstr(b, &mut p)?
    } else {
        (0, -1)
    };

    let mut attrs = Vec::new();
    if raw_caps & CLIENT_CONNECT_ATTRS != 0 {
        let (total, inc) = read_lenenc(b, p)?;
        p = p.checked_add(inc)?;
        let total = usize::try_from(total).ok()?;
        if total > MAX_CONNECT_ATTRS_LEN {
            return None;
        }
        let attrs_end = p.checked_add(total)?;
        if attrs_end > b.len() {
            return None;
        }

        let reserve = (total / 2).min(MAX_CONNECT_ATTR_COUNT);
        attrs.try_reserve_exact(reserve).ok()?;
        while p < attrs_end {
            if attrs.len() == MAX_CONNECT_ATTR_COUNT {
                return None;
            }
            let (key_len, key_inc) = read_lenenc(&b[..attrs_end], p)?;
            p = p.checked_add(key_inc)?;
            let key = take_field(&b[..attrs_end], &mut p, usize::try_from(key_len).ok()?)?;
            let (val_len, val_inc) = read_lenenc(&b[..attrs_end], p)?;
            p = p.checked_add(val_inc)?;
            let val = take_field(&b[..attrs_end], &mut p, usize::try_from(val_len).ok()?)?;
            attrs.push(NioLoginAttr {
                key_off: key.0,
                key_len: key.1,
                value_off: val.0,
                value_len: val.1,
            });
        }
    }

    let transport_caps = negotiate_client_capabilities(raw_caps, server_caps);

    Some(ParsedLogin {
        raw_caps,
        transport_caps,
        charset,
        user,
        auth,
        db,
        plugin,
        attrs,
    })
}
