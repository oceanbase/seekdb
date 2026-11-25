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

use crate::{ConfigError, DeviceConfig, DeviceId, DeviceLimits, Service};
use file_io::{Backend, BackendKind, DirectoryCapabilities};
use std::borrow::Cow;
use std::io;
use std::num::NonZeroUsize;
use std::path::Path;
use std::sync::{Arc, Mutex};

pub(crate) struct Endpoint {
    pub(crate) service: Arc<Service>,
    pub(crate) id: DeviceId,
    capability: DirectoryCapabilities,
    worker_threads: usize,
    depth: usize,
}

impl Endpoint {
    pub(crate) fn native_aio(&self) -> bool {
        self.capability == DirectoryCapabilities::DirectNativeAio
    }

    pub(crate) fn direct_io(&self) -> bool {
        self.capability != DirectoryCapabilities::BufferedWorker
    }
}

fn worker_count(configured: usize, depth: usize) -> usize {
    let requested = if configured == 0 {
        let cpus = std::thread::available_parallelism().map_or(1, NonZeroUsize::get);
        if cpus <= 16 {
            cpus * 6
        } else {
            96 + (cpus - 16).min(464) * 2
        }
    } else {
        configured
    };
    requested.min(depth)
}

#[derive(Default)]
enum SessionState {
    #[default]
    Empty,
    Open(Arc<Endpoint>),
    Closing(Arc<Endpoint>),
}
#[derive(Default)]
pub(crate) struct FileSession(Mutex<SessionState>);
impl FileSession {
    pub(crate) fn prepare<'a>(
        &self,
        service: impl FnOnce() -> io::Result<Arc<Service>>,
        path: impl FnOnce() -> io::Result<Cow<'a, Path>>,
        worker_threads: usize,
        depth: usize,
    ) -> io::Result<()> {
        let mut state = self.0.lock().unwrap();
        if !matches!(*state, SessionState::Empty) {
            return Err(io::ErrorKind::AlreadyExists.into());
        }
        let capability = file_io::probe_directory(&path()?)?;
        if depth == 0 {
            return Err(io::ErrorKind::InvalidInput.into());
        }
        let service = service()?;
        let id = service.allocate_device_id()?;
        let worker_threads = worker_count(worker_threads, depth);
        let worker = Backend::new(
            BackendKind::Worker {
                threads: worker_threads,
            },
            depth,
        )?;
        let native_aio = capability == DirectoryCapabilities::DirectNativeAio;
        let native = if native_aio {
            Some(Backend::new(BackendKind::NativeAio, depth)?)
        } else {
            None
        };
        service
            .register_device_with_limits(
                DeviceConfig { id, worker, native },
                DeviceLimits { depth },
            )
            .map_err(|error| match error {
                ConfigError::Invalid(error) | ConfigError::Unavailable(error) => error,
            })?;
        *state = SessionState::Open(Arc::new(Endpoint {
            service,
            id,
            capability,
            worker_threads,
            depth,
        }));
        Ok(())
    }
    pub(crate) fn capture<T>(
        &self,
        capture: impl FnOnce(&Arc<Endpoint>) -> io::Result<T>,
    ) -> io::Result<T> {
        let state = self.0.lock().unwrap();
        match &*state {
            SessionState::Open(endpoint) => capture(endpoint),
            _ => Err(io::ErrorKind::NotConnected.into()),
        }
    }
    pub(crate) fn close(&self, close_file: impl FnOnce()) -> io::Result<()> {
        let endpoint = {
            let mut state = self.0.lock().unwrap();
            let endpoint = match &*state {
                SessionState::Empty => None,
                SessionState::Open(endpoint) | SessionState::Closing(endpoint) => {
                    Some(Arc::clone(endpoint))
                }
            };
            if let Some(endpoint) = &endpoint {
                *state = SessionState::Closing(Arc::clone(endpoint));
            }
            close_file();
            endpoint
        };
        let Some(endpoint) = endpoint else {
            return Ok(());
        };
        let result = endpoint
            .service
            .remove_device(endpoint.id)
            .and_then(|()| endpoint.service.wait_device_drained(endpoint.id));
        if result.is_ok() {
            let mut state = self.0.lock().unwrap();
            if matches!(&*state, SessionState::Closing(current) if Arc::ptr_eq(current, &endpoint))
            {
                *state = SessionState::Empty;
            }
        }
        result
    }
    pub(crate) fn validate_workers(&self, configured: usize) -> io::Result<()> {
        self.capture(|endpoint| {
            if worker_count(configured, endpoint.depth) == endpoint.worker_threads {
                Ok(())
            } else {
                Err(io::ErrorKind::Unsupported.into())
            }
        })
    }
}
