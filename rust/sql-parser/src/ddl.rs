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

use crate::{
    bridge::ffi::{ViewCheck, ViewDefinition},
    node::Node,
};

pub(crate) struct View {
    pub relation: Node,
    pub definer: Option<Node>,
    pub columns: Vec<Node>,
    pub query: Node,
    pub replace: bool,
    pub alter: bool,
    pub check: u64,
}

impl View {
    pub fn into_node(self) -> Node {
        let mut children = vec![self.relation, self.query];
        children.extend(self.definer);
        if !self.columns.is_empty() {
            children.push(Node::new(Kind::ColumnList, self.columns));
        }
        if self.replace {
            children.push(Node::new(Kind::Replace, vec![]));
        }
        children.push(Node::new(Kind::ViewCheckOption,
            vec![Node::new(Kind::HiddenInt, vec![]).value(self.check)]));
        Node::new(Kind::CreateView, children).value(u64::from(self.alter))
    }
}

pub(crate) fn view_definition(node: &Node) -> ViewDefinition<'_> {
    ViewDefinition {
        relation: child(node, Kind::Relation).unwrap(),
        definer: list(node, Kind::Definer),
        columns: list(node, Kind::ColumnList),
        query: child(node, Kind::Select).unwrap(),
        replace: child(node, Kind::Replace).is_some(),
        alter: node.value != 0,
        check: match child(node, Kind::ViewCheckOption).unwrap().children[0].value {
            0 => ViewCheck::None,
            1 => ViewCheck::Local,
            2 => ViewCheck::Cascaded,
            _ => unreachable!(),
        },
    }
}

use crate::bridge::ffi::{
    AlterSubpartitionAddition, AnalyzeAction, AnalyzeDefinition, ColumnDefinition, ColumnGeneration, ColumnPosition, ColumnSkipIndexSyntax,
    ForeignKeyDefinition, JsonStorageDefinition, RestoreDatabaseDefinition,
    IndexDefinition, IndexKind, IndexMethod, PartitionDefinition, PartitionElement, PartitionKind,
    PartitionValues, ReferenceAction, ReferenceEvent, ReferentialAction, TableDefinition,
    TableOrganization, TableRowFormat, TableSelectMode,
};
use crate::{
    node::Kind,
    query::{child, list},
};

pub(crate) enum OutlineBody {
    Statement { statement: Node, target: Option<Node> },
    SqlId { value: Node, hint: Node },
}

pub(crate) struct Outline {
    pub kind: Kind,
    pub relation: Node,
    pub body: Option<OutlineBody>,
    pub format: bool,
    pub replace: bool,
}

impl Outline {
    pub fn into_node(self) -> Node {
        let mut children = vec![self.relation];
        match self.body {
            Some(OutlineBody::Statement { statement, target }) => {
                children.push(statement);
                if let Some(target) = target {
                    children.push(Node::new(Kind::OutlineTarget, vec![target]));
                }
            }
            Some(OutlineBody::SqlId { value, hint }) => children.extend([value, hint]),
            None => {}
        }
        Node::new(self.kind, children)
            .value(u64::from(self.format) | (u64::from(self.replace) << 1))
    }
}

pub(crate) fn outline_definition(node: &Node) -> crate::bridge::ffi::OutlineDefinition<'_> {
    use crate::bridge::ffi::{OutlineDefinition, OutlineSource};
    let source = if node.kind == Kind::DropOutline {
        OutlineSource::None
    } else if node.children[1].kind == Kind::String {
        OutlineSource::SqlId
    } else {
        OutlineSource::Statement
    };
    OutlineDefinition {
        relation: &node.children[0],
        format: node.value & 1 != 0,
        replace: node.value & 2 != 0,
        source,
        has_target: child(node, Kind::OutlineTarget).is_some(),
        valid_hint: source == OutlineSource::SqlId && !node.children[2].children.is_empty(),
    }
}

