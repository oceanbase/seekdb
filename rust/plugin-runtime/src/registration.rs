// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Registration ownership and atomic staging. C++ normalizes public ABI data;
//! this journal owns the normalized payloads, tokens, budgets and commit state.
//! The host serializes ALL calls (including reads/destruction) with its mutex.
//! Release callbacks destroy host data only: they must not unwind or reenter.

use crate::{INVALID, OK, STATE_MISMATCH};
use std::alloc::{alloc, Layout};
use std::collections::HashMap;
use std::ffi::c_void;
use std::ptr;

pub const NO_MEMORY: i32 = 5;
pub const CONFLICT: i32 = 6;
pub const LIMIT: i32 = 7;
pub const SERVICE: u32 = 1;
pub const EXTENSION: u32 = 2;
const MAX_ENTRIES: usize = 4096;
const MAX_TOKENS: usize = 65536;
const MAX_BYTES: u64 = 67_108_864;
type Release = unsafe extern "C" fn(*mut c_void);

struct Entry {
    family: u32,
    major: u32,
    key: Vec<u8>,
    bytes: u64,
    payload: *mut c_void,
    release: Release,
}
impl Entry {
    fn matches(&self, family: u32, major: u32, key: &[u8]) -> bool {
        self.family == family && self.major == major && self.key == key
    }
}
impl Drop for Entry {
    fn drop(&mut self) {
        unsafe { (self.release)(self.payload) };
    }
}

pub struct RegistrationToken {
    open: bool,
    entries: Vec<Entry>,
}

#[derive(Default)]
pub struct Registration {
    opened: bool,
    accepting: bool,
    // Boxes intentionally keep token addresses stable across vector growth.
    // Ended tokens remain tombstones until domain destruction, preventing ABA.
    // The per-domain issuance limit bounds these tombstones as well as live txns.
    #[allow(clippy::vec_box)]
    tokens: Vec<Box<RegistrationToken>>,
    token_indices: HashMap<*const RegistrationToken, usize>,
    open_transactions: usize,
    committed: Vec<Entry>,
    services: usize,
    extensions: usize,
    bytes: u64,
}

fn try_box<T>(value: T) -> Result<Box<T>, i32> {
    let pointer = unsafe { alloc(Layout::new::<T>()).cast::<T>() };
    if pointer.is_null() {
        Err(NO_MEMORY)
    } else {
        unsafe {
            pointer.write(value);
            Ok(Box::from_raw(pointer))
        }
    }
}

impl Registration {
    fn find_open(&self, token: *const RegistrationToken) -> Result<usize, i32> {
        // Never dereference an unrecognized caller token.
        self.token_indices
            .get(&token)
            .copied()
            .filter(|&index| self.tokens[index].open)
            .ok_or(STATE_MISMATCH)
    }

    fn open(&mut self) -> i32 {
        if self.opened {
            return STATE_MISMATCH;
        }
        self.opened = true;
        self.accepting = true;
        OK
    }

    fn begin(&mut self) -> Result<*mut RegistrationToken, i32> {
        if !self.accepting {
            return Err(STATE_MISMATCH);
        }
        if self.tokens.len() >= MAX_TOKENS || self.open_transactions >= MAX_ENTRIES {
            return Err(LIMIT);
        }
        self.tokens.try_reserve(1).map_err(|_| NO_MEMORY)?;
        self.token_indices.try_reserve(1).map_err(|_| NO_MEMORY)?;
        let mut token = try_box(RegistrationToken {
            open: true,
            entries: Vec::new(),
        })?;
        let pointer = token.as_mut() as *mut RegistrationToken;
        self.token_indices.insert(pointer, self.tokens.len());
        self.tokens.push(token);
        self.open_transactions += 1;
        Ok(pointer)
    }

