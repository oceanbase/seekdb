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

fn edge(provider: u32, consumer: u32) -> Edge {
    Edge { provider, consumer }
}

#[test]
fn ready_order_is_stable_and_reconsiders_newly_ready_nodes() {
    assert_eq!(plan(0, &[], true), Ok(vec![]));
    assert_eq!(plan(4, &[], true), Ok(vec![0, 1, 2, 3]));
    assert_eq!(
        plan(5, &[edge(3, 0), edge(0, 2)], true),
        Ok(vec![1, 3, 0, 2, 4])
    );
}

#[test]
fn diamond_and_duplicate_service_edges_have_one_indegree_each() {
    let mut edges = vec![edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 3), edge(0, 1)];
    assert_eq!(plan(4, &edges, true), Ok(vec![0, 1, 2, 3]));
    edges.reverse();
    assert_eq!(plan(4, &edges, true), Ok(vec![0, 1, 2, 3]));
}

#[test]
fn self_dependency_policy_is_explicit_and_does_not_hide_other_cycles() {
    assert_eq!(plan(1, &[edge(0, 0)], true), Ok(vec![0]));
    assert_eq!(plan(1, &[edge(0, 0)], false), Err((CYCLE, 0)));
    assert_eq!(
        plan(2, &[edge(0, 0), edge(0, 1), edge(1, 0)], true),
        Err((CYCLE, 0))
    );
}

#[test]
fn cycle_reports_blocked_node_not_a_false_cycle_member() {
    assert_eq!(
        plan(4, &[edge(2, 3), edge(3, 2), edge(2, 0)], true),
        Err((CYCLE, 0))
    );
    assert_eq!(plan(2, &[edge(0, 2)], true), Err((INVALID, u32::MAX)));
}

#[test]
fn ffi_errors_leave_plan_untouched_and_initialize_diagnostic() {
    let cycle = [edge(1, 2), edge(2, 1)];
    let mut order = [99; 3];
    let mut blocked = 123;
    assert_eq!(
        unsafe {
            seekdb_runtime_dependency_plan(
                3,
                cycle.as_ptr(),
                2,
                1,
                order.as_mut_ptr(),
                3,
                &mut blocked,
            )
        },
        CYCLE
    );
    assert_eq!(order, [99; 3]);
    assert_eq!(blocked, 1);
    for (count, edge_count, capacity, self_policy, expected) in [
        (3, 2, 2, 1, INVALID),
        (3, 2, 3, 2, INVALID),
        (MAX_NODES + 1, 2, 3, 1, LIMIT),
        (3, MAX_EDGES + 1, 3, 1, LIMIT),
    ] {
        assert_eq!(
            unsafe {
                seekdb_runtime_dependency_plan(
                    count,
                    cycle.as_ptr(),
                    edge_count,
                    self_policy,
                    order.as_mut_ptr(),
                    capacity,
                    &mut blocked,
                )
            },
            expected
        );
        assert_eq!(blocked, u32::MAX);
        assert_eq!(order, [99; 3]);
    }
    assert_eq!(
        unsafe {
            seekdb_runtime_dependency_plan(
                3,
                ptr::null(),
                2,
                1,
                order.as_mut_ptr(),
                3,
                &mut blocked,
            )
        },
        INVALID
    );
    assert_eq!(
        unsafe {
            seekdb_runtime_dependency_plan(0, ptr::null(), 0, 1, ptr::null_mut(), 0, &mut blocked)
        },
        OK
    );
}

#[test]
fn maximum_length_chain_is_iterative() {
    let edges: Vec<_> = (1..MAX_NODES).map(|i| edge(i, i - 1)).collect();
    let order = plan(MAX_NODES, &edges, true).unwrap();
    assert_eq!(order.len(), MAX_NODES as usize);
    assert!(order.windows(2).all(|pair| pair[0] == pair[1] + 1));
}

#[test]
fn all_four_node_graphs_match_small_reference_scheduler() {
    let possible: Vec<_> = (0..4)
        .flat_map(|p| (0..4).filter(move |c| *c != p).map(move |c| edge(p, c)))
        .collect();
    for mask in 0u32..(1 << possible.len()) {
        let edges: Vec<_> = possible
            .iter()
            .enumerate()
            .filter(|(i, _)| mask & (1 << i) != 0)
            .map(|(_, edge)| *edge)
            .collect();
        let mut expected = Vec::new();
        loop {
            let ready = (0..4).find(|candidate| {
                !expected.contains(candidate)
                    && edges
                        .iter()
                        .filter(|edge| edge.consumer == *candidate)
                        .all(|edge| expected.contains(&edge.provider))
            });
            if let Some(node) = ready {
                expected.push(node);
            } else {
                break;
            }
        }
        match plan(4, &edges, true) {
            Ok(order) => assert_eq!(order, expected),
            Err((status, _)) => {
                assert_eq!(status, CYCLE);
                assert!(expected.len() < 4);
            }
        }
    }
}
