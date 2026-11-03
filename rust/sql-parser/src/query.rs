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
    WindowDistributionOptions, AggregationSort, AliasForm, ExplainDisplay, ExplainFormat, IndexHintKind, InsertSource, IntoPosition,
    JoinCondition, JoinKind, LockKind, LockWait, MergeDirection, NullPlacement, ParallelPolicy, PlanCachePolicy,
    ReadConsistency, SampleMethod, SampleScope, SetOperator, SortDirection,
};
use crate::node::{Kind, Node};

#[derive(Clone, Copy)]
pub struct Query<'a>(&'a Node);

#[derive(Clone, Copy)]
pub struct Projection<'a>(&'a Node);

impl Node {
    pub fn as_query(&self) -> Option<Query<'_>> {
        (self.kind == Kind::Select).then_some(Query(self))
    }
}

impl<'a> Query<'a> {
    pub fn projections(self) -> impl ExactSizeIterator<Item = Projection<'a>> + 'a {
        query_projection(self.0).iter().map(Projection)
    }

    pub fn sources(self) -> &'a [Node] {
        query_sources(self.0)
    }

    pub fn filter(self) -> Option<&'a Node> {
        query_has_filter(self.0).then(|| query_filter(self.0))
    }
}

impl<'a> Projection<'a> {
    pub(crate) fn new(node: &'a Node) -> Self {
        Self(node)
    }

    pub(crate) fn node(self) -> &'a Node {
        self.0
    }

    pub fn expression(self) -> &'a Node {
        projection_expression(self.0)
    }

    pub fn alias(self) -> Option<&'a Node> {
        (projection_alias_form(self.0) != AliasForm::Absent).then(|| projection_alias(self.0))
    }
}

pub(crate) struct RelationReferences<'a> {
    pending: Vec<&'a Node>,
    current: Option<&'a Node>,
}

pub(crate) fn relation_references(query: &Node) -> Box<RelationReferences<'_>> {
    Box::new(RelationReferences {
        pending: vec![query],
        current: None,
    })
}

impl<'a> RelationReferences<'a> {
    pub(crate) fn advance_relation(&mut self) -> bool {
        while let Some(node) = self.pending.pop() {
            self.pending.extend(node.children.iter().rev());
            if node.kind == Kind::Relation {
                self.current = Some(node);
                return true;
            }
        }
        self.current = None;
        false
    }

    pub(crate) fn relation_reference(&self) -> &'a Node {
        self.current.unwrap()
    }
}

pub(crate) fn child(node: &Node, kind: Kind) -> Option<&Node> {
    node.children.iter().find(|child| child.kind == kind)
}

pub(crate) fn list(node: &Node, kind: Kind) -> &[Node] {
    child(node, kind).map_or(&[], |child| child.children.as_slice())
}

fn required(node: &Node, kind: Kind) -> &Node {
    child(node, kind).unwrap()
}

pub(crate) fn explain_statement(explain: &Node) -> &Node {
    explain.children.last().unwrap()
}
pub(crate) fn explain_format(explain: &Node) -> ExplainFormat {
    explain
        .children
        .iter()
        .find_map(|node| match node.kind {
            Kind::ExplainBasic => Some(ExplainFormat::Basic),
            Kind::ExplainOutline => Some(ExplainFormat::Outline),
            Kind::ExplainExtended => Some(ExplainFormat::Extended),
            Kind::ExplainExtendedNoAddr => Some(ExplainFormat::ExtendedNoAddr),
            Kind::ExplainPartitions => Some(ExplainFormat::Partitions),
            Kind::ExplainPlanRegress => Some(ExplainFormat::PlanRegress),
            Kind::ExplainTraditional => Some(ExplainFormat::Traditional),
            Kind::ExplainJson => Some(ExplainFormat::Json),
            _ => None,
        })
        .unwrap_or(ExplainFormat::Default)
}
pub(crate) fn explain_display(explain: &Node) -> ExplainDisplay {
    if child(explain, Kind::ExplainPretty).is_some() {
        ExplainDisplay::Pretty
    } else if child(explain, Kind::ExplainPrettyColor).is_some() {
        ExplainDisplay::PrettyColor
    } else {
        ExplainDisplay::Default
    }
}
pub(crate) fn explain_has_into_table(explain: &Node) -> bool {
    child(explain, Kind::ExplainIntoTable).is_some()
}
pub(crate) fn explain_into_table(explain: &Node) -> &Node {
    &required(explain, Kind::ExplainIntoTable).children[0]
}
pub(crate) fn explain_has_statement_id(explain: &Node) -> bool {
    child(explain, Kind::ExplainStatementId).is_some()
}
pub(crate) fn explain_statement_id(explain: &Node) -> &Node {
    &required(explain, Kind::ExplainStatementId).children[0]
}
pub(crate) fn identifier_is_quoted(identifier: &Node) -> bool {
    identifier.value != 0
}