    // Consumes payload ONLY on success. Validation/reservation precede creation
    // of the owning Entry, so every failure leaves ownership with the caller.
    #[allow(clippy::too_many_arguments)]
    unsafe fn stage(
        &mut self,
        token: *const RegistrationToken,
        family: u32,
        major: u32,
        key: &[u8],
        bytes: u64,
        payload: *mut c_void,
        release: Release,
    ) -> i32 {
        if !self.accepting {
            return STATE_MISMATCH;
        }
        let index = match self.find_open(token) {
            Ok(i) => i,
            Err(e) => return e,
        };
        if payload.is_null()
            || key.is_empty()
            || key.len() > 255
            || !key
                .iter()
                .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || b"._-".contains(c))
            || !matches!(family, SERVICE | EXTENSION)
            || (family == SERVICE && (major == 0 || bytes != 0))
            || (family == EXTENSION && (major != 0 || bytes == 0 || bytes > 65536))
        {
            return INVALID;
        }
        if (family == SERVICE && self.services >= MAX_ENTRIES)
            || (family == EXTENSION
                && (self.extensions >= MAX_ENTRIES || self.bytes + bytes > MAX_BYTES))
        {
            return LIMIT;
        }
        if self
            .committed
            .iter()
            .chain(&self.tokens[index].entries)
            .any(|entry| entry.matches(family, major, key))
        {
            return CONFLICT;
        }
        let mut owned_key = Vec::new();
        if owned_key.try_reserve_exact(key.len()).is_err()
            || self.tokens[index].entries.try_reserve(1).is_err()
        {
            return NO_MEMORY;
        }
        owned_key.extend_from_slice(key);
        self.tokens[index].entries.push(Entry {
            family,
            major,
            key: owned_key,
            bytes,
            payload,
            release,
        });
        if family == SERVICE {
            self.services += 1;
        } else {
            self.extensions += 1;
            self.bytes += bytes;
        }
        OK
    }

    fn commit(&mut self, token: *const RegistrationToken) -> i32 {
        if !self.accepting {
            return STATE_MISMATCH;
        }
        let index = match self.find_open(token) {
            Ok(i) => i,
            Err(e) => return e,
        };
        let transaction = &mut self.tokens[index];
        if transaction.entries.iter().any(|pending| {
            self.committed
                .iter()
                .any(|live| live.matches(pending.family, pending.major, &pending.key))
        }) {
            return CONFLICT;
        }
        if self
            .committed
            .try_reserve(transaction.entries.len())
            .is_err()
        {
            return NO_MEMORY;
        }
        // No fallible operation after reserve: services and objects move as one.
        self.committed.append(&mut transaction.entries);
        transaction.entries = Vec::new();
        transaction.open = false;
        self.open_transactions -= 1;
        OK
    }

    fn abort(&mut self, token: *const RegistrationToken) -> i32 {
        let index = match self.find_open(token) {
            Ok(i) => i,
            Err(e) => return e,
        };
        let transaction = &mut self.tokens[index];
        for entry in &transaction.entries {
            if entry.family == SERVICE {
                self.services -= 1;
            } else {
                self.extensions -= 1;
                self.bytes -= entry.bytes;
            }
        }
        transaction.open = false;
        self.open_transactions -= 1;
        transaction.entries = Vec::new();
        OK
    }

    fn seal(&mut self) -> i32 {
        if !self.accepting {
            return STATE_MISMATCH;
        }
        self.accepting = false;
        if self.open_transactions != 0 {
            STATE_MISMATCH
        } else {
            OK
        }
    }

    fn clear(&mut self) {
        self.opened = true;
        self.accepting = false;
        self.committed = Vec::new();
        for token in &mut self.tokens {
            token.open = false;
            token.entries = Vec::new();
        }
        self.services = 0;
        self.open_transactions = 0;
        self.extensions = 0;
        self.bytes = 0;
    }
}

#[no_mangle]
pub extern "C" fn seekdb_runtime_registration_create() -> *mut Registration {
    match try_box(Registration::default()) {
        Ok(journal) => Box::into_raw(journal),
        Err(_) => ptr::null_mut(),
    }
}

/// # Safety
/// Null or a live create result with exclusive ownership; no borrows remain.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_registration_destroy(journal: *mut Registration) {
    if !journal.is_null() {
        drop(unsafe { Box::from_raw(journal) });
    }
}

