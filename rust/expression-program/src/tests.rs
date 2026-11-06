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
use Operation::*;

fn program(
    operations: Vec<Operation>,
    filters: Vec<NodeId>,
    projections: Vec<NodeId>,
    inputs: usize,
) -> Program {
    Program::new(
        operations
            .into_iter()
            .map(|operation| Node { operation })
            .collect(),
        filters,
        projections,
        inputs,
    )
    .unwrap()
}

fn execution(program: &Program, capacity: usize) -> Execution<Vec<u64>> {
    Execution::new(
        program,
        capacity,
        vec![0; required_words(program, capacity).unwrap()],
    )
    .unwrap()
}

fn bind<B: AsRef<[u64]> + AsMut<[u64]>>(
    execution: &mut Execution<B>,
    columns: &[&[Option<i64>]],
    skip: &[u8],
) {
    let capacity = execution.capacity();
    let values = execution.input_values_mut();
    for (column, rows) in columns.iter().enumerate() {
        for (row, value) in rows.iter().enumerate() {
            values[column * capacity + row] = value.unwrap_or(0) as u64;
        }
    }
    let validity = execution.input_validity_mut();
    for (column, rows) in columns.iter().enumerate() {
        for (row, value) in rows.iter().enumerate() {
            validity[column * capacity + row] = u64::from(value.is_some());
        }
    }
    execution.begin_batch(skip.len(), skip).unwrap();
}

fn results<B: AsRef<[u64]> + AsMut<[u64]>>(
    execution: &Execution<B>,
    node: NodeId,
) -> Vec<Option<i64>> {
    execution
        .node_values(node)
        .iter()
        .zip(execution.node_flags(node))
        .map(|(&value, &flags)| {
            assert_ne!(flags & EVALUATED, 0);
            (flags & VALID != 0).then_some(value as i64)
        })
        .collect()
}

#[test]
fn multi_root_shared_node_is_filled_for_different_branch_masks() {
    let program = program(
        vec![
            Input(0),
            Constant(Some(10)),
            Add(0, 1),
            Constant(Some(0)),
            Gt(0, 3),
            Not(4),
            Case {
                arms: vec![(4, 2)],
                otherwise: Some(3),
            },
            Case {
                arms: vec![(5, 2)],
                otherwise: Some(3),
            },
        ],
        vec![],
        vec![6, 7],
        1,
    );
    let mut execution = execution(&program, 3);
    bind(&mut execution, &[&[Some(-2), Some(3), Some(0)]], &[0, 0, 0]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 6), [Some(0), Some(13), Some(0)]);
    assert_eq!(results(&execution, 7), [Some(8), Some(0), Some(10)]);
    assert_eq!(results(&execution, 2), [Some(8), Some(13), Some(10)]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 7), [Some(8), Some(0), Some(10)]);
}

#[test]
fn case_evaluates_only_matching_arms_and_treats_null_as_unmatched() {
    let program = program(
        vec![
            Constant(None),
            Constant(Some(1)),
            Constant(Some(0)),
            Div {
                left: 1,
                right: 2,
                error_on_zero: true,
            },
            Constant(Some(-9)),
            Case {
                arms: vec![(0, 3), (1, 4), (1, 3)],
                otherwise: Some(3),
            },
            Case {
                arms: vec![(0, 3)],
                otherwise: None,
            },
        ],
        vec![],
        vec![5, 6],
        0,
    );
    let mut execution = execution(&program, 2);
    bind(&mut execution, &[], &[0, 0]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 5), [Some(-9), Some(-9)]);
    assert_eq!(results(&execution, 6), [None, None]);
    assert_eq!(execution.node_flags(3), [0, 0]);
}

