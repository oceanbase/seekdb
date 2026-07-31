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

use crate::packet;

const SERVER_CHARSET: u8 = 46;

const AUTH_PLUGIN: &[u8] = b"mysql_native_password\0";

const AUTH_PLUGIN_DATA_LEN: u8 = 21;

pub fn build_greeting(
    session_id: u32,
    scramble: &[u8; 20],
    version: &[u8],
    caps: u32,
    status_flags: u16,
) -> Vec<u8> {
    let mut p: Vec<u8> = Vec::with_capacity(96);
    p.push(10);
    p.extend_from_slice(version);
    p.push(0);
    p.extend_from_slice(&session_id.to_le_bytes());
    p.extend_from_slice(&scramble[..8]);
    p.push(0);
    p.extend_from_slice(&(caps as u16).to_le_bytes());
    p.push(SERVER_CHARSET);
    p.extend_from_slice(&status_flags.to_le_bytes());
    p.extend_from_slice(&((caps >> 16) as u16).to_le_bytes());
    p.push(AUTH_PLUGIN_DATA_LEN);
    p.extend_from_slice(&[0u8; 10]);
    p.extend_from_slice(&scramble[8..20]);
    p.push(0);
    p.extend_from_slice(AUTH_PLUGIN);

    let mut out = Vec::with_capacity(p.len() + packet::HEADER_SIZE);
    packet::write_header(&mut out, p.len(), 0);
    out.extend_from_slice(&p);
    out
}
