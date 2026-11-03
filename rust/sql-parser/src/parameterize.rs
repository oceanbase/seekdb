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

mod numeric;
mod projection;

pub use numeric::{resolve_unary, UnaryOperand, UnaryResolution};
pub use projection::projection_names;
use projection::parameter_template;

use std::collections::{HashMap, HashSet};

use crate::bridge::ffi::*;
use crate::expression::*;
use crate::ddl::{outline_definition, outline_statement, outline_target};
use crate::inspect::syntax_literals;
use crate::node::{Kind, Node};
use crate::numeric::{negative_operand, negative_unsigned_kind, NegativeOperand, UnaryPath};
use crate::query::*;
use crate::select_into::{query_into_kind, file_output_definition, output_metadata};
use crate::routine::routine_call_arguments;

type Result<T> = std::result::Result<T, ParameterError>;

#[derive(Default)]
pub struct ParameterEffects {
    by_node: HashMap<*const Node, NodeEffect>,
}

#[derive(Default)]
struct NodeEffect {
    binding: Option<ParameterBinding>,
    lowering: Option<ParameterLowering>,
}

impl ParameterEffects {
    pub fn binding(&self, node: &Node) -> Option<&ParameterBinding> {
        self.by_node.get(&(node as *const Node))?.binding.as_ref()
    }

    pub fn lowering(&self, node: &Node) -> Option<&ParameterLowering> {
        self.by_node.get(&(node as *const Node))?.lowering.as_ref()
    }

    pub(crate) fn parameter_binding(&self, node: &Node) -> &[ParameterBinding] {
        self.binding(node).map_or(&[], std::slice::from_ref)
    }

    pub(crate) fn numeric_lowering(&self, node: &Node) -> &[ParameterLowering] {
        self.lowering(node).map_or(&[], std::slice::from_ref)
    }

    fn bind(&mut self, node: &Node, binding: ParameterBinding) {
        self.by_node.entry(node as *const Node).or_default().binding = Some(binding);
    }

    fn lower(&mut self, node: &Node, operation: ParameterLoweringKind, value: ParameterValue) {
        self.by_node.entry(node as *const Node).or_default().lowering = Some(ParameterLowering {
            operation,
            value: NumericValue { kind: value.kind, integer: value.integer, text: value.text },
        });
    }
}

#[derive(Clone, Copy, Default)]
struct Scope<'a> {
    excluded: bool,
    projection: bool,
    direct_value: bool,
    assigned_value: bool,
    arithmetic: bool,
    ignore_scale: bool,
    positive: bool,
    subtraction: Option<(&'a Node, &'a Node)>,
}

impl Scope<'_> {
    fn expression(self) -> Self {
        Self {
            direct_value: false,
            arithmetic: false,
            ignore_scale: false,
            ..self
        }
    }
    fn excluded(self) -> Self {
        Self {
            excluded: true,
            ..self
        }
    }
}

struct Occurrence<'a> {
    literal: &'a Node,
    expression: &'a Node,
    scope: Scope<'a>,
    origin: ParameterOrigin,
    source_begin: usize,
}

struct Collector<'a> {
    literals: Vec<Occurrence<'a>>,
    exclusions: Vec<&'a Node>,
    projections: Vec<&'a Node>,
    user_variables: Vec<String>,
    ps_eligible: bool,
}

fn numeric(kind: LiteralKind) -> bool {
    matches!(
        kind,
        LiteralKind::Integer | LiteralKind::Unsigned | LiteralKind::Decimal | LiteralKind::Float
    )
}

impl<'a> Collector<'a> {
    fn literal(
        &mut self,
        node: &'a Node,
        expression: &'a Node,
        scope: Scope<'a>,
        origin: ParameterOrigin,
    ) {
        let value = expression_literal(node);
        if value.hidden {
            if scope.excluded && node.start < node.end {
                self.exclusions.push(node);
            }
            return;
        }
        if scope.excluded
            || !value.charset.is_empty()
            || (scope.projection && value.kind != LiteralKind::Placeholder)
        {
            self.ps_eligible = false;
        }
        let source_begin = scope
            .subtraction
            .filter(|(_, operand)| std::ptr::eq(*operand, node))
            .map_or(expression.start, |(parent, _)| binary_left(parent).end);
        self.literals.push(Occurrence {
            literal: node,
            expression,
            scope,
            origin,
            source_begin,
        });
    }