pub(crate) fn outline_statement(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn outline_target(node: &Node) -> &Node {
    &child(node, Kind::OutlineTarget).unwrap().children[0]
}
pub(crate) fn outline_sql_id(node: &Node) -> &[u8] {
    node.children[1].text.as_bytes()
}
pub(crate) fn outline_hint(node: &Node) -> &Node {
    &node.children[2]
}

pub(crate) struct CreateTable {
    pub relation: Node,
    pub elements: Vec<Node>,
    pub options: Vec<Node>,
    pub partition: Option<Node>,
    pub query: Option<TableQuery>,
    pub temporary: bool,
    pub if_not_exists: bool,
}

pub(crate) struct TableQuery {
    pub query: Node,
    pub modifier: Option<Kind>,
    pub hints: Vec<Node>,
}

impl CreateTable {
    pub fn into_node(self) -> Node {
        let mut children = vec![self.relation];
        if !self.elements.is_empty() {
            children.push(Node::new(Kind::TableElements, self.elements));
        }
        if !self.options.is_empty() {
            children.push(Node::new(Kind::TableOptions, self.options));
        }
        children.extend(self.partition);
        if let Some(query) = self.query {
            children.push(query.query);
            if !query.hints.is_empty() {
                children.push(Node::new(Kind::Hints, query.hints));
            }
            if let Some(modifier) = query.modifier {
                children.push(Node::new(modifier, vec![]));
            }
        }
        if self.temporary {
            children.push(Node::new(Kind::Temporary, vec![]));
        }
        if self.if_not_exists {
            children.push(Node::new(Kind::IfNotExists, vec![]));
        }
        Node::new(Kind::CreateTable, children)
    }
}

pub(crate) fn table_definition(node: &Node) -> TableDefinition<'_> {
    TableDefinition {
        relation: child(node, Kind::Relation).unwrap(),
        elements: list(node, Kind::TableElements),
        options: list(node, Kind::TableOptions),
        hints: list(node, Kind::Hints),
        temporary: child(node, Kind::Temporary).is_some(),
        if_not_exists: child(node, Kind::IfNotExists).is_some(),
        select_mode: if child(node, Kind::Select).is_none() {
            TableSelectMode::None
        } else if child(node, Kind::Ignore).is_some() {
            TableSelectMode::Ignore
        } else if child(node, Kind::Replace).is_some() {
            TableSelectMode::Replace
        } else {
            TableSelectMode::Select
        },
        has_partition: child(node, Kind::PartitionOption).is_some(),
    }
}
pub(crate) fn table_definition_query(node: &Node) -> &Node {
    child(node, Kind::Select).unwrap()
}
pub(crate) fn table_definition_partition(node: &Node) -> &Node {
    &child(node, Kind::PartitionOption).unwrap().children[0]
}
pub(crate) struct TableLike {
    pub relation: Node,
    pub source: Node,
    pub temporary: bool,
    pub if_not_exists: bool,
}

impl TableLike {
    pub fn into_node(self) -> Node {
        let mut children = vec![self.relation, Node::new(Kind::TableLikeSource, vec![self.source])];
        if self.temporary {
            children.push(Node::new(Kind::Temporary, vec![]));
        }
        if self.if_not_exists {
            children.push(Node::new(Kind::IfNotExists, vec![]));
        }
        Node::new(Kind::CreateTableLike, children)
    }
}

pub(crate) fn table_like_relation(node: &Node) -> &Node {
    child(node, Kind::Relation).unwrap()
}
pub(crate) fn table_like_source(node: &Node) -> &Node {
    &child(node, Kind::TableLikeSource).unwrap().children[0]
}
pub(crate) fn table_like_temporary(node: &Node) -> bool {
    child(node, Kind::Temporary).is_some()
}
pub(crate) fn table_like_if_not_exists(node: &Node) -> bool {
    child(node, Kind::IfNotExists).is_some()
}

pub(crate) struct Column {
    pub name: Node,
    pub data_type: Node,
    pub tail: ColumnTail,
}

#[derive(Default)]
pub(crate) struct ColumnTail {
    pub attributes: Vec<Node>,
    pub generation: Option<GeneratedColumn>,
    pub position: Option<Node>,
    pub collation: Option<Node>,
}

pub(crate) struct GeneratedColumn {
    pub expression: Node,
    pub storage: Option<Node>,
    pub always: bool,
}