#[test]
fn and_or_skip_errors_but_rebinding_can_demand_them() {
    let program = program(
        vec![
            Input(0),
            Constant(Some(1)),
            Constant(Some(0)),
            Div {
                left: 1,
                right: 2,
                error_on_zero: true,
            },
            And(vec![0, 3]),
            Not(0),
            Or(vec![5, 3]),
        ],
        vec![],
        vec![4, 6],
        1,
    );
    let mut execution = execution(&program, 2);
    bind(&mut execution, &[&[Some(0), Some(0)]], &[0, 0]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 4), [Some(0), Some(0)]);
    assert_eq!(results(&execution, 6), [Some(1), Some(1)]);
    assert_eq!(execution.node_flags(3), [0, 0]);
    bind(&mut execution, &[&[Some(0), Some(1)]], &[0, 0]);
    let error = execution.filter_project(&program, &mut ()).unwrap_err();
    assert_eq!(
        (error.kind, error.node, error.row),
        (ErrorKind::DivisionByZero, 3, 1)
    );
}

#[test]
fn nullable_truth_tables_and_predicate_normalization() {
    let program = program(
        vec![
            Input(0),
            Input(1),
            And(vec![0, 1]),
            Or(vec![0, 1]),
            Not(0),
            Truth(0),
            IsNull(0),
            IsNotNull(0),
            NullSafeEq(0, 1),
        ],
        vec![],
        (2..9).collect(),
        2,
    );
    let mut execution = execution(&program, 9);
    bind(
        &mut execution,
        &[
            &[
                Some(-7),
                Some(-7),
                Some(-7),
                Some(0),
                Some(0),
                Some(0),
                None,
                None,
                None,
            ],
            &[
                Some(-7),
                Some(0),
                None,
                Some(-7),
                Some(0),
                None,
                Some(-7),
                Some(0),
                None,
            ],
        ],
        &[0; 9],
    );
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(
        results(&execution, 2),
        [
            Some(1),
            Some(0),
            None,
            Some(0),
            Some(0),
            Some(0),
            None,
            Some(0),
            None
        ]
    );
    assert_eq!(
        results(&execution, 3),
        [
            Some(1),
            Some(1),
            Some(1),
            Some(1),
            Some(0),
            None,
            Some(1),
            None,
            None
        ]
    );
    assert_eq!(
        results(&execution, 4),
        [
            Some(0),
            Some(0),
            Some(0),
            Some(1),
            Some(1),
            Some(1),
            None,
            None,
            None
        ]
    );
    assert_eq!(
        results(&execution, 5),
        [
            Some(1),
            Some(1),
            Some(1),
            Some(0),
            Some(0),
            Some(0),
            None,
            None,
            None
        ]
    );
    assert_eq!(
        results(&execution, 6),
        [
            Some(0),
            Some(0),
            Some(0),
            Some(0),
            Some(0),
            Some(0),
            Some(1),
            Some(1),
            Some(1)
        ]
    );
    assert_eq!(
        results(&execution, 7),
        [
            Some(1),
            Some(1),
            Some(1),
            Some(1),
            Some(1),
            Some(1),
            Some(0),
            Some(0),
            Some(0)
        ]
    );
    assert_eq!(
        results(&execution, 8),
        [
            Some(1),
            Some(0),
            Some(0),
            Some(0),
            Some(1),
            Some(0),
            Some(0),
            Some(0),
            Some(1)
        ]
    );
}

#[test]
fn comparisons_propagate_null_and_compare_signed_values() {
    let program = program(
        vec![
            Input(0),
            Input(1),
            Eq(0, 1),
            Ne(0, 1),
            Lt(0, 1),
            Le(0, 1),
            Gt(0, 1),
            Ge(0, 1),
        ],
        vec![],
        (2..8).collect(),
        2,
    );
    let mut execution = execution(&program, 4);
    bind(
        &mut execution,
        &[
            &[Some(-1), Some(3), Some(4), None],
            &[Some(0), Some(3), Some(-4), Some(1)],
        ],
        &[0; 4],
    );
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 2), [Some(0), Some(1), Some(0), None]);
    assert_eq!(results(&execution, 3), [Some(1), Some(0), Some(1), None]);
    assert_eq!(results(&execution, 4), [Some(1), Some(0), Some(0), None]);
    assert_eq!(results(&execution, 5), [Some(1), Some(1), Some(0), None]);
    assert_eq!(results(&execution, 6), [Some(0), Some(0), Some(1), None]);
    assert_eq!(results(&execution, 7), [Some(0), Some(1), Some(1), None]);
}