    fn exclude(&mut self, node: &'a Node) {
        self.ps_eligible = false;
        if node.kind == Kind::JsonColumn && json_column_kind(node) == JsonColumnKind::Nested {
            self.exclude(json_path(node));
            for column in json_nested_columns(node) {
                self.exclude(column);
            }
            return;
        }
        for reference in syntax_literals(node) {
            self.literal(
                reference.node,
                reference.node,
                Scope::default().excluded(),
                ParameterOrigin::Literal,
            );
        }
        if node.kind == Kind::JsonColumn && json_column_kind(node) == JsonColumnKind::Value {
            self.exclude(json_error_clause(node));
            self.exclude(json_empty_clause(node));
        }
        if node.start < node.end {
            self.exclusions.push(node);
        }
    }

    fn list(&mut self, nodes: &'a [Node], scope: Scope<'a>) -> Result<()> {
        for node in nodes {
            self.collect(node, scope)?;
        }
        Ok(())
    }

    fn query(&mut self, node: &'a Node, scope: Scope<'a>) -> Result<()> {
        let scope = scope.expression();
        for cte in query_ctes(node) {
            self.collect(cte_query(cte), scope)?;
        }
        match node.kind {
            Kind::Select | Kind::PlSubquery => {
                self.list(query_set_operands(node), scope)?;
                for projection in query_projection(node) {
                    self.projections.push(projection);
                    self.collect(
                        projection_expression(projection),
                        Scope {
                            projection: true,
                            ..scope
                        },
                    )?;
                    if projection_alias_form(projection) == AliasForm::String {
                        self.exclude(projection_alias(projection));
                    }
                }
                if query_has_grouping(node) {
                    for key in grouping_keys(query_grouping(node)) {
                        self.collect(sort_expression(key), scope.excluded())?;
                    }
                }
                if query_has_having(node) {
                    self.collect(query_having(node), scope)?;
                }
                for named in query_windows(node) {
                    self.exclude(named);
                }
            }
            Kind::Insert | Kind::Replace => {
                let value_scope = Scope {
                    direct_value: true,
                    assigned_value: true,
                    ..scope
                };
                match insert_source(node) {
                    InsertSource::Query | InsertSource::AliasedRows => self.collect(insert_query(node), scope)?,
                    InsertSource::Rows => {
                        for row in insert_rows(node) {
                            self.list(row_elements(row), value_scope)?;
                        }
                    }
                    _ => {
                        for assignment in insert_assignments(node) {
                            self.collect(assignment_value(assignment), value_scope)?;
                        }
                    }
                }
                for assignment in insert_on_duplicate(node) {
                    self.collect(assignment_value(assignment), value_scope)?;
                }
            }
            Kind::Update => {
                for assignment in query_assignments(node) {
                    self.collect(
                        assignment_value(assignment),
                        Scope {
                            direct_value: true,
                            assigned_value: true,
                            ..scope
                        },
                    )?;
                }
            }
            _ => {}
        }
        if matches!(query_into_kind(node), IntoKind::Outfile | IntoKind::Dumpfile) {
            self.collect(file_output_definition(node).filename, scope)?;
            if let Some(metadata) = output_metadata(node) {
                self.exclude(metadata);
            }
            self.ps_eligible = false;
        }
        for source in query_sources(node) {
            self.table(source, scope)?;
        }
        if query_has_filter(node) {
            self.collect(query_filter(node), scope)?;
        }
        let order = query_order(node);
        let vector_order = order.len() == 1 && {
            let expression = sort_expression(&order[0]);
            expression.kind == Kind::Function
                && matches!(
                    call_name(expression).to_ascii_lowercase().as_str(),
                    "l2_distance"
                        | "negative_inner_product"
                        | "cosine_distance"
                        | "semantic_distance"
                        | "semantic_vector_distance"
                )
        };
        for key in order {
            self.collect(
                sort_expression(key),
                if vector_order {
                    scope
                } else {
                    scope.excluded()
                },
            )?;
        }
        if query_has_limit(node) {
            let limit = query_limit(node);
            let scope = Scope {
                positive: true,
                ..scope
            };
            self.collect(limit_count(limit), scope)?;
            if limit_has_offset(limit) {
                self.collect(limit_offset(limit), scope)?;
            }
        }
        if query_has_lock(node) {
            self.exclude(query_lock(node));
        }
        for parameter in query_vector_parameters(node) {
            self.exclude(parameter);
        }
        Ok(())
    }

