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

fn begin(s: &mut InputState, op: u32, input: u32) -> Effect {
    let mut effect = Effect::default();
    assert_eq!(s.begin(op, input, &mut effect), OK);
    assert_ne!(effect.ticket, 0);
    effect
}
fn finish(s: &mut InputState, effect: Effect, outcome: u32) {
    let mut result = Effect::default();
    assert_eq!(s.finish(effect.ticket, outcome, &mut result), OK);
    if outcome != ERROR {
        assert_eq!(result, Effect::default());
    }
}
fn row(s: &mut InputState, input: u32) {
    let e = begin(s, READ, input);
    finish(s, e, ROW);
}
fn bind(s: &mut InputState, input: u32) {
    let e = begin(s, BIND, input);
    finish(s, e, DONE);
}

#[test]
fn fan_in_needs_every_current_source_and_preserves_unrelated_branches() {
    let edges = [
        Edge {
            source: 0,
            target: 2,
        },
        Edge {
            source: 1,
            target: 2,
        },
        Edge {
            source: 3,
            target: 4,
        },
    ];
    let mut s = InputState::new(5, &edges).unwrap();
    row(&mut s, 0);
    let mut e = Effect::default();
    assert_eq!(s.begin(BIND, 2, &mut e), STATE_MISMATCH);
    assert!(s.failed);
    assert_eq!(e.rows, 31);
    assert_eq!(e.bindings, 31);
    s.reset(true);
    row(&mut s, 0);
    row(&mut s, 1);
    bind(&mut s, 2);
    row(&mut s, 2);
    row(&mut s, 3);
    bind(&mut s, 4);
    row(&mut s, 4);
    let e = begin(&mut s, RESCAN, 0);
    assert_eq!((e.rows, e.bindings), (0b00101, 0b00100));
    assert_eq!(s.rows, 0b11010);
    assert_eq!(s.bound, 0b10000);
    finish(&mut s, e, DONE);
    // Rewinding does not make a source row available for the next binding.
    assert_eq!(s.begin(BIND, 2, &mut Effect::default()), STATE_MISMATCH);
}
#[test]
fn dependent_read_and_rewind_preserve_own_environment_but_invalidate_descendants() {
    let mut s = InputState::new(
        3,
        &[
            Edge {
                source: 0,
                target: 1,
            },
            Edge {
                source: 1,
                target: 2,
            },
        ],
    )
    .unwrap();
    row(&mut s, 0);
    bind(&mut s, 1);
    row(&mut s, 1);
    bind(&mut s, 2);
    row(&mut s, 2);
    let e = begin(&mut s, READ, 1);
    assert_eq!((e.rows, e.bindings), (6, 4));
    assert_eq!(s.bound, 2);
    finish(&mut s, e, EOF);
    for _ in 0..2 {
        let e = begin(&mut s, RESCAN, 1);
        finish(&mut s, e, DONE);
        assert_eq!(s.rows, 1);
        assert_eq!(s.bound, 2);
    }
    row(&mut s, 1);
    bind(&mut s, 2);
    row(&mut s, 2);
    let e = begin(&mut s, BIND, 1);
    assert_eq!((e.rows, e.bindings), (6, 6));
    finish(&mut s, e, DONE);
    assert_eq!(s.rows, 1);
    assert_eq!(s.bound, 2);
}
#[test]
fn failed_work_wrong_completion_and_reentrancy_poison_until_full_reset() {
    for fault in 0..5 {
        let mut s = InputState::new(
            2,
            &[Edge {
                source: 0,
                target: 1,
            }],
        )
        .unwrap();
        row(&mut s, 0);
        let e = begin(&mut s, BIND, 1);
        let mut cleared = Effect::default();
        let result = match fault {
            0 => s.finish(e.ticket, ERROR, &mut cleared),
            1 => s.finish(e.ticket + 1, DONE, &mut cleared),
            2 => s.finish(e.ticket, ROW, &mut cleared),
            3 => s.begin(READ, 0, &mut cleared),
            _ => {
                s.reset(true);
                s.finish(e.ticket, DONE, &mut cleared)
            }
        };
        assert_eq!(
            result,
            if fault == 0 {
                OK
            } else if fault == 2 {
                INVALID
            } else {
                STATE_MISMATCH
            }
        );
        assert_eq!((cleared.rows, cleared.bindings), (3, 3));
        assert!(s.failed);
        assert_eq!((s.rows, s.bound), (0, 0));
        assert_eq!(s.begin(READ, 0, &mut Effect::default()), STATE_MISMATCH);
        s.reset(true);
        let fresh = begin(&mut s, READ, 0);
        assert!(fresh.ticket > e.ticket);
        finish(&mut s, fresh, ROW);
        bind(&mut s, 1);
    }
}
#[test]
fn bounds_duplicate_edges_cycles_and_ticket_exhaustion() {
    assert!(matches!(InputState::new(65, &[]), Err(LIMIT)));
    assert!(matches!(
        InputState::new(
            2,
            &[Edge {
                source: 2,
                target: 0
            }]
        ),
        Err(INVALID)
    ));
    assert!(matches!(
        InputState::new(
            2,
            &[Edge {
                source: 1,
                target: 1
            }]
        ),
        Err(CYCLE)
    ));
    assert!(matches!(
        InputState::new(
            2,
            &[
                Edge {
                    source: 0,
                    target: 1
                },
                Edge {
                    source: 1,
                    target: 0
                }
            ]
        ),
        Err(CYCLE)
    ));
    let mut s = InputState::new(
        64,
        &[Edge {
            source: 0,
            target: 63,
        }; 1024],
    )
    .unwrap();
    row(&mut s, 0);
    bind(&mut s, 63);
    row(&mut s, 63);
    assert_eq!(s.reset(true).rows, u64::MAX);
    s.next_ticket = u64::MAX - 1;
    let e = begin(&mut s, READ, 0);
    finish(&mut s, e, ROW);
    assert_eq!(s.begin(BIND, 63, &mut Effect::default()), STATE_MISMATCH);
    s.reset(true);
    assert_eq!(s.begin(READ, 0, &mut Effect::default()), STATE_MISMATCH); // Never wrap after reset.
    let mut empty = InputState::new(0, &[]).unwrap();
    assert_eq!(empty.reset(true), Effect::default());
    assert_eq!(empty.begin(READ, 0, &mut Effect::default()), INVALID);
}
#[test]
fn all_four_input_graphs_match_independent_reachability_and_invalidation() {
    let pairs: Vec<_> = (0..4)
        .flat_map(|s| {
            (0..4).filter(move |t| *t != s).map(move |t| Edge {
                source: s,
                target: t,
            })
        })
        .collect();
    let mut dags = 0;
    for mask in 0u32..1 << pairs.len() {
        let edges: Vec<_> = pairs
            .iter()
            .enumerate()
            .filter(|(i, _)| mask & (1 << i) != 0)
            .map(|(_, e)| *e)
            .collect();
        let reach: Vec<u64> = (0..4)
            .map(|start| {
                let mut seen = 0;
                let mut todo = vec![start];
                while let Some(node) = todo.pop() {
                    for e in edges.iter().filter(|e| e.source == node) {
                        if seen & (1 << e.target) == 0 {
                            seen |= 1 << e.target;
                            todo.push(e.target);
                        }
                    }
                }
                seen
            })
            .collect();
        let cycle = (0..4).any(|i| reach[i] & (1 << i) != 0);
        let mut s = match InputState::new(4, &edges) {
            Err(code) => {
                assert!(cycle);
                assert_eq!(code, CYCLE);
                continue;
            }
            Ok(s) => {
                assert!(!cycle);
                s
            }
        };
        dags += 1;
        // Produce all rows in a reference topological order.
        let mut seen = 0u64;
        while seen != 15 {
            let i = (0..4)
                .find(|i| {
                    seen & (1 << i) == 0
                        && edges
                            .iter()
                            .filter(|e| e.target == *i)
                            .all(|e| seen & (1 << e.source) != 0)
                })
                .unwrap();
            if edges.iter().any(|e| e.target == i) {
                bind(&mut s, i);
            }
            row(&mut s, i);
            seen |= 1 << i;
        }
        assert_eq!(s.rows, 15);
        for i in 0..4 {
            let mut copy = s.clone();
            let effect = begin(&mut copy, RESCAN, i);
            assert_eq!(effect.rows, reach[i as usize] | (1 << i));
            assert_eq!(effect.bindings, reach[i as usize]);
            finish(&mut copy, effect, DONE);
            assert_eq!(copy.rows, 15 & !effect.rows);
            assert_eq!(copy.bound, s.bound & !effect.bindings);
        }
    }
    assert_eq!(dags, 543);
}
#[test]
fn ffi_copies_graph_checks_outputs_and_closes_ownership() {
    unsafe {
        let mut state = ptr::dangling_mut();
        assert_eq!(
            seekdb_runtime_input_state_create(2, ptr::null(), 1, &mut state),
            INVALID
        );
        assert!(state.is_null());
        let mut edge = Edge {
            source: 0,
            target: 1,
        };
        assert_eq!(
            seekdb_runtime_input_state_create(2, &edge, 1, &mut state),
            OK
        );
        edge.target = 0;
        assert_eq!(edge.target, 0); // Caller storage is no longer borrowed.
        let mut e = Effect::default();
        assert_eq!(
            seekdb_runtime_input_state_begin(state, READ, 0, ptr::null_mut()),
            INVALID
        );
        assert_eq!(seekdb_runtime_input_state_begin(state, READ, 0, &mut e), OK);
        let ticket = e.ticket;
        assert_eq!((e.rows, e.bindings), (3, 2));
        assert_eq!(
            seekdb_runtime_input_state_finish(state, ticket, ROW, &mut e),
            OK
        );
        assert_eq!(seekdb_runtime_input_state_begin(state, BIND, 1, &mut e), OK);
        assert_eq!(
            seekdb_runtime_input_state_finish(state, e.ticket, DONE, &mut e),
            OK
        );
        assert_eq!(seekdb_runtime_input_state_reset(state, 2, &mut e), INVALID);
        assert_eq!(seekdb_runtime_input_state_begin(state, READ, 1, &mut e), OK);
        assert_eq!(
            seekdb_runtime_input_state_finish(state, e.ticket, ERROR, &mut e),
            OK
        );
        assert_eq!(seekdb_runtime_input_state_reset(state, 1, &mut e), OK);
        assert_eq!((e.rows, e.bindings), (3, 3));
        seekdb_runtime_input_state_destroy(state);
        seekdb_runtime_input_state_destroy(ptr::null_mut());
        assert_eq!(mem::size_of::<Edge>(), 8);
        assert_eq!(mem::size_of::<Effect>(), 24);
    }
}
