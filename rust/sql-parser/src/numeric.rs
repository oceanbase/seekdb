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

use crate::bridge::ffi::{LiteralKind, UnaryOperator};
use crate::expression::{is_literal, parenthesized_expression, unary_operand, unary_operator};
use crate::node::{Kind, Node};

pub(crate) struct UnaryPath<'a> {
    operand: &'a Node,
}

pub(crate) struct UnaryStep<'a> {
    pub node: &'a Node,
    pub operator: Option<UnaryOperator>,
    pub operand: &'a Node,
}

impl<'a> UnaryPath<'a> {
    pub fn new(node: &'a Node) -> Self {
        Self { operand: node }
    }

    pub fn operand(&self) -> &'a Node {
        self.operand
    }
}

impl<'a> Iterator for UnaryPath<'a> {
    type Item = UnaryStep<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        let node = self.operand;
        let (operator, operand) = match node.kind {
            Kind::Parenthesized => (None, parenthesized_expression(node)),
            Kind::Unary if matches!(unary_operator(node), UnaryOperator::Plus | UnaryOperator::Minus) => {
                (Some(unary_operator(node)), unary_operand(node))
            }
            _ => return None,
        };
        self.operand = operand;
        Some(UnaryStep { node, operator, operand })
    }
}

pub(crate) enum NegativeOperand<'a> {
    DirectLiteral(&'a Node),
    ParenthesizedUnsigned(&'a Node),
    Other,
}

pub(crate) fn negative_operand(mut operand: &Node) -> NegativeOperand<'_> {
    if is_literal(operand) {
        return NegativeOperand::DirectLiteral(operand);
    }
    while operand.kind == Kind::Parenthesized {
        operand = parenthesized_expression(operand);
    }
    if operand.kind == Kind::Uint {
        NegativeOperand::ParenthesizedUnsigned(operand)
    } else {
        NegativeOperand::Other
    }
}

pub(crate) fn negative_unsigned_kind(integer: u64) -> LiteralKind {
    if integer == 1 << 63 { LiteralKind::Integer } else { LiteralKind::Decimal }
}