#[test]
fn filters_run_in_order_before_projection_and_keep_skip_rows_untouched() {
    let program = program(
        vec![
            Input(0),
            Constant(Some(0)),
            Gt(0, 1),
            Constant(Some(12)),
            Div {
                left: 3,
                right: 0,
                error_on_zero: true,
            },
            Lt(4, 3),
        ],
        vec![2, 5],
        vec![4],
        1,
    );
    let mut execution = execution(&program, 5);
    bind(
        &mut execution,
        &[&[Some(0), Some(1), Some(3), None, Some(0)]],
        &[0, 0, 0, 0, 1],
    );
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.selection(), [1, 1, 0, 1, 1]);
    assert_eq!(execution.node_values(4)[2] as i64, 4);
    assert_eq!(
        execution.node_flags(4),
        [0, EVALUATED | VALID, EVALUATED | VALID, 0, 0]
    );
    assert_eq!(execution.node_flags(0)[4], 0);
}

#[test]
fn all_skipped_and_empty_batches_do_not_evaluate_fallible_constants() {
    let program = program(
        vec![Constant(Some(i64::MAX)), Constant(Some(1)), Add(0, 1)],
        vec![2],
        vec![2],
        0,
    );
    let mut execution = execution(&program, 3);
    bind(&mut execution, &[], &[1, 1, 1]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.selection(), [1, 1, 1]);
    assert_eq!(execution.node_flags(2), [0, 0, 0]);
    bind(&mut execution, &[], &[]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert!(execution.selection().is_empty());
    bind(&mut execution, &[], &[0]);
    assert_eq!(
        execution
            .filter_project(&program, &mut ())
            .unwrap_err()
            .kind,
        ErrorKind::Overflow
    );
}

#[test]
fn arithmetic_checks_overflow_and_preserves_error_operands() {
    let cases = [
        (i64::MAX, 1, Add(0, 1)),
        (i64::MIN, 1, Sub(0, 1)),
        (i64::MAX, 2, Mul(0, 1)),
        (
            i64::MIN,
            -1,
            Div {
                left: 0,
                right: 1,
                error_on_zero: false,
            },
        ),
    ];
    for (left, right, operation) in cases {
        let program = program(
            vec![Constant(Some(left)), Constant(Some(right)), operation],
            vec![],
            vec![2],
            0,
        );
        let mut execution = execution(&program, 1);
        bind(&mut execution, &[], &[0]);
        let error = execution.filter_project(&program, &mut ()).unwrap_err();
        assert_eq!(
            error,
            EvalError {
                kind: ErrorKind::Overflow,
                node: 2,
                row: 0,
                left: Some(left),
                right: Some(right),
                host_code: 0,
            }
        );
        assert_eq!(
            execution
                .filter_project(&program, &mut ())
                .unwrap_err()
                .kind,
            ErrorKind::InvalidBatch
        );
    }
}

#[test]
fn null_arithmetic_still_demands_the_right_operand() {
    let program = program(
        vec![
            Constant(None),
            Constant(Some(i64::MAX)),
            Constant(Some(1)),
            Add(1, 2),
            Add(0, 3),
        ],
        vec![],
        vec![4],
        0,
    );
    let mut execution = execution(&program, 1);
    bind(&mut execution, &[], &[0]);
    assert_eq!(
        execution
            .filter_project(&program, &mut ())
            .unwrap_err()
            .node,
        3
    );
}

#[test]
fn null_comparisons_skip_the_right_operand_but_null_safe_equality_demands_it() {
    let program = program(
        vec![
            Input(0),
            Constant(Some(i64::MAX)),
            Constant(Some(1)),
            Add(1, 2),
            Eq(0, 3),
            Ne(0, 3),
            Lt(0, 3),
            Le(0, 3),
            Gt(0, 3),
            Ge(0, 3),
        ],
        vec![],
        (4..10).collect(),
        1,
    );
    let mut execution = execution(&program, 2);
    bind(&mut execution, &[&[None, None]], &[0, 0]);
    execution.filter_project(&program, &mut ()).unwrap();
    for root in 4..10 {
        assert_eq!(results(&execution, root), [None, None]);
    }
    assert_eq!(execution.node_flags(3), [0, 0]);
    bind(&mut execution, &[&[None, Some(1)]], &[0, 0]);
    assert_eq!(
        execution
            .filter_project(&program, &mut ())
            .unwrap_err()
            .node,
        3
    );

    let null_safe = Program::new(
        vec![
            Node {
                operation: Constant(None),
            },
            Node {
                operation: Constant(Some(i64::MAX)),
            },
            Node {
                operation: Constant(Some(1)),
            },
            Node {
                operation: Add(1, 2),
            },
            Node {
                operation: NullSafeEq(0, 3),
            },
        ],
        vec![],
        vec![4],
        0,
    )
    .unwrap();
    let mut null_safe_execution = super::Execution::new(
        &null_safe,
        1,
        vec![0; required_words(&null_safe, 1).unwrap()],
    )
    .unwrap();
    bind(&mut null_safe_execution, &[], &[0]);
    assert_eq!(
        null_safe_execution
            .filter_project(&null_safe, &mut ())
            .unwrap_err()
            .node,
        3
    );
}

#[test]
fn division_truncates_toward_zero_and_zero_policy_is_explicit() {
    let program = program(
        vec![
            Input(0),
            Input(1),
            Div {
                left: 0,
                right: 1,
                error_on_zero: false,
            },
        ],
        vec![],
        vec![2],
        2,
    );
    let mut execution = execution(&program, 4);
    bind(
        &mut execution,
        &[
            &[Some(-7), Some(7), Some(0), None],
            &[Some(3), Some(-3), Some(0), Some(0)],
        ],
        &[0; 4],
    );
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 2), [Some(-2), Some(-2), None, None]);
}