    fn table(&mut self, node: &'a Node, scope: Scope<'a>) -> Result<()> {
        match node.kind {
            Kind::Table => {
                let source = table_source(node);
                if source.kind != Kind::Relation {
                    self.collect(source, scope)?;
                }
                if table_has_sample(node) {
                    self.exclude(table_sample(node));
                }
                if table_has_snapshot(node) {
                    self.collect(table_snapshot(node), scope)?;
                }
            }
            Kind::JoinedTable => {
                self.table(join_left(node), scope)?;
                self.table(join_right(node), scope)?;
                if join_condition(node) == JoinCondition::On {
                    self.collect(join_predicate(node), scope)?;
                }
            }
            Kind::JsonTable => {
                self.collect(json_document(node), scope)?;
                self.exclude(json_path(node));
                for column in json_table_columns(node) {
                    self.exclude(column);
                }
            }
            Kind::TableCollection => self.collect(collection_table_expression(node), scope)?,
            Kind::HybridSearchTable => {
                self.collect(hybrid_search_table(node), scope.excluded())?;
                self.collect(hybrid_search_parameters(node), scope.excluded())?;
            }
            Kind::UnnestTable => {
                for argument in unnest_arguments(node) {
                    self.collect(argument, scope)?;
                }
            }
            Kind::Select => self.query(node, scope)?,
            _ => return Err(ParameterError::Unsupported),
        }
        Ok(())
    }

    fn window(&mut self, node: &'a Node, scope: Scope<'a>) -> Result<()> {
        self.list(window_partition(node), scope)?;
        for key in window_order(node) {
            self.collect(sort_expression(key), scope.excluded())?;
        }
        if window_has_frame(node) {
            let frame = window_frame(node);
            let mut bound = |value| {
                if matches!(
                    frame_bound(value),
                    FrameBound::Preceding | FrameBound::Following
                ) {
                    self.collect(frame_bound_offset(value), scope)?;
                }
                Ok(())
            };
            bound(frame_start(frame))?;
            if frame_has_end(frame) {
                bound(frame_end(frame))?;
            }
        }
        Ok(())
    }

    fn call(&mut self, node: &'a Node, scope: Scope<'a>) -> Result<()> {
        let args = call_arguments(node);
        let name = call_name(node).to_ascii_lowercase();
        if name == "weight_string" {
            self.ps_eligible = false;
            let syntax = weight_string_definition(node);
            for length in syntax.length { self.exclude(length); }
            for level in syntax.levels { self.exclude(level); }
        }
        let excluded = match (name.as_str(), args.len()) {
            ("userenv" | "unix_timestamp", 1) => 0..1,
            ("substr", 3) => 1..3,
            ("weight_string", 5) => 1..5,
            (
                "convert" | "char" | "str_to_date" | "date_format" | "from_unixtime" | "round"
                | "left" | "substr" | "truncate",
                2,
            ) => 1..2,
            ("name_const" | "json_schema_valid" | "json_schema_validation_report", 2) => 0..1,
            ("json_overlaps" | "json_extract" | "json_member_of", 2) => 0..2,
            _ => args.len()..args.len(),
        };
        if node.kind == Kind::StringConcat
            || matches!(
                name.as_str(),
                "json_equal" | "json_member_of" | "json_contains"
            )
            || !excluded.is_empty()
        {
            self.ps_eligible = false;
        }
        let scope = Scope {
            direct_value: false,
            arithmetic: false,
            ..scope
        };
        if matches!(
            node.kind,
            Kind::ArrayMap | Kind::ArrayFilter | Kind::ArrayFirst | Kind::ArraySortBy
        ) {
            self.collect(collection_lambda(node), scope)?;
        }
        for (i, argument) in args.iter().enumerate() {
            let time_precision = matches!(
                node.kind,
                Kind::CurrentTimestamp
                    | Kind::CurrentTime
                    | Kind::CurrentDate
                    | Kind::UtcTimestamp
                    | Kind::UtcTime
                    | Kind::Sysdate
            );
            self.collect(
                argument,
                Scope {
                    excluded: scope.excluded || excluded.contains(&i) || time_precision
                        || node.kind == Kind::PercentileCont && i == 0,
                    ignore_scale: node.kind == Kind::Point || name == "st_point",
                    ..scope
                },
            )?;
        }
        for key in aggregate_order(node) {
            self.collect(sort_expression(key), scope.excluded())?;
        }
        if aggregate_has_separator(node) {
            self.collect(aggregate_separator(node), scope)?;
        }
        Ok(())
    }

