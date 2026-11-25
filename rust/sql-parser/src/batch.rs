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
    BatchCompression, ParameterError, RestoreParameter, RestoredParameter, RestoredSql,
};
use crate::prefix::{Token, Tokens};

struct Row<'a> {
    end: usize,
    parameters: usize,
    shape: Vec<&'a [u8]>,
}

fn row<'a>(tokens: &mut Tokens<'a>, first: Token<'a>, constructor: bool) -> Option<Row<'a>> {
    let mut token = first;
    if constructor {
        if !token.is(b"row") {
            return None;
        }
        token = tokens.next().ok()??;
    }
    if !token.is(b"(") {
        return None;
    }
    let mut result = Row {
        end: token.end,
        parameters: 0,
        shape: vec![token.text],
    };
    let mut depth = 1;
    let mut needs_value = true;
    while depth > 0 {
        let token = tokens.next().ok()??;
        result.shape.push(token.text);
        if token.is(b"(") {
            if constructor && !needs_value {
                return None;
            }
            depth += 1;
        } else if token.is(b")") {
            if constructor && needs_value {
                return None;
            }
            depth -= 1;
        } else if token.is(b"?") {
            if constructor && !needs_value {
                return None;
            }
            result.parameters += 1;
            needs_value = false;
        } else if token.is(b",") && constructor {
            if depth != 1 || needs_value {
                return None;
            }
            needs_value = true;
        } else if constructor || token.is(b";") || token.text.starts_with(b"/*!") {
            return None;
        }
        result.end = token.end;
    }
    Some(result)
}

fn ineligible() -> BatchCompression {
    BatchCompression {
        eligible: false,
        recognized_rows: false,
        sql: Vec::new(),
        rows: 0,
        row_parameters: 0,
        suffix_parameters: 0,
        removed_length: 0,
        source_end: 0,
        output_end: 0,
    }
}

fn compress(
    sql: &[u8],
    values_offset: usize,
    constructor: bool,
    recognized_rows: &mut bool,
) -> Option<BatchCompression> {
    if values_offset >= sql.len() {
        return None;
    }
    let mut tokens = Tokens::at(sql, values_offset);
    let keyword = tokens.next().ok()??;
    if !keyword.is(b"values") && (constructor || !keyword.is(b"value")) {
        return None;
    }
    let token = tokens.next().ok()??;
    let first = row(&mut tokens, token, constructor)?;
    *recognized_rows = true;
    let mut last_end = first.end;
    let mut rows = 1;
    let suffix = loop {
        let next = tokens.next().ok()?;
        if next.is_some_and(|token| token.is(b",")) {
            let next = tokens.next().ok()??;
            let current = row(&mut tokens, next, constructor)?;
            if current.parameters != first.parameters || current.shape != first.shape {
                return None;
            }
            last_end = current.end;
            rows += 1;
        } else {
            break next;
        }
    };
    let mut suffix_parameters = 0;
    let mut suffix_end = sql.len();
    if !constructor {
        suffix_end = last_end;
        match suffix {
            Some(token) if token.is(b"on") => {
                for word in [b"duplicate".as_slice(), b"key", b"update"] {
                    if !tokens.next().ok()??.is(word) {
                        return None;
                    }
                }
                let mut has_assignment = false;
                let mut finished = false;
                while let Some(token) = tokens.next().ok()? {
                    if token.is(b";") {
                        finished = true;
                    } else if finished || token.text.starts_with(b"/*!") {
                        return None;
                    } else {
                        suffix_end = token.end;
                        has_assignment = true;
                        if token.is(b"?") {
                            suffix_parameters += 1;
                        }
                    }
                }
                if !has_assignment {
                    return None;
                }
            }
            Some(token) if token.is(b";") => {
                if tokens.next().ok()?.is_some() {
                    return None;
                }
            }
            Some(_) => return None,
            None => {}
        }
    }
    if rows < 2 || first.parameters == 0 {
        return None;
    }
    let mut output = Vec::with_capacity(sql.len() - (last_end - first.end));
    output.extend_from_slice(&sql[..first.end]);
    output.extend_from_slice(&sql[last_end..suffix_end]);
    Some(BatchCompression {
        eligible: true,
        recognized_rows: true,
        sql: output,
        rows,
        row_parameters: first.parameters,
        suffix_parameters,
        removed_length: last_end - first.end,
        source_end: last_end,
        output_end: first.end,
    })
}

pub fn compress_insert(sql: &[u8], values_offset: usize) -> BatchCompression {
    let mut recognized_rows = false;
    let mut result =
        compress(sql, values_offset, false, &mut recognized_rows).unwrap_or_else(ineligible);
    result.recognized_rows = recognized_rows;
    result
}

pub fn compress_values(sql: &[u8], values_offset: usize) -> BatchCompression {
    let mut recognized_rows = false;
    let mut result =
        compress(sql, values_offset, true, &mut recognized_rows).unwrap_or_else(ineligible);
    result.recognized_rows = recognized_rows;
    result
}

pub fn restore_parameter_sql(
    sql: &[u8],
    parameters: &[RestoreParameter<'_>],
    terminate: bool,
) -> RestoredSql {
    let mut result = RestoredSql {
        sql: Vec::with_capacity(sql.len()),
        parameters: Vec::with_capacity(parameters.len()),
        error: ParameterError::None,
    };
    let mut copied = 0;
    for parameter in parameters {
        let Some(offset) = parameter.sql_offset.checked_sub(parameter.removed_before) else {
            result.error = ParameterError::InvalidArgument;
            return result;
        };
        if offset < copied || sql.get(offset) != Some(&b'?') {
            result.error = ParameterError::InvalidArgument;
            return result;
        }
        result.sql.extend_from_slice(&sql[copied..offset]);
        result.parameters.push(RestoredParameter {
            sql_offset: offset,
            source_offset: result.sql.len(),
        });
        result.sql.extend_from_slice(parameter.source);
        copied = offset + 1;
    }
    result.sql.extend_from_slice(&sql[copied..]);
    if terminate {
        result.sql.push(b';');
    }
    result
}
