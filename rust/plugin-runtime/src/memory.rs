// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Per-module ownership of allocations explicitly made through host.alloc/free.
//! Not a global allocator, query arena, tenant budget, or native-code sandbox.
//! No plugin callbacks execute here. Account destruction requires exclusive
//! ownership after deinit/drain and all potential host API callers have ended.

use crate::{INVALID, OK};
use std::alloc::{alloc, dealloc, Layout};
use std::collections::HashMap;
use std::ffi::c_void;
use std::ptr;
use std::sync::atomic::{fence, AtomicUsize, Ordering};
use std::sync::{Mutex, MutexGuard};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MemoryUsage {
    pub bytes: u64,
    pub peak_bytes: u64,
    pub allocations: u64,
    pub peak_allocations: u64,
    pub allocation_failures: u64,
    pub invalid_frees: u64,
    pub byte_limit: u64,
    pub allocation_limit: u64,
}

struct Allocation {
    layout: Layout,
    alignment: u32,
    owned: bool,
}

struct Inner {
    // Addresses are exposed by ptr->usize and recovered only after ownership
    // lookup. Never read a header through a foreign/already-freed pointer.
    live: HashMap<usize, Allocation>,
    usage: MemoryUsage,
    closed: bool,
}

pub struct MemoryAccount {
    references: AtomicUsize,
    inner: Mutex<Inner>,
    #[cfg(test)]
    dropped: Option<std::sync::Arc<std::sync::atomic::AtomicBool>>,
}

impl MemoryAccount {
    fn new(byte_limit: u64, allocation_limit: u64) -> Self {
        Self {
            references: AtomicUsize::new(1), // C++ root; owned buffers retain independently.
            #[cfg(test)]
            dropped: None,
            inner: Mutex::new(Inner {
                live: HashMap::new(), // No tracking table until first allocation.
                usage: MemoryUsage {
                    byte_limit,
                    allocation_limit,
                    ..MemoryUsage::default()
                },
                closed: false,
            }),
        }
    }

    fn lock(&self) -> MutexGuard<'_, Inner> {
        // Host builds use panic=abort. Poisoned ownership is not recoverable.
        self.inner.lock().unwrap_or_else(|_| std::process::abort())
    }

    fn allocate(&self, bytes: u64, alignment: u32) -> *mut c_void {
        self.allocate_kind(bytes, alignment, false)
    }

    fn allocate_kind(&self, bytes: u64, alignment: u32, owned: bool) -> *mut c_void {
        let mut inner = self.lock();
        let layout = usize::try_from(bytes).ok().and_then(|size| {
            if size == 0 || !alignment.is_power_of_two() {
                None
            } else {
                Layout::from_size_align(size, alignment as usize).ok()
            }
        });
        let within_limit = !inner.closed
            && bytes <= inner.usage.byte_limit - inner.usage.bytes
            && inner.usage.allocations < inner.usage.allocation_limit;
        if !within_limit || layout.is_none() || inner.live.try_reserve(1).is_err() {
            inner.usage.allocation_failures = inner.usage.allocation_failures.saturating_add(1);
            return ptr::null_mut();
        }
        let layout = layout.unwrap();
        // SAFETY: validated nonzero layout. The allocation is owned by this
        // account until a matching free or exclusive terminal destruction.
        let memory = unsafe { alloc(layout) };
        if memory.is_null() {
            inner.usage.allocation_failures = inner.usage.allocation_failures.saturating_add(1);
            return ptr::null_mut();
        }
        inner.live.insert(
            memory as usize,
            Allocation {
                layout,
                alignment,
                owned,
            },
        );
        inner.usage.bytes += bytes; // Subtraction check above excludes overflow.
        inner.usage.allocations += 1;
        inner.usage.peak_bytes = inner.usage.peak_bytes.max(inner.usage.bytes);
        inner.usage.peak_allocations = inner.usage.peak_allocations.max(inner.usage.allocations);
        memory.cast()
    }

    fn release(&self, memory: *mut c_void, bytes: u64, alignment: u32) -> i32 {
        self.release_kind(memory, bytes, alignment, false)
    }

    fn release_kind(&self, memory: *mut c_void, bytes: u64, alignment: u32, owned: bool) -> i32 {
        if memory.is_null() {
            return OK;
        }
        let mut inner = self.lock();
        let address = memory as usize;
        let matches = inner.live.get(&address).is_some_and(|allocation| {
            allocation.layout.size() as u64 == bytes
                && allocation.alignment == alignment
                && allocation.owned == owned
        });
        if !matches {
            inner.usage.invalid_frees = inner.usage.invalid_frees.saturating_add(1);
            return INVALID;
        }
        let allocation = inner.live.remove(&address).unwrap();
        // Deallocate under the same account lock: a concurrent allocation must
        // not spend released quota before the owned allocation has been freed.
        unsafe { dealloc(memory.cast(), allocation.layout) };
        inner.usage.bytes -= bytes;
        inner.usage.allocations -= 1;
        OK
    }
}