    fn unary(&mut self, node: &'a Node, mut scope: Scope<'a>) -> Result<()> {
        let mut path = UnaryPath::new(node);
        for step in path.by_ref() {
            let Some(operator) = step.operator else { continue };
            scope.ignore_scale = false;
            let negative = (operator == UnaryOperator::Minus)
                .then(|| negative_operand(step.operand));
            if let Some(NegativeOperand::DirectLiteral(operand)) = negative {
                let value = expression_literal(operand);
                if numeric(value.kind) {
                    self.literal(operand, step.node, scope, ParameterOrigin::UnaryNegation);
                } else {
                    scope.direct_value = false;
                    scope.arithmetic = false;
                    self.literal(
                        operand,
                        operand,
                        scope,
                        if value.kind == LiteralKind::String {
                            ParameterOrigin::UnaryNegation
                        } else {
                            ParameterOrigin::Literal
                        },
                    );
                }
                return Ok(());
            }
            scope.direct_value = false;
            scope.arithmetic = false;
            if matches!(negative, Some(NegativeOperand::ParenthesizedUnsigned(_))) {
                scope.excluded = true;
            }
        }
        self.collect(path.operand(), scope)
    }

    fn collect(&mut self, node: &'a Node, mut scope: Scope<'a>) -> Result<()> {
        if scope.excluded {
            self.ps_eligible = false;
        }
        if is_literal(node) {
            self.literal(node, node, scope, ParameterOrigin::Literal);
            return Ok(());
        }
        match node.kind {
            Kind::Select
            | Kind::PlSubquery
            | Kind::Insert
            | Kind::Replace
            | Kind::Update
            | Kind::Delete => self.query(node, scope)?,
            Kind::PlCall => self.list(routine_call_arguments(node), scope)?,
            Kind::PlNamedArgument => self.collect(named_argument_value(node), scope)?,
            Kind::Explain => {
                if explain_has_statement_id(node) {
                    self.exclude(explain_statement_id(node));
                }
                self.collect(explain_statement(node), scope)?;
            }
            Kind::CreateOutline | Kind::AlterOutline => {
                let outline = outline_definition(node);
                if outline.source == OutlineSource::Statement {
                    self.collect(outline_statement(node), scope)?;
                    if outline.has_target {
                        self.collect(outline_target(node), scope)?;
                    }
                }
            }
            Kind::Ident
            | Kind::Column
            | Kind::Star
            | Kind::Relation
            | Kind::Default
            | Kind::SystemVariable
            | Kind::MatchScore => {}
            Kind::Parenthesized => self.unary(node, scope)?,
            Kind::OdbcEscape => self.collect(odbc_operand(node), scope.expression())?,
            Kind::UserVariable | Kind::GetUserVariable => {
                self.user_variables.push(variable_name(node).into())
            }
            Kind::UserAssignment => self.collect(variable_assignment_value(node), scope)?,
            Kind::Unary => {
                if matches!(unary_operator(node), UnaryOperator::Plus | UnaryOperator::Minus) {
                    self.unary(node, scope)?;
                } else {
                    self.collect(unary_operand(node), scope.expression())?;
                }
            }
            Kind::Binary => {
                let op = binary_operator(node);
                scope.direct_value = false;
                scope.ignore_scale = false;
                scope.arithmetic = matches!(
                    op,
                    BinaryOperator::Add
                        | BinaryOperator::Subtract
                        | BinaryOperator::Multiply
                        | BinaryOperator::Divide
                );
                self.collect(binary_left(node), scope)?;
                if op == BinaryOperator::Like && like_has_escape(node) {
                    scope.excluded = true;
                    self.ps_eligible = false;
                }
                if op == BinaryOperator::Subtract {
                    let mut operand = binary_right(node);
                    while operand.kind == Kind::Binary
                        && matches!(
                            binary_operator(operand),
                            BinaryOperator::Multiply
                                | BinaryOperator::Divide
                                | BinaryOperator::IntegerDivide
                                | BinaryOperator::Modulo
                        )
                    {
                        operand = binary_left(operand);
                    }
                    if is_literal(operand) && numeric(expression_literal(operand).kind) {
                        scope.subtraction = Some((node, operand));
                    }
                }
                self.collect(binary_right(node), scope)?;
                if like_has_escape(node) {
                    self.collect(like_escape(node), scope)?;
                }
            }
            Kind::In => {
                scope = scope.expression();
                self.collect(predicate_subject(node), scope)?;
                if membership_source(node) == MembershipSource::Query {
                    self.collect(membership_query(node), scope)?;
                } else {
                    self.list(membership_expressions(node), scope)?;
                }
            }
            Kind::Between => {
                scope = scope.expression();
                self.collect(predicate_subject(node), scope)?;
                self.collect(between_lower(node), scope)?;
                self.collect(between_upper(node), scope)?;
            }
            Kind::Is => {
                scope = scope.expression();
                self.collect(predicate_subject(node), scope)?;
                self.collect(is_test(node), scope.excluded())?;
            }
            Kind::All | Kind::Any => self.collect(quantified_query(node), scope)?,
            Kind::Exists => self.collect(exists_query(node), scope)?,
            Kind::ExprList | Kind::ValueVector => self.list(row_elements(node), scope)?,
            Kind::Case => {
                scope = scope.expression();
                if case_has_operand(node) {
                    self.collect(case_operand(node), scope)?;
                }
                for arm in case_arms(node) {
                    self.collect(case_condition(arm), scope)?;
                    self.collect(case_result(arm), scope)?;
                }
                if case_has_else(node) {
                    self.collect(case_else(node), scope)?;
                }
            }
            Kind::Cast => {
                self.collect(cast_argument(node), scope.expression())?;
                self.exclude(cast_target(node));
            }
            Kind::SetCollation => {
                self.collect(collation_argument(node), scope)?;
                self.exclude(collation_specification(node));
            }
            Kind::WindowFunction => {
                self.collect(window_function(node), scope)?;
                self.window(window_definition(node), scope)?;
            }
            Kind::Lambda => self.collect(lambda_body(node), scope)?,
            Kind::JsonValue | Kind::JsonQuery => {
                self.collect(json_document(node), scope)?;
                self.exclude(json_path(node));
                self.exclude(json_returning(node));
                self.exclude(json_error_clause(node));
                self.exclude(json_empty_clause(node));
                if node.kind == Kind::JsonQuery {
                    self.exclude(json_mismatch_clause(node));
                }
                let options = json_options(node);
                if options.on_error == JsonResponse::Default {
                    self.exclude(json_error_default(node));
                }
                if options.on_empty == JsonResponse::Default {
                    self.exclude(json_empty_default(node));
                }
            }
            Kind::MatchAgainst => self.collect(fulltext_search(node), scope)?,
            Kind::ElasticMatch => {
                for column in fulltext_columns(node) {
                    self.collect(fulltext_column_boost(column), scope)?;
                }
                self.collect(fulltext_search(node), scope)?;
                self.collect(fulltext_options(node), scope)?;
            }
            Kind::Table | Kind::JoinedTable | Kind::JsonTable | Kind::TableCollection | Kind::UnnestTable | Kind::HybridSearchTable => {
                self.table(node, scope)?
            }
            Kind::Values => self.list(values_rows(node), scope)?,
            _ if is_call_expression(node) => self.call(node, scope)?,
            _ => return Err(ParameterError::Unsupported),
        }
        Ok(())
    }
}

