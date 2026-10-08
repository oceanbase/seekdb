// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
use std::sync::{mpsc, Arc, Barrier};
use std::thread;

fn initializing() -> Generation {
    let g = Generation::new();
    for state in [State::Validated, State::Loaded, State::Initializing] {
        assert_eq!(g.transition(state as u8), OK);
    }
    g
}

fn active() -> Generation {
    let g = initializing();
    assert_eq!(g.reserve(), OK);
    assert_eq!(g.promote(), OK);
    g
}

#[test]
fn transition_matrix_is_exhaustive() {
    let edges = [
        (0, 1),
        (0, 7),
        (1, 2),
        (1, 7),
        (2, 3),
        (2, 7),
        (3, 4),
        (3, 7),
        (3, 8),
        (4, 5),
        (4, 7),
        (4, 8),
        (5, 6),
        (5, 7),
        (5, 8),
        (7, 5),
        (7, 6),
    ];
    for from in 0..=8 {
        for to in 0..=8 {
            let g = Generation::new();
            g.lock().state = State::from_byte(from).unwrap();
            let expected = if edges.contains(&(from, to)) {
                OK
            } else {
                STATE_MISMATCH
            };
            assert_eq!(g.transition(to), expected, "{from} -> {to}");
        }
    }
    assert_eq!(active().transition(255), INVALID);
}

#[test]
fn reservation_prevents_competing_changes() {
    let g = initializing();
    assert_eq!(g.promote(), STATE_MISMATCH);
    assert_eq!(g.reserve(), OK);
    assert_eq!(g.reserve(), BUSY);
    assert_eq!(g.transition(State::Failed as u8), BUSY);
    assert_eq!(g.quiesce(), STATE_MISMATCH);
    assert!(!g.acquire());
    assert_eq!(g.promote(), OK);
    assert_eq!(g.reserve(), STATE_MISMATCH);
    assert!(g.acquire());
    assert_eq!(g.release(), OK);
    assert_eq!(g.release(), STATE_MISMATCH);
}

#[test]
fn rollback_releases_reservation() {
    let g = initializing();
    assert_eq!(g.reserve(), OK);
    unsafe {
        assert_eq!(seekdb_runtime_generation_abort(&g), OK);
    }
    assert_eq!(g.transition(State::Failed as u8), OK);
    assert_eq!(g.promote(), STATE_MISMATCH);
}

#[test]
fn drain_closes_admission_and_preserves_live_leases() {
    let g = active();
    assert!(g.acquire());
    assert_eq!(g.wait_for_drain(0), STATE_MISMATCH);
    assert_eq!(g.quiesce(), OK);
    assert_eq!(g.quiesce(), OK);
    assert!(!g.acquire());
    assert_eq!(g.wait_for_drain(-1), INVALID);
    assert_eq!(g.wait_for_drain(0), TIMEOUT);
    assert_eq!(g.transition(State::Stopped as u8), BUSY);
    assert_eq!(g.release(), OK);
    assert_eq!(g.wait_for_drain(0), OK);
    assert_eq!(g.transition(State::Stopped as u8), OK);
    assert!(!g.acquire());
}

#[test]
fn blocked_requires_terminal_authority_and_no_leases() {
    let g = active();
    assert!(g.acquire());
    assert_eq!(g.terminal_stop(), STATE_MISMATCH);
    assert_eq!(g.transition(State::Blocked as u8), OK);
    assert_eq!(g.transition(State::Stopped as u8), STATE_MISMATCH);
    assert_eq!(g.quiesce(), STATE_MISMATCH);
    assert_eq!(g.terminal_stop(), BUSY);
    assert_eq!(g.release(), OK);
    assert_eq!(g.terminal_stop(), OK);
}

#[test]
fn concurrent_drain_wakes_all_waiters() {
    let g = Arc::new(active());
    assert!(g.acquire());
    assert_eq!(g.quiesce(), OK);
    let ready = Arc::new(Barrier::new(5));
    let (send, receive) = mpsc::channel();
    let threads: Vec<_> = (0..4)
        .map(|_| {
            let g = g.clone();
            let ready = ready.clone();
            let send = send.clone();
            thread::spawn(move || {
                ready.wait();
                send.send(g.wait_for_drain(2_000_000)).unwrap();
            })
        })
        .collect();
    ready.wait();
    assert_eq!(g.release(), OK);
    for _ in 0..4 {
        assert_eq!(receive.recv_timeout(Duration::from_secs(3)).unwrap(), OK);
    }
    for t in threads {
        t.join().unwrap();
    }
}

#[test]
fn concurrent_acquire_and_quiesce_are_linearizable() {
    let g = Arc::new(active());
    let ready = Arc::new(Barrier::new(9));
    let threads: Vec<_> = (0..8)
        .map(|_| {
            let g = g.clone();
            let ready = ready.clone();
            thread::spawn(move || {
                ready.wait();
                for _ in 0..1000 {
                    if g.acquire() {
                        assert_eq!(g.release(), OK);
                    }
                }
            })
        })
        .collect();
    ready.wait();
    assert_eq!(g.quiesce(), OK);
    for t in threads {
        t.join().unwrap();
    }
    assert!(!g.acquire());
    assert_eq!(g.wait_for_drain(0), OK);
    assert_eq!(g.lock().leases, 0);
}

#[test]
fn ffi_allocation_and_null_contract() {
    unsafe {
        let g = seekdb_runtime_generation_create();
        assert!(!g.is_null());
        assert_eq!(seekdb_runtime_generation_state(g), 0);
        assert_eq!(seekdb_runtime_generation_leases(g), 0);
        assert_eq!(seekdb_runtime_generation_transition(g, 255), INVALID);
        seekdb_runtime_generation_destroy(g);
        seekdb_runtime_generation_destroy(std::ptr::null_mut());
        assert_eq!(seekdb_runtime_generation_state(std::ptr::null()), 8);
        assert_eq!(seekdb_runtime_generation_reserve(std::ptr::null()), INVALID);
        assert_eq!(seekdb_runtime_generation_acquire(std::ptr::null()), 0);
    }
}
