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
    bridge::ffi::{StatisticsColumnKind, StatisticsScope, StatisticsSizeKind},
    node::{Kind, Node},
};

pub(crate) fn statistics_clauses(options: &Node) -> &[Node] {
    &options.children
}

pub(crate) fn statistics_scope(clause: &Node) -> StatisticsScope {
    match clause.kind {
        Kind::StatsAll => match clause.children[0].value {
            0 => StatisticsScope::All,
            1 => StatisticsScope::Indexed,
            2 => StatisticsScope::Hidden,
            _ => unreachable!(),
        },
        Kind::StatsColumns => StatisticsScope::Columns,
        _ => unreachable!(),
    }
}

pub(crate) fn statistics_columns(clause: &Node) -> &[Node] {
    &clause.children
}

pub(crate) fn statistics_size(clause_or_column: &Node) -> StatisticsSizeKind {
    let size = &clause_or_column.children[1];
    match size.kind {
        Kind::Missing => StatisticsSizeKind::Omitted,
        Kind::StatsBucketCount => StatisticsSizeKind::Count,
        Kind::HiddenInt => match size.value {
            0 => StatisticsSizeKind::Auto,
            1 => StatisticsSizeKind::Repeat,
            2 => StatisticsSizeKind::SkewOnly,
            _ => unreachable!(),
        },
        _ => unreachable!(),
    }
}

pub(crate) fn statistics_bucket_count(clause_or_column: &Node) -> &Node {
    &clause_or_column.children[1].children[0]
}

pub(crate) fn statistics_column_kind(column: &Node) -> StatisticsColumnKind {
    match column.children[0].kind {
        Kind::Missing => StatisticsColumnKind::Default,
        Kind::Column => StatisticsColumnKind::Named,
        Kind::StatsExtension => StatisticsColumnKind::Extension,
        _ => unreachable!(),
    }
}

pub(crate) fn statistics_column_name(column: &Node) -> &str {
    column.children[0].children[1].text.as_str()
}