fn syntax_value(node: &Node) -> ParameterValue {
    let value = expression_literal(node);
    ParameterValue {
        kind: value.kind,
        integer: value.integer,
        text: if value.binary_notation == BinaryNotation::None {
            value.text.to_vec()
        } else {
            crate::lex::decode_binary_literal(value.text, value.binary_notation)
        },
        charset: value.charset.into(),
        date_unit: value.date_unit.into(),
        concatenated: value.concatenated,
        first_part_length: value.first_part_length,
    }
}

fn token_value(token: &ParameterToken<'_>) -> ParameterValue {
    ParameterValue {
        kind: token.kind,
        integer: token.integer,
        text: token.text.to_vec(),
        charset: String::new(),
        date_unit: String::new(),
        concatenated: token.concatenated,
        first_part_length: token.first_part_length,
    }
}

fn numeric_value(node: &Node, negative: bool) -> ParameterValue {
    let mut value = syntax_value(node);
    if value.kind == LiteralKind::Unsigned && negative {
        value.kind = negative_unsigned_kind(value.integer);
    }
    if negative {
        value.text.insert(0, b'-');
        if value.kind == LiteralKind::Integer {
            value.integer = value.integer.wrapping_neg();
        }
    }
    value
}

fn empty_value() -> ParameterValue {
    ParameterValue {
        kind: LiteralKind::Null,
        integer: 0,
        text: Vec::new(),
        charset: String::new(),
        date_unit: String::new(),
        concatenated: false,
        first_part_length: 0,
    }
}

