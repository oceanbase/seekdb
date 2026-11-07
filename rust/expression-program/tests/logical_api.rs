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

use expression_program::{
    logical::{self, Execution, InputProvider, Instruction, Opcode, ProgramSpec},
    Control, EVALUATED, VALID,
};

struct Inputs {
    values: Vec<Vec<Option<i64>>>,
    calls: Vec<(usize, Vec<u64>)>,
    fail: Option<usize>,
    cancelled: bool,
    checks: usize,
    cancel_after: Option<usize>,
}
impl Control for Inputs {
    fn check(&mut self) -> Result<(), i32> {
        self.checks += 1;
        if self.cancelled || self.cancel_after.is_some_and(|limit| self.checks >= limit) {
            Err(-4012)
        } else {
            Ok(())
        }
    }
}
impl InputProvider for Inputs {
    fn demand(
        &mut self,
        node: usize,
        demand: &[u64],
        values: &mut [u64],
        flags: &mut [u64],
    ) -> Result<(), i32> {
        self.calls.push((node, demand.to_vec()));
        if self.fail == Some(node) {
            return Err(-4136);
        }
        for (row, &needed) in demand.iter().enumerate() {
            if needed != 0 {
                values[row] = self.values[node][row].unwrap_or(0) as u64;
                flags[row] = EVALUATED
                    | if self.values[node][row].is_some() {
                        VALID
                    } else {
                        0
                    };
            }
        }
        Ok(())
    }
}
fn inputs(values: Vec<Vec<Option<i64>>>) -> Inputs {
    Inputs {
        values,
        calls: vec![],
        fail: None,
        cancelled: false,
        checks: 0,
        cancel_after: None,
    }
}
fn instruction(opcode: Opcode, argument_start: u32, argument_count: u32) -> Instruction {
    Instruction {
        opcode,
        argument_start,
        argument_count,
    }
}
fn image(nodes: &[Instruction], arguments: &[u32]) -> logical::ProgramImage<Vec<u64>> {
    let spec = ProgramSpec {
        instructions: nodes,
        arguments,
    };
    logical::compile_in(spec, vec![0; logical::image_words(spec).unwrap()]).unwrap()
}

#[test]
fn truth_tables_short_circuit_and_noncanonical_truth_values() {
    for opcode in [Opcode::And, Opcode::Or] {
        let program = image(
            &[
                instruction(Opcode::Input, 0, 0),
                instruction(Opcode::Input, 0, 0),
                instruction(opcode, 0, 2),
            ],
            &[0, 1],
        );
        let choices = [None, Some(0), Some(-9)];
        let mut left = vec![];
        let mut right = vec![];
        let mut expected = vec![];
        for a in choices {
            for b in choices {
                left.push(a);
                right.push(b);
                expected.push(match opcode {
                    Opcode::And if a == Some(0) || b == Some(0) => Some(0),
                    Opcode::Or if a.is_some_and(|x| x != 0) || b.is_some_and(|x| x != 0) => Some(1),
                    _ if a.is_none() || b.is_none() => None,
                    Opcode::And => Some(1),
                    _ => Some(0),
                });
            }
        }
        let mut provider = inputs(vec![left, right]);
        let mut execution = Execution::new(
            &program,
            9,
            vec![0; logical::required_words(&program, 9).unwrap()],
        )
        .unwrap();
        execution.execute(&[0; 9], &mut provider).unwrap();
        for (row, expected) in expected.iter().enumerate() {
            assert_eq!(execution.root_flags()[row] & VALID != 0, expected.is_some());
            if let Some(value) = expected {
                assert_eq!(execution.root_values()[row], *value);
            }
        }
        assert_eq!(provider.calls.len(), 2);
        assert!(provider.calls[1].1.contains(&0));
    }
}

#[test]
fn shared_nodes_fill_different_masks_and_only_whole_masks_cross_provider() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::And, 0, 2),
            instruction(Opcode::Not, 2, 1),
            instruction(Opcode::Or, 3, 2),
        ],
        &[0, 1, 1, 2, 3],
    );
    let mut provider = inputs(vec![
        vec![Some(0), Some(1), Some(0)],
        vec![Some(1), Some(0), None],
    ]);
    let mut execution = Execution::new(
        &program,
        3,
        vec![0; logical::required_words(&program, 3).unwrap()],
    )
    .unwrap();
    execution.execute(&[0; 3], &mut provider).unwrap();
    assert_eq!(
        provider.calls,
        vec![(0, vec![1, 1, 1]), (1, vec![0, 1, 0]), (1, vec![1, 0, 1])]
    );
    assert_eq!(execution.root_values()[..2], [0, 1]);
    assert_eq!(execution.root_flags()[2], EVALUATED);
}