pub(crate) fn query_options(query: &Node) -> &[Node] {
    list(query, Kind::SelectOptions)
}
pub(crate) fn query_projection(query: &Node) -> &[Node] {
    list(query, Kind::ProjectList)
}
pub(crate) fn query_sources(query: &Node) -> &[Node] {
    list(query, Kind::FromList)
}
pub(crate) fn query_has_filter(query: &Node) -> bool {
    child(query, Kind::Where).is_some()
}
pub(crate) fn query_filter(query: &Node) -> &Node {
    &required(query, Kind::Where).children[0]
}
pub(crate) fn query_has_grouping(query: &Node) -> bool {
    child(query, Kind::GroupBy).is_some()
}
pub(crate) fn query_grouping(query: &Node) -> &Node {
    required(query, Kind::GroupBy)
}
pub(crate) fn query_has_having(query: &Node) -> bool {
    child(query, Kind::Having).is_some()
}
pub(crate) fn query_having(query: &Node) -> &Node {
    &required(query, Kind::Having).children[0]
}
pub(crate) fn query_windows(query: &Node) -> &[Node] {
    list(query, Kind::NamedWindows)
}
pub(crate) fn query_order(query: &Node) -> &[Node] {
    list(query, Kind::Order)
}
fn limit(query: &Node) -> Option<&Node> {
    query
        .children
        .iter()
        .find(|child| matches!(child.kind, Kind::Limit | Kind::CommaLimit))
}
pub(crate) fn query_has_limit(query: &Node) -> bool {
    limit(query).is_some()
}
pub(crate) fn query_limit(query: &Node) -> &Node {
    limit(query).unwrap()
}
pub(crate) fn query_has_lock(query: &Node) -> bool {
    child(query, Kind::QueryLock).is_some()
}
pub(crate) fn query_lock(query: &Node) -> &Node {
    &required(query, Kind::QueryLock).children[0]
}
pub(crate) fn query_hints(query: &Node) -> &[Node] {
    list(query, Kind::Hints)
}
pub(crate) fn query_ctes(query: &Node) -> &[Node] {
    list(query, Kind::WithClause)
}
pub(crate) fn query_recursive(query: &Node) -> bool {
    child(query, Kind::WithClause).is_some_and(|with| with.value != 0)
}
fn set(query: &Node) -> Option<&Node> {
    query.children.iter().find(|child| {
        matches!(
            child.kind,
            Kind::Union | Kind::UnionAll | Kind::Intersect | Kind::Except
        )
    })
}
pub(crate) fn query_set_operator(query: &Node) -> SetOperator {
    match set(query).map(|set| set.kind) {
        None => SetOperator::None,
        Some(Kind::Union) => SetOperator::Union,
        Some(Kind::UnionAll) => SetOperator::UnionAll,
        Some(Kind::Intersect) => SetOperator::Intersect,
        Some(Kind::Except) => SetOperator::Except,
        _ => unreachable!(),
    }
}
pub(crate) fn query_set_operands(query: &Node) -> &[Node] {
    set(query).map_or(&[], |set| set.children.as_slice())
}
pub(crate) fn into(query: &Node) -> Option<&Node> {
    query.children.iter().find(|child| {
        matches!(
            child.kind,
            Kind::IntoVariables | Kind::TrailingIntoVariables
                | Kind::IntoOutfile | Kind::TrailingIntoOutfile
                | Kind::IntoDumpfile | Kind::TrailingIntoDumpfile
        )
    })
}
pub(crate) fn query_into_position(query: &Node) -> IntoPosition {
    match into(query).map(|into| into.kind) {
        None => IntoPosition::Absent,
        Some(Kind::IntoVariables | Kind::IntoOutfile | Kind::IntoDumpfile) => IntoPosition::AfterProjection,
        Some(Kind::TrailingIntoVariables | Kind::TrailingIntoOutfile | Kind::TrailingIntoDumpfile) => IntoPosition::AfterQuery,
        _ => unreachable!(),
    }
}
pub(crate) fn query_into_variables(query: &Node) -> &[Node] {
    into(query).map_or(&[], |into| list(into, Kind::IntoVariableList))
}
pub(crate) fn query_approximate(query: &Node) -> bool {
    child(query, Kind::Approximate).is_some()
}
pub(crate) fn query_vector_parameters(query: &Node) -> &[Node] {
    list(query, Kind::VectorParameters)
}
pub(crate) fn vector_parameter_name(parameter: &Node) -> &str {
    parameter.children[0].text.as_str()
}
pub(crate) fn vector_parameter_value(parameter: &Node) -> &Node {
    &parameter.children[1]
}
pub(crate) fn query_ignore(query: &Node) -> bool {
    child(query, Kind::Ignore).is_some()
}
pub(crate) fn query_assignments(query: &Node) -> &[Node] {
    list(query, Kind::AssignList)
}
pub(crate) fn delete_targets(query: &Node) -> &[Node] {
    list(query, Kind::DeleteTargets)
}