impl Column {
    pub fn into_node(self) -> Node {
        let mut data_type = self.data_type;
        data_type.children.extend(self.tail.collation);
        let mut children = vec![self.name, data_type];
        let last_default = self.tail.attributes.iter().rposition(|attr| attr.kind == Kind::ColumnDefault);
        let attributes: Vec<_> = self.tail.attributes.into_iter().enumerate()
            .filter_map(|(index, attr)| {
                (attr.kind != Kind::Missing
                    && (attr.kind != Kind::ColumnDefault || Some(index) == last_default)).then_some(attr)
            }).collect();
        if !attributes.is_empty() {
            children.push(Node::new(Kind::ColumnAttributes, attributes));
        }
        if let Some(generation) = self.tail.generation {
            children.push(Node::new(
                Kind::GeneratedColumn,
                vec![generation.expression],
            ));
            children.extend(generation.storage);
            if generation.always {
                children.push(Node::new(Kind::Always, vec![]));
            }
        }
        children.extend(self.tail.position);
        Node::new(Kind::ColumnDefinition, children)
    }
}

fn column_position(node: &Node) -> Option<&Node> {
    node.children.iter().find(|child| {
        matches!(
            child.kind,
            Kind::ColumnFirst | Kind::ColumnBefore | Kind::ColumnAfter
        )
    })
}

pub(crate) fn column_definition(node: &Node) -> ColumnDefinition<'_> {
    ColumnDefinition {
        name: child(node, Kind::Column).unwrap(),
        data_type: node
            .children
            .iter()
            .find(|child| {
                matches!(
                    child.kind,
                    Kind::DataType | Kind::CollectionType | Kind::EnumType | Kind::SetType
                )
            })
            .unwrap(),
        attributes: list(node, Kind::ColumnAttributes),
        generation: if child(node, Kind::GeneratedColumn).is_none() {
            ColumnGeneration::None
        } else if child(node, Kind::StoredColumn).is_some() {
            ColumnGeneration::Stored
        } else {
            ColumnGeneration::Virtual
        },
        position: match column_position(node).map(|position| position.kind) {
            Some(Kind::ColumnFirst) => ColumnPosition::First,
            Some(Kind::ColumnBefore) => ColumnPosition::Before,
            Some(Kind::ColumnAfter) => ColumnPosition::After,
            _ => ColumnPosition::None,
        },
    }
}
pub(crate) fn column_generation_expression(node: &Node) -> &Node {
    &child(node, Kind::GeneratedColumn).unwrap().children[0]
}
pub(crate) fn column_position_name(node: &Node) -> &Node {
    &column_position(node).unwrap().children[0]
}
pub(crate) fn column_attribute_expression(attribute: &Node) -> &Node {
    &attribute.children[0]
}
pub(crate) fn column_attribute_text(attribute: &Node) -> &[u8] {
    if attribute.kind == Kind::Collation {
        attribute.text.as_bytes()
    } else {
        attribute.children[0].text.as_bytes()
    }
}
pub(crate) fn column_srid(attribute: &Node) -> u64 {
    attribute.children[0].value
}
pub(crate) fn column_skip_index(attribute: &Node) -> ColumnSkipIndexSyntax {
    ColumnSkipIndexSyntax {
        min_max: child(attribute, Kind::SkipIndexMinMax).is_some(),
        sum: child(attribute, Kind::SkipIndexSum).is_some(),
    }
}

pub(crate) struct Index {
    pub kind: u64,
    pub name: Option<Node>,
    pub table: Option<Node>,
    pub columns: Vec<Node>,
    pub options: Node,
    pub method: Option<Node>,
    pub partition: Option<Node>,
    pub if_not_exists: bool,
    pub hints: Vec<Node>,
}

