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

#![deny(improper_ctypes_definitions)]

use std::alloc::{alloc_zeroed, dealloc, handle_alloc_error, Layout};
use std::collections::{HashMap, TryReserveError};
use std::ffi::{c_char, c_int, c_void, CStr};
use std::io::{Read, Write};
use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use std::ptr::NonNull;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicU8, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, LazyLock, Mutex, RwLock, Weak};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use mio::net::{TcpListener, TcpStream};
#[cfg(unix)]
use mio::net::{UnixListener, UnixStream};
#[cfg(windows)]
use mio::windows::NamedPipe;
use mio::{Events, Interest, Poll, Registry, Token, Waker};
use slab::Slab;

mod abi_layout;
mod capability;
mod cert;
mod codec;
mod command;
mod compress;
mod conn;
mod ffi_check;
mod ffi_types;
mod handshake;
mod login;
mod packet;
mod pump;
mod reactor;
mod registry;
mod request;
mod response;
mod response_api;
mod row_encode;
mod session_storage;
mod stmt_execute;
mod tls;
mod transport;

use crate::capability::{CLIENT_COMPRESS, CLIENT_SSL};
use crate::compress::{DeframeStep, COMPRESS_HEADER_SIZE};
use crate::login::{parse_login, NioLoginAttr, ParsedLogin, MAX_LOGIN_BODY_LEN};
use crate::packet::{
    assemble_command_limited_with_scratch, AssembleStep, HEADER_SIZE, MAX_LOGICAL_PAYLOAD,
    MAX_PAYLOAD,
};

pub(crate) use crate::ffi_types::*;

pub(crate) use crate::conn::*;
pub(crate) use crate::pump::*;
pub(crate) use crate::registry::*;
pub(crate) use crate::request::*;
pub(crate) use crate::response_api::*;
pub(crate) use crate::row_encode::*;
pub(crate) use crate::session_storage::*;
pub(crate) use crate::tls::*;
pub(crate) use crate::transport::*;

const LISTENER: Token = Token(0);
const WAKER: Token = Token(1);
#[cfg_attr(not(unix), allow(dead_code))]
const LOCAL_LISTENER: Token = Token(2);
const FIRST_CONN: usize = 3;
const NIO_ABI_VERSION: u32 = 26;
const MAX_IO_THREADS: usize = 128;

const REQUEST_BUSY: u8 = 1 << 0;
const REQUEST_READ_READY: u8 = 1 << 1;

const RETAINED_INPUT_CAPACITY: usize = 64 * 1024;

const RESPONSE_BATCH_TARGET: usize = (1 << 14) - 3 * 1024;
const RESPONSE_ENCODE_HEADROOM: usize = 256;
const RETAINED_RESPONSE_CAPACITY: usize = RETAINED_INPUT_CAPACITY;
const PACKED_ROW_BLOB_MAGIC: [u8; 4] = *b"SRWB";
const PACKED_ROW_BLOB_VERSION: u8 = 1;
const PACKED_ROW_BLOB_HEADER_LEN: usize = 16;

static NEXT_REQUEST_TICKET: AtomicU64 = AtomicU64::new(1);

const NIO_PACKET_LOGIN: c_int = 1;
const NIO_PACKET_COMMAND: c_int = 2;
const NIO_PACKET_AUTH_SWITCH_RESPONSE: c_int = 3;