pub(crate) fn projection_expression(projection: &Node) -> &Node {
    let expression = &projection.children[0];
    if matches!(expression.kind, Kind::Alias | Kind::StringAlias) {
        &expression.children[0]
    } else {
        expression
    }
}
pub(crate) fn projection_alias_form(projection: &Node) -> AliasForm {
    match projection.children[0].kind {
        Kind::Alias => AliasForm::Identifier,
        Kind::StringAlias => AliasForm::String,
        _ => AliasForm::Absent,
    }
}
pub(crate) fn projection_alias(projection: &Node) -> &Node {
    &projection.children[0].children[1]
}
pub(crate) fn grouping_keys(grouping: &Node) -> &[Node] {
    list(grouping, Kind::SortList)
}
pub(crate) fn grouping_with_rollup(grouping: &Node) -> bool {
    child(grouping, Kind::Rollup).is_some()
}
pub(crate) fn sort_expression(sort: &Node) -> &Node {
    &sort.children[0]
}
pub(crate) fn sort_direction(sort: &Node) -> SortDirection {
    match sort.children.get(1).map(|direction| direction.kind) {
        Some(Kind::Asc) => SortDirection::Ascending,
        Some(Kind::Desc) => SortDirection::Descending,
        _ => SortDirection::Unspecified,
    }
}
pub(crate) fn sort_null_placement(sort: &Node) -> NullPlacement {
    match sort.value {
        1 => NullPlacement::First,
        2 => NullPlacement::Last,
        _ => NullPlacement::Default,
    }
}
pub(crate) fn limit_count(limit: &Node) -> &Node {
    &limit.children[usize::from(limit.kind == Kind::CommaLimit)]
}
pub(crate) fn limit_has_offset(limit: &Node) -> bool {
    limit.children.len() == 2
}
pub(crate) fn limit_offset(limit: &Node) -> &Node {
    &limit.children[usize::from(limit.kind == Kind::Limit)]
}
pub(crate) fn limit_comma_form(limit: &Node) -> bool {
    limit.kind == Kind::CommaLimit
}
pub(crate) fn lock_kind(lock: &Node) -> LockKind {
    if lock.kind == Kind::SkipLocked {
        LockKind::SkipLocked
    } else {
        LockKind::Wait
    }
}
pub(crate) fn lock_wait(lock: &Node) -> LockWait {
    if lock.value == u64::MAX && lock.text.is_empty() {
        LockWait::Default
    } else if lock.start == lock.end {
        LockWait::NoWait
    } else {
        LockWait::Duration
    }
}
pub(crate) fn lock_wait_seconds(lock: &Node) -> &str {
    lock.text.as_str()
}
pub(crate) fn cte_name(cte: &Node) -> &Node {
    required(cte, Kind::Ident)
}
pub(crate) fn cte_columns(cte: &Node) -> &[Node] {
    list(cte, Kind::ColumnList)
}
pub(crate) fn cte_query(cte: &Node) -> &Node {
    required(cte, Kind::Select)
}
pub(crate) fn assignment_target(assignment: &Node) -> &Node {
    &assignment.children[0]
}
pub(crate) fn assignment_value(assignment: &Node) -> &Node {
    &assignment.children[1]
}
pub(crate) fn insert_target(insert: &Node) -> &Node {
    &required(insert, Kind::InsertTarget).children[0]
}
pub(crate) fn insert_columns(insert: &Node) -> &[Node] {
    list(insert, Kind::InsertColumns)
}
pub(crate) fn insert_source(insert: &Node) -> InsertSource {
    if child(insert, Kind::InsertRows).is_some() {
        InsertSource::Rows
    } else if child(insert, Kind::InsertQuery).is_some() {
        InsertSource::Query
    } else if child(insert, Kind::InsertAliasedRows).is_some() {
        InsertSource::AliasedRows
    } else {
        InsertSource::Assignments
    }
}
pub(crate) fn insert_query(insert: &Node) -> &Node {
    &child(insert, Kind::InsertQuery)
        .unwrap_or_else(|| required(insert, Kind::InsertAliasedRows)).children[0]
}
pub(crate) fn insert_row_alias_table(insert: &Node) -> &Node {
    &query_sources(insert_query(insert))[0]
}
pub(crate) fn insert_rows(insert: &Node) -> &[Node] {
    list(insert, Kind::InsertRows)
}
pub(crate) fn insert_assignments(insert: &Node) -> &[Node] {
    list(insert, Kind::InsertAssignments)
}
pub(crate) fn insert_on_duplicate(insert: &Node) -> &[Node] {
    list(insert, Kind::DuplicateKeyAssignments)
}
pub(crate) fn insert_replace(insert: &Node) -> bool {
    child(insert, Kind::Replace).is_some()
}
pub(crate) fn row_elements(row: &Node) -> &[Node] {
    &row.children
}