macro_rules! journal_call {
    ($name:ident, |$j:ident| $body:expr $(, $arg:ident: $ty:ty)*) => {
        /// # Safety
        /// A live journal must be externally serialized for the whole call.
        /// Callbacks may neither unwind nor reenter the journal. Output pointers
        /// must be writable and not alias journal storage. Token pointers are
        /// compared by identity without dereferencing unknown pointers.
        #[no_mangle]
        pub unsafe extern "C" fn $name(pointer: *mut Registration, $($arg: $ty),*) -> i32 {
            match unsafe { pointer.as_mut() } { Some($j) => $body, None => INVALID }
        }
    };
}

journal_call!(seekdb_runtime_registration_open, |j| j.open());
journal_call!(seekdb_runtime_registration_seal, |j| j.seal());
journal_call!(seekdb_runtime_registration_clear, |j| {
    j.clear();
    OK
});
journal_call!(seekdb_runtime_registration_commit, |j| j.commit(token), token: *const RegistrationToken);
journal_call!(seekdb_runtime_registration_abort, |j| j.abort(token), token: *const RegistrationToken);
journal_call!(seekdb_runtime_registration_check, |j| {
    if !j.accepting { STATE_MISMATCH } else { j.find_open(token).map_or_else(|e| e, |_| OK) }
}, token: *const RegistrationToken);
journal_call!(seekdb_runtime_registration_begin, |j| {
    if output.is_null() { return INVALID; }
    unsafe { *output = ptr::null_mut(); }
    match j.begin() { Ok(t) => { unsafe { *output = t; } OK }, Err(e) => e }
}, output: *mut *mut RegistrationToken);

/// # Safety
/// Journal access is exclusive. key/key_length must be readable for this call
/// and must not alias journal-owned memory.
/// On OK only, the journal owns payload and calls release exactly once, possibly
/// during abort/clear/destroy. Otherwise payload remains owned by the caller.
/// release destroys host-owned normalized data, never plugin resources, and
/// must neither unwind nor reenter this journal. No payload references may
/// outlive the journal's ownership. Tokens must belong to this domain.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_registration_stage(
    pointer: *mut Registration,
    token: *const RegistrationToken,
    family: u32,
    major: u32,
    key: *const u8,
    key_length: u32,
    bytes: u64,
    payload: *mut c_void,
    release: Option<Release>,
) -> i32 {
    let Some(journal) = (unsafe { pointer.as_mut() }) else {
        return INVALID;
    };
    let Some(release) = release else {
        return INVALID;
    };
    if key.is_null() || key_length == 0 || key_length > 255 {
        return INVALID;
    }
    unsafe {
        journal.stage(
            token,
            family,
            major,
            std::slice::from_raw_parts(key, key_length as usize),
            bytes,
            payload,
            release,
        )
    }
}

#[repr(C)]
#[derive(Default)]
pub struct RegistrationStats {
    pub committed_services: u32,
    pub committed_extensions: u32,
    pub open_transactions: u32,
    pub total_services: u32,
    pub total_extensions: u32,
    pub issued_transactions: u32,
    pub extension_bytes: u64,
}

journal_call!(seekdb_runtime_registration_stats, |j| {
    if output.is_null() { return INVALID; }
    let mut stats = RegistrationStats {
        open_transactions: j.open_transactions as u32,
        total_services: j.services as u32,
        total_extensions: j.extensions as u32,
        issued_transactions: j.tokens.len() as u32,
        extension_bytes: j.bytes,
        ..RegistrationStats::default()
    };
    for entry in &j.committed {
        if entry.family == SERVICE { stats.committed_services += 1; }
        else { stats.committed_extensions += 1; }
    }
    unsafe { *output = stats; }
    OK
}, output: *mut RegistrationStats);

journal_call!(seekdb_runtime_registration_get, |j| {
    if family.is_null() || payload.is_null() { return INVALID; }
    unsafe { *family = 0; *payload = ptr::null(); }
    let Some(entry) = j.committed.get(index as usize) else { return INVALID; };
    unsafe { *family = entry.family; *payload = entry.payload; }
    OK
}, index: u32, family: *mut u32, payload: *mut *const c_void);

#[cfg(test)]
mod tests;