#[test]
fn cached_internal_node_never_demands_its_stale_children_and_allskip_is_empty() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Not, 0, 1),
            instruction(Opcode::Not, 1, 1),
        ],
        &[0, 1],
    );
    let mut provider = inputs(vec![]);
    provider.fail = Some(0);
    let mut execution = Execution::new(
        &program,
        2,
        vec![0; logical::required_words(&program, 2).unwrap()],
    )
    .unwrap();
    execution.values_mut()[2..4].copy_from_slice(&[1, 0]);
    execution.flags_mut()[2..4].fill(EVALUATED | VALID);
    execution.execute(&[0, 1], &mut provider).unwrap();
    assert_eq!(execution.root_values(), &[0, 0]);
    assert_eq!(execution.root_flags(), &[EVALUATED | VALID, 0]);
    assert!(provider.calls.is_empty());
    provider.cancelled = true;
    execution.execute(&[1, 1], &mut provider).unwrap();
    assert_eq!(provider.checks, 1);
}

#[test]
fn errors_preserve_completed_internal_snapshots_but_hide_public_results() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Not, 0, 1),
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::And, 1, 2),
        ],
        &[0, 1, 2],
    );
    let mut provider = inputs(vec![vec![Some(0)], vec![], vec![Some(0)]]);
    provider.fail = Some(2);
    let mut storage = vec![0; logical::required_words(&program, 1).unwrap()];
    {
        let mut execution = Execution::new(&program, 1, &mut storage[..]).unwrap();
        let error = execution.execute(&[0], &mut provider).unwrap_err();
        assert_eq!(error.host_code, -4136);
        assert!(execution.root_values().is_empty());
        assert!(execution.root_flags().is_empty());
    }
    assert_eq!(storage[program.node_count() + 1], EVALUATED | VALID);
    assert_eq!(storage[program.node_count() + 3] & EVALUATED, 0);
    provider.cancelled = true;
    let mut execution = Execution::new(
        &program,
        1,
        vec![0; logical::required_words(&program, 1).unwrap()],
    )
    .unwrap();
    assert_eq!(
        execution
            .execute(&[0], &mut provider)
            .unwrap_err()
            .host_code,
        -4012
    );
    assert_eq!(provider.calls.len(), 2);
}

#[test]
fn dynamic_images_cover_deep_and_wide_graphs_without_admission_fallback() {
    let mut nodes = vec![instruction(Opcode::Input, 0, 0)];
    let mut arguments = vec![];
    for id in 1..600 {
        arguments.push(id - 1);
        nodes.push(instruction(Opcode::Not, id - 1, 1));
    }
    let program = image(&nodes, &arguments);
    let mut execution = Execution::new(
        &program,
        1,
        vec![0; logical::required_words(&program, 1).unwrap()],
    )
    .unwrap();
    let mut provider = inputs(vec![vec![Some(1)]]);
    execution.execute(&[0], &mut provider).unwrap();
    assert_eq!(execution.root_values(), &[0]);
    assert_eq!(provider.calls.len(), 1);
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::And, 0, 5000),
        ],
        &vec![0; 5000],
    );
    let mut execution = Execution::new(
        &program,
        1,
        vec![0; logical::required_words(&program, 1).unwrap()],
    )
    .unwrap();
    execution.execute(&[0], &mut provider).unwrap();
    assert_eq!(execution.root_values(), &[1]);
    assert!(logical::required_words(&program, usize::MAX).is_err());
}

#[test]
fn images_reject_malformed_graphs_and_leave_compile_destination_unchanged() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Not, 0, 1),
        ],
        &[0],
    );
    for length in 0..program.words().len() {
        assert!(logical::ProgramImage::parse(&program.words()[..length]).is_err());
    }
    for (index, value) in [
        (0, 0),
        (1, u64::MAX),
        (2, u64::MAX),
        (3, 42),
        (4, u64::MAX),
        (6, 0),
        (7, 1),
        (8, 2),
        (9, 1),
    ] {
        let mut corrupt = program.words().to_vec();
        corrupt[index] = value;
        assert!(
            logical::ProgramImage::parse(corrupt).is_err(),
            "{index}: {value}"
        );
    }
    let mut storage = vec![99; program.words().len()];
    let invalid = [
        instruction(Opcode::Input, 0, 0),
        instruction(Opcode::Not, 0, 1),
    ];
    assert!(logical::compile_in(
        ProgramSpec {
            instructions: &invalid,
            arguments: &[1]
        },
        &mut storage[..]
    )
    .is_err());
    assert!(storage.iter().all(|&value| value == 99));
}