pub(crate) fn name_parts(name: &Node) -> &[Node] {
    if name.kind == Kind::Ident {
        return std::slice::from_ref(name);
    }
    let name = child(name, Kind::CallName).unwrap_or(name);
    let first = name
        .children
        .iter()
        .position(|part| part.kind == Kind::Ident)
        .unwrap_or(name.children.len());
    let count = name.children[first..]
        .iter()
        .take_while(|part| part.kind == Kind::Ident)
        .count();
    &name.children[first..first + count]
}
pub(crate) fn name_is_wildcard(name: &Node) -> bool {
    name.is_wildcard()
}
pub(crate) fn relation_has_database(relation: &Node) -> bool {
    name_parts(relation).len() == 2
}
pub(crate) fn relation_database(relation: &Node) -> &Node {
    &name_parts(relation)[0]
}
pub(crate) fn relation_name(relation: &Node) -> &Node {
    name_parts(relation).last().unwrap()
}
pub(crate) fn table_source(table: &Node) -> &Node {
    &table.children[0]
}
fn table_alias_node(table: &Node) -> Option<&Node> {
    child(table, Kind::TableAlias)
}
pub(crate) fn table_has_alias(table: &Node) -> bool {
    table_alias_node(table).is_some()
}
pub(crate) fn table_alias(table: &Node) -> &Node {
    &table_alias_node(table).unwrap().children[0]
}
pub(crate) fn table_columns(table: &Node) -> &[Node] {
    list(table, Kind::DerivedColumns)
}
pub(crate) fn table_lateral(table: &Node) -> bool {
    child(table, Kind::Lateral).is_some()
}
pub(crate) fn table_parenthesized(table: &Node) -> bool {
    child(table, Kind::ParenthesizedTable).is_some()
}
pub(crate) fn table_partitions(table: &Node) -> &[Node] {
    child(table, Kind::UsePartition).map_or(&[], |partition| list(partition, Kind::Names))
}
pub(crate) fn table_index_hints(table: &Node) -> &[Node] {
    list(table, Kind::TableIndexHints)
}
pub(crate) fn table_has_sample(table: &Node) -> bool {
    child(table, Kind::Sample).is_some()
}
pub(crate) fn table_sample(table: &Node) -> &Node {
    required(table, Kind::Sample)
}
pub(crate) fn table_has_snapshot(table: &Node) -> bool {
    child(table, Kind::Snapshot).is_some()
}
pub(crate) fn table_snapshot(table: &Node) -> &Node {
    &required(table, Kind::Snapshot).children[0]
}
pub(crate) fn sample_method(sample: &Node) -> SampleMethod {
    match sample.children[0].value {
        1 => SampleMethod::Row,
        2 => SampleMethod::Block,
        3 => SampleMethod::Hybrid,
        _ => unreachable!(),
    }
}
pub(crate) fn sample_scope(sample: &Node) -> SampleScope {
    match sample.children[3].kind {
        Kind::All => SampleScope::All,
        Kind::SampleBase => SampleScope::Base,
        Kind::SampleIncremental => SampleScope::Incremental,
        _ => unreachable!(),
    }
}
pub(crate) fn sample_percent(sample: &Node) -> &Node {
    &sample.children[1]
}
pub(crate) fn sample_has_seed(sample: &Node) -> bool {
    sample.children[2].kind != Kind::Missing
}
pub(crate) fn sample_seed(sample: &Node) -> &Node {
    &sample.children[2]
}
pub(crate) fn index_hint_kind(hint: &Node) -> IndexHintKind {
    match hint.children[0].kind {
        Kind::Use => IndexHintKind::Use,
        Kind::Force => IndexHintKind::Force,
        Kind::Ignore => IndexHintKind::Ignore,
        _ => unreachable!(),
    }
}
pub(crate) fn index_hint_names(hint: &Node) -> &[Node] {
    list(hint, Kind::Names)
}
pub(crate) fn joined_table(kind: Kind, left: Node, right: Node, condition: Node) -> Result<Node, &'static str> {
    let mut children = vec![Node::new(kind, vec![]), left, right];
    if condition.kind != Kind::Missing {
        children.push(condition);
    }
    Node::new(Kind::JoinedTable, children).expression(0, 0)
}