impl Index {
    pub fn into_node(self) -> Node {
        let kind = if self.table.is_some() { Kind::CreateIndex } else { Kind::Index };
        let mut children = Vec::new();
        if let Some(name) = self.name {
            children.push(Node::new(Kind::IndexName, vec![name]));
        }
        if let Some(table) = self.table {
            children.push(Node::new(Kind::IndexTable, vec![table]));
        }
        children.push(Node::new(Kind::IndexColumns, self.columns));
        if self.options.kind != Kind::Missing {
            children.push(self.options);
        }
        children.extend(self.method);
        children.extend(self.partition);
        if self.if_not_exists {
            children.push(Node::new(Kind::IfNotExists, vec![]));
        }
        if !self.hints.is_empty() {
            children.push(Node::new(Kind::Hints, self.hints));
        }
        Node::new(kind, children).value(self.kind)
    }
}

pub(crate) fn index_definition(node: &Node) -> IndexDefinition<'_> {
    IndexDefinition {
        columns: list(node, Kind::IndexColumns),
        options: list(node, Kind::TableOptions),
        kind: match node.value {
            0 => IndexKind::Normal,
            1 => IndexKind::Unique,
            2 => IndexKind::Spatial,
            3 => IndexKind::Fulltext,
            6 => IndexKind::Vector,
            _ => unreachable!(),
        },
        method: if child(node, Kind::UsingHash).is_some() {
            IndexMethod::Hash
        } else {
            IndexMethod::Default
        },
        has_name: child(node, Kind::IndexName).is_some(),
        has_partition: child(node, Kind::PartitionOption).is_some(),
        if_not_exists: child(node, Kind::IfNotExists).is_some(),
    }
}
pub(crate) fn index_definition_name(node: &Node) -> &Node {
    &child(node, Kind::IndexName).unwrap().children[0]
}
pub(crate) fn index_definition_table(node: &Node) -> &Node {
    &child(node, Kind::IndexTable).unwrap().children[0]
}
pub(crate) fn index_definition_partition(node: &Node) -> &Node {
    &child(node, Kind::PartitionOption).unwrap().children[0]
}
pub(crate) fn index_column_expression(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn index_column_has_prefix(node: &Node) -> bool {
    node.children[1].kind != Kind::Missing
}
pub(crate) fn index_column_prefix(node: &Node) -> u64 {
    node.children[1].value
}
pub(crate) fn primary_key_columns(node: &Node) -> &[Node] {
    &node.children[0].children
}
pub(crate) fn primary_key_method(node: &Node) -> IndexMethod {
    if node.children[1].kind == Kind::UsingHash {
        IndexMethod::Hash
    } else {
        IndexMethod::Default
    }
}

pub(crate) struct Partition {
    pub kind: Kind,
    pub expression: Node,
    pub start: usize,
    pub end: usize,
    pub count: Option<Node>,
    pub elements: Option<Node>,
    pub subpartition: Option<Node>,
    pub template: bool,
}

impl Partition {
    pub fn into_node(self) -> Node {
        let mut children = vec![Node::new(Kind::PartitionExpression, vec![self.expression])];
        let count = self.count.map(|count| Node::new(Kind::PartitionCount, vec![count]));
        if matches!(self.kind, Kind::HashPartition | Kind::KeyPartition) {
            children.extend(count);
            children.extend(self.elements);
            children.extend(self.subpartition);
        } else {
            children.extend(self.elements);
            children.extend(self.subpartition);
            children.extend(count);
        }
        Node::new(self.kind, children).value(u64::from(self.template)).at(self.start, self.end)
    }
}

fn subpartition(node: &Node) -> Option<&Node> {
    node.children.iter().find(|child| matches!(child.kind,
        Kind::HashPartition | Kind::KeyPartition | Kind::RangePartition |
        Kind::RangeColumnsPartition | Kind::ListPartition | Kind::ListColumnsPartition))
}

fn partition_elements(node: &Node) -> &[Node] {
    node.children.iter().find(|child| matches!(child.kind,
        Kind::Partitions | Kind::HashSubpartitions | Kind::RangeSubpartitions | Kind::ListSubpartitions))
        .map_or(&[], |elements| &elements.children)
}

pub(crate) fn partition_definition(node: &Node) -> PartitionDefinition<'_> {
    let count = child(node, Kind::PartitionCount);
    PartitionDefinition {
        kind: match node.kind {
            Kind::HashPartition => PartitionKind::Hash,
            Kind::KeyPartition => PartitionKind::Key,
            Kind::RangePartition => PartitionKind::Range,
            Kind::RangeColumnsPartition => PartitionKind::RangeColumns,
            Kind::ListPartition => PartitionKind::List,
            Kind::ListColumnsPartition => PartitionKind::ListColumns,
            _ => unreachable!(),
        },
        expression: &child(node, Kind::PartitionExpression).unwrap().children[0],
        elements: partition_elements(node),
        has_count: count.is_some(),
        count: count.map_or(0, |count| count.children[0].value),
        has_subpartition: subpartition(node).is_some(),
        is_template: node.value != 0,
    }
}
pub(crate) fn partition_subpartition(node: &Node) -> &Node {
    subpartition(node).unwrap()
}

