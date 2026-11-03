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

use std::collections::HashMap;

use crate::bridge::ffi::{
    AliasForm, EmbeddedSql, SqlReference, SqlReferenceKind as Role, SqlSymbol, SqlSymbolBinding,
};
use crate::inspect::{embedded_sql_references, syntax_literals};
use crate::node::{Kind, Node};
use crate::query::{name_is_wildcard, name_parts, projection_alias_form, projection_expression};

pub fn embedded_sql_symbols<'a>(statement: &'a Node, sql: &[u8]) -> Vec<SqlSymbol<'a>> {
    let mut symbols = Vec::new();
    for reference in embedded_sql_references(statement) {
        let node = reference.node;
        if reference.kind == Role::Column && !name_is_wildcard(node) {
            let names = name_parts(node);
            if names.len() == 1 {
                symbols.push(SqlSymbol {
                    node,
                    name: names[0].text.as_bytes().to_vec(),
                });
            }
        } else if reference.kind == Role::Projection
            && projection_alias_form(node) == AliasForm::Absent
        {
            let expression = projection_expression(node);
            if expression.kind == Kind::Column && !name_is_wildcard(expression) {
                symbols.push(SqlSymbol {
                    node,
                    name: sql[node.start..node.end].to_vec(),
                });
            }
        }
    }
    symbols
}

enum Replacement<'a> {
    Parameter(&'a Node, i64),
    TriggerColumn(&'a Node),
    Alias(&'a Node),
    Into,
}

pub fn rewrite_embedded_sql<'a>(
    statement: &'a Node,
    sql: &[u8],
    symbols: &[SqlSymbolBinding<'_>],
    origin: &'a Node,
) -> EmbeddedSql<'a> {
    fn placeholders(node: &Node) -> Vec<&Node> {
        let mut nodes: Vec<_> = syntax_literals(node).into_iter()
            .map(|literal| literal.node)
            .filter(|node| node.kind == Kind::Parameter)
            .collect();
        nodes.sort_by_key(|node| node.start);
        nodes
    }
    let parsed_parameters = placeholders(statement);
    let original_parameters = placeholders(origin);
    if parsed_parameters.len() != original_parameters.len() {
        return EmbeddedSql { parameters_match: false, sql: Vec::new(), bindings: Vec::new() };
    }
    let original_parameters: HashMap<*const Node, &Node> = parsed_parameters.into_iter()
        .zip(original_parameters)
        .map(|(parsed, original)| (parsed as *const Node, original))
        .collect();
    let parameters: HashMap<*const Node, i64> = symbols
        .iter()
        .map(|binding| (binding.node as *const Node, binding.parameter))
        .collect();
    let mut replacements = Vec::new();
    for reference in embedded_sql_references(statement) {
        let node = reference.node;
        match reference.kind {
            Role::Parameter => {
                let original = original_parameters[&(node as *const Node)];
                replacements.push((node.start, node.end,
                    Replacement::Parameter(original, crate::expression::placeholder_index(original) as i64)));
            }
            Role::Into => replacements.push((node.start, node.end, Replacement::Into)),
            Role::Column if !name_is_wildcard(node) => {
                let names = name_parts(node);
                if names.len() == 2
                    && (names[0].text.as_bytes().eq_ignore_ascii_case(b"NEW")
                        || names[0].text.as_bytes().eq_ignore_ascii_case(b"OLD"))
                {
                    replacements.push((node.start, node.end, Replacement::TriggerColumn(node)));
                } else if let Some(&index) = parameters.get(&(node as *const Node)) {
                    replacements.push((node.start, node.end, Replacement::Parameter(node, index)));
                }
            }
            Role::Projection if projection_alias_form(node) == AliasForm::Absent => {
                let expression = projection_expression(node);
                let alias = match expression.kind {
                    Kind::String | Kind::Star => false,
                    Kind::Column => parameters.contains_key(&(node as *const Node)),
                    _ => true,
                };
                if alias {
                    replacements.push((node.end, node.end, Replacement::Alias(node)));
                }
            }
            _ => {}
        }
    }
    replacements.sort_by_key(|replacement| replacement.0);
    let mut output = Vec::with_capacity(sql.len());
    let mut bindings = Vec::new();
    let mut offset = 0;
    for (start, end, replacement) in replacements {
        output.extend_from_slice(&sql[offset..start]);
        match replacement {
            Replacement::Parameter(node, index) => {
                output.push(b':');
                output.extend_from_slice(index.to_string().as_bytes());
                bindings.push(SqlReference {
                    kind: if node.kind == Kind::Parameter { Role::Parameter } else { Role::Column },
                    node,
                });
            }
            Replacement::TriggerColumn(node) => {
                let names = name_parts(node);
                output.push(b':');
                output.extend_from_slice(names[0].text.as_bytes());
                output.push(b'.');
                output.extend_from_slice(names[1].text.as_bytes());
                bindings.push(SqlReference {
                    kind: Role::Column,
                    node,
                });
            }
            Replacement::Alias(node) => {
                output.extend_from_slice(b" AS `");
                for &byte in &sql[node.start..node.end] {
                    output.push(byte);
                    if byte == b'`' {
                        output.push(b'`');
                    }
                }
                output.extend_from_slice(b"` ");
            }
            Replacement::Into => {}
        }
        offset = end;
    }
    output.extend_from_slice(&sql[offset..]);
    EmbeddedSql {
        parameters_match: true,
        sql: output,
        bindings,
    }
}