pub(crate) fn associate_join(kind: Kind, left: Node, mut right: Node, condition: Node) -> Result<Node, &'static str> {
    if matches!(kind, Kind::InnerJoin | Kind::StraightJoin)
        && condition.kind == Kind::Missing
        && right.kind == Kind::JoinedTable
        && !table_parenthesized(&right)
    {
        let right_left = std::mem::replace(&mut right.children[1], Node::missing());
        right.children[1] = associate_join(kind, left, right_left, condition)?;
        Node::new(Kind::JoinedTable, right.into_children()).expression(0, 0)
    } else {
        joined_table(kind, left, right, condition)
    }
}

pub(crate) fn join_kind(join: &Node) -> JoinKind {
    match join.children[0].kind {
        Kind::InnerJoin => JoinKind::Inner,
        Kind::StraightJoin => JoinKind::Straight,
        Kind::LeftJoin => JoinKind::Left,
        Kind::RightJoin => JoinKind::Right,
        Kind::FullJoin => JoinKind::Full,
        _ => unreachable!(),
    }
}
pub(crate) fn join_left(join: &Node) -> &Node {
    &join.children[1]
}
pub(crate) fn join_right(join: &Node) -> &Node {
    &join.children[2]
}
pub(crate) fn join_condition(join: &Node) -> JoinCondition {
    if child(join, Kind::JoinOn).is_some() {
        JoinCondition::On
    } else if child(join, Kind::ColumnList).is_some() {
        JoinCondition::Using
    } else if child(join, Kind::NaturalJoin).is_some() {
        JoinCondition::Natural
    } else {
        JoinCondition::None
    }
}
pub(crate) fn join_predicate(join: &Node) -> &Node {
    &required(join, Kind::JoinOn).children[0]
}
pub(crate) fn join_columns(join: &Node) -> &[Node] {
    list(join, Kind::ColumnList)
}
pub(crate) fn values_rows(values: &Node) -> &[Node] {
    list(values, Kind::ValuesRows)
}
pub(crate) fn unnest_arguments(table: &Node) -> &[Node] {
    list(table, Kind::ExprList)
}
pub(crate) fn collection_table_expression(table: &Node) -> &Node {
    &table.children[0]
}

