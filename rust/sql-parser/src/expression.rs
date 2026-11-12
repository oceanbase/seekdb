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
    AggregateQuantifier, BinaryNotation, BinaryOperator, ComparisonQuantifier, ExpressionLiteral,
    FrameBound, FrameUnit, FulltextMode, GeometryType, JsonColumnKind, JsonOptions, JsonResponse,
    JsonScalars, JsonWrapper, LiteralKind, LogicalOperand, MembershipSource, NthOrigin, NullTreatment, OdbcAction, TypeName,
    TypeSyntax, UnaryOperator, VariableScope, VectorMetric,
};
use crate::node::{Kind, Node};
use crate::query::{child, list};

#[derive(Clone, Copy)]
pub struct Binary<'a>(&'a Node);

#[derive(Clone, Copy)]
pub struct Unary<'a>(&'a Node);

impl Node {
    pub(crate) fn odbc_escape(action: &str, operand: Self, start: usize, end: usize) -> Result<Self, &'static str> {
        let action = if action.eq_ignore_ascii_case("d") {
            OdbcAction::Date
        } else if action.eq_ignore_ascii_case("t") {
            OdbcAction::Time
        } else if action.eq_ignore_ascii_case("ts") {
            OdbcAction::Timestamp
        } else {
            OdbcAction::Expression
        };
        Self::new(Kind::OdbcEscape, vec![operand]).value(action.repr as u64).expression(start, end)
    }

    pub fn as_binary(&self) -> Option<Binary<'_>> {
        (self.kind == Kind::Binary).then_some(Binary(self))
    }

    pub fn as_unary(&self) -> Option<Unary<'_>> {
        (self.kind == Kind::Unary).then_some(Unary(self))
    }

    pub fn as_literal(&self) -> Option<ExpressionLiteral<'_>> {
        is_literal(self).then(|| expression_literal(self))
    }
}

impl<'a> Binary<'a> {
    pub fn operator(self) -> BinaryOperator { binary_operator(self.0) }
    pub fn left(self) -> &'a Node { binary_left(self.0) }
    pub fn right(self) -> &'a Node { binary_right(self.0) }
}

impl<'a> Unary<'a> {
    pub fn operator(self) -> UnaryOperator { unary_operator(self.0) }
    pub fn operand(self) -> &'a Node { unary_operand(self.0) }
}

pub(crate) fn odbc_operand(expression: &Node) -> &Node {
    let mut operand = &expression.children[0];
    while operand.kind == Kind::Parenthesized {
        operand = parenthesized_expression(operand);
    }
    operand
}

pub(crate) fn odbc_action(expression: &Node) -> OdbcAction {
    let operand = odbc_operand(expression);
    if !matches!(operand.kind, Kind::String | Kind::NationalString | Kind::Parameter | Kind::CharsetLiteral) {
        return OdbcAction::Expression;
    }
    match expression.value {
        0 => OdbcAction::Expression,
        1 => OdbcAction::Date,
        2 => OdbcAction::Time,
        3 => OdbcAction::Timestamp,
        _ => unreachable!(),
    }
}

pub(crate) fn is_literal(expression: &Node) -> bool {
    matches!(
        expression.kind,
        Kind::Int
            | Kind::SampleInt
            | Kind::SampleDecimal
            | Kind::Uint
            | Kind::Number
            | Kind::Double
            | Kind::String
            | Kind::NationalString
            | Kind::HexString
            | Kind::BitString
            | Kind::Bool
            | Kind::Null
            | Kind::DefaultNull
            | Kind::DateLiteral
            | Kind::TimeLiteral
            | Kind::TimestampLiteral
            | Kind::Parameter
            | Kind::CharsetLiteral
            | Kind::HiddenInt
            | Kind::HiddenDouble
            | Kind::HiddenString
            | Kind::HiddenNull
            | Kind::DefaultInt
            | Kind::DateUnit
            | Kind::CharsetName
    )
}

