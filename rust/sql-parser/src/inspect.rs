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

use crate::bridge::{
    self,
    ffi::{ColumnReference, ExpressionInfo, ParseMode},
};
use crate::node::{Kind, Node};
use crate::{ParseErrorKind, SqlMode, StackBounds};

pub(crate) fn inspect_expression(sql: &[u8], mode: SqlMode, bounds: StackBounds) -> ExpressionInfo {
    let parsed = match crate::parser::parse(sql, mode, ParseMode::Expression, bounds) {
        Ok(parsed) => parsed,
        Err(error) => return ExpressionInfo {
            root_kind: Kind::Missing,
            cast_array: false,
            function_name: String::new(),
            qualified_function: false,
            columns: Vec::new(),
            error: error.message,
            diagnostic: error.diagnostic,
            error_kind: error.kind,
        },
    };
    let mut root = parsed.root();
    while root.kind == Kind::Parenthesized {
        root = &root.children[0];
    }
    let function_name = match root.kind {
        Kind::Function | Kind::Cast | Kind::StringConcat => crate::expression::call_name(root).into(),
        _ => String::new(),
    };
    let mut columns = Vec::new();
    collect_columns(root, &mut columns);
    ExpressionInfo {
        root_kind: root.kind,
        cast_array: root.kind == Kind::Cast && crate::expression::cast_array(root),
        function_name,
        qualified_function: !crate::expression::call_qualifiers(root).is_empty(),
        columns,
        error: String::new(),
        diagnostic: Default::default(),
        error_kind: ParseErrorKind::Syntax,
    }
}

fn collect_columns(node: &Node, columns: &mut Vec<ColumnReference>) {
    let mut pending = vec![node];
    while let Some(node) = pending.pop() {
        if node.kind == Kind::Column {
            columns.push(ColumnReference {
                parts: node
                    .children
                    .iter()
                    .filter(|child| child.kind == Kind::Ident)
                    .map(|child| child.text.as_str().to_owned())
                    .collect(),
                wildcard: node.children.iter().any(|child| child.kind == Kind::Star),
            });
        }
        pending.extend(node.children.iter().rev());
    }
}

pub(crate) fn embedded_sql_references(statement: &Node) -> Vec<bridge::ffi::SqlReference<'_>> {
    fn visit<'a>(node: &'a Node, bound: &mut Vec<&'a str>, references: &mut Vec<bridge::ffi::SqlReference<'a>>) {
        use bridge::ffi::{SqlReference, SqlReferenceKind as Role};
        if node.kind == Kind::Lambda {
            let outer = bound.len();
            bound.extend(crate::expression::lambda_parameters(node).iter().map(|name| name.text.as_str()));
            visit(crate::expression::lambda_body(node), bound, references);
            bound.truncate(outer);
            return;
        }
        if node.kind == Kind::InsertColumns {
            return;
        }
        if node.kind == Kind::Assignment {
            visit(crate::query::assignment_value(node), bound, references);
            return;
        }
        if matches!(node.kind, Kind::IntoVariables | Kind::TrailingIntoVariables) {
            references.push(SqlReference {
                kind: Role::Into,
                node,
            });
            return;
        }
        if node.kind == Kind::Diagnostics {
            let arguments = std::iter::once(crate::command::diagnostics_condition(node))
                .chain(crate::command::diagnostics_items(node).iter().map(crate::command::diagnostics_target));
            for argument in arguments {
                if argument.kind == Kind::Ident {
                    references.push(SqlReference {
                        kind: Role::Column,
                        node: argument,
                    });
                } else {
                    visit(argument, bound, references);
                }
            }
            return;
        }
        let column_argument = node.kind == Kind::DefaultValue
            || node.kind == Kind::Function
                && crate::expression::call_qualifiers(node).is_empty()
                && crate::expression::call_name(node).eq_ignore_ascii_case("values");
        if !column_argument {
            for child in &node.children {
                visit(child, bound, references);
            }
        }
        if !bound.is_empty() {
            let column = if node.kind == Kind::Projection
                && crate::query::projection_alias_form(node) == crate::AliasForm::Absent
            {
                crate::query::projection_expression(node)
            } else {
                node
            };
            if column.kind == Kind::Column && !crate::query::name_is_wildcard(column) {
                let names = crate::query::name_parts(column);
                if names.len() == 1 && bound.contains(&names[0].text.as_str()) {
                    return;
                }
            }
        }
        let kind = match node.kind {
            Kind::Parameter => Role::Parameter,
            Kind::Column => Role::Column,
            Kind::Relation => Role::Relation,
            Kind::Function => Role::Function,
            Kind::Projection => Role::Projection,
            _ => return,
        };
        references.push(SqlReference { kind, node });
    }
    let mut references = Vec::new();
    visit(statement, &mut Vec::new(), &mut references);
    references
}

pub(crate) fn query_has_mixed_string_array(node: &Node) -> bool {
    fn literal_kind(mut node: &Node) -> Option<crate::bridge::ffi::LiteralKind> {
        use crate::bridge::ffi::{LiteralKind, UnaryOperator};
        while node.kind == Kind::Parenthesized {
            node = crate::expression::parenthesized_expression(node);
        }
        if node.kind == Kind::Unary
            && matches!(
                crate::expression::unary_operator(node),
                UnaryOperator::Plus | UnaryOperator::Minus
            )
        {
            return literal_kind(crate::expression::unary_operand(node)).filter(|kind| {
                matches!(
                    *kind,
                    LiteralKind::Integer
                        | LiteralKind::Unsigned
                        | LiteralKind::Decimal
                        | LiteralKind::Float
                )
            });
        }
        if node.kind == Kind::CharsetLiteral {
            Some(LiteralKind::String)
        } else if crate::expression::is_literal(node) {
            Some(crate::expression::expression_literal(node).kind)
        } else {
            None
        }
    }
    if node.kind == Kind::ArrayLiteral {
        use crate::bridge::ffi::LiteralKind;
        let mut string = false;
        let mut scalar = false;
        for value in crate::expression::call_arguments(node) {
            match literal_kind(value) {
                Some(LiteralKind::String | LiteralKind::NationalString) => string = true,
                Some(LiteralKind::Null | LiteralKind::Placeholder) | None => {}
                Some(_) => scalar = true,
            }
        }
        if string && scalar {
            return true;
        }
    }
    node.children.iter().any(query_has_mixed_string_array)
}

pub fn syntax_literals(syntax: &Node) -> Vec<bridge::ffi::LiteralReference<'_>> {
    fn collect<'a>(node: &'a Node, literals: &mut Vec<bridge::ffi::LiteralReference<'a>>) {
        if crate::expression::is_literal(node) {
            literals.push(bridge::ffi::LiteralReference { node });
        } else {
            for child in &node.children {
                collect(child, literals);
            }
        }
    }
    let mut literals = Vec::new();
    collect(syntax, &mut literals);
    literals
}