#[test]
fn rescan_rebind_and_smaller_batches_invalidate_all_cached_values() {
    let program = program(
        vec![Input(0), Input(1), Add(0, 1), Identity(2)],
        vec![],
        vec![3],
        2,
    );
    let mut execution = execution(&program, 4);
    bind(
        &mut execution,
        &[&[Some(1), Some(2), None, Some(4)], &[Some(10); 4]],
        &[0; 4],
    );
    execution.filter_project(&program, &mut ()).unwrap();
    let retained = results(&execution, 3);
    bind(&mut execution, &[&[Some(-5)], &[Some(3)]], &[0]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 3), [Some(-2)]);
    assert_eq!(retained, [Some(11), Some(12), None, Some(14)]);
    bind(
        &mut execution,
        &[&[Some(1), Some(2), None, Some(4)], &[Some(10); 4]],
        &[0; 4],
    );
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 3), retained);
}

#[test]
fn query_owned_backing_keeps_results_alive_after_program_drop() {
    let program = program(vec![Constant(Some(-42))], vec![], vec![0], 0);
    let mut storage = [0u64; 3];
    let mut execution = Execution::new(&program, 1, storage.as_mut_slice()).unwrap();
    bind(&mut execution, &[], &[0]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 0), [Some(-42)]);
    drop(program);
    assert_eq!(execution.node_values(0)[0] as i64, -42);
    assert_eq!(execution.node_flags(0)[0], EVALUATED | VALID);
}

struct CancelAfter {
    checks: usize,
    allowed: usize,
}

impl Control for CancelAfter {
    fn check(&mut self) -> Result<(), i32> {
        self.checks += 1;
        if self.checks > self.allowed {
            Err(-1234)
        } else {
            Ok(())
        }
    }
}

#[test]
fn cancellation_is_polled_at_entry_and_bounded_during_execution() {
    let program = program(vec![Constant(Some(1))], vec![], vec![0], 0);
    let mut execution = execution(&program, 10_000);
    bind(&mut execution, &[], &[0; 10_000]);
    let mut control = CancelAfter {
        checks: 0,
        allowed: 0,
    };
    let error = execution
        .filter_project(&program, &mut control)
        .unwrap_err();
    assert_eq!(
        (error.kind, error.host_code, error.row),
        (ErrorKind::Cancelled, -1234, 0)
    );
    bind(&mut execution, &[], &[0; 10_000]);
    let mut control = CancelAfter {
        checks: 0,
        allowed: 1,
    };
    let error = execution
        .filter_project(&program, &mut control)
        .unwrap_err();
    assert_eq!((error.kind, error.host_code), (ErrorKind::Cancelled, -1234));
    assert!(error.row > 0 && error.row <= POLL_INTERVAL);
    bind(&mut execution, &[], &[1; 10_000]);
    let mut control = CancelAfter {
        checks: 0,
        allowed: 1,
    };
    assert_eq!(
        execution
            .filter_project(&program, &mut control)
            .unwrap_err()
            .kind,
        ErrorKind::Cancelled
    );
    bind(&mut execution, &[], &[0]);
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 0), [Some(1)]);
}

