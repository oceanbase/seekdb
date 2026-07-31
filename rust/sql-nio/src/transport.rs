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

use crate::*;
use socket2::{SockRef, TcpKeepalive};

#[derive(Clone, Copy, Default)]
pub(crate) struct TcpKeepaliveConfig {
    pub(crate) enabled: bool,
    pub(crate) idle: u32,
    pub(crate) interval: u32,
    pub(crate) count: u32,
}

pub(crate) enum Stream {
    Tcp(TcpStream),
    #[cfg(unix)]
    Unix(UnixStream),
    #[cfg(windows)]
    Pipe(NamedPipe),
}

pub(crate) type ConnStream = Stream;

impl From<TcpStream> for Stream {
    fn from(s: TcpStream) -> Self {
        Stream::Tcp(s)
    }
}

#[cfg(unix)]
impl From<UnixStream> for Stream {
    fn from(s: UnixStream) -> Self {
        Stream::Unix(s)
    }
}

impl Stream {
    pub(crate) fn is_local(&self) -> bool {
        match self {
            Stream::Tcp(_) => false,
            #[cfg(unix)]
            Stream::Unix(_) => true,
            #[cfg(windows)]
            Stream::Pipe(_) => true,
        }
    }

    pub(crate) fn apply_tcp_keepalive(&self, config: TcpKeepaliveConfig) -> std::io::Result<()> {
        let Stream::Tcp(stream) = self else {
            return Ok(());
        };
        let socket = SockRef::from(stream);
        if config.enabled {
            let keepalive = TcpKeepalive::new()
                .with_time(Duration::from_secs(config.idle.into()))
                .with_interval(Duration::from_secs(config.interval.into()))
                .with_retries(config.count);
            socket.set_tcp_keepalive(&keepalive)?;
            socket.set_keepalive(true)
        } else {
            socket.set_keepalive(false)
        }
    }

    pub(crate) fn shutdown(&self, how: std::net::Shutdown) -> std::io::Result<()> {
        match self {
            Stream::Tcp(s) => s.shutdown(how),
            #[cfg(unix)]
            Stream::Unix(s) => s.shutdown(how),
            #[cfg(windows)]
            Stream::Pipe(p) => {
                let _ = how;
                p.disconnect()
            }
        }
    }
}

impl Write for Stream {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        match self {
            Stream::Tcp(s) => s.write(buf),
            #[cfg(unix)]
            Stream::Unix(s) => s.write(buf),
            #[cfg(windows)]
            Stream::Pipe(p) => p.write(buf),
        }
    }

    fn flush(&mut self) -> std::io::Result<()> {
        match self {
            Stream::Tcp(s) => s.flush(),
            #[cfg(unix)]
            Stream::Unix(s) => s.flush(),
            #[cfg(windows)]
            Stream::Pipe(p) => p.flush(),
        }
    }
}

#[cfg(not(unix))]
impl Read for Stream {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        match self {
            Stream::Tcp(s) => s.read(buf),
            #[cfg(windows)]
            Stream::Pipe(p) => p.read(buf),
        }
    }
}

impl mio::event::Source for Stream {
    fn register(
        &mut self,
        registry: &Registry,
        token: Token,
        interests: Interest,
    ) -> std::io::Result<()> {
        match self {
            Stream::Tcp(s) => s.register(registry, token, interests),
            #[cfg(unix)]
            Stream::Unix(s) => s.register(registry, token, interests),
            #[cfg(windows)]
            Stream::Pipe(p) => p.register(registry, token, interests),
        }
    }

    fn reregister(
        &mut self,
        registry: &Registry,
        token: Token,
        interests: Interest,
    ) -> std::io::Result<()> {
        match self {
            Stream::Tcp(s) => s.reregister(registry, token, interests),
            #[cfg(unix)]
            Stream::Unix(s) => s.reregister(registry, token, interests),
            #[cfg(windows)]
            Stream::Pipe(p) => p.reregister(registry, token, interests),
        }
    }

    fn deregister(&mut self, registry: &Registry) -> std::io::Result<()> {
        match self {
            Stream::Tcp(s) => s.deregister(registry),
            #[cfg(unix)]
            Stream::Unix(s) => s.deregister(registry),
            #[cfg(windows)]
            Stream::Pipe(p) => p.deregister(registry),
        }
    }
}

#[cfg(windows)]
pub(crate) fn create_pipe_instance(name_wide: &[u16]) -> std::io::Result<NamedPipe> {
    use std::os::windows::io::{FromRawHandle, RawHandle};
    use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
    use windows_sys::Win32::Storage::FileSystem::{FILE_FLAG_OVERLAPPED, PIPE_ACCESS_DUPLEX};
    use windows_sys::Win32::System::Pipes::{
        CreateNamedPipeW, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES,
    };

    let open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    let handle = unsafe {
        CreateNamedPipeW(
            name_wide.as_ptr(),
            open_mode,
            PIPE_TYPE_BYTE,
            PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            0,
            std::ptr::null(),
        )
    };
    if handle == INVALID_HANDLE_VALUE {
        return Err(std::io::Error::last_os_error());
    }
    Ok(unsafe { NamedPipe::from_raw_handle(handle as RawHandle) })
}

pub(crate) fn raw_fd(s: &ConnStream) -> c_int {
    #[cfg(unix)]
    {
        use std::os::fd::AsRawFd;
        match s {
            Stream::Tcp(t) => t.as_raw_fd(),
            Stream::Unix(u) => u.as_raw_fd(),
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::AsRawSocket;
        match s {
            Stream::Tcp(t) => t.as_raw_socket() as c_int,
            Stream::Pipe(_) => -1,
        }
    }
}
