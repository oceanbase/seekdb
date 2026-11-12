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

use crate::bridge::ffi::{
    BinaryOperator, DefaultExpression, DefaultForm, ExpressionLiteral, LiteralKind,
    MembershipSource, UnaryOperator,
};
use crate::expression::{
    between_lower, between_upper, binary_left, binary_operator, binary_right, call_arguments,
    call_qualifiers, case_arms, case_condition, case_else, case_has_else, case_has_operand,
    case_operand, case_result, cast_argument, expression_literal, integer_literal_value,
    is_literal, like_escape, like_has_escape, membership_expressions, membership_source,
    parenthesized_expression, predicate_subject, unary_operand, unary_operator,
};
use crate::node::{Kind, Node};
use crate::numeric::{negative_operand, negative_unsigned_kind, NegativeOperand};

fn unparenthesized(mut node: &Node) -> &Node {
    while node.kind() == Kind::Parenthesized {
        node = parenthesized_expression(node);
    }
    node
}

pub fn default_expression(node: &Node) -> DefaultExpression<'_> {
    let column_default = node.kind() == Kind::ColumnDefault;
    let expression = if column_default {
        crate::ddl::column_attribute_expression(node)
    } else {
        node
    };
    let literal_timestamp = column_default && expression.kind() != Kind::Parenthesized;
    let mut node = unparenthesized(expression);
    let form = if node.kind() == Kind::HiddenDouble {
        DefaultForm::Hidden
    } else if is_literal(node) && node.kind() != Kind::CharsetName {
        DefaultForm::Literal
    } else {
        match node.kind() {
            Kind::CurrentTimestamp => DefaultForm::Timestamp,
            Kind::Unary if unary_operator(node) == UnaryOperator::Plus => {
                node = unary_operand(node);
                DefaultForm::Positive
            }
            Kind::Unary if unary_operator(node) == UnaryOperator::Minus => {
                node = unary_operand(node);
                match negative_operand(node) {
                    NegativeOperand::DirectLiteral(literal) if literal.kind() == Kind::Uint => {
                        node = literal;
                        if negative_unsigned_kind(expression_literal(literal).integer)
                            == LiteralKind::Integer
                        {
                            DefaultForm::MinimumInteger
                        } else {
                            DefaultForm::UnsignedDecimal
                        }
                    }
                    NegativeOperand::ParenthesizedUnsigned(literal) => {
                        node = literal;
                        DefaultForm::UnsignedDecimal
                    }
                    _ if unparenthesized(node).kind() == Kind::Number => DefaultForm::NegateDecimal,
                    _ => DefaultForm::NegateValue,
                }
            }
            Kind::Ident
            | Kind::CurrentDate
            | Kind::UtcDate
            | Kind::Unary
            | Kind::Between
            | Kind::Case
            | Kind::ExprList
            | Kind::If
            | Kind::IsNull
            | Kind::StringConcat
            | Kind::Sum
            | Kind::Min
            | Kind::Max
            | Kind::Avg
            | Kind::CurrentTime
            | Kind::UtcTimestamp
            | Kind::UtcTime
            | Kind::Sysdate
            | Kind::ArrayLiteral
            | Kind::MapLiteral => DefaultForm::Expression,
            Kind::Column => DefaultForm::ColumnReference,
            Kind::Binary => match binary_operator(node) {
                BinaryOperator::IntegerDivide
                | BinaryOperator::NotLike
                | BinaryOperator::LogicalXor
                | BinaryOperator::Regexp
                | BinaryOperator::NotRegexp => DefaultForm::IllegalType,
                BinaryOperator::Concat => DefaultForm::Concatenation,
                _ => DefaultForm::Expression,
            },
            Kind::In if membership_source(node) == MembershipSource::Query => {
                DefaultForm::QueryMembership
            }
            Kind::In => DefaultForm::Expression,
            Kind::Function if !call_qualifiers(node).is_empty() => DefaultForm::IllegalType,
            Kind::Function => DefaultForm::Expression,
            Kind::Cast => DefaultForm::Cast,
            Kind::Point if call_arguments(node).len() != 2 => DefaultForm::InvalidPoint,
            Kind::Point => DefaultForm::Expression,
            _ => DefaultForm::IllegalType,
        }
    };
    let timestamp_precision = if form == DefaultForm::Timestamp {
        call_arguments(node)
            .first()
            .map_or(0, integer_literal_value)
    } else {
        0
    };
    DefaultExpression {
        node,
        form,
        literal_timestamp,
        timestamp_precision,
    }
}

pub fn default_literal(node: &Node) -> ExpressionLiteral<'_> {
    let mut literal = expression_literal(node);
    if node.kind() == Kind::CharsetLiteral {
        literal.kind = LiteralKind::String;
    }
    literal
}

pub struct DefaultArguments<'a> {
    node: &'a Node,
    index: usize,
    current: Option<&'a Node>,
}

pub fn default_arguments(node: &Node) -> Box<DefaultArguments<'_>> {
    Box::new(DefaultArguments {
        node,
        index: 0,
        current: None,
    })
}

impl<'a> DefaultArguments<'a> {
    pub fn advance(&mut self) -> bool {
        self.current = self.next_argument();
        self.index += 1;
        self.current.is_some()
    }

    pub fn argument(&self) -> &'a Node {
        self.current.unwrap()
    }

    fn next_argument(&self) -> Option<&'a Node> {
        let node = self.node;
        let index = self.index;
        match node.kind() {
            Kind::Unary => (index == 0).then(|| unary_operand(node)),
            Kind::Binary => match index {
                0 => Some(binary_left(node)),
                1 => Some(binary_right(node)),
                2 if like_has_escape(node) => Some(like_escape(node)),
                _ => None,
            },
            Kind::In => {
                if index == 0 {
                    Some(predicate_subject(node))
                } else if membership_source(node) != MembershipSource::Query {
                    membership_expressions(node).get(index - 1)
                } else {
                    None
                }
            }
            Kind::Between => match index {
                0 => Some(predicate_subject(node)),
                1 => Some(between_lower(node)),
                2 => Some(between_upper(node)),
                _ => None,
            },
            Kind::Case => {
                if case_has_operand(node) && index == 0 {
                    return Some(case_operand(node));
                }
                let index = index - usize::from(case_has_operand(node));
                let arms = case_arms(node);
                if let Some(arm) = arms.get(index / 2) {
                    Some(if index.is_multiple_of(2) {
                        case_condition(arm)
                    } else {
                        case_result(arm)
                    })
                } else if index == arms.len() * 2 && case_has_else(node) {
                    Some(case_else(node))
                } else {
                    None
                }
            }
            Kind::ExprList => crate::query::row_elements(node).get(index),
            Kind::Cast => (index == 0).then(|| cast_argument(node)),
            Kind::Function
            | Kind::Point
            | Kind::If
            | Kind::IsNull
            | Kind::StringConcat
            | Kind::Sum
            | Kind::Min
            | Kind::Max
            | Kind::Avg
            | Kind::CurrentTime
            | Kind::UtcTimestamp
            | Kind::UtcTime
            | Kind::Sysdate
            | Kind::ArrayLiteral
            | Kind::MapLiteral => call_arguments(node).get(index),
            _ => None,
        }
    }
}
