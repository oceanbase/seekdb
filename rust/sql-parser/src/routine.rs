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

use crate::node::{Kind, Node};

pub(crate) fn do_expressions(node: &Node) -> &[Node] {
    &node.children
}

pub(crate) fn block_declarations(node: &Node) -> &[Node] {
    &node.children[0].children
}

pub(crate) fn block_statements(node: &Node) -> &[Node] {
    match node.kind {
        Kind::PlBlock => &node.children[1].children,
        Kind::PlStatements
        | Kind::PlDeclarations
        | Kind::PackageStatements
        | Kind::PackageBodyStatements => &node.children,
        _ => unreachable!("statement sequence"),
    }
}

pub(crate) fn labeled_body(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn block_label(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn block_has_end_label(node: &Node) -> bool {
    node.children[2].kind != Kind::Missing
}

pub(crate) fn block_end_label(node: &Node) -> &Node {
    &node.children[2]
}

pub(crate) fn declaration_names(node: &Node) -> &[Node] {
    &node.children[0].children
}

pub(crate) fn declaration_type(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn declaration_has_default(node: &Node) -> bool {
    node.children[2].kind != Kind::Missing
}

pub(crate) fn declaration_default(node: &Node) -> &Node {
    &node.children[2].children[0]
}

pub(crate) fn control_condition(node: &Node) -> &Node {
    match node.kind {
        Kind::PlRepeat => &node.children[1],
        Kind::PlIf | Kind::PlCase | Kind::PlWhen | Kind::PlWhile => &node.children[0],
        _ => unreachable!("conditional statement"),
    }
}

pub(crate) fn control_body(node: &Node) -> &Node {
    match node.kind {
        Kind::PlLoop | Kind::PlRepeat | Kind::PlElse => &node.children[0],
        Kind::PlIf | Kind::PlWhen | Kind::PlWhile => &node.children[1],
        _ => unreachable!("control body"),
    }
}

pub(crate) fn control_has_alternative(node: &Node) -> bool {
    node.children[2].kind != Kind::Missing
}

pub(crate) fn control_alternative(node: &Node) -> &Node {
    &node.children[2].children[0]
}

pub(crate) fn case_branches(node: &Node) -> &[Node] {
    &node.children[1].children
}

pub(crate) fn return_expression(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn loop_target(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn cursor_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn cursor_query(node: &Node) -> &Node {
    &node.children[3]
}

pub(crate) fn cursor_fetch_target(mut value: Node) -> Node {
    while value.kind == Kind::Parenthesized {
        value = value.children.pop().unwrap();
    }
    value
}

pub(crate) fn cursor_fetch_targets(node: &Node) -> &[Node] {
    &node.children[1].children[0].children
}

pub(crate) fn handler_conditions(node: &Node) -> &[Node] {
    &node.children[0].children
}

pub(crate) fn handler_body(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn handler_continues(node: &Node) -> bool {
    node.value == 1
}

pub(crate) fn condition_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn condition_declaration_value(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn condition_is_sqlstate(node: &Node) -> bool {
    match node.kind {
        Kind::SqlState => true,
        Kind::PlCondition => node.children[0].kind == Kind::SqlState,
        _ => false,
    }
}

pub(crate) fn condition_sqlstate(node: &Node) -> &[u8] {
    let state = if node.kind == Kind::PlCondition {
        &node.children[0]
    } else {
        node
    };
    state.children[0].text.as_bytes()
}

pub(crate) fn condition_error_code(node: &Node) -> u64 {
    node.children[0].value
}

pub(crate) fn signal_has_condition(node: &Node) -> bool {
    node.children[0].kind != Kind::Missing
}

pub(crate) fn signal_condition(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn signal_items(node: &Node) -> &[Node] {
    &node.children[1].children
}

pub(crate) fn signal_item_name(node: &Node) -> &str {
    match node.value {
        0 => "CLASS_ORIGIN",
        1 => "SUBCLASS_ORIGIN",
        2 => "CONSTRAINT_CATALOG",
        3 => "CONSTRAINT_SCHEMA",
        4 => "CONSTRAINT_NAME",
        5 => "CATALOG_NAME",
        6 => "SCHEMA_NAME",
        7 => "TABLE_NAME",
        8 => "COLUMN_NAME",
        9 => "CURSOR_NAME",
        10 => "MESSAGE_TEXT",
        11 => "MYSQL_ERRNO",
        _ => unreachable!("signal item"),
    }
}

pub(crate) fn signal_item_expression(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn routine_call_names(node: &Node) -> &[Node] {
    let names = &node.children[0].children;
    &names[names
        .iter()
        .position(|part| part.kind != Kind::Missing)
        .unwrap()..]
}

pub(crate) fn routine_call_arguments(node: &Node) -> &[Node] {
    &node.children[1].children
}

pub(crate) fn routine_sql_statement(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn package_declarations(node: &Node) -> &[Node] {
    let index = if node.kind == Kind::Package { 2 } else { 1 };
    &node.children[index].children
}

pub(crate) fn package_has_initialization(node: &Node) -> bool {
    node.kind == Kind::PackageBody && node.children[2].kind != Kind::Missing
}

pub(crate) fn package_initialization(node: &Node) -> &Node {
    &node.children[2]
}

pub(crate) fn subprogram_declaration(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn subprogram_definition(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn subprogram_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn subprogram_parameters(node: &Node) -> &[Node] {
    &node.children[1].children
}

pub(crate) fn subprogram_return_type(node: &Node) -> &Node {
    &node.children[2]
}

pub(crate) fn routine_parameter_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn routine_parameter_type(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn routine_parameter_input(node: &Node) -> bool {
    node.value != 1
}

pub(crate) fn routine_parameter_output(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn routine_parameter_has_default(node: &Node) -> bool {
    node.children
        .get(2)
        .is_some_and(|value| value.kind != Kind::Missing)
}

pub(crate) fn routine_parameter_default(node: &Node) -> &Node {
    &node.children[2].children[0]
}

pub(crate) fn interface_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn row_type_names(node: &Node) -> &[Node] {
    let names = &node.children[0].children[0].children;
    &names[names
        .iter()
        .position(|part| part.kind != Kind::Missing)
        .unwrap()..]
}

pub(crate) fn routine_characteristics(node: &Node) -> &[Node] {
    let index = match node.kind {
        Kind::CreateProcedure => 3,
        Kind::CreateFunction => 4,
        Kind::AlterProcedure | Kind::AlterFunction | Kind::Package => 1,
        _ => unreachable!("routine characteristics"),
    };
    &node.children[index].children
}

pub(crate) fn routine_invoker_rights(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn routine_data_access(node: &Node) -> crate::bridge::ffi::RoutineDataAccess {
    use crate::bridge::ffi::RoutineDataAccess;
    match node.value {
        0 => RoutineDataAccess::ContainsSql,
        1 => RoutineDataAccess::NoSql,
        2 => RoutineDataAccess::ReadsSqlData,
        3 => RoutineDataAccess::ModifiesSqlData,
        _ => unreachable!("routine data access"),
    }
}

pub(crate) fn routine_name(node: &Node) -> &Node {
    match node.kind {
        Kind::CreateProcedure | Kind::CreateFunction => &node.children[1],
        Kind::AlterProcedure | Kind::AlterFunction | Kind::DropProcedure | Kind::DropFunction => {
            &node.children[0]
        }
        _ => unreachable!("routine command"),
    }
}

pub(crate) fn routine_has_definer(node: &Node) -> bool {
    node.children[0].kind != Kind::Missing
}

pub(crate) fn routine_definer(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn routine_body(node: &Node) -> &Node {
    node.children.last().unwrap()
}

pub(crate) fn routine_parameters(node: &Node) -> &[Node] {
    &node.children[2].children
}

pub(crate) fn routine_return_type(node: &Node) -> &Node {
    &node.children[3]
}

pub(crate) fn routine_if_not_exists(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn routine_if_exists(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn package_definition(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn package_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn package_has_end_name(node: &Node) -> bool {
    node.children[3].kind != Kind::Missing
}

pub(crate) fn package_end_name(node: &Node) -> &Node {
    &node.children[3]
}

pub(crate) fn package_drop_body(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn package_replace(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn trigger_source(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn trigger_has_definer(node: &Node) -> bool {
    node.children[0].kind != Kind::Missing
}

pub(crate) fn trigger_definer(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn trigger_name(node: &Node) -> &Node {
    &node.children[0]
}

pub(crate) fn trigger_definition(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn trigger_table(node: &Node) -> &Node {
    &node.children[1]
}

pub(crate) fn trigger_body(node: &Node) -> &Node {
    &node.children[3]
}

pub(crate) fn trigger_after(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn trigger_event(node: &Node) -> crate::bridge::ffi::NodeKind {
    node.children[0].kind
}

pub(crate) fn trigger_has_order(node: &Node) -> bool {
    node.children[2].kind != Kind::Missing
}

pub(crate) fn trigger_order_name(node: &Node) -> &Node {
    &node.children[2].children[0]
}

pub(crate) fn trigger_order_follows(node: &Node) -> bool {
    node.children[2].value == 1
}

pub(crate) fn trigger_if_not_exists(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn trigger_if_exists(node: &Node) -> bool {
    node.value != 0
}

pub(crate) fn routine_parameter_list(node: &Node) -> &Node {
    &node.children[2]
}