#[test]
fn malformed_programs_and_unbounded_storage_requests_are_rejected() {
    let make = |operations: Vec<Operation>, filters, projections, inputs| {
        Program::new(
            operations
                .into_iter()
                .map(|operation| Node { operation })
                .collect(),
            filters,
            projections,
            inputs,
        )
    };
    for invalid in [
        make(vec![], vec![], vec![], 0),
        make(vec![Input(0)], vec![], vec![], 0),
        make(vec![Identity(0)], vec![], vec![], 0),
        make(vec![Constant(None), Add(0, 2)], vec![], vec![], 0),
        make(vec![Constant(None)], vec![1], vec![], 0),
        make(vec![Constant(None)], vec![], vec![1], 0),
        make(vec![And(vec![])], vec![], vec![], 0),
        make(vec![Or(vec![])], vec![], vec![], 0),
        make(
            vec![Case {
                arms: vec![],
                otherwise: None,
            }],
            vec![],
            vec![],
            0,
        ),
        make(vec![Constant(None); MAX_NODES + 1], vec![], vec![], 0),
        make(vec![Constant(None)], vec![], vec![], MAX_NODES + 1),
    ] {
        assert!(matches!(
            invalid,
            Err(EvalError {
                kind: ErrorKind::InvalidProgram,
                ..
            })
        ));
    }
    let mut deep = vec![Constant(None)];
    for id in 0..MAX_DEPTH {
        deep.push(Identity(id));
    }
    assert!(matches!(
        make(deep, vec![], vec![], 0),
        Err(EvalError {
            kind: ErrorKind::InvalidProgram,
            ..
        })
    ));
    let program = program(vec![Constant(None)], vec![], vec![0], 0);
    assert_eq!(
        required_words(&program, usize::MAX).unwrap_err().kind,
        ErrorKind::Memory
    );
    assert!(matches!(
        Execution::new(&program, 1, vec![0; 2]),
        Err(EvalError {
            kind: ErrorKind::Memory,
            ..
        })
    ));
}

#[test]
fn execution_rejects_different_programs_and_requires_rebind_after_input_mutation() {
    let first = program(vec![Input(0)], vec![], vec![0], 1);
    let second = program(vec![Input(0)], vec![], vec![0], 1);
    let mut execution = execution(&first, 1);
    assert_eq!(
        execution.begin_batch(2, &[0, 0]).unwrap_err().kind,
        ErrorKind::InvalidBatch
    );
    assert_eq!(
        execution.begin_batch(1, &[]).unwrap_err().kind,
        ErrorKind::InvalidBatch
    );
    bind(&mut execution, &[&[Some(7)]], &[0]);
    assert_eq!(
        execution.filter_project(&second, &mut ()).unwrap_err().kind,
        ErrorKind::InvalidBatch
    );
    execution.filter_project(&first, &mut ()).unwrap();
    execution.input_values_mut()[0] = 8;
    assert_eq!(
        execution.filter_project(&first, &mut ()).unwrap_err().kind,
        ErrorKind::InvalidBatch
    );
    execution.begin_batch(1, &[0]).unwrap();
    execution.filter_project(&first, &mut ()).unwrap();
    assert_eq!(results(&execution, 0), [Some(8)]);
}

#[test]
fn one_prepared_program_supports_independent_concurrent_executions() {
    let program = program(
        vec![Input(0), Constant(Some(2)), Mul(0, 1)],
        vec![],
        vec![2],
        1,
    );
    std::thread::scope(|scope| {
        let workers: Vec<_> = (0..4)
            .map(|value| {
                let program = &program;
                scope.spawn(move || {
                    let mut execution = execution(program, 1);
                    bind(&mut execution, &[&[Some(value)]], &[0]);
                    execution.filter_project(program, &mut ()).unwrap();
                    results(&execution, 2)
                })
            })
            .collect();
        for (value, worker) in workers.into_iter().enumerate() {
            assert_eq!(worker.join().unwrap(), [Some(value as i64 * 2)]);
        }
    });
}