#[test]
fn pending_cached_ancestor_hides_stale_available_descendant() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Not, 0, 1),
            instruction(Opcode::Not, 1, 1),
        ],
        &[0, 1],
    );
    let mut provider = inputs(vec![vec![], vec![Some(7)]]);
    provider.fail = Some(0);
    let mut execution = Execution::new(
        &program,
        1,
        vec![0; logical::required_words(&program, 1).unwrap()],
    )
    .unwrap();
    execution.flags_mut()[0..2].fill(logical::MATERIALIZED_PENDING);
    execution.execute(&[0], &mut provider).unwrap();
    assert_eq!(provider.calls, vec![(1, vec![1])]);
    assert_eq!(execution.root_values(), &[0]);
    assert_eq!(execution.root_flags(), &[EVALUATED | VALID]);
}

#[test]
fn fresh_binding_clears_prior_computed_results_and_polls_large_batches() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Not, 0, 1),
        ],
        &[0],
    );
    let rows = 10000;
    let mut provider = inputs(vec![vec![Some(0); rows]]);
    let mut execution = Execution::new(
        &program,
        rows,
        vec![0; logical::required_words(&program, rows).unwrap()],
    )
    .unwrap();
    execution.execute(&vec![0; rows], &mut provider).unwrap();
    assert!(provider.checks > 1);
    assert_eq!(execution.root_values()[0], 1);
    execution.values_mut()[0] = 1;
    execution.flags_mut()[0] = EVALUATED | VALID;
    execution.execute(&vec![0; rows], &mut provider).unwrap();
    assert_eq!(execution.root_values()[0], 0);
}

#[test]
fn shared_immutable_program_has_isolated_concurrent_demand_scopes() {
    let program = image(
        &[
            instruction(Opcode::Input, 0, 0),
            instruction(Opcode::Not, 0, 1),
        ],
        &[0],
    );
    std::thread::scope(|scope| {
        let program = &program;
        let handles: Vec<_> = [0, 1]
            .into_iter()
            .map(|value| {
                scope.spawn(move || {
                    let mut provider = inputs(vec![vec![Some(value)]]);
                    let mut execution = Execution::new(
                        program,
                        1,
                        vec![0; logical::required_words(program, 1).unwrap()],
                    )
                    .unwrap();
                    execution.execute(&[0], &mut provider).unwrap();
                    (execution.root_values()[0], provider.calls.len())
                })
            })
            .collect();
        let results: Vec<_> = handles
            .into_iter()
            .map(|handle| handle.join().unwrap())
            .collect();
        assert_eq!(results, vec![(1, 1), (0, 1)]);
    });
}

#[test]
fn sizing_failures_and_mid_execution_cancellation_do_not_publish_results() {
    let nodes = [
        instruction(Opcode::Input, 0, 0),
        instruction(Opcode::Not, 0, 1),
    ];
    let spec = ProgramSpec {
        instructions: &nodes,
        arguments: &[0],
    };
    let mut short = vec![99; logical::image_words(spec).unwrap() - 1];
    assert!(logical::compile_in(spec, &mut short[..]).is_err());
    assert!(short.iter().all(|&word| word == 99));
    let program = image(&nodes, &[0]);
    let rows = 10000;
    let words = logical::required_words(&program, rows).unwrap();
    assert!(Execution::new(&program, rows, vec![0; words - 1]).is_err());
    let mut provider = inputs(vec![vec![Some(0); rows]]);
    provider.cancel_after = Some(2);
    let mut execution = Execution::new(&program, rows, vec![0; words]).unwrap();
    assert_eq!(
        execution
            .execute(&vec![0; rows], &mut provider)
            .unwrap_err()
            .host_code,
        -4012
    );
    assert!(execution.root_values().is_empty());
    assert!(execution.root_flags().is_empty());
    assert_eq!(provider.checks, 2);
    let mut execution = Execution::new(
        &program,
        1,
        vec![0; logical::required_words(&program, 1).unwrap()],
    )
    .unwrap();
    assert!(execution.execute(&[], &mut provider).is_err());
    assert!(execution.root_flags().is_empty());
}