fn empty_result() -> Parameterization<'static> {
    Parameterization {
        tokens: Vec::new(),
        bindings: Vec::new(),
        effects: Box::default(),
        projections: Vec::new(),
        user_variables: Vec::new(),
        unchecked_placeholders: Vec::new(),
        ps_eligible: true,
        error: ParameterError::None,
    }
}

fn apply<'a>(
    collector: &mut Collector<'a>,
    original_sql: &[u8],
    tokens: &[ParameterToken<'_>],
    options: ParameterOptions,
) -> Result<Parameterization<'a>> {
    let mut result = empty_result();
    let mut unchecked = Vec::new();
    let mut checked = HashSet::new();
    collector
        .literals
        .sort_by_key(|occurrence| occurrence.literal.start);
    let mut occurrence_index = 0;
    let mut placeholder_count = 0;
    for (raw_index, token) in tokens.iter().enumerate() {
        if token.start > token.end || token.end > original_sql.len() {
            return Err(ParameterError::Unsupported);
        }
        while occurrence_index < collector.literals.len()
            && collector.literals[occurrence_index].literal.end <= token.start
        {
            occurrence_index += 1;
        }
        let occurrence = collector
            .literals
            .get_mut(occurrence_index)
            .filter(|candidate| {
                candidate.source_begin <= token.start && token.end <= candidate.literal.end
            });
        let Some(occurrence) = occurrence else {
            if !collector
                .exclusions
                .iter()
                .any(|node| node.start <= token.start && token.end <= node.end)
            {
                return Err(ParameterError::Unsupported);
            }
            collector.ps_eligible = false;
            result.tokens.push(ParameterTokenPolicy {
                action: ParameterAction::Preserve,
                charset: String::new(),
                positive: false,
                arithmetic: false,
                placeholder: -1,
                excluded_placeholder: false,
            });
            continue;
        };
        let syntax = expression_literal(occurrence.literal);
        let placeholder = syntax.kind == LiteralKind::Placeholder;
        let placeholder_offset = if placeholder {
            placeholder_index(occurrence.literal) as i64
        } else {
            -1
        };
        if placeholder {
            // Independent question marks cannot preserve a repeated bind's shared slot.
            collector.ps_eligible &= placeholder_offset == placeholder_count;
            placeholder_count += 1;
        }
        let subtraction = occurrence.scope.subtraction.filter(|(_, operand)| {
            std::ptr::eq(*operand, occurrence.literal) && token.start < operand.start
        });
        let preserve_subtraction = subtraction.is_some_and(|(_, operand)| {
            operand.kind == Kind::Uint || options.no_unsigned_subtraction
        });
        let unsigned_negation = occurrence.origin == ParameterOrigin::UnaryNegation
            && occurrence.expression.kind == Kind::Unary
            && occurrence.literal.kind == Kind::Uint
            && token.start >= occurrence.literal.start;
        let mut lowered = None;
        if let Some((parent, _)) = subtraction.filter(|_| !preserve_subtraction) {
            lowered = Some(numeric_value(occurrence.literal, !options.from_pl));
            result.effects.lower(
                occurrence.literal,
                ParameterLoweringKind::NegateLiteral,
                numeric_value(occurrence.literal, !options.from_pl),
            );
            result.effects.lower(parent, ParameterLoweringKind::AddNegatedOperand, empty_value());
        }
        let preserve = occurrence.scope.excluded || preserve_subtraction || unsigned_negation;
        let mut policy = ParameterTokenPolicy {
            action: ParameterAction::Normal,
            charset: syntax.charset.into(),
            positive: false,
            arithmetic: false,
            placeholder: placeholder_offset,
            excluded_placeholder: preserve && placeholder,
        };
        if preserve {
            collector.ps_eligible = false;
            policy.action = ParameterAction::Preserve;
            result.tokens.push(policy);
            continue;
        }
        if occurrence.origin == ParameterOrigin::UnaryNegation
            && occurrence.expression.kind == Kind::Unary
        {
            if token.start < occurrence.literal.start {
                lowered = Some(numeric_value(occurrence.literal, true));
                result.effects.lower(
                    occurrence.expression,
                    ParameterLoweringKind::FoldUnaryExpression,
                    numeric_value(occurrence.literal, true),
                );
            } else {
                occurrence.expression = occurrence.literal;
                occurrence.origin = ParameterOrigin::Literal;
                occurrence.scope.arithmetic = false;
            }
        }
        let origin = if subtraction.is_some() {
            ParameterOrigin::Subtraction
        } else {
            occurrence.origin
        };
        let value = lowered.unwrap_or_else(|| {
            if placeholder
                && options.mode == ParameterMode::Text
                && token.kind != LiteralKind::Placeholder
            {
                token_value(token)
            } else {
                syntax_value(occurrence.literal)
            }
        });
        policy.action = match origin {
            ParameterOrigin::UnaryNegation => ParameterAction::UnaryNegation,
            ParameterOrigin::Subtraction => ParameterAction::Subtraction,
            _ => ParameterAction::Normal,
        };
        policy.arithmetic = occurrence.scope.arithmetic
            && !occurrence.scope.assigned_value
            && syntax.kind == LiteralKind::Integer;
        policy.positive = occurrence.scope.positive
            && (numeric(value.kind) || value.kind == LiteralKind::Boolean || placeholder);
        if placeholder {
            if occurrence.scope.direct_value {
                unchecked.push(placeholder_offset as usize);
            } else {
                checked.insert(placeholder_offset as usize);
            }
        }
        let append = options.mode != ParameterMode::Execute;
        let parameter_index =
            options.parameter_base + if append { result.bindings.len() } else { 0 };
        if append {
            result.effects.bind(occurrence.expression, ParameterBinding {
                parameter_index,
                raw_index,
                origin,
            });
        }
        result.bindings.push(ParameterBindingRecord {
            node: occurrence.expression,
            raw_index,
            parameter_index,
            origin,
            value,
            check_type: !occurrence.scope.direct_value,
            scale_policy: if !placeholder && occurrence.scope.ignore_scale {
                ParameterScalePolicy::IgnoreNonDecimalScale
            } else {
                ParameterScalePolicy::Check
            },
            boolean: syntax.kind == LiteralKind::Boolean,
            fixed: !placeholder,
            append,
        });
        result.tokens.push(policy);
    }
    if options.outline
        && collector.literals.iter().any(|occurrence| {
            occurrence.literal.kind == Kind::Parameter && occurrence.scope.excluded
        })
    {
        return Err(ParameterError::InvalidOutline);
    }
    if options.projection_names {
        for projection in &collector.projections {
            if let Some(template) = parameter_template(
                Projection::new(projection),
                original_sql,
                collector.literals.iter().map(|occurrence| (occurrence.literal, occurrence.expression)),
                tokens,
                &result.effects,
                options.projection_name_limit,
            )? {
                result.projections.push(template);
            }
        }
    }
    result.unchecked_placeholders = unchecked
        .into_iter()
        .filter(|offset| !checked.contains(offset))
        .collect();
    result.user_variables = std::mem::take(&mut collector.user_variables);
    result.ps_eligible = collector.ps_eligible;
    Ok(result)
}

