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

use super::ParameterEffects;
use crate::bridge::ffi::{
    LiteralKind, ParameterError, ParameterOrigin, ParameterProjection, ParameterToken,
    ProjectionNames,
};
use crate::expression::{expression_literal, is_literal, parenthesized_expression};
use crate::node::{Kind, Node};
use crate::query::{name_is_wildcard, Projection};

fn display<'a>(projection: Projection<'a>) -> &'a Node {
    let mut expression = projection.expression();
    while expression.kind == Kind::Parenthesized {
        expression = parenthesized_expression(expression);
    }
    expression
}

fn string_name<'a>(projection: Projection<'a>) -> Option<&'a [u8]> {
    let display = display(projection);
    if projection.alias().is_none() && is_literal(display) {
        let literal = expression_literal(display);
        if literal.kind == LiteralKind::String {
            return Some(&literal.text[..literal.first_part_length]);
        }
    }
    None
}

pub fn projection_names<'a>(
    projection: Projection<'a>,
    expression_source: &'a [u8],
    effects: Option<&ParameterEffects>,
    null_template: bool,
) -> ProjectionNames<'a> {
    let expression = if null_template {
        b"NULL"
    } else {
        string_name(projection).unwrap_or(expression_source)
    };
    let display = display(projection);
    let alias = if display.kind == Kind::Null
        && effects
            .and_then(|effects| effects.binding(display))
            .is_none()
    {
        b"NULL"
    } else {
        expression
    };
    ProjectionNames {
        expression,
        alias,
        explicit_alias: projection
            .alias()
            .map_or(&[], |alias| alias.text.as_bytes()),
        has_alias: projection.alias().is_some(),
    }
}

pub(super) fn parameter_template<'a>(
    projection: Projection<'a>,
    original_sql: &[u8],
    occurrences: impl IntoIterator<Item = (&'a Node, &'a Node)>,
    tokens: &[ParameterToken<'_>],
    effects: &ParameterEffects,
    limit: usize,
) -> Result<Option<ParameterProjection<'a>>, ParameterError> {
    if projection.alias().is_some() {
        return Ok(None);
    }
    let expression = projection.expression();
    if expression.kind == Kind::Star
        || (expression.kind == Kind::Column && name_is_wildcard(expression))
    {
        return Ok(None);
    }
    let string_literal = string_name(projection).is_some();
    let original = original_sql
        .get(expression.start..expression.end)
        .ok_or(ParameterError::Unsupported)?;
    let mut result = ParameterProjection {
        node: projection.node(),
        name: Vec::new(),
        questions: Vec::new(),
        parameters: Vec::new(),
        negative_parameters: Vec::new(),
        escaped_string: false,
        null_literal: false,
    };
    let mut position = expression.start;
    for (literal, parameter_expression) in occurrences {
        let Some(binding) = effects.binding(parameter_expression) else {
            continue;
        };
        if literal.start < expression.start || literal.end > expression.end {
            continue;
        }
        if result.name.len() >= limit {
            break;
        }
        let token = &tokens[binding.raw_index];
        if !string_literal && token.start > position {
            let start = position
                .checked_sub(expression.start)
                .ok_or(ParameterError::Unsupported)?;
            let count = (token.start - position).min(limit - result.name.len());
            result.name.extend_from_slice(
                original
                    .get(start..start + count)
                    .ok_or(ParameterError::Unsupported)?,
            );
        }
        position = token.end;
        result.questions.push(result.name.len());
        result.parameters.push(binding.raw_index);
        if result.name.len() < limit {
            result.name.push(b'?');
        }
        if binding.origin == ParameterOrigin::UnaryNegation {
            result.negative_parameters.push(binding.raw_index);
        }
    }
    if !string_literal {
        let offset = position
            .checked_sub(expression.start)
            .ok_or(ParameterError::Unsupported)?;
        let suffix = original.get(offset..).ok_or(ParameterError::Unsupported)?;
        result
            .name
            .extend_from_slice(&suffix[..suffix.len().min(limit - result.name.len())]);
    }
    result.escaped_string = string_literal && effects.binding(display(projection)).is_some();
    if result.parameters.len() == 1 && result.name == b"?" {
        result.null_literal = tokens[result.parameters[0]].kind == LiteralKind::Null;
    }
    Ok((!result.parameters.is_empty()).then_some(result))
}