pub(crate) fn expression_literal(expression: &Node) -> ExpressionLiteral<'_> {
    let literal = if expression.kind == Kind::CharsetLiteral {
        &expression.children[1]
    } else {
        expression
    };
    let kind = match literal.kind {
        Kind::Int | Kind::HiddenInt | Kind::DefaultInt | Kind::DateUnit | Kind::SampleInt => {
            LiteralKind::Integer
        }
        Kind::Uint => LiteralKind::Unsigned,
        Kind::Number | Kind::SampleDecimal => LiteralKind::Decimal,
        Kind::Double | Kind::HiddenDouble => LiteralKind::Float,
        Kind::String | Kind::HiddenString | Kind::CharsetName => LiteralKind::String,
        Kind::NationalString => LiteralKind::NationalString,
        Kind::HexString | Kind::BitString => LiteralKind::Binary,
        Kind::Bool => LiteralKind::Boolean,
        Kind::Null | Kind::DefaultNull | Kind::HiddenNull => LiteralKind::Null,
        Kind::DateLiteral => LiteralKind::Date,
        Kind::TimeLiteral => LiteralKind::Time,
        Kind::TimestampLiteral => LiteralKind::Timestamp,
        Kind::Parameter => LiteralKind::Placeholder,
        _ => unreachable!(),
    };
    let first_part = child(literal, Kind::StringColumnName);
    ExpressionLiteral {
        kind,
        integer: literal.value,
        text: literal.text.as_bytes(),
        charset: if expression.kind == Kind::CharsetLiteral {
            expression.children[0].text.as_str()
        } else {
            ""
        },
        binary_notation: match literal.kind {
            Kind::HexString => BinaryNotation::Hex,
            Kind::BitString => BinaryNotation::Bit,
            _ => BinaryNotation::None,
        },
        hidden: matches!(
            literal.kind,
            Kind::HiddenInt
                | Kind::HiddenDouble
                | Kind::HiddenString
                | Kind::HiddenNull
                | Kind::DateUnit
                | Kind::CharsetName
        ),
        concatenated: first_part.is_some(),
        first_part_length: first_part.map_or(literal.text.len(), |part| part.text.len()),
        date_unit: if literal.kind == Kind::DateUnit {
            literal.text.as_str()
        } else {
            ""
        },
    }
}