#[test]
fn compact_program_and_workspace_use_only_caller_owned_storage() {
    let instructions = [
        Instruction {
            opcode: Opcode::Input,
            ..Instruction::default()
        },
        Instruction {
            opcode: Opcode::Constant,
            value: 5,
            ..Instruction::default()
        },
        Instruction {
            opcode: Opcode::Add,
            argument_count: 2,
            ..Instruction::default()
        },
        Instruction {
            opcode: Opcode::Constant,
            value: 0,
            ..Instruction::default()
        },
        Instruction {
            opcode: Opcode::Gt,
            argument_start: 2,
            argument_count: 2,
            ..Instruction::default()
        },
        Instruction {
            opcode: Opcode::Case,
            argument_start: 4,
            argument_count: 3,
            ..Instruction::default()
        },
    ];
    let spec = ProgramSpec {
        instructions: &instructions,
        arguments: &[0, 1, 2, 3, 4, 2, 3],
        filters: &[4],
        projections: &[5, 2],
        inputs: 1,
    };
    let words = program_words(&spec).unwrap();
    let mut program_storage = [u64::MAX; 64];
    let mut workspace = [u64::MAX; 45];
    {
        let program = Program::from_instructions_in(spec, program_storage.as_mut_slice()).unwrap();
        assert_eq!(program.storage_words(), words);
        assert_eq!(program.node_count(), 6);
        assert_eq!(program.filters(), [4]);
        assert_eq!(program.projections(), [5, 2]);
        let mut execution = Execution::new(&program, 3, workspace.as_mut_slice()).unwrap();
        bind(&mut execution, &[&[Some(-1), Some(1), None]], &[0, 0, 0]);
        execution.filter_project(&program, &mut ()).unwrap();
        assert_eq!(execution.selection(), [0, 0, 1]);
        assert_eq!(&execution.node_values(5)[..2], [4, 6]);
        assert_eq!(
            execution.node_flags(5),
            [EVALUATED | VALID, EVALUATED | VALID, 0]
        );
    }
    assert!(program_storage[words..]
        .iter()
        .all(|&word| word == u64::MAX));
}

#[test]
fn invalid_compact_instructions_leave_caller_storage_unchanged() {
    let base = Instruction {
        opcode: Opcode::Constant,
        value: 8,
        ..Instruction::default()
    };
    let invalid_instructions = [
        Instruction {
            argument_count: 1,
            ..base
        },
        Instruction {
            opcode: Opcode::Input,
            input: 1,
            ..base
        },
        Instruction {
            opcode: Opcode::Add,
            argument_count: 2,
            ..base
        },
        Instruction {
            opcode: Opcode::Case,
            argument_count: 1,
            ..base
        },
        Instruction {
            opcode: Opcode::And,
            argument_count: 0,
            ..base
        },
        Instruction {
            opcode: Opcode::Not,
            argument_start: u32::MAX,
            argument_count: 1,
            ..base
        },
    ];
    for instruction in invalid_instructions {
        let mut backing = [91u64; 20];
        let spec = ProgramSpec {
            instructions: &[instruction],
            arguments: &[0, 0],
            filters: &[],
            projections: &[0],
            inputs: 1,
        };
        let result = Program::from_instructions_in(spec, backing.as_mut_slice());
        assert!(matches!(
            result,
            Err(EvalError {
                kind: ErrorKind::InvalidProgram,
                ..
            })
        ));
        assert_eq!(backing, [91; 20]);
    }
    let mut too_small = [77u64; 1];
    let spec = ProgramSpec {
        instructions: &[base],
        arguments: &[],
        filters: &[],
        projections: &[0],
        inputs: 0,
    };
    assert!(matches!(
        Program::from_instructions_in(spec, too_small.as_mut_slice()),
        Err(EvalError {
            kind: ErrorKind::Memory,
            ..
        })
    ));
    assert_eq!(too_small, [77]);
}

