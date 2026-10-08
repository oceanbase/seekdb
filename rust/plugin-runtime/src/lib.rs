// Copyright (c) 2026 OceanBase.
// Licensed under the Apache License, Version 2.0.

//! Host runtime, not a plugin SDK. The C++ adapter owns each opaque generation
//! and keeps it alive with its existing shared_ptr leases. No plugin callback
//! executes under these locks. Host builds retain their panic=abort policy.
#![deny(unsafe_op_in_unsafe_fn, improper_ctypes_definitions)]

use std::alloc::{alloc, Layout};
use std::sync::{Condvar, Mutex, MutexGuard};
use std::time::Duration;

pub mod build_contract;
pub mod dependency;
pub mod extension_dependency;
pub mod extension_drop;
pub mod extension_install;
pub mod extension_update;
pub mod hook;
pub mod hook_v2;
pub mod input_state;
pub mod memory;
pub mod memory_limit;
pub mod native;
pub mod object_catalog;
pub mod package;
pub mod query_operation;
pub mod query_transaction;
pub mod registration;
pub mod resolution;

pub const OK: i32 = 0;
pub const INVALID: i32 = 1;
pub const STATE_MISMATCH: i32 = 2;
pub const BUSY: i32 = 3;
pub const TIMEOUT: i32 = 4;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
enum State {
    Discovered = 0,
    Validated = 1,
    Loaded = 2,
    Initializing = 3,
    Active = 4,
    Quiescing = 5,
    Stopped = 6,
    Failed = 7,
    Blocked = 8,
}

impl State {
    fn from_byte(value: u8) -> Option<Self> {
        Some(match value {
            0 => Self::Discovered,
            1 => Self::Validated,
            2 => Self::Loaded,
            3 => Self::Initializing,
            4 => Self::Active,
            5 => Self::Quiescing,
            6 => Self::Stopped,
            7 => Self::Failed,
            8 => Self::Blocked,
            _ => return None,
        })
    }

    fn allows(self, next: Self) -> bool {
        use State::*;
        matches!(
            (self, next),
            (Discovered, Validated | Failed)
                | (Validated, Loaded | Failed)
                | (Loaded, Initializing | Failed)
                | (Initializing, Active | Failed | Blocked)
                | (Active, Quiescing | Failed | Blocked)
                | (Quiescing, Stopped | Failed | Blocked)
                | (Failed, Quiescing | Stopped)
        )
    }
}

struct Inner {
    state: State,
    leases: i64,
    activation_reserved: bool,
}

pub struct Generation {
    inner: Mutex<Inner>,
    drained: Condvar,
}

impl Generation {
    fn new() -> Self {
        Self {
            inner: Mutex::new(Inner {
                state: State::Discovered,
                leases: 0,
                activation_reserved: false,
            }),
            drained: Condvar::new(),
        }
    }

    fn lock(&self) -> MutexGuard<'_, Inner> {
        // Continuing with poisoned lifecycle state could unload live code.
        // This is a host invariant failure, never a recoverable plugin error.
        self.inner.lock().unwrap_or_else(|_| std::process::abort())
    }

    fn transition(&self, next: u8) -> i32 {
        let Some(next) = State::from_byte(next) else {
            return INVALID;
        };
        let mut inner = self.lock();
        if inner.activation_reserved {
            BUSY
        } else if !inner.state.allows(next) {
            STATE_MISMATCH
        } else if next == State::Stopped && inner.leases != 0 {
            BUSY
        } else {
            inner.state = next;
            OK
        }
    }

    fn reserve(&self) -> i32 {
        let mut inner = self.lock();
        if inner.activation_reserved {
            BUSY
        } else if inner.state != State::Initializing {
            STATE_MISMATCH
        } else {
            inner.activation_reserved = true;
            OK
        }
    }

    fn promote(&self) -> i32 {
        let mut inner = self.lock();
        if !inner.activation_reserved || inner.state != State::Initializing {
            STATE_MISMATCH
        } else {
            inner.state = State::Active;
            inner.activation_reserved = false;
            OK
        }
    }

    fn acquire(&self) -> bool {
        let mut inner = self.lock();
        if inner.state != State::Active || inner.leases == i64::MAX {
            false
        } else {
            inner.leases += 1;
            true
        }
    }

    fn release(&self) -> i32 {
        let mut inner = self.lock();
        if inner.leases == 0 {
            STATE_MISMATCH
        } else {
            inner.leases -= 1;
            if inner.leases == 0 {
                self.drained.notify_all();
            }
            OK
        }
    }

    fn quiesce(&self) -> i32 {
        let mut inner = self.lock();
        match inner.state {
            State::Active | State::Failed => {
                inner.state = State::Quiescing;
                OK
            }
            State::Quiescing => OK,
            _ => STATE_MISMATCH,
        }
    }

    fn wait_for_drain(&self, timeout_us: i64) -> i32 {
        if timeout_us < 0 {
            return INVALID;
        }
        let inner = self.lock();
        if !matches!(inner.state, State::Quiescing | State::Failed) {
            return STATE_MISMATCH;
        }
        let (inner, _) = self
            .drained
            .wait_timeout_while(inner, Duration::from_micros(timeout_us as u64), |s| {
                s.leases != 0
            })
            .unwrap_or_else(|_| std::process::abort());
        if inner.leases == 0 {
            OK
        } else {
            TIMEOUT
        }
    }

    fn terminal_stop(&self) -> i32 {
        let mut inner = self.lock();
        if inner.state != State::Blocked {
            STATE_MISMATCH
        } else if inner.leases != 0 {
            BUSY
        } else {
            inner.state = State::Stopped;
            OK
        }
    }
}