pub(crate) fn integer_literal_value(literal: &Node) -> u64 {
    literal.value
}
pub(crate) fn placeholder_index(parameter: &Node) -> usize {
    parameter.value as usize
}
pub(crate) fn parenthesized_expression(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn unary_operator(expression: &Node) -> UnaryOperator {
    expression.unary_operator()
}
pub(crate) fn unary_operand(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn binary_operator(expression: &Node) -> BinaryOperator {
    expression.binary_operator()
}
pub(crate) fn binary_left(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn binary_right(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn logical_operands(expression: &Node) -> Vec<LogicalOperand<'_>> {
    let operator = binary_operator(expression);
    let mut operands = Vec::new();
    let mut pending = vec![expression];
    while let Some(mut node) = pending.pop() {
        while node.kind == Kind::Parenthesized {
            node = parenthesized_expression(node);
        }
        if node.kind == Kind::Binary && binary_operator(node) == operator {
            pending.push(binary_right(node));
            pending.push(binary_left(node));
        } else {
            operands.push(LogicalOperand { node });
        }
    }
    operands
}

pub(crate) fn like_has_escape(expression: &Node) -> bool {
    expression.children.len() == 3
}
pub(crate) fn like_escape(expression: &Node) -> &Node {
    &expression.children[2]
}
pub(crate) fn predicate_negated(expression: &Node) -> bool {
    expression.value != 0
}
pub(crate) fn predicate_subject(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn between_lower(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn between_upper(expression: &Node) -> &Node {
    &expression.children[2]
}
pub(crate) fn is_test(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn membership_source(expression: &Node) -> MembershipSource {
    if expression.children[1].kind == Kind::Select {
        MembershipSource::Query
    } else {
        MembershipSource::Expressions
    }
}
pub(crate) fn membership_expressions(expression: &Node) -> &[Node] {
    &expression.children[1].children
}
pub(crate) fn membership_query(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn quantified_comparison(expression: &Node) -> ComparisonQuantifier {
    if expression.kind == Kind::All {
        ComparisonQuantifier::All
    } else {
        ComparisonQuantifier::Any
    }
}
pub(crate) fn quantified_query(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn exists_query(expression: &Node) -> &Node {
    &expression.children[0]
}

pub(crate) fn is_call_expression(expression: &Node) -> bool {
    child(expression, Kind::CallArguments).is_some()
}
pub(crate) fn call_name(call: &Node) -> &str {
    if let Some(name) = child(call, Kind::CallName) {
        return name.children.last().unwrap().text.as_str();
    }
    match call.kind {
        Kind::GroupConcat => "group_concat",
        Kind::ArrayLiteral => "array",
        Kind::ArrayMembership => "array_contains",
        Kind::MapLiteral => "map",
        Kind::ArrayAgg => "array_agg",
        Kind::ArrayMap => "array_map",
        Kind::ArrayFilter => "array_filter",
        Kind::ArrayFirst => "array_first",
        Kind::ArraySortBy => "array_sortby",
        Kind::VectorDistance => "vector_distance",
        Kind::VectorSimilarity => "vector_similarity",
        Kind::Point => "point",
        Kind::FirstValue => "first_value",
        Kind::LastValue => "last_value",
        Kind::NthValue => "nth_value",
        Kind::Lead => "lead",
        Kind::Lag => "lag",
        _ => unreachable!(),
    }
}
pub(crate) fn call_qualifiers(call: &Node) -> &[Node] {
    child(call, Kind::CallName).map_or(&[], |name| &name.children[..name.children.len() - 1])
}
pub(crate) fn call_arguments(mut call: &Node) -> &[Node] {
    loop {
        call = match call.kind {
            Kind::Parenthesized => parenthesized_expression(call),
            Kind::OdbcEscape => odbc_operand(call),
            _ => return list(call, Kind::CallArguments),
        };
    }
}
pub(crate) fn weight_string_definition(call: &Node) -> crate::bridge::ffi::WeightStringDefinition<'_> {
    let cast_type = child(call, Kind::WeightStringType);
    crate::bridge::ffi::WeightStringDefinition {
        length: cast_type.map_or(&[], |node| node.children.as_slice()),
        binary: cast_type.is_some_and(|node| node.value != 0),
        levels: list(call, Kind::WeightStringLevels),
    }
}
pub(crate) fn weight_string_level(level: &Node) -> crate::bridge::ffi::WeightStringLevel<'_> {
    crate::bridge::ffi::WeightStringLevel {
        numbers: &level.children,
        descending: level.value & 1 != 0,
        reverse: level.value & 2 != 0,
    }
}
pub(crate) fn aggregate_quantifier(aggregate: &Node) -> AggregateQuantifier {
    if child(aggregate, Kind::Distinct).is_some() {
        AggregateQuantifier::Distinct
    } else if child(aggregate, Kind::All).is_some() {
        AggregateQuantifier::All
    } else {
        AggregateQuantifier::Default
    }
}
pub(crate) fn aggregate_order(aggregate: &Node) -> &[Node] {
    list(aggregate, Kind::Order)
}
pub(crate) fn aggregate_has_separator(aggregate: &Node) -> bool {
    child(aggregate, Kind::Separator).is_some()
}
pub(crate) fn aggregate_separator(aggregate: &Node) -> &Node {
    &child(aggregate, Kind::Separator).unwrap().children[0]
}
pub(crate) fn case_has_operand(expression: &Node) -> bool {
    expression.children[0].kind != Kind::Missing
}
pub(crate) fn case_operand(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn case_arms(expression: &Node) -> &[Node] {
    list(expression, Kind::WhenList)
}
pub(crate) fn case_has_else(expression: &Node) -> bool {
    expression.children[2].kind != Kind::DefaultNull
}
pub(crate) fn case_else(expression: &Node) -> &Node {
    &expression.children[2]
}
pub(crate) fn case_condition(arm: &Node) -> &Node {
    &arm.children[0]
}
pub(crate) fn case_result(arm: &Node) -> &Node {
    &arm.children[1]
}
pub(crate) fn cast_argument(cast: &Node) -> &Node {
    &call_arguments(cast)[0]
}
pub(crate) fn cast_target(cast: &Node) -> &Node {
    &call_arguments(cast)[1]
}
pub(crate) fn cast_ignore(cast: &Node) -> bool {
    cast.value & 1 != 0
}
pub(crate) fn cast_array(cast: &Node) -> bool {
    cast.value & 2 != 0
}
pub(crate) fn collation_argument(expression: &Node) -> &Node {
    &call_arguments(expression)[0]
}
pub(crate) fn collation_name(expression: &Node) -> &[u8] {
    collation_specification(expression).text.as_bytes()
}
pub(crate) fn collation_specification(expression: &Node) -> &Node {
    &call_arguments(expression)[1]
}

pub(crate) fn type_parameters(target: &Node) -> &[Node] {
    let count = target
        .children
        .iter()
        .take_while(|node| matches!(node.kind, Kind::Int | Kind::Uint | Kind::Number | Kind::HiddenInt))
        .count();
    &target.children[..count]
}
pub(crate) fn type_charset(target: &Node) -> &[u8] {
    child(target, Kind::Charset).map_or(&[][..], |charset| charset.text.as_bytes())
}
pub(crate) fn type_collation(target: &Node) -> &[u8] {
    child(target, Kind::Collation).map_or(&[][..], |collation| collation.text.as_bytes())
}
pub(crate) fn type_unsigned(target: &Node) -> bool {
    target.kind == Kind::DataType && target.text.as_str() != "geometry" && target.value & 1 != 0
        || target.text.as_str() == "unsigned"
}
pub(crate) fn type_zerofill(target: &Node) -> bool {
    target.kind == Kind::DataType && target.text.as_str() != "geometry" && target.value & 2 != 0
}
pub(crate) fn type_syntax(target: &Node) -> TypeSyntax<'_> {
    let name = match target.kind {
        Kind::EnumType => TypeName::Enum,
        Kind::SetType => TypeName::Set,
        Kind::CollectionType => match target.value {
            0 => TypeName::Array,
            1 => TypeName::Vector,
            2 => TypeName::Map,
            3 => TypeName::SparseVector,
            _ => unreachable!(),
        },
        _ => match target.text.as_str() {
            "tinyint" => TypeName::TinyInteger,
            "smallint" => TypeName::SmallInteger,
            "mediumint" => TypeName::MediumInteger,
            "int" => TypeName::Integer,
            "bigint" => TypeName::BigInteger,
            "float" => TypeName::Float,
            "double" => TypeName::Double,
            "decimal" => TypeName::Decimal,
            "bool" | "boolean" => TypeName::Boolean,
            "char" => TypeName::Char,
            "varchar" => TypeName::VarChar,
            "binary" => TypeName::Binary,
            "varbinary" => TypeName::VarBinary,
            "tinytext" => TypeName::TinyText,
            "text" => TypeName::Text,
            "mediumtext" => TypeName::MediumText,
            "longtext" => TypeName::LongText,
            "tinyblob" => TypeName::TinyBlob,
            "blob" => TypeName::Blob,
            "mediumblob" => TypeName::MediumBlob,
            "longblob" => TypeName::LongBlob,
            "string" => TypeName::String,
            "date" => TypeName::Date,
            "time" => TypeName::Time,
            "datetime" => TypeName::DateTime,
            "timestamp" => TypeName::Timestamp,
            "year" => TypeName::Year,
            "json" => TypeName::Json,
            "bit" => TypeName::Bit,
            "geometry" => TypeName::Geometry,
            "signed" => TypeName::Signed,
            "unsigned" => TypeName::Unsigned,
            _ => unreachable!(),
        },
    };
    let parameters = type_parameters(target);
    let first = parameters.first().map_or(0, |parameter| parameter.value);
    let second = parameters.get(1).map_or(0, |parameter| parameter.value);
    let numeric = matches!(name, TypeName::Decimal | TypeName::Float | TypeName::Double);
    let temporal = matches!(
        name,
        TypeName::Time | TypeName::DateTime | TypeName::Timestamp
    );
    let geometry = if name == TypeName::Geometry {
        match target.value {
            0 => GeometryType::Geometry,
            1 => GeometryType::Point,
            2 => GeometryType::LineString,
            3 => GeometryType::Polygon,
            4 => GeometryType::MultiPoint,
            5 => GeometryType::MultiLineString,
            6 => GeometryType::MultiPolygon,
            7 => GeometryType::GeometryCollection,
            _ => unreachable!(),
        }
    } else {
        GeometryType::Geometry
    };
    TypeSyntax {
        name,
        spelling: target.text.as_str(),
        argument_count: parameters.len(),
        parameters,
        has_length: !numeric && !temporal && !parameters.is_empty(),
        length: if !numeric && !temporal { first } else { 0 },
        has_precision: numeric && !parameters.is_empty(),
        precision: if numeric { first } else { 0 },
        has_scale: numeric && parameters.len() == 2 || temporal && !parameters.is_empty(),
        scale: if numeric {
            second
        } else if temporal {
            first
        } else {
            0
        },
        is_unsigned: type_unsigned(target),
        zerofill: type_zerofill(target),
        binary: child(target, Kind::BinaryAttribute).is_some(),
        has_charset: child(target, Kind::Charset).is_some(),
        charset: type_charset(target),
        has_collation: child(target, Kind::Collation).is_some(),
        collation: type_collation(target),
        geometry,
    }
}
pub(crate) fn type_element(target: &Node) -> &Node {
    &target.children[0]
}
pub(crate) fn type_key(target: &Node) -> &Node {
    &target.children[0]
}
pub(crate) fn type_value(target: &Node) -> &Node {
    &target.children[1]
}
pub(crate) fn type_members(target: &Node) -> &[Node] {
    list(target, Kind::StringList)
}
pub(crate) fn variable_name(expression: &Node) -> &str {
    if matches!(
        expression.kind,
        Kind::GetUserVariable | Kind::UserAssignment
    ) {
        expression.children[0].text.as_str()
    } else {
        expression.text.as_str()
    }
}
pub(crate) fn variable_scope(expression: &Node) -> VariableScope {
    match expression.value {
        0 => VariableScope::Default,
        1 => VariableScope::Global,
        2 => VariableScope::Session,
        _ => unreachable!(),
    }
}
pub(crate) fn variable_assignment_value(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn named_argument_name(argument: &Node) -> &Node {
    let mut name = &argument.children[0];
    while name.kind == Kind::Parenthesized {
        name = parenthesized_expression(name);
    }
    if name.kind == Kind::Column && !name.is_wildcard() && name.children.len() == 1 {
        &name.children[0]
    } else {
        name
    }
}
pub(crate) fn named_argument_value(argument: &Node) -> &Node {
    &argument.children[1]
}

pub(crate) fn window_function(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn window_definition(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn window_has_reference(window: &Node) -> bool {
    child(window, Kind::Ident).is_some()
}
pub(crate) fn window_reference(window: &Node) -> &Node {
    child(window, Kind::Ident).unwrap()
}
pub(crate) fn window_partition(window: &Node) -> &[Node] {
    child(window, Kind::WindowSpec).map_or(&[], |spec| list(spec, Kind::ExprList))
}
pub(crate) fn window_order(window: &Node) -> &[Node] {
    child(window, Kind::WindowSpec).map_or(&[], |spec| list(spec, Kind::Order))
}
pub(crate) fn window_has_frame(window: &Node) -> bool {
    child(window, Kind::WindowSpec).is_some_and(|spec| child(spec, Kind::WindowFrame).is_some())
}
pub(crate) fn window_frame(window: &Node) -> &Node {
    child(child(window, Kind::WindowSpec).unwrap(), Kind::WindowFrame).unwrap()
}
pub(crate) fn named_window_name(window: &Node) -> &Node {
    &window.children[0]
}
pub(crate) fn named_window_definition(window: &Node) -> &Node {
    &window.children[1]
}
pub(crate) fn window_null_treatment(function: &Node) -> NullTreatment {
    if child(function, Kind::Ignore).is_some() {
        NullTreatment::Ignore
    } else if child(function, Kind::Respect).is_some() {
        NullTreatment::Respect
    } else {
        NullTreatment::Unspecified
    }
}
pub(crate) fn window_nth_origin(function: &Node) -> NthOrigin {
    if child(function, Kind::First).is_some() {
        NthOrigin::First
    } else if child(function, Kind::Last).is_some() {
        NthOrigin::Last
    } else {
        NthOrigin::Unspecified
    }
}
pub(crate) fn frame_unit(frame: &Node) -> FrameUnit {
    if frame.children[0].value == 1 {
        FrameUnit::Rows
    } else {
        FrameUnit::Range
    }
}
pub(crate) fn frame_start(frame: &Node) -> &Node {
    &frame.children[1]
}
pub(crate) fn frame_has_end(frame: &Node) -> bool {
    frame.children.len() == 3
}
pub(crate) fn frame_end(frame: &Node) -> &Node {
    &frame.children[2]
}
pub(crate) fn frame_bound(bound: &Node) -> FrameBound {
    if bound.value == 1 {
        FrameBound::CurrentRow
    } else {
        let unbounded = bound.children[0].text.as_str() == "unbounded";
        let preceding = bound.children[1].value == 1;
        match (unbounded, preceding) {
            (true, true) => FrameBound::UnboundedPreceding,
            (true, false) => FrameBound::UnboundedFollowing,
            (false, true) => FrameBound::Preceding,
            (false, false) => FrameBound::Following,
        }
    }
}
pub(crate) fn frame_bound_offset(bound: &Node) -> &Node {
    &bound.children[0].children[0]
}
pub(crate) fn frame_bound_has_unit(bound: &Node) -> bool {
    bound.children[0].children.len() == 2
}
pub(crate) fn frame_bound_unit(bound: &Node) -> &Node {
    &bound.children[0].children[1]
}
pub(crate) fn lambda_parameters(lambda: &Node) -> &[Node] {
    &lambda.children[0].children
}
pub(crate) fn lambda_body(lambda: &Node) -> &Node {
    &lambda.children[1]
}
pub(crate) fn collection_lambda(call: &Node) -> &Node {
    child(call, Kind::Lambda).unwrap()
}
pub(crate) fn vector_metric(call: &Node) -> VectorMetric {
    match child(call, Kind::HiddenInt).map(|metric| metric.value) {
        None => VectorMetric::Unspecified,
        Some(0) => VectorMetric::Cosine,
        Some(1) => VectorMetric::Dot,
        Some(2) => VectorMetric::Euclidean,
        Some(3) => VectorMetric::Manhattan,
        _ => unreachable!(),
    }
}
pub(crate) fn fulltext_columns(expression: &Node) -> &[Node] {
    list(expression, Kind::MatchColumns)
}
pub(crate) fn fulltext_search(expression: &Node) -> &Node {
    &expression.children[1]
}
pub(crate) fn fulltext_options(expression: &Node) -> &Node {
    &expression.children[2]
}
pub(crate) fn fulltext_column_reference(column: &Node) -> &Node {
    if column.kind == Kind::MatchColumn { &column.children[0] } else { column }
}
pub(crate) fn fulltext_column_boost(column: &Node) -> &Node {
    &column.children[1]
}
pub(crate) fn fulltext_mode(expression: &Node) -> FulltextMode {
    match expression.value {
        0 => FulltextMode::Natural,
        1 => FulltextMode::NaturalExpansion,
        2 => FulltextMode::Boolean,
        3 => FulltextMode::Expansion,
        4 => FulltextMode::Phrase,
        _ => unreachable!(),
    }
}

pub(crate) fn json_document(expression: &Node) -> &Node {
    &expression.children[0]
}
pub(crate) fn json_path(expression: &Node) -> &Node {
    if expression.kind == Kind::JsonColumn {
        &expression.children[if expression.value == 5 { 0 } else { 3 }]
    } else {
        &expression.children[1]
    }
}
pub(crate) fn json_returning(expression: &Node) -> &Node {
    &expression.children[2]
}
fn response(value: u64, query: bool) -> JsonResponse {
    match (query, value) {
        (_, 0) => JsonResponse::Error,
        (_, 1) => JsonResponse::Null,
        (false, 2) => JsonResponse::Default,
        (false, 3) => JsonResponse::Unspecified,
        (true, 2) => JsonResponse::Empty,
        (true, 3) => JsonResponse::EmptyArray,
        (true, 4) => JsonResponse::EmptyObject,
        (true, 5) => JsonResponse::Unspecified,
        _ => unreachable!(),
    }
}
pub(crate) fn json_options(expression: &Node) -> JsonOptions {
    let mut options = JsonOptions {
        truncate: false,
        ascii: false,
        pretty: false,
        asis: false,
        multivalue: false,
        scalars: JsonScalars::Unspecified,
        wrapper: JsonWrapper::Unspecified,
        on_error: JsonResponse::Unspecified,
        on_empty: JsonResponse::Unspecified,
        on_mismatch: JsonResponse::Unspecified,
    };
    if expression.kind == Kind::JsonQuery {
        let values = &expression.children;
        options.truncate = values[3].value != 0;
        options.scalars = match values[4].value {
            0 => JsonScalars::Allow,
            1 => JsonScalars::Disallow,
            2 => JsonScalars::Unspecified,
            _ => unreachable!(),
        };
        options.pretty = values[5].value != 0;
        options.ascii = values[6].value != 0;
        options.wrapper = match values[7].value {
            0 => JsonWrapper::Without,
            1 => JsonWrapper::WithoutArray,
            2 => JsonWrapper::With,
            3 => JsonWrapper::WithArray,
            4 => JsonWrapper::Unconditional,
            5 => JsonWrapper::Conditional,
            6 => JsonWrapper::UnconditionalArray,
            7 => JsonWrapper::ConditionalArray,
            8 => JsonWrapper::Unspecified,
            _ => unreachable!(),
        };
        options.asis = values[8].value != 0;
        options.on_error = response(values[9].value, true);
        options.on_empty = response(values[10].value, true);
        options.on_mismatch = match values[11].value {
            0 => JsonResponse::Error,
            1 => JsonResponse::Null,
            2 => JsonResponse::Unspecified,
            3 => JsonResponse::Dot,
            _ => unreachable!(),
        };
        options.multivalue = values[12].value != 0;
    } else if expression.kind == Kind::JsonValue {
        options.truncate = expression.children[3].value != 0;
        options.ascii = expression.children[4].value != 0;
        options.on_empty = response(expression.children[5].value, false);
        options.on_error = response(expression.children[7].value, false);
    } else if expression.kind == Kind::JsonColumn && expression.value == 4 {
        let values = &expression.children[4].children;
        options.on_empty = response(values[0].value, false);
        options.on_error = response(values[2].value, false);
    }
    options
}
pub(crate) fn json_error_clause(expression: &Node) -> &Node {
    match expression.kind {
        Kind::JsonQuery => &expression.children[9],
        Kind::JsonValue => &expression.children[7],
        Kind::JsonColumn => &expression.children[4].children[2],
        _ => unreachable!(),
    }
}
pub(crate) fn json_empty_clause(expression: &Node) -> &Node {
    match expression.kind {
        Kind::JsonQuery => &expression.children[10],
        Kind::JsonValue => &expression.children[5],
        Kind::JsonColumn => &expression.children[4].children[0],
        _ => unreachable!(),
    }
}
pub(crate) fn json_mismatch_clause(expression: &Node) -> &Node {
    &expression.children[11]
}
pub(crate) fn json_error_default(expression: &Node) -> &Node {
    if expression.kind == Kind::JsonColumn {
        &expression.children[4].children[3]
    } else {
        &expression.children[8]
    }
}
pub(crate) fn json_empty_default(expression: &Node) -> &Node {
    if expression.kind == Kind::JsonColumn {
        &expression.children[4].children[1]
    } else {
        &expression.children[6]
    }
}
pub(crate) fn json_table_columns(table: &Node) -> &[Node] {
    &table.children[3].children
}
pub(crate) fn json_column_kind(column: &Node) -> JsonColumnKind {
    match column.value {
        1 => JsonColumnKind::Ordinality,
        2 => JsonColumnKind::Exists,
        4 => JsonColumnKind::Value,
        5 => JsonColumnKind::Nested,
        _ => unreachable!(),
    }
}
pub(crate) fn json_column_name(column: &Node) -> &Node {
    &column.children[0]
}
pub(crate) fn json_column_type(column: &Node) -> &Node {
    &column.children[1]
}
pub(crate) fn json_nested_columns(column: &Node) -> &[Node] {
    &column.children[1].children
}

pub(crate) fn equivalent_expressions(left: &Node, right: &Node) -> bool {
    let left = if left.kind == Kind::Parenthesized {
        parenthesized_expression(left)
    } else {
        left
    };
    let right = if right.kind == Kind::Parenthesized {
        parenthesized_expression(right)
    } else {
        right
    };
    if left.kind == Kind::Parenthesized || right.kind == Kind::Parenthesized {
        return equivalent_expressions(left, right);
    }
    left.kind == right.kind
        && left.text.as_bytes() == right.text.as_bytes()
        && left.value == right.value
        && left.children.len() == right.children.len()
        && left
            .children
            .iter()
            .zip(&right.children)
            .all(|(a, b)| equivalent_expressions(a, b))
}
