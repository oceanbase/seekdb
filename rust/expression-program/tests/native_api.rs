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

//! Unlike internal unit tests, this consumer links the normal library build.

use expression_program::{required_words, Execution, Node, Operation, Program, EVALUATED, VALID};

#[test]
fn native_caller_filters_and_projects_scalar_and_batch_inputs() {
    let program = Program::new(
        vec![
            Node {
                operation: Operation::Input(0),
            },
            Node {
                operation: Operation::Constant(Some(0)),
            },
            Node {
                operation: Operation::Gt(0, 1),
            },
            Node {
                operation: Operation::Constant(Some(7)),
            },
            Node {
                operation: Operation::Add(0, 3),
            },
            Node {
                operation: Operation::Mul(4, 4),
            },
        ],
        vec![2],
        vec![4, 5],
        1,
    )
    .unwrap();
    let mut execution =
        Execution::new(&program, 4, vec![0; required_words(&program, 4).unwrap()]).unwrap();

    execution.input_values_mut()[0] = 3;
    execution.input_validity_mut()[0] = 1;
    execution.begin_batch(1, &[0]).unwrap();
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.selection(), &[0]);
    assert_eq!(execution.node_values(4), &[10]);
    assert_eq!(execution.node_values(5), &[100]);
    let scalar_result = execution.node_values(5)[0];

    execution
        .input_values_mut()
        .copy_from_slice(&[(-2i64) as u64, 5, 10, 7]);
    execution
        .input_validity_mut()
        .copy_from_slice(&[1, 1, 0, 1]);
    execution.begin_batch(4, &[0, 0, 0, 1]).unwrap();
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.selection(), &[1, 0, 1, 1]);
    assert_eq!(execution.node_flags(4), &[0, EVALUATED | VALID, 0, 0]);
    assert_eq!(execution.node_flags(5), &[0, EVALUATED | VALID, 0, 0]);
    assert_eq!(execution.node_values(4)[1], 12);
    assert_eq!(execution.node_values(5)[1], 144);

    drop(program);
    assert_eq!(scalar_result, 100);
    assert_eq!(execution.node_values(5)[1], 144);
}