#[test]
fn batch_deepest_valid_program_preserves_word_boundary_rows() {
    let mut operations = vec![Input(0)];
    for node in 1..MAX_DEPTH {
        operations.push(Identity(node - 1));
    }
    let program = program(operations, vec![], vec![MAX_DEPTH - 1], 1);
    let mut execution = execution(&program, 256);
    let values: Vec<_> = (0..256).map(|row| Some(row as i64 - 128)).collect();
    let skip: Vec<_> = (0..256)
        .map(|row| u8::from(![0, 63, 64, 127, 128, 255].contains(&row)))
        .collect();
    bind(&mut execution, &[&values], &skip);
    execution.filter_project(&program, &mut ()).unwrap();
    for (row, &skipped) in skip.iter().enumerate() {
        assert_eq!(
            execution.node_flags(MAX_DEPTH - 1)[row],
            if skipped == 0 { EVALUATED | VALID } else { 0 }
        );
        if skipped == 0 {
            assert_eq!(
                execution.node_values(MAX_DEPTH - 1)[row] as i64,
                row as i64 - 128
            );
        }
    }
}

#[test]
fn reused_dirty_backing_defaults_inputs_to_null_and_invalidates_growing_batches() {
    let program = program(
        vec![Input(0), Constant(Some(3)), Add(0, 1)],
        vec![],
        vec![2],
        1,
    );
    let mut execution = Execution::new(
        &program,
        256,
        vec![u64::MAX; required_words(&program, 256).unwrap()],
    )
    .unwrap();
    execution.begin_batch(256, &[0; 256]).unwrap();
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(results(&execution, 2), vec![None; 256]);
    for rows in [256, 1, 8, 128, 256] {
        let column = vec![Some(rows as i64); rows];
        bind(&mut execution, &[&column], &vec![0; rows]);
        execution.filter_project(&program, &mut ()).unwrap();
        assert_eq!(results(&execution, 2), vec![Some(rows as i64 + 3); rows]);
    }
}

#[test]
fn batch_shared_fallible_node_is_demanded_by_disjoint_case_roots() {
    let program = program(
        vec![
            Input(0),
            Input(1),
            Div {
                left: 0,
                right: 1,
                error_on_zero: true,
            },
            Constant(Some(0)),
            Gt(0, 3),
            Not(4),
            Case {
                arms: vec![(4, 2)],
                otherwise: Some(3),
            },
            Case {
                arms: vec![(5, 2)],
                otherwise: Some(3),
            },
        ],
        vec![],
        vec![6, 7],
        2,
    );
    let mut execution = execution(&program, 65);
    let mut left = (0..65)
        .map(|row| Some(if row % 2 == 0 { 9 } else { -9 }))
        .collect::<Vec<_>>();
    left[64] = None;
    let mut right = vec![Some(3); 65];
    right[64] = Some(0);
    bind(&mut execution, &[&left, &right], &[0; 65]);
    execution.filter_project(&program, &mut ()).unwrap();
    for row in 0..64 {
        assert_eq!(execution.node_flags(2)[row], EVALUATED | VALID);
        assert_eq!(
            execution.node_values(2)[row] as i64,
            if row % 2 == 0 { 3 } else { -3 }
        );
        assert_eq!(
            execution.node_values(6)[row] as i64,
            if row % 2 == 0 { 3 } else { 0 }
        );
        assert_eq!(
            execution.node_values(7)[row] as i64,
            if row % 2 == 0 { 0 } else { -3 }
        );
    }
    assert_eq!(execution.node_flags(2)[64], 0);
    assert_eq!(execution.node_values(6)[64], 0);
    assert_eq!(execution.node_values(7)[64], 0);

    right[63] = Some(0);
    bind(&mut execution, &[&left, &right], &[0; 65]);
    let error = execution.filter_project(&program, &mut ()).unwrap_err();
    assert_eq!(
        (error.kind, error.node, error.row),
        (ErrorKind::DivisionByZero, 2, 63)
    );
    assert!(execution
        .node_flags(6)
        .iter()
        .all(|&flags| flags == EVALUATED | VALID));
}
