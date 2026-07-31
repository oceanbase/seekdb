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

pub const CLIENT_PROTOCOL_41: u32 = 0x0000_0200;

pub const CLIENT_SSL: u32 = 0x0000_0800;

pub const CLIENT_CONNECT_WITH_DB: u32 = 0x0000_0008;

pub const CLIENT_COMPRESS: u32 = 0x0000_0020;

pub const CLIENT_SECURE_CONNECTION: u32 = 0x0000_8000;

pub const CLIENT_MULTI_STATEMENTS: u32 = 0x0001_0000;

pub const CLIENT_MULTI_RESULTS: u32 = 0x0002_0000;

pub const CLIENT_PLUGIN_AUTH: u32 = 0x0008_0000;

pub const CLIENT_CONNECT_ATTRS: u32 = 0x0010_0000;

pub const CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA: u32 = 0x0020_0000;

pub const CLIENT_TRANSACTIONS: u32 = 0x0000_2000;

pub const CLIENT_SESSION_TRACK: u32 = 0x0080_0000;

pub const SERVER_CAPABILITIES: u32 =
    0x009C_F7DF | CLIENT_COMPRESS | CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS;

pub const fn server_capabilities(tls_enabled: bool) -> u32 {
    if tls_enabled {
        SERVER_CAPABILITIES | CLIENT_SSL
    } else {
        SERVER_CAPABILITIES
    }
}

pub const fn negotiate_client_capabilities(client: u32, server: u32) -> u32 {
    client & server
}

pub const fn session_client_capabilities(client: u32) -> u32 {
    client | CLIENT_MULTI_STATEMENTS
}