// Intrusive ownership preserves fallible allocation without requiring unstable
// Arc::try_new. Only heap accounts from memory_create enter this protocol.
unsafe fn release_account(account: *const MemoryAccount) {
    if unsafe { &*account }
        .references
        .fetch_sub(1, Ordering::Release)
        == 1
    {
        fence(Ordering::Acquire);
        drop(unsafe { Box::from_raw(account.cast_mut()) });
    }
}

pub struct MemoryBuffer {
    account: *const MemoryAccount,
    memory: *mut c_void,
    bytes: u64,
    alignment: u32,
}
// The buffer owns its bytes and one account reference. Byte access must be
// exclusive for mutation; account bookkeeping and release are synchronized.
unsafe impl Send for MemoryBuffer {}
unsafe impl Sync for MemoryBuffer {}
impl Drop for MemoryBuffer {
    fn drop(&mut self) {
        let account = unsafe { &*self.account };
        if account.release_kind(self.memory, self.bytes, self.alignment, true) != OK {
            std::process::abort(); // Owned token and allocation ledger must agree.
        }
        unsafe { release_account(self.account) };
    }
}

/// Allocate an independent owned-byte token, not a lease on plugin code.
/// # Safety
/// Account is a live heap account from memory_create, borrowed for this call.
/// Returned bytes are exclusively owned until buffer_destroy; no raw free of
/// the token's data is permitted. All operations on the token require it alive.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_buffer_create(
    account: *const MemoryAccount,
    bytes: u64,
    alignment: u32,
) -> *mut MemoryBuffer {
    let Some(owner) = (unsafe { account.as_ref() }) else {
        return ptr::null_mut();
    };
    if owner
        .references
        .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |count| {
            (count < isize::MAX as usize).then_some(count + 1)
        })
        .is_err()
    {
        let mut inner = owner.lock();
        inner.usage.allocation_failures = inner.usage.allocation_failures.saturating_add(1);
        return ptr::null_mut();
    }
    let memory = owner.allocate_kind(bytes, alignment, true);
    if memory.is_null() {
        unsafe { release_account(account) };
        return ptr::null_mut();
    }
    let token = unsafe { alloc(Layout::new::<MemoryBuffer>()) }.cast::<MemoryBuffer>();
    if token.is_null() {
        let _ = owner.release_kind(memory, bytes, alignment, true);
        {
            let mut inner = owner.lock();
            inner.usage.allocation_failures = inner.usage.allocation_failures.saturating_add(1);
        }
        unsafe { release_account(account) };
    } else {
        unsafe {
            token.write(MemoryBuffer {
                account,
                memory,
                bytes,
                alignment,
            })
        };
    }
    token
}

/// # Safety
/// Token is live; returned data is valid until token destruction, with its
/// original size/alignment. Callers synchronize all reads/writes themselves.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_buffer_data(
    token: *const MemoryBuffer,
) -> *mut c_void {
    unsafe { token.as_ref() }.map_or(ptr::null_mut(), |token| token.memory)
}

/// # Safety
/// Exclusive token ownership; all byte users have ended. Callable on any host
/// thread, even after the C++ root account is closed/destroyed. Null is a no-op.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_buffer_destroy(token: *mut MemoryBuffer) {
    if !token.is_null() {
        drop(unsafe { Box::from_raw(token) });
    }
}

impl Drop for MemoryAccount {
    fn drop(&mut self) {
        let inner = self
            .inner
            .get_mut()
            .unwrap_or_else(|_| std::process::abort());
        // Final root/token reference. Logical stop only closes admission.
        for (address, allocation) in inner.live.drain() {
            unsafe { dealloc(address as *mut u8, allocation.layout) };
        }
        #[cfg(test)]
        if let Some(dropped) = &self.dropped {
            dropped.store(true, Ordering::Release);
        }
    }
}