pub(crate) struct PartitionEntry {
    pub kind: Kind,
    pub name: Node,
    pub values: Node,
    pub engine: Option<Node>,
    pub subpartitions: Option<Node>,
}

impl PartitionEntry {
    pub fn into_node(self) -> Node {
        let mut children = vec![self.name];
        if self.values.kind != Kind::Missing {
            children.push(self.values);
        }
        children.extend(self.engine);
        children.extend(self.subpartitions);
        Node::new(self.kind, children)
    }
}

pub(crate) fn partition_element(node: &Node) -> PartitionElement<'_> {
    PartitionElement {
        name: child(node, Kind::Relation).unwrap(),
        engine: list(node, Kind::Engine),
        values: list(node, Kind::ExprList),
        value_kind: if child(node, Kind::Default).is_some() {
            PartitionValues::Default
        } else if child(node, Kind::ExprList).is_none() {
            PartitionValues::None
        } else if node.kind == Kind::RangePartitionElement {
            PartitionValues::Range
        } else {
            PartitionValues::List
        },
        subpartitions: partition_elements(node),
    }
}
pub(crate) fn check_constraint_has_name(node: &Node) -> bool {
    node.children[0].kind != Kind::Missing && node.children[0].children[0].kind != Kind::Missing
}
pub(crate) fn check_constraint_name(node: &Node) -> &Node {
    &node.children[0].children[0]
}
pub(crate) fn check_constraint_expression(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn check_constraint_enforced(node: &Node) -> bool {
    node.children[2].kind == Kind::Enforced
}
pub(crate) struct ForeignKey {
    pub columns: Vec<Node>,
    pub parent: Node,
    pub parent_columns: Vec<Node>,
    pub actions: Vec<Node>,
    pub constraint: Option<Node>,
    pub index_name: Option<Node>,
}

impl ForeignKey {
    pub fn into_node(self) -> Node {
        let mut children = vec![
            Node::new(Kind::ColumnList, self.columns),
            Node::new(Kind::ForeignKeyParent, vec![self.parent, Node::new(Kind::ColumnList, self.parent_columns)]),
        ];
        if !self.actions.is_empty() {
            children.push(Node::new(Kind::ReferenceOptions, self.actions));
        }
        children.extend(self.constraint.filter(|name| name.children[0].kind != Kind::Missing));
        children.extend(self.index_name);
        Node::new(Kind::ForeignKey, children)
    }
}

pub(crate) fn foreign_key_definition(node: &Node) -> ForeignKeyDefinition<'_> {
    let parent = child(node, Kind::ForeignKeyParent).unwrap();
    ForeignKeyDefinition {
        columns: list(node, Kind::ColumnList),
        parent: child(parent, Kind::Relation).unwrap(),
        parent_columns: list(parent, Kind::ColumnList),
        actions: list(node, Kind::ReferenceOptions),
        has_name: child(node, Kind::CheckConstraint).is_some(),
        has_index_name: child(node, Kind::Ident).is_some(),
    }
}
pub(crate) fn foreign_key_name(node: &Node) -> &Node {
    &child(node, Kind::CheckConstraint).unwrap().children[0]
}
pub(crate) fn referential_action(node: &Node) -> ReferentialAction {
    ReferentialAction {
        event: if node.value != 0 {
            ReferenceEvent::Delete
        } else {
            ReferenceEvent::Update
        },
        action: match node.children[0].kind {
            Kind::Restrict => ReferenceAction::Restrict,
            Kind::Cascade => ReferenceAction::Cascade,
            Kind::SetNull => ReferenceAction::SetNull,
            Kind::NoAction => ReferenceAction::NoAction,
            Kind::SetDefault => ReferenceAction::SetDefault,
            _ => unreachable!(),
        },
    }
}
pub(crate) fn json_storage_definition(node: &Node) -> JsonStorageDefinition<'_> {
    JsonStorageDefinition { column: &node.children[0], chunks: &node.children[1..] }
}
pub(crate) fn table_option_value(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn table_option_text(node: &Node) -> &[u8] {
    if node.kind == Kind::Collation {
        node.text.as_bytes()
    } else {
        node.children[0].text.as_bytes()
    }
}
pub(crate) fn table_option_number(node: &Node) -> u64 {
    node.children[0].value
}
pub(crate) fn table_option_engine_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn table_option_names(node: &Node) -> &[Node] {
    &node.children
}
pub(crate) fn table_option_properties(node: &Node) -> &[Node] {
    &node.children
}
pub(crate) fn table_option_charset(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn table_option_organization(node: &Node) -> TableOrganization {
    if node.children[0].kind == Kind::OrganizationIndex {
        TableOrganization::Index
    } else {
        TableOrganization::Heap
    }
}
pub(crate) fn table_option_row_format(node: &Node) -> TableRowFormat {
    match node.children[0].value {
        1 => TableRowFormat::Redundant,
        2 => TableRowFormat::Compact,
        3 => TableRowFormat::Dynamic,
        4 => TableRowFormat::Compressed,
        5 => TableRowFormat::Condensed,
        _ => unreachable!(),
    }
}
pub(crate) fn index_option_global(node: &Node) -> bool {
    node.children[0].value != 0
}

pub(crate) fn database_name(node: &Node) -> &Node {
    &node.children[if matches!(node.kind, Kind::CreateDatabase | Kind::DropDatabase) {
        1
    } else {
        0
    }]
}
pub(crate) fn database_has_name(node: &Node) -> bool {
    database_name(node).kind != Kind::Missing
}
pub(crate) fn database_options(node: &Node) -> &[Node] {
    list(node, Kind::DatabaseOptions)
}
pub(crate) fn database_if_not_exists(node: &Node) -> bool {
    node.children[0].kind == Kind::IfNotExists
}
pub(crate) fn drop_if_exists(node: &Node) -> bool {
    child(node, Kind::IfExists).is_some()
}
pub(crate) fn drop_temporary(node: &Node) -> bool {
    child(node, Kind::Temporary).is_some()
}
pub(crate) fn drop_relations(node: &Node) -> &[Node] {
    if node.kind == Kind::DropView {
        &node.children[2].children
    } else {
        list(node, Kind::TableList)
    }
}
pub(crate) fn fork_source(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn fork_destination(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn merge_table_definition(node: &Node) -> crate::bridge::ffi::MergeTableDefinition<'_> {
    use crate::bridge::ffi::{MergeStrategy, MergeTableDefinition};
    MergeTableDefinition {
        incoming: &node.children[0],
        current: &node.children[1],
        strategy: match node.value {
            0 => MergeStrategy::Fail,
            1 => MergeStrategy::Theirs,
            2 => MergeStrategy::Ours,
            _ => unreachable!(),
        },
    }
}
pub(crate) fn diff_table_definition(node: &Node) -> crate::bridge::ffi::DiffTableDefinition<'_> {
    crate::bridge::ffi::DiffTableDefinition {
        incoming: &node.children[0],
        current: &node.children[1],
    }
}
pub(crate) fn rename_actions(node: &Node) -> &[Node] {
    &node.children
}
pub(crate) fn rename_source(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn rename_destination(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn truncate_relation(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn drop_index_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn drop_index_relation(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn alter_table_relation(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_table_actions(node: &Node) -> &[Node] {
    &node.children[1].children
}
pub(crate) fn alter_column_definitions(node: &Node) -> &[Node] {
    match node.kind {
        Kind::ColumnAdd => &node.children[..node.children.len() - alter_column_storage(node).len()],
        Kind::ColumnModify => &node.children,
        Kind::ColumnChange => &node.children[1..],
        _ => unreachable!(),
    }
}
pub(crate) fn alter_column_storage(node: &Node) -> &[Node] {
    match node.children.last() {
        Some(storage) if storage.kind == Kind::JsonStorage => std::slice::from_ref(storage),
        _ => &[],
    }
}
pub(crate) fn alter_column_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_column_new_name(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn alter_column_has_default(node: &Node) -> bool {
    node.children[1].kind == Kind::ColumnDefault
}
pub(crate) fn alter_column_default(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn alter_constraint_names(node: &Node) -> &[Node] {
    &node.children
}
pub(crate) fn alter_constraint_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_constraint_check_only(node: &Node) -> bool {
    node.value != 0
}
pub(crate) fn alter_constraint_enforced(node: &Node) -> bool {
    node.children[1].kind == Kind::Enforced
}
pub(crate) fn alter_partition_names(node: &Node) -> &[Node] {
    &node.children[0].children
}
pub(crate) fn alter_partition_definition(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_partition_additions(node: &Node) -> &[Node] {
    &node.children
}
pub(crate) fn alter_subpartition_addition(node: &Node) -> AlterSubpartitionAddition<'_> {
    AlterSubpartitionAddition {
        parent: &node.children[0],
        elements: &node.children[1].children,
    }
}
pub(crate) fn alter_partition_exchange_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_partition_exchange_table(node: &Node) -> &Node {
    &node.children[1]
}
pub(crate) fn alter_table_renamed_relation(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_charset_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn alter_charset_has_collation(node: &Node) -> bool {
    node.children[1].kind != Kind::Missing
}
pub(crate) fn alter_charset_collation(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn optimize_relations(node: &Node) -> &[Node] {
    &node.children[0].children
}
pub(crate) fn analyze_definition(node: &Node) -> AnalyzeDefinition<'_> {
    let buckets = child(node, Kind::StatsBucketCount);
    AnalyzeDefinition {
        action: match node.value {
            0 => AnalyzeAction::Gather,
            1 => AnalyzeAction::UpdateHistogram,
            2 => AnalyzeAction::DropHistogram,
            _ => unreachable!(),
        },
        relations: list(node, Kind::TableList),
        columns: list(node, Kind::ColumnList),
        bucket_count: buckets.map_or(&[], |buckets| buckets.children.as_slice()),
    }
}
pub(crate) fn purge_relation(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn restore_database_definition(node: &Node) -> RestoreDatabaseDefinition<'_> {
    RestoreDatabaseDefinition {
        source: node.children[0].text.as_bytes(),
        target: node.children.get(1).map_or(&[][..], |target| target.text.as_bytes()),
    }
}

pub(crate) fn restore_table_source(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn restore_table_has_target(node: &Node) -> bool {
    node.children.len() > 1
}
pub(crate) fn restore_table_target(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn partition_element_values(node: &Node) -> &Node {
    node.children.iter().find(|child| matches!(child.kind, Kind::ExprList | Kind::Default)).unwrap()
}

pub(crate) fn alter_table_options(node: &Node) -> &[Node] {
    &node.children
}
pub(crate) fn alter_index_name(node: &Node) -> &Node {
    &node.children[0]
}
pub(crate) fn index_visible(node: &Node) -> bool {
    node.value == 0
}
pub(crate) fn alter_index_parallel(node: &Node) -> u64 {
    node.children[1].value
}
pub(crate) fn alter_index_new_name(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn index_column_has_direction(node: &Node) -> bool {
    node.children[2].kind != Kind::Missing
}
pub(crate) fn primary_key_has_comment(node: &Node) -> bool {
    node.children
        .get(2)
        .is_some_and(|value| value.kind != Kind::Missing)
}
pub(crate) fn primary_key_comment(node: &Node) -> &Node {
    &node.children[2]
}
pub(crate) fn index_hints(node: &Node) -> &[Node] {
    list(node, Kind::Hints)
}