fn query_block(hint: &Node) -> Option<&Node> {
    if matches!(
        hint.kind,
        Kind::QbName
            | Kind::Leading
            | Kind::Ordered
            | Kind::PqDistribute
            | Kind::PqDistributeWindow
            | Kind::JoinFilter
            | Kind::NoJoinFilter
            | Kind::PartJoinFilter
            | Kind::NoPartJoinFilter
            | Kind::PqMap
            | Kind::PqSet
            | Kind::PqSubquery
            | Kind::PqGroupBy
            | Kind::PqDistinct
            | Kind::TableParallel
            | Kind::TableDynamicSampling
            | Kind::FullScan
            | Kind::UseDas
            | Kind::NoUseDas
            | Kind::IndexScan
            | Kind::NoIndex
            | Kind::IndexAsc
            | Kind::IndexDesc
            | Kind::UnionMerge
            | Kind::UseNl
            | Kind::NoUseNl
            | Kind::UseNlMaterialization
            | Kind::NoUseNlMaterialization
            | Kind::UseHash
            | Kind::NoUseHash
            | Kind::UseMerge
            | Kind::NoUseMerge
            | Kind::UseConcat
            | Kind::NoExpand
            | Kind::MergeView
            | Kind::NoMergeView
            | Kind::SemiToInner
            | Kind::EliminateJoin
            | Kind::NoEliminateJoin
            | Kind::LeftToAnti
            | Kind::NoLeftToAnti
            | Kind::PlaceGroupBy
            | Kind::NoPlaceGroupBy
            | Kind::WindowMagic
            | Kind::NoWindowMagic
            | Kind::Materialize
            | Kind::CoalesceSubqueries
            | Kind::CountToExists
            | Kind::NoCountToExists
    ) || hint.children.len() == 1
    {
        hint.children
            .first()
            .filter(|node| node.kind == Kind::Ident)
    } else {
        None
    }
}
pub(crate) fn hint_has_query_block(hint: &Node) -> bool {
    query_block(hint).is_some()
}
pub(crate) fn hint_query_block(hint: &Node) -> &Node {
    query_block(hint).unwrap()
}
pub(crate) fn hint_tables(hint: &Node) -> &[Node] {
    if matches!(hint.kind, Kind::HintTables | Kind::JoinHintTables) {
        return &hint.children;
    }
    if let Some(tables) = child(hint, Kind::JoinHintTables) {
        return &tables.children;
    }
    if let Some(table) = hint
        .children
        .iter()
        .find(|node| matches!(node.kind, Kind::HintRelation | Kind::HintTables))
    {
        return std::slice::from_ref(table);
    }
    &[]
}
pub(crate) fn hint_table_relation(table: &Node) -> &Node {
    &table.children[0]
}
pub(crate) fn hint_table_has_query_block(table: &Node) -> bool {
    table.children[1].kind != Kind::Missing
}
pub(crate) fn hint_table_query_block(table: &Node) -> &Node {
    &table.children[1]
}
pub(crate) fn hint_filter_table(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_filter_left_tables(hint: &Node) -> &[Node] {
    if hint.children[2].kind == Kind::Missing { &[] } else { &hint.children[2..3] }
}
pub(crate) fn hint_filter_has_pushdown_table(hint: &Node) -> bool {
    hint.children[3].kind != Kind::Missing
}
pub(crate) fn hint_filter_pushdown_table(hint: &Node) -> &Node {
    &hint.children[3]
}
pub(crate) fn hint_leading(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_leading_group(group: &Node) -> &[Node] {
    &group.children
}
pub(crate) fn hint_query_block_groups(hint: &Node) -> &[Node] {
    list(hint, Kind::HintQbGroups)
}
pub(crate) fn hint_query_block_names(group: &Node) -> &[Node] {
    &group.children
}
pub(crate) fn hint_subquery_names(hint: &Node) -> &[Node] {
    list(hint, Kind::HintQbNames)
}
pub(crate) fn hint_distribution_methods(hint: &Node) -> &[Node] {
    match hint.kind {
        Kind::PqSet | Kind::PqSubquery => list(hint, Kind::DistributeMethods),
        Kind::PqDistribute if hint.children[2].kind != Kind::Missing => &hint.children[2..4],
        Kind::PqGroupBy | Kind::PqDistinct => &hint.children[1..2],
        _ => &[],
    }
}
pub(crate) fn hint_window_distributions(hint: &Node) -> &[Node] {
    &hint.children[1..]
}
pub(crate) fn window_distribution_indexes(option: &Node) -> &[Node] {
    &option.children[1..]
}
pub(crate) fn window_distribution_method(option: &Node) -> Kind {
    option.children[0].kind
}
pub(crate) fn window_distribution_options(option: &Node) -> WindowDistributionOptions {
    WindowDistributionOptions {
        partition_sort: option.value & 1 != 0,
        pushdown: option.value & 2 != 0,
        topn: option.value & 4 != 0,
    }
}
pub(crate) fn hint_has_target(hint: &Node) -> bool {
    hint.children
        .get(1)
        .is_some_and(|target| target.kind == Kind::Ident)
}
pub(crate) fn hint_target(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_aggregation_sort(hint: &Node) -> AggregationSort {
    match hint.value {
        0 => AggregationSort::Normal,
        1 => AggregationSort::Partition,
        u64::MAX => AggregationSort::Default,
        _ => unreachable!(),
    }
}
pub(crate) fn hint_parallel_policy(hint: &Node) -> ParallelPolicy {
    match hint.value {
        0 => ParallelPolicy::Explicit,
        1 => ParallelPolicy::Manual,
        2 => ParallelPolicy::Automatic,
        _ => unreachable!(),
    }
}
pub(crate) fn hint_parallel_degree(hint: &Node) -> &Node {
    hint.children.last().unwrap()
}
pub(crate) fn hint_dop_fragment(hint: &Node) -> &Node {
    &hint.children[0]
}
pub(crate) fn hint_dop_degree(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_allocation_targets(hint: &Node) -> &[Node] {
    &hint.children
}
pub(crate) fn hint_timeout(hint: &Node) -> &Node {
    &hint.children[0]
}
pub(crate) fn hint_log_level(hint: &Node) -> &[u8] {
    hint.children[0].text.as_bytes()
}
pub(crate) fn hint_load_batch_size(hint: &Node) -> &Node {
    &hint.children[0]
}
pub(crate) fn hint_has_load_buffer_size(hint: &Node) -> bool {
    hint.children.len() > 1
}
pub(crate) fn hint_load_buffer_size(hint: &Node) -> &[u8] {
    hint.children.get(1).map_or(&[][..], |node| node.text.as_bytes())
}
pub(crate) fn hint_topk_minimum(hint: &Node) -> &Node {
    &hint.children[0]
}
pub(crate) fn hint_topk_precision(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_sampling_level(hint: &Node) -> &Node {
    &hint.children[if hint.kind == Kind::TableDynamicSampling {
        2
    } else {
        0
    }]
}
pub(crate) fn hint_has_sampling_blocks(hint: &Node) -> bool {
    hint.kind == Kind::TableDynamicSampling && hint.children[3].kind != Kind::Missing
}
pub(crate) fn hint_sampling_blocks(hint: &Node) -> &Node {
    &hint.children[3]
}
pub(crate) fn hint_index_name(hint: &Node) -> &Node {
    &hint.children[2]
}
pub(crate) fn hint_index_names(hint: &Node) -> &[Node] {
    list(hint, Kind::Names)
}
pub(crate) fn hint_has_index_prefix(hint: &Node) -> bool {
    hint.children[3].kind != Kind::Missing
}
pub(crate) fn hint_index_prefix(hint: &Node) -> &Node {
    &hint.children[3]
}
pub(crate) fn hint_has_condition(hint: &Node) -> bool {
    hint.children[1].kind != Kind::Missing
}
pub(crate) fn hint_condition(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_merge_direction(hint: &Node) -> MergeDirection {
    if !hint_has_target(hint) {
        MergeDirection::Default
    } else if hint.value == 1 {
        MergeDirection::LessThan
    } else {
        MergeDirection::GreaterThan
    }
}
pub(crate) fn hint_read_consistency(hint: &Node) -> ReadConsistency {
    match hint.value {
        2 => ReadConsistency::Frozen,
        3 => ReadConsistency::Weak,
        4 => ReadConsistency::Strong,
        _ => unreachable!(),
    }
}
pub(crate) fn hint_plan_cache_policy(hint: &Node) -> PlanCachePolicy {
    match hint.value {
        1 => PlanCachePolicy::None,
        2 => PlanCachePolicy::Default,
        _ => unreachable!(),
    }
}
pub(crate) fn hint_parameter_name(hint: &Node) -> &Node {
    &hint.children[0]
}
pub(crate) fn hint_parameter_value(hint: &Node) -> &Node {
    &hint.children[1]
}
pub(crate) fn hint_has_sort_method(hint: &Node) -> bool {
    hint.children[2].kind != Kind::Missing
}
pub(crate) fn hint_sort_method(hint: &Node) -> &Node {
    &hint.children[2]
}

pub(crate) fn vector_parameters(option: &Node) -> &[Node] {
    &option.children
}

pub(crate) fn hybrid_search_table(table: &Node) -> &Node {
    &table.children[0]
}

pub(crate) fn hybrid_search_parameters(table: &Node) -> &Node {
    &table.children[1]
}
