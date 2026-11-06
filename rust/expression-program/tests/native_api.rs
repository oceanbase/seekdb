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

use expression_program::{
    compile_image_in, image_words, required_words, ErrorKind, Execution, Instruction, Node, Opcode,
    Operation, Program, ProgramImage, ProgramSpec, EVALUATED, VALID,
};

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

#[test]
fn native_image_restores_owned_programs_with_independent_identity_and_results() {
    let (program, second) = {
        let instructions = [
            Instruction::default(),
            Instruction {
                input: 1,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Constant,
                value: 7,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Add,
                argument_count: 2,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Constant,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Gt,
                argument_start: 2,
                argument_count: 2,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Div,
                argument_start: 4,
                argument_count: 2,
                error_on_zero: true,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Case,
                argument_start: 6,
                argument_count: 3,
                ..Instruction::default()
            },
            Instruction {
                opcode: Opcode::Mul,
                argument_start: 9,
                argument_count: 2,
                ..Instruction::default()
            },
        ];
        let spec = ProgramSpec {
            instructions: &instructions,
            arguments: &[0, 2, 1, 4, 3, 1, 5, 6, 4, 7, 7],
            filters: &[],
            projections: &[7, 8],
            inputs: 2,
        };
        let mut storage = [0; 128];
        let image = compile_image_in(spec, &mut storage[..image_words(&spec).unwrap()]).unwrap();
        let copied_words = image.words().to_vec();
        let copied = ProgramImage::parse(copied_words).unwrap();
        let program = Program::from_image_in(&copied, vec![0; copied.program_words()]).unwrap();
        let second = Program::from_image_in(&copied, vec![0; copied.program_words()]).unwrap();
        assert_eq!(
            program.storage_words(),
            expression_program::program_words(&spec).unwrap()
        );
        (program, second)
    };
    let mut execution =
        Execution::new(&program, 1, vec![0; required_words(&program, 1).unwrap()]).unwrap();
    assert!(execution.node_values(7).is_empty());
    execution.input_values_mut().copy_from_slice(&[5, 2]);
    execution.input_validity_mut().fill(1);
    execution.begin_batch(1, &[0]).unwrap();
    assert!(execution.node_flags(7).is_empty());
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.node_values(7), &[6]);
    assert_eq!(execution.node_values(8), &[36]);
    let copied_result = execution.node_values(8)[0];

    let error = execution.filter_project(&second, &mut ()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::InvalidBatch);
    assert!(execution.node_values(8).is_empty());
    assert!(execution.node_flags(8).is_empty());
    assert!(execution.selection().is_empty());
    assert_eq!(
        execution
            .filter_project(&program, &mut ())
            .unwrap_err()
            .kind,
        ErrorKind::InvalidBatch
    );

    execution.input_values_mut().copy_from_slice(&[5, 0]);
    execution.begin_batch(1, &[0]).unwrap();
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.node_values(8), &[0]);
    assert_eq!(execution.node_flags(6), &[0]);
    execution
        .input_values_mut()
        .copy_from_slice(&[i64::MAX as u64, 1]);
    assert!(execution.node_values(8).is_empty());
    execution.begin_batch(1, &[0]).unwrap();
    assert_eq!(
        execution
            .filter_project(&program, &mut ())
            .unwrap_err()
            .kind,
        ErrorKind::Overflow
    );
    assert!(execution.node_values(8).is_empty());
    assert!(execution.node_flags(8).is_empty());
    assert!(execution.selection().is_empty());

    execution.input_values_mut().copy_from_slice(&[9, 2]);
    execution.begin_batch(1, &[0]).unwrap();
    execution.filter_project(&program, &mut ()).unwrap();
    assert_eq!(execution.node_values(8), &[64]);
    execution.begin_batch(2, &[0, 0]).unwrap_err();
    assert!(execution.node_values(8).is_empty());
    assert_eq!(copied_result, 36);
    drop(program);
    drop(second);
    assert_eq!(copied_result, 36);
}
