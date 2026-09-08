// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
use std::cell::Cell;
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Barrier, Condvar, Mutex};

static ATOMIC_SUM: AtomicU64 = AtomicU64::new(0);
static LOCKED_SUM: Mutex<u64> = Mutex::new(0);
static TLS_DROPS: AtomicU32 = AtomicU32::new(0);

struct LocalState(Cell<u64>);
impl Drop for LocalState {
    fn drop(&mut self) {
        TLS_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}
thread_local! {
    static LOCAL: LocalState = const { LocalState(Cell::new(0)) };
}

#[no_mangle]
pub extern "C" fn seekdb_rust_thread_work(id: u64, count: u32) -> u64 {
    LOCAL.with(|value| {
        assert_eq!(value.0.get(), 0);
        value.0.set(id);
    });
    let mut data = Vec::new();
    for i in 0..count {
        let value = id + u64::from(i);
        data.push(value);
        ATOMIC_SUM.fetch_add(value, Ordering::SeqCst);
        *LOCKED_SUM.lock().unwrap() += value;
        if i % 31 == 0 {
            std::thread::yield_now();
        }
        LOCAL.with(|local| assert_eq!(local.0.get(), id));
    }
    assert_eq!(data.len(), count as usize);
    data.into_iter().sum()
}

#[no_mangle]
pub extern "C" fn seekdb_rust_spawn_workers() -> u64 {
    let ready = Arc::new(Barrier::new(3));
    let wake = Arc::new((Mutex::new(false), Condvar::new()));
    let workers: Vec<_> = (0..2)
        .map(|index| {
            let ready = ready.clone();
            let wake = wake.clone();
            std::thread::Builder::new()
                .stack_size(1024 * 1024)
                .spawn(move || {
                    ready.wait();
                    let (lock, cond) = &*wake;
                    let mut go = lock.lock().unwrap();
                    while !*go {
                        go = cond.wait(go).unwrap();
                    }
                    drop(go);
                    seekdb_rust_thread_work((1u64 << 40) + index, 10000)
                })
                .unwrap()
        })
        .collect();
    ready.wait();
    *wake.0.lock().unwrap() = true;
    wake.1.notify_all();
    workers.into_iter().map(|worker| worker.join().unwrap()).sum()
}

#[no_mangle]
pub extern "C" fn seekdb_rust_atomic_sum() -> u64 {
    ATOMIC_SUM.load(Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn seekdb_rust_locked_sum() -> u64 {
    *LOCKED_SUM.lock().unwrap()
}

#[no_mangle]
pub extern "C" fn seekdb_rust_tls_drops() -> u32 {
    TLS_DROPS.load(Ordering::SeqCst)
}