/// Create a generation-local account. Zero limits forbid allocation; u64::MAX
/// means no explicit limit. Limits account requested payload, not allocator or
/// HashMap metadata. Failure allocates no account and returns null.
#[no_mangle]
pub extern "C" fn seekdb_runtime_memory_create(bytes: u64, allocations: u64) -> *mut MemoryAccount {
    let value = MemoryAccount::new(bytes, allocations);
    let layout = Layout::new::<MemoryAccount>();
    let memory = unsafe { alloc(layout) }.cast::<MemoryAccount>();
    if !memory.is_null() {
        unsafe { memory.write(value) };
    }
    memory
}

/// # Safety
/// Non-null account must remain alive for this call. Concurrent borrowing calls
/// are supported. Memory returned is writable for size bytes with alignment.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_alloc(
    account: *const MemoryAccount,
    size: u64,
    alignment: u32,
) -> *mut c_void {
    unsafe { account.as_ref() }.map_or(ptr::null_mut(), |account| account.allocate(size, alignment))
}

/// # Safety
/// Account must remain alive; the caller relinquishes access to memory on a
/// successful non-null free. Wrong account/size/alignment or unknown pointers
/// return INVALID without dereferencing/deallocating the pointer. ABA/dangling
/// pointer misuse by trusted native code is not prevented by an address ledger.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_free(
    account: *const MemoryAccount,
    memory: *mut c_void,
    size: u64,
    alignment: u32,
) -> i32 {
    unsafe { account.as_ref() }.map_or(INVALID, |account| account.release(memory, size, alignment))
}

/// # Safety
/// Account is borrowed for this call; output is aligned, writable and disjoint
/// from account/other outputs. Output is zeroed on failure.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_usage(
    account: *const MemoryAccount,
    output: *mut MemoryUsage,
) -> i32 {
    let Some(output) = (unsafe { output.as_mut() }) else {
        return INVALID;
    };
    *output = MemoryUsage::default();
    let Some(account) = (unsafe { account.as_ref() }) else {
        return INVALID;
    };
    *output = account.lock().usage;
    OK
}

/// Deny new allocations after deinit, retaining blocks for late matching frees
/// and inspection. Idempotent; does not destroy or reclaim borrowed memory.
/// # Safety
/// Account must remain alive for this call; null is a no-op.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_close(account: *const MemoryAccount) {
    if let Some(account) = unsafe { account.as_ref() } {
        account.lock().closed = true;
    }
}