/// Allocate a generation, returning null on allocation failure.
#[no_mangle]
pub extern "C" fn seekdb_runtime_generation_create() -> *mut Generation {
    // Use the fallible allocator rather than Box::new's OOM abort. The C++
    // loader already handles allocation failure before publishing a module.
    unsafe {
        let pointer = alloc(Layout::new::<Generation>()).cast::<Generation>();
        if !pointer.is_null() {
            pointer.write(Generation::new());
        }
        pointer
    }
}

/// # Safety
/// `generation` must be null or a live result of create. The caller must own
/// it exclusively: no concurrent operation, waiter or execution lease remains.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_generation_destroy(generation: *mut Generation) {
    if !generation.is_null() {
        let owned = unsafe { Box::from_raw(generation) };
        if owned.lock().leases != 0 {
            std::process::abort();
        }
        drop(owned);
    }
}

// All borrowing entrypoints share the same lifetime contract. Invalid/null
// handles are programming errors, not plugin-supplied values. Null is still
// handled without dereferencing so the C ABI can fail closed.
macro_rules! generation_call {
    ($name:ident, $result:ty, $fallback:expr, |$g:ident| $body:expr $(, $arg:ident: $ty:ty)*) => {
        /// # Safety
        /// The handle must reference a live generation for the whole call.
        /// Concurrent borrowing calls are allowed; destroy must not race them.
        #[no_mangle]
        pub unsafe extern "C" fn $name(pointer: *const Generation, $($arg: $ty),*) -> $result {
            match unsafe { pointer.as_ref() } {
                Some($g) => $body,
                None => $fallback,
            }
        }
    };
}

generation_call!(
    seekdb_runtime_generation_state,
    u8,
    State::Blocked as u8,
    |g| g.lock().state as u8
);
generation_call!(seekdb_runtime_generation_leases, i64, -1, |g| g
    .lock()
    .leases);
generation_call!(seekdb_runtime_generation_transition, i32, INVALID,
    |g| g.transition(next), next: u8);
generation_call!(seekdb_runtime_generation_reserve, i32, INVALID, |g| g
    .reserve());
generation_call!(seekdb_runtime_generation_abort, i32, INVALID, |g| {
    g.lock().activation_reserved = false;
    OK
});
generation_call!(seekdb_runtime_generation_promote, i32, INVALID, |g| g
    .promote());
generation_call!(seekdb_runtime_generation_acquire, u8, 0, |g| u8::from(
    g.acquire()
));
generation_call!(seekdb_runtime_generation_release, i32, INVALID, |g| g
    .release());
generation_call!(seekdb_runtime_generation_quiesce, i32, INVALID, |g| g
    .quiesce());
generation_call!(seekdb_runtime_generation_drain, i32, INVALID,
    |g| g.wait_for_drain(timeout_us), timeout_us: i64);
generation_call!(seekdb_runtime_generation_terminal_stop, i32, INVALID, |g| g
    .terminal_stop());

#[cfg(test)]
mod tests;
