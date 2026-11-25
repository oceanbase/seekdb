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

use super::*;
use std::cell::Cell;
use std::sync::Condvar;
use tokio::runtime::{Builder, Runtime};

thread_local! {
    static CALLBACK_EXECUTOR: Cell<usize> = const { Cell::new(0) };
}

#[derive(Default)]
struct State {
    runtime: Option<Runtime>,
    threads: usize,
    pending: usize,
    closed: bool,
    failed: bool,
}

pub(super) struct Executor {
    state: Mutex<State>,
    changed: Condvar,
    #[cfg(test)]
    pub(super) before_enqueue: Mutex<Option<Box<dyn FnOnce() + Send>>>,
    #[cfg(test)]
    pub(super) before_close: Mutex<Option<Box<dyn FnOnce() + Send>>>,
}

// Keep the executor alive through delivery, and release pending admission even if
// a Rust destructor panics in an unwind-enabled test build.
struct Pending(Arc<Executor>);

impl Drop for Pending {
    fn drop(&mut self) {
        let mut state = self.0.state.lock().unwrap();
        state.failed |= std::thread::panicking();
        state.pending -= 1;
        self.0.changed.notify_all();
    }
}

impl Executor {
    pub(super) fn new() -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(State::default()),
            changed: Condvar::new(),
            #[cfg(test)]
            before_enqueue: Mutex::new(None),
            #[cfg(test)]
            before_close: Mutex::new(None),
        })
    }

    pub(super) fn prepare(&self, threads: usize, start: bool) -> io::Result<()> {
        let mut state = self.state.lock().unwrap();
        if state.closed {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        if state.runtime.is_some() {
            return if state.threads == threads {
                Ok(())
            } else {
                Err(io::Error::new(
                    io::ErrorKind::Unsupported,
                    "changing callback thread limits requires a restart",
                ))
            };
        }
        if !start {
            return Ok(());
        }
        if threads == 0 {
            return Err(io::ErrorKind::InvalidInput.into());
        }
        // The address identifies the executor, but is never dereferenced by a
        // worker. Every submitted task owns an Arc until delivery has finished.
        let identity = self as *const Self as usize;
        state.runtime = Some(
            Builder::new_current_thread()
                .max_blocking_threads(threads)
                .thread_keep_alive(Duration::from_secs(10))
                .thread_name("io-callback")
                .on_thread_start(move || CALLBACK_EXECUTOR.set(identity))
                .on_thread_stop(|| CALLBACK_EXECUTOR.set(0))
                .build()?,
        );
        state.threads = threads;
        Ok(())
    }

    pub(super) fn enqueue(
        self: &Arc<Self>,
        request: Arc<request::RequestState>,
        completion: Completion,
    ) {
        #[cfg(test)]
        if let Some(hook) = self.before_enqueue.lock().unwrap().take() {
            hook();
        }
        let handle = {
            let mut state = self.state.lock().unwrap();
            if state.closed {
                // Foreign destructors and cancellation may reenter the service.
                drop(state);
                request::discard(request, completion);
                return;
            }
            let handle = state
                .runtime
                .as_ref()
                .expect("callback executor prepared")
                .handle()
                .clone();
            state.pending += 1;
            handle
        };
        let pending = Pending(Arc::clone(self));
        handle.spawn_blocking(move || {
            let _pending = pending;
            request::deliver(request, completion);
        });
    }

    pub(super) fn is_current_worker(&self) -> bool {
        CALLBACK_EXECUTOR.get() == self as *const Self as usize
    }

    pub(super) fn shutdown(&self) -> io::Result<()> {
        if self.is_current_worker() {
            return Err(io::Error::new(
                io::ErrorKind::WouldBlock,
                "callback cannot shut down its own executor",
            ));
        }
        let (runtime, failed) = {
            let mut state = self.state.lock().unwrap();
            while state.pending != 0 {
                state = self.changed.wait(state).unwrap();
            }
            #[cfg(test)]
            if let Some(hook) = self.before_close.lock().unwrap().take() {
                hook();
            }
            // Draining and closing admission are one transition. Pending tasks
            // protect the runtime while a publisher calls spawn_blocking.
            state.closed = true;
            (state.runtime.take(), state.failed)
        };
        drop(runtime);
        if failed {
            Err(io::Error::other("callback worker panicked"))
        } else {
            Ok(())
        }
    }
}

impl Drop for Executor {
    fn drop(&mut self) {
        let is_current_worker = self.is_current_worker();
        let state = self.state.get_mut().unwrap();
        state.closed = true;
        if let Some(runtime) = state.runtime.take() {
            if is_current_worker {
                // The final task may own the last Arc. Joining this runtime from
                // its own blocking worker would wait for the current task.
                runtime.shutdown_background();
            } else {
                drop(runtime);
            }
        }
    }
}