pub fn parameterize<'a>(
    tree: &'a Node,
    original_sql: &[u8],
    tokens: &[ParameterToken<'_>],
    options: ParameterOptions,
) -> Parameterization<'a> {
    let mut collector = Collector {
        literals: Vec::new(),
        exclusions: Vec::new(),
        projections: Vec::new(),
        user_variables: Vec::new(),
        ps_eligible: true,
    };
    match collector
        .collect(tree, Scope::default())
        .and_then(|()| apply(&mut collector, original_sql, tokens, options))
    {
        Ok(result) => result,
        Err(error) => Parameterization {
            error,
            ..empty_result()
        },
    }
}

pub fn classify_parameter_tokens(
    tokens: &[ParameterToken<'_>],
    expected: usize,
    check_order: bool,
) -> ParameterClassification {
    if expected != tokens.len()
        || (check_order && tokens.windows(2).any(|pair| pair[0].start > pair[1].start))
    {
        return ParameterClassification {
            actions: Vec::new(),
            error: ParameterError::Unsupported,
        };
    }
    ParameterClassification {
        actions: tokens
            .iter()
            .map(|token| {
                if token.action == ParameterAction::UnaryNegation
                    && token.kind == LiteralKind::String
                {
                    ParameterAction::Normal
                } else {
                    token.action
                }
            })
            .collect(),
        error: ParameterError::None,
    }
}

pub fn unchecked_parameter_types(
    offsets: &[usize],
    representations: &[ParameterRepresentation],
) -> Vec<ParameterTypeCheck> {
    let offsets: HashSet<usize> = offsets.iter().copied().collect();
    representations
        .iter()
        .enumerate()
        .map(|(i, &representation)| {
            if !offsets.contains(&i) {
                ParameterTypeCheck::Check
            } else if representation == ParameterRepresentation::Extended {
                ParameterTypeCheck::SkipExtendedType
            } else {
                ParameterTypeCheck::SkipType
            }
        })
        .collect()
}

pub fn rewrite_parameter_sql(
    sql: &[u8],
    tokens: &[ParameterToken<'_>],
    fold_in: bool,
) -> ParameterSql {
    let rewrite = || -> Result<Vec<u8>> {
        let mut result = Vec::with_capacity(sql.len());
        let mut position = 0;
        for token in tokens {
            if token.action == ParameterAction::Normal {
                continue;
            }
            if token.sql_offset < position || sql.get(token.sql_offset) != Some(&b'?') {
                return Err(ParameterError::Unsupported);
            }
            result.extend_from_slice(&sql[position..token.sql_offset]);
            match token.action {
                ParameterAction::Preserve => result.extend_from_slice(token.source),
                ParameterAction::UnaryNegation => result.push(b'?'),
                ParameterAction::Subtraction => {
                    let mut text = token
                        .text
                        .iter()
                        .copied()
                        .skip_while(u8::is_ascii_whitespace);
                    if text.next() != Some(b'-') {
                        return Err(ParameterError::Unsupported);
                    }
                    result.push(b'-');
                    let spaces = text.take_while(u8::is_ascii_whitespace).count();
                    result.resize(result.len() + spaces, b' ');
                    result.push(b'?');
                }
                _ => return Err(ParameterError::InvalidArgument),
            }
            position = token.sql_offset + 1;
        }
        result.extend_from_slice(&sql[position..]);
        Ok(result)
    };
    match rewrite() {
        Ok(sql) if fold_in => fold_parameter_in_lists(&sql),
        Ok(sql) => ParameterSql {
            sql,
            folded_in_list: false,
            error: ParameterError::None,
        },
        Err(error) => ParameterSql {
            sql: Vec::new(),
            folded_in_list: false,
            error,
        },
    }
}

pub fn fold_parameter_in_lists(sql: &[u8]) -> ParameterSql {
    fn prefix(byte: u8) -> bool {
        matches!(byte, b' ' | b'\n' | b'\r' | b'\t')
    }
    fn vector(sql: &[u8], start: usize) -> Option<(usize, usize)> {
        let mut level = 0;
        let mut questions = 1;
        for (i, &byte) in sql.iter().enumerate().skip(start) {
            match byte {
                b' ' => {}
                b'(' => {
                    level += 1;
                    if level == 2 {
                        questions = 0;
                    }
                }
                _ if level == 0 || level > 2 => return None,
                b')' => {
                    level -= 1;
                    if level == 0 {
                        return Some((i + 1, questions));
                    }
                }
                b'?' => {
                    if level == 2 {
                        questions += 1;
                    }
                }
                b',' => {}
                _ => return None,
            }
        }
        None
    }
    let mut result = ParameterSql {
        sql: Vec::with_capacity(sql.len()),
        folded_in_list: false,
        error: ParameterError::None,
    };
    let mut copied = 0;
    let mut search = 0;
    while search + 3 < sql.len() {
        let found = (search..sql.len().saturating_sub(3)).find(|&i| {
            prefix(sql[i])
                && sql[i + 1].eq_ignore_ascii_case(&b'i')
                && sql[i + 2].eq_ignore_ascii_case(&b'n')
                && (prefix(sql[i + 3]) || sql[i + 3] == b'(')
        });
        let Some(found) = found else {
            break;
        };
        let start = found + if sql[found + 3] == b'(' { 3 } else { 4 };
        if let Some((end, questions)) = vector(sql, start) {
            result.sql.extend_from_slice(&sql[copied..start]);
            result
                .sql
                .extend_from_slice(if questions > 1 { b"((...))" } else { b"(...)" });
            result.folded_in_list = true;
            copied = end;
            search = end;
        } else {
            search = start;
        }
    }
    result.sql.extend_from_slice(&sql[copied..]);
    result
}