/// # Safety
/// Exclusive root ownership: all root borrowing calls and users of raw
/// allocations must have ended. Closes allocation admission and releases the
/// root reference. Owned-byte tokens remain valid until their final release;
/// only then are unreturned raw allocations reclaimed. Runs no plugin code.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_destroy(account: *mut MemoryAccount) {
    if !account.is_null() {
        unsafe { seekdb_runtime_memory_close(account) };
        unsafe { release_account(account) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Barrier};

    #[test]
    fn owned_bytes_keep_account_after_root_release_and_free_on_another_thread() {
        unsafe {
            let account = seekdb_runtime_memory_create(8, 2);
            let dropped = Arc::new(std::sync::atomic::AtomicBool::new(false));
            (*account).dropped = Some(dropped.clone());
            let first = seekdb_runtime_memory_buffer_create(account, 4, 16);
            let second = seekdb_runtime_memory_buffer_create(account, 4, 32);
            assert!(!first.is_null() && !second.is_null());
            let data = seekdb_runtime_memory_buffer_data(first);
            data.cast::<u32>().write(17);
            // Mixing raw free with an owned token must not steal its allocation.
            assert_eq!(seekdb_runtime_memory_free(account, data, 4, 16), INVALID);
            assert!(seekdb_runtime_memory_buffer_create(account, 1, 1).is_null());
            assert_eq!((*account).references.load(Ordering::Relaxed), 3);
            seekdb_runtime_memory_close(account);
            assert!(seekdb_runtime_memory_buffer_create(account, 1, 1).is_null());
            seekdb_runtime_memory_destroy(account);
            assert!(!dropped.load(Ordering::Acquire));
            let owned = Box::from_raw(first);
            std::thread::spawn(move || {
                assert_eq!(owned.memory.cast::<u32>().read(), 17);
                drop(owned);
            })
            .join()
            .unwrap();
            assert!(!dropped.load(Ordering::Acquire));
            assert_eq!((*(*second).account).lock().usage.bytes, 4);
            seekdb_runtime_memory_buffer_destroy(second);
            assert!(dropped.load(Ordering::Acquire));
        }
    }

    #[test]
    fn owned_creation_failures_release_references_and_share_raw_quota() {
        unsafe {
            assert!(seekdb_runtime_memory_buffer_create(ptr::null(), 1, 1).is_null());
            assert!(seekdb_runtime_memory_buffer_data(ptr::null()).is_null());
            seekdb_runtime_memory_buffer_destroy(ptr::null_mut());
            let account = seekdb_runtime_memory_create(8, 2);
            for (size, alignment) in [(0, 1), (1, 0), (1, 3), (u64::MAX, 1)] {
                assert!(seekdb_runtime_memory_buffer_create(account, size, alignment).is_null());
                assert_eq!((*account).references.load(Ordering::Relaxed), 1);
            }
            let raw = seekdb_runtime_memory_alloc(account, 3, 1);
            let token = seekdb_runtime_memory_buffer_create(account, 5, 1);
            assert!(!raw.is_null() && !token.is_null());
            assert!(seekdb_runtime_memory_alloc(account, 1, 1).is_null());
            seekdb_runtime_memory_buffer_destroy(token);
            assert_eq!((*account).lock().usage.bytes, 3);
            assert_eq!(seekdb_runtime_memory_free(account, raw, 3, 1), OK);
            seekdb_runtime_memory_destroy(account);
        }
    }

    #[test]
    fn concurrent_owned_releases_destroy_the_account_once() {
        unsafe {
            let account = seekdb_runtime_memory_create(512, 8);
            let dropped = Arc::new(std::sync::atomic::AtomicBool::new(false));
            (*account).dropped = Some(dropped.clone());
            let mut tokens = Vec::new();
            for _ in 0..8 {
                let token = seekdb_runtime_memory_buffer_create(account, 64, 64);
                assert!(!token.is_null());
                tokens.push(Box::from_raw(token));
            }
            seekdb_runtime_memory_destroy(account);
            assert!(!dropped.load(Ordering::Acquire));
            let threads: Vec<_> = tokens
                .into_iter()
                .map(|token| std::thread::spawn(move || drop(token)))
                .collect();
            for thread in threads {
                thread.join().unwrap();
            }
            assert!(dropped.load(Ordering::Acquire));
        }
    }

    #[test]
    fn unused_account_has_no_tracking_table_and_limits_are_exact() {
        let account = MemoryAccount::new(100, 2);
        assert_eq!(account.lock().live.capacity(), 0);
        let first = account.allocate(60, 64);
        assert!(!first.is_null());
        assert_eq!(first as usize % 64, 0);
        assert!(account.allocate(41, 1).is_null());
        let second = account.allocate(40, 1);
        assert!(!second.is_null());
        assert!(account.allocate(1, 1).is_null());
        assert_eq!(account.release(first, 60, 64), OK);
        let third = account.allocate(60, 128);
        assert!(!third.is_null());
        assert_eq!(account.release(second, 40, 1), OK);
        assert_eq!(account.release(third, 60, 128), OK);
        let usage = account.lock().usage;
        assert_eq!((usage.bytes, usage.allocations), (0, 0));
        assert_eq!(
            (
                usage.peak_bytes,
                usage.peak_allocations,
                usage.allocation_failures
            ),
            (100, 2, 2)
        );
    }

    #[test]
    fn count_quota_zero_limits_and_invalid_layouts() {
        for (bytes, count) in [(0, u64::MAX), (u64::MAX, 0)] {
            assert!(MemoryAccount::new(bytes, count).allocate(1, 1).is_null());
        }
        let account = MemoryAccount::new(u64::MAX, 1);
        for (size, alignment) in [
            (0, 1),
            (1, 0),
            (1, 3),
            (u64::MAX, 1),
            (i64::MAX as u64, 4096),
        ] {
            assert!(account.allocate(size, alignment).is_null());
        }
        let memory = account.allocate(1, 1);
        assert!(!memory.is_null());
        assert!(account.allocate(1, 1).is_null());
        assert_eq!(account.release(memory, 1, 1), OK);
        assert_eq!(account.lock().usage.allocation_failures, 6);
    }

    #[test]
    fn wrong_owner_layout_and_duplicate_free_do_not_consume_allocation() {
        let account = MemoryAccount::new(100, 2);
        let other = MemoryAccount::new(100, 2);
        let memory = account.allocate(8, 8);
        assert!(!memory.is_null());
        unsafe { memory.cast::<u64>().write(0x1234) };
        assert_eq!(other.release(memory, 8, 8), INVALID);
        assert_eq!(account.release(memory, 7, 8), INVALID);
        assert_eq!(account.release(memory, 8, 4), INVALID);
        assert_eq!(unsafe { memory.cast::<u64>().read() }, 0x1234);
        assert_eq!(account.lock().usage.bytes, 8);
        assert_eq!(account.release(memory, 8, 8), OK);
        assert_eq!(account.release(memory, 8, 8), INVALID);
        assert_eq!(account.release(ptr::null_mut(), u64::MAX, 0), OK);
        assert_eq!(account.lock().usage.invalid_frees, 3);
        assert_eq!(other.lock().usage.invalid_frees, 1);
    }

    #[test]
    fn close_keeps_owned_memory_until_explicit_free_or_terminal_destroy() {
        unsafe {
            let account = seekdb_runtime_memory_create(100, 4);
            assert!(!account.is_null());
            let memory = seekdb_runtime_memory_alloc(account, 16, 16);
            assert!(!memory.is_null());
            memory.cast::<u64>().write(7);
            seekdb_runtime_memory_close(account);
            seekdb_runtime_memory_close(account);
            assert!(seekdb_runtime_memory_alloc(account, 1, 1).is_null());
            assert_eq!(memory.cast::<u64>().read(), 7);
            let mut usage = MemoryUsage::default();
            assert_eq!(seekdb_runtime_memory_usage(account, &mut usage), OK);
            assert_eq!(usage.bytes, 16);
            assert_eq!(seekdb_runtime_memory_free(account, memory, 16, 16), OK);
            seekdb_runtime_memory_destroy(account);
            let account = seekdb_runtime_memory_create(10, 1);
            assert!(!seekdb_runtime_memory_alloc(account, 10, 1).is_null());
            seekdb_runtime_memory_destroy(account); // Terminal reclamation, not Drop callback execution.
        }
    }

    #[test]
    fn null_handles_and_output_are_rejected_without_stale_usage() {
        unsafe {
            let mut usage = MemoryUsage {
                bytes: 99,
                ..MemoryUsage::default()
            };
            assert!(seekdb_runtime_memory_alloc(ptr::null(), 1, 1).is_null());
            assert_eq!(
                seekdb_runtime_memory_free(ptr::null(), ptr::null_mut(), 0, 0),
                INVALID
            );
            assert_eq!(
                seekdb_runtime_memory_usage(ptr::null(), &mut usage),
                INVALID
            );
            assert_eq!(usage, MemoryUsage::default());
            assert_eq!(
                seekdb_runtime_memory_usage(ptr::null(), ptr::null_mut()),
                INVALID
            );
            seekdb_runtime_memory_close(ptr::null());
            seekdb_runtime_memory_destroy(ptr::null_mut());
        }
    }

    #[test]
    fn concurrent_allocations_share_one_quota_and_snapshot() {
        let account = Arc::new(MemoryAccount::new(8 * 64, 8));
        let allocated = Arc::new(Barrier::new(9));
        let release = Arc::new(Barrier::new(9));
        let mut threads = Vec::new();
        for _ in 0..8 {
            let (account, allocated, release) =
                (account.clone(), allocated.clone(), release.clone());
            threads.push(std::thread::spawn(move || {
                let memory = account.allocate(64, 64);
                assert!(!memory.is_null());
                allocated.wait();
                release.wait();
                assert_eq!(account.release(memory, 64, 64), OK);
            }));
        }
        allocated.wait();
        let usage = account.lock().usage;
        assert_eq!((usage.bytes, usage.allocations), (512, 8));
        assert!(account.allocate(1, 1).is_null());
        release.wait();
        for thread in threads {
            thread.join().unwrap();
        }
        assert_eq!(account.lock().usage.bytes, 0);
    }
}
