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

use std::cmp::Ordering;

use crate::bridge::ffi;
use ffi::{
    Action as LocalAction, Aggregate as LocalAggregate, Compare as LocalCompare, Kind as LocalKind,
};
use std::sync::Arc;
const I: LocalKind = LocalKind::Integer;

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Cell {
    Null,
    Integer(i64),
    Text(Vec<u8>),
    Blob(Vec<u8>),
}
impl Cell {
    pub(crate) fn kind(&self) -> LocalKind {
        match self {
            Self::Null => LocalKind::Null,
            Self::Integer(_) => LocalKind::Integer,
            Self::Text(_) => LocalKind::Text,
            Self::Blob(_) => LocalKind::Blob,
        }
    }
}

#[derive(Debug)]
pub enum Error {
    Storage(kv::Error),
    Invalid,
    Duplicate,
}
impl From<kv::Error> for Error {
    fn from(e: kv::Error) -> Self {
        Self::Storage(e)
    }
}
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Clone)]
pub struct Schema {
    namespace_id: u8,
    kinds: Vec<LocalKind>,
    nullable: Vec<usize>,
    primary: Vec<usize>,
    indexes: Vec<usize>,
}
impl Schema {
    pub fn from_descriptor(descriptor: &ffi::Schema) -> Result<Self> {
        let schema = Self {
            namespace_id: descriptor.namespace_id,
            kinds: descriptor.columns.iter().map(|c| c.kind).collect(),
            nullable: descriptor
                .columns
                .iter()
                .enumerate()
                .filter_map(|(i, c)| c.nullable.then_some(i))
                .collect(),
            primary: descriptor.primary.iter().map(|&c| c as usize).collect(),
            indexes: descriptor.indexes.iter().map(|&c| c as usize).collect(),
        };
        schema.validate()?;
        Ok(schema)
    }
    fn validate(&self) -> Result<()> {
        if self.kinds.is_empty()
            || self.kinds.len() > u8::MAX as usize
            || self.primary.is_empty()
            || self
                .kinds
                .iter()
                .any(|&k| !matches!(k, LocalKind::Integer | LocalKind::Text | LocalKind::Blob))
            || self.nullable.iter().any(|&c| c >= self.kinds.len())
            || 2 + self.primary.len() * 8 > kv::MAX_KEY_SIZE
            || (!self.indexes.is_empty() && 11 + self.primary.len() * 8 > kv::MAX_KEY_SIZE)
        {
            return Err(Error::Invalid);
        }
        for columns in [&self.primary, &self.indexes] {
            for (i, &column) in columns.iter().enumerate() {
                if column >= self.kinds.len()
                    || self.kinds[column] != I
                    || self.nullable.contains(&column)
                    || columns[..i].contains(&column)
                {
                    return Err(Error::Invalid);
                }
            }
        }
        Ok(())
    }

    fn uses_index(&self, order: Option<usize>) -> bool {
        order.is_some_and(|column| column != self.primary[0] && self.indexes.contains(&column))
    }

    fn prefix(&self, order: Option<usize>) -> Result<Vec<u8>> {
        match order {
            None => Ok(vec![b'r', self.namespace_id]),
            Some(column) if column == self.primary[0] => Ok(vec![b'r', self.namespace_id]),
            Some(column) if self.indexes.contains(&column) => {
                Ok(vec![b'i', self.namespace_id, column as u8])
            }
            _ => Err(Error::Invalid),
        }
    }

    fn row_key(&self, row: &[Cell]) -> Result<Vec<u8>> {
        let mut key = self.prefix(None)?;
        for &column in &self.primary {
            if let Cell::Integer(n) = row[column] {
                key.extend(ordered(n));
            } else {
                return Err(Error::Invalid);
            }
        }
        Ok(key)
    }
    fn index_key(&self, column: usize, row: &[Cell], primary: &[u8]) -> Result<Vec<u8>> {
        let mut key = vec![b'i', self.namespace_id, column as u8];
        if let Cell::Integer(n) = row[column] {
            key.extend(ordered(n));
        } else {
            return Err(Error::Invalid);
        }
        key.extend(&primary[2..]);
        Ok(key)
    }
    fn validate_row(&self, row: &[Cell]) -> Result<()> {
        if row.len() != self.kinds.len() {
            return Err(Error::Invalid);
        }
        for (column, value) in row.iter().enumerate() {
            if value.kind() != self.kinds[column]
                && !(value == &Cell::Null && self.nullable.contains(&column))
            {
                return Err(Error::Invalid);
            }
        }
        Ok(())
    }
}
fn ordered(n: i64) -> [u8; 8] {
    ((n as u64) ^ (1 << 63)).to_be_bytes()
}

fn encode(row: &[Cell]) -> Result<Vec<u8>> {
    let mut result = vec![1, row.len() as u8];
    for value in row {
        result.push(value.kind().repr);
        match value {
            Cell::Null => {}
            Cell::Integer(n) => result.extend(n.to_le_bytes()),
            Cell::Text(bytes) | Cell::Blob(bytes) => {
                let length = u32::try_from(bytes.len()).map_err(|_| Error::Invalid)?;
                result.extend(length.to_le_bytes());
                result.extend(bytes);
            }
        }
    }
    if result.len() > kv::MAX_VALUE_SIZE {
        return Err(Error::Storage(kv::Error::ValueTooLarge));
    }
    Ok(result)
}
fn corrupt() -> Error {
    Error::Storage(kv::Error::Corrupt("invalid metadata record"))
}
fn decode(table: &Schema, bytes: &[u8]) -> Result<Vec<Cell>> {
    if bytes.len() < 2 || bytes[0] != 1 || bytes[1] as usize != table.kinds.len() {
        return Err(corrupt());
    }
    let mut input = &bytes[2..];
    fn take<'a>(input: &mut &'a [u8], n: usize) -> Result<&'a [u8]> {
        if input.len() < n {
            return Err(corrupt());
        }
        let (value, tail) = input.split_at(n);
        *input = tail;
        Ok(value)
    }
    let mut row = Vec::with_capacity(bytes[1] as usize);
    for _ in 0..bytes[1] {
        let kind = take(&mut input, 1)?[0];
        row.push(match kind {
            0 => Cell::Null,
            1 => Cell::Integer(i64::from_le_bytes(take(&mut input, 8)?.try_into().unwrap())),
            2 | 3 => {
                let n = u32::from_le_bytes(take(&mut input, 4)?.try_into().unwrap()) as usize;
                let bytes = take(&mut input, n)?.to_vec();
                if kind == 2 {
                    Cell::Text(bytes)
                } else {
                    Cell::Blob(bytes)
                }
            }
            _ => return Err(corrupt()),
        });
    }
    if !input.is_empty() {
        return Err(corrupt());
    }
    table.validate_row(&row).map_err(|_| corrupt())?;
    Ok(row)
}

#[derive(Clone)]
pub struct Predicate {
    pub column: usize,
    pub compare: LocalCompare,
    pub values: Vec<Cell>,
}
impl Predicate {
    fn matches(&self, row: &[Cell]) -> bool {
        let cell = &row[self.column];
        if *cell == Cell::Null {
            return false;
        }
        if self.compare == LocalCompare::In {
            return self.values.contains(cell);
        }
        if self.values[0] == Cell::Null {
            return false;
        }
        let order = cell.cmp(&self.values[0]);
        match self.compare {
            LocalCompare::Equal => order == Ordering::Equal,
            LocalCompare::NotEqual => order != Ordering::Equal,
            LocalCompare::Less => order == Ordering::Less,
            LocalCompare::LessEqual => order != Ordering::Greater,
            LocalCompare::Greater => order == Ordering::Greater,
            LocalCompare::GreaterEqual => order != Ordering::Less,
            LocalCompare::In => unreachable!(),
            _ => false,
        }
    }
}
#[derive(Clone)]
pub struct Query {
    pub table: Arc<Schema>,
    pub columns: Vec<usize>,
    pub predicates: Vec<Predicate>,
    pub order: Option<usize>,
    pub limit: Option<usize>,
    pub offset: usize,
    pub aggregate: LocalAggregate,
}
impl Query {
    /// Stream the transaction's own view. The visitor must not mutate it.
    pub fn visit_transaction(
        &self,
        tx: &kv::Transaction<'_>,
        mut visitor: impl FnMut(&[Cell]) -> bool,
    ) -> Result<()> {
        let mut range = self.range()?;
        let points = self.point_keys();
        let mut point_index = 0;
        let mut count = 0;
        let mut skipped = 0;
        let mut extreme: Option<i64> = None;
        loop {
            if self.aggregate == LocalAggregate::None && self.limit.is_some_and(|n| count >= n) {
                break;
            }
            let next = if let Some(points) = &points {
                let mut next = None;
                while let Some(key) = points.get(point_index) {
                    point_index += 1;
                    if let Some(value) = tx.get(key)? {
                        next = Some((key.clone(), value));
                        break;
                    }
                }
                next
            } else {
                tx.scan(range.clone())?.next().transpose()?
            };
            let Some((key, value)) = next else {
                break;
            };
            range.start = std::ops::Bound::Excluded(key);
            let bytes = if self.table.uses_index(self.order) {
                tx.get(&value)?.ok_or_else(corrupt)?
            } else {
                value
            };
            let row = decode(&self.table, &bytes)?;
            if !self.matches(&row) {
                continue;
            }
            if self.aggregate == LocalAggregate::None && skipped < self.offset {
                skipped += 1;
                continue;
            }
            count += 1;
            if self.aggregate == LocalAggregate::None {
                if !visitor(&self.project(&row)) {
                    break;
                }
            } else if let Some(&column) = self.columns.first() {
                if let Cell::Integer(n) = row[column] {
                    extreme = Some(match extreme {
                        None => n,
                        Some(old) => {
                            if self.aggregate == LocalAggregate::Min {
                                old.min(n)
                            } else {
                                old.max(n)
                            }
                        }
                    });
                }
            }
        }
        if self.aggregate != LocalAggregate::None {
            visitor(&[if self.aggregate == LocalAggregate::Count {
                Cell::Integer(count as i64)
            } else {
                extreme.map(Cell::Integer).unwrap_or(Cell::Null)
            }]);
        }
        Ok(())
    }
    #[cfg(test)]
    pub fn all(table: Arc<Schema>) -> Self {
        Self {
            table: table.clone(),
            columns: (0..table.kinds.len()).collect(),
            predicates: vec![],
            order: None,
            limit: None,
            offset: 0,
            aggregate: LocalAggregate::None,
        }
    }
    fn range(&self) -> Result<kv::Range> {
        let schema = &self.table;
        schema.validate()?;
        if !matches!(
            self.aggregate,
            LocalAggregate::None
                | LocalAggregate::Count
                | LocalAggregate::Min
                | LocalAggregate::Max
        ) {
            return Err(Error::Invalid);
        }
        if self.columns.iter().any(|&c| c >= schema.kinds.len()) {
            return Err(Error::Invalid);
        }
        for condition in &self.predicates {
            if !matches!(
                condition.compare,
                LocalCompare::Equal
                    | LocalCompare::NotEqual
                    | LocalCompare::Less
                    | LocalCompare::LessEqual
                    | LocalCompare::Greater
                    | LocalCompare::GreaterEqual
                    | LocalCompare::In
            ) || condition.column >= schema.kinds.len()
                || (condition.compare != LocalCompare::In && condition.values.len() != 1)
                || condition
                    .values
                    .iter()
                    .any(|v| v.kind() != schema.kinds[condition.column] && *v != Cell::Null)
            {
                return Err(Error::Invalid);
            }
        }
        if matches!(self.aggregate, LocalAggregate::Min | LocalAggregate::Max)
            && (self.columns.len() != 1 || schema.kinds[self.columns[0]] != I)
        {
            return Err(Error::Invalid);
        }
        let full_key = schema.primary.iter().all(|&column| {
            self.predicates
                .iter()
                .any(|p| p.column == column && p.compare == LocalCompare::Equal)
        });
        let effective_order = if full_key && !schema.uses_index(self.order) {
            None
        } else {
            self.order
        };
        let mut prefix = self.table.prefix(effective_order)?;
        let mut range_column = effective_order.or(Some(schema.primary[0]));
        if effective_order.is_none() || effective_order == Some(schema.primary[0]) {
            for &column in &schema.primary {
                let value = self
                    .predicates
                    .iter()
                    .find(|p| p.column == column && p.compare == LocalCompare::Equal);
                if let Some(Predicate { values, .. }) = value {
                    if let Cell::Integer(n) = values[0] {
                        prefix.extend(ordered(n));
                        range_column = schema
                            .primary
                            .iter()
                            .copied()
                            .skip_while(|&c| c != column)
                            .nth(1);
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
        }
        let mut range = kv::Range::prefix(&prefix);
        if let Some(column) = range_column {
            for predicate in self
                .predicates
                .iter()
                .filter(|p| p.column == column && p.values.len() == 1)
            {
                let Cell::Integer(n) = predicate.values[0] else {
                    continue;
                };
                let mut boundary = prefix.clone();
                boundary.extend(ordered(n));
                let upper = kv::Range::prefix(&boundary).end;
                match predicate.compare {
                    LocalCompare::Greater | LocalCompare::GreaterEqual | LocalCompare::Equal => {
                        let start = if predicate.compare == LocalCompare::Greater {
                            upper.clone()
                        } else {
                            std::ops::Bound::Included(boundary.clone())
                        };
                        let key = match start {
                            std::ops::Bound::Included(k) | std::ops::Bound::Excluded(k) => k,
                            std::ops::Bound::Unbounded => continue,
                        };
                        if matches!(&range.start,std::ops::Bound::Included(old) if old<&key) {
                            range.start = std::ops::Bound::Included(key);
                        }
                    }
                    _ => {}
                }
                match predicate.compare {
                    LocalCompare::Less | LocalCompare::LessEqual | LocalCompare::Equal => {
                        let end = if predicate.compare == LocalCompare::Less {
                            std::ops::Bound::Excluded(boundary)
                        } else {
                            upper
                        };
                        if let std::ops::Bound::Excluded(key) = end {
                            if matches!(&range.end,std::ops::Bound::Excluded(old) if old>&key) {
                                range.end = std::ops::Bound::Excluded(key);
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
        Ok(range)
    }
    fn point_keys(&self) -> Option<Vec<Vec<u8>>> {
        let schema = &self.table;
        if schema.primary.len() != 1 || self.order.is_some_and(|c| c != schema.primary[0]) {
            return None;
        }
        let predicate = self
            .predicates
            .iter()
            .find(|p| p.column == schema.primary[0] && p.compare == LocalCompare::In)?;
        let mut keys: Vec<_> = predicate
            .values
            .iter()
            .filter_map(|value| {
                if let Cell::Integer(n) = value {
                    let mut key = vec![b'r', self.table.namespace_id];
                    key.extend(ordered(*n));
                    Some(key)
                } else {
                    None
                }
            })
            .collect();
        keys.sort();
        keys.dedup();
        Some(keys)
    }
    fn matches(&self, row: &[Cell]) -> bool {
        self.predicates.iter().all(|p| p.matches(row))
    }
    fn project(&self, row: &[Cell]) -> Vec<Cell> {
        self.columns.iter().map(|&c| row[c].clone()).collect()
    }
}

pub struct RowCursor {
    snapshot: kv::Snapshot,
    points: Option<Vec<Vec<u8>>>,
    point_index: usize,
    query: Query,
    range: kv::Range,
    count: usize,
    skipped: usize,
    done: bool,
}
impl RowCursor {
    pub fn new(store: &kv::Store, query: Query) -> Result<Self> {
        let range = query.range()?;
        Ok(Self {
            snapshot: store.snapshot()?,
            points: query.point_keys(),
            point_index: 0,
            query,
            range,
            count: 0,
            skipped: 0,
            done: false,
        })
    }
    fn row(&mut self) -> Result<Option<Vec<Cell>>> {
        loop {
            let next = if let Some(points) = &self.points {
                let mut result = None;
                while let Some(key) = points.get(self.point_index) {
                    self.point_index += 1;
                    if let Some(value) = self.snapshot.get(key)? {
                        result = Some((key.clone(), value));
                        break;
                    }
                }
                result
            } else {
                self.snapshot.scan(self.range.clone())?.next().transpose()?
            };
            let Some((key, value)) = next else {
                return Ok(None);
            };
            self.range.start = std::ops::Bound::Excluded(key);
            let bytes = if self.range_is_index() {
                self.snapshot.get(&value)?.ok_or_else(corrupt)?
            } else {
                value
            };
            let row = decode(&self.query.table, &bytes)?;
            if self.query.matches(&row) {
                return Ok(Some(row));
            }
        }
    }
    fn range_is_index(&self) -> bool {
        self.query.table.uses_index(self.query.order)
    }
    pub fn next_row(&mut self) -> Result<Option<Vec<Cell>>> {
        if self.done {
            return Ok(None);
        }
        if self.query.aggregate != LocalAggregate::None {
            let mut n = 0;
            let mut extreme: Option<i64> = None;
            while let Some(row) = self.row()? {
                n += 1;
                if let Some(&column) = self.query.columns.first() {
                    if let Cell::Integer(value) = row[column] {
                        extreme = Some(match extreme {
                            None => value,
                            Some(previous) => {
                                if self.query.aggregate == LocalAggregate::Min {
                                    previous.min(value)
                                } else {
                                    previous.max(value)
                                }
                            }
                        });
                    }
                }
            }
            self.done = true;
            return Ok(Some(vec![
                if self.query.aggregate == LocalAggregate::Count {
                    Cell::Integer(n)
                } else {
                    extreme.map(Cell::Integer).unwrap_or(Cell::Null)
                },
            ]));
        }
        while self.skipped < self.query.offset {
            if self.row()?.is_none() {
                self.done = true;
                return Ok(None);
            }
            self.skipped += 1;
        }
        if self.query.limit.is_some_and(|limit| self.count >= limit) {
            self.done = true;
            return Ok(None);
        }
        let row = self.row()?;
        self.done = row.is_none();
        if row.is_some() {
            self.count += 1;
        }
        Ok(row.map(|row| self.query.project(&row)))
    }
}

pub struct Assignment {
    pub column: usize,
    pub value: Cell,
    pub clamp_column: Option<usize>,
}
pub struct Mutation {
    pub action: LocalAction,
    pub query: Query,
    pub assignments: Vec<Assignment>,
}
fn put(tx: &mut kv::Transaction<'_>, table: &Schema, row: &[Cell]) -> Result<()> {
    table.validate_row(row)?;
    let primary = table.row_key(row)?;
    if let Some(previous) = tx.get(&primary)? {
        let previous = decode(table, &previous)?;
        for &column in &table.indexes {
            tx.delete(&table.index_key(column, &previous, &primary)?)?;
        }
    }
    tx.put(&primary, &encode(row)?)?;
    for &column in &table.indexes {
        tx.put(&table.index_key(column, row, &primary)?, &primary)?;
    }
    Ok(())
}
fn erase(tx: &mut kv::Transaction<'_>, table: &Schema, row: &[Cell]) -> Result<()> {
    let primary = table.row_key(row)?;
    for &column in &table.indexes {
        tx.delete(&table.index_key(column, row, &primary)?)?;
    }
    tx.delete(&primary)?;
    Ok(())
}
impl Mutation {
    pub fn execute(&self, tx: &mut kv::Transaction<'_>) -> Result<u64> {
        if !matches!(
            self.action,
            LocalAction::Insert | LocalAction::Upsert | LocalAction::Update | LocalAction::Delete
        ) {
            return Err(Error::Invalid);
        }
        let mut range = self.query.range()?;
        let table = &self.query.table;
        for assignment in &self.assignments {
            if assignment.column >= table.kinds.len()
                || assignment
                    .clamp_column
                    .is_some_and(|c| c >= table.kinds.len())
                || table.primary.contains(&assignment.column) && self.action == LocalAction::Update
            {
                return Err(Error::Invalid);
            }
        }
        let apply = |row: &mut Vec<Cell>| -> Result<()> {
            for assignment in &self.assignments {
                row[assignment.column] = if let Some(column) = assignment.clamp_column {
                    match (&row[column], &assignment.value) {
                        (Cell::Integer(a), Cell::Integer(b)) => Cell::Integer((*a).min(*b)),
                        _ => return Err(Error::Invalid),
                    }
                } else {
                    assignment.value.clone()
                };
            }
            Ok(())
        };
        if matches!(self.action, LocalAction::Insert | LocalAction::Upsert) {
            let mut row = vec![Cell::Null; table.kinds.len()];
            apply(&mut row)?;
            table.validate_row(&row)?;
            if self.action == LocalAction::Insert && tx.get(&table.row_key(&row)?)?.is_some() {
                return Err(Error::Duplicate);
            }
            put(tx, table, &row)?;
            return Ok(1);
        }
        let mut count = 0;
        let mut skipped = 0;
        let points = self.query.point_keys();
        let mut point_index = 0;
        loop {
            if self.query.limit.is_some_and(|limit| count >= limit as u64) {
                break;
            }
            let next = if let Some(points) = &points {
                let mut result = None;
                while let Some(key) = points.get(point_index) {
                    point_index += 1;
                    if let Some(value) = tx.get(key)? {
                        result = Some((key.clone(), value));
                        break;
                    }
                }
                result
            } else {
                tx.scan(range.clone())?.next().transpose()?
            };
            let Some((key, value)) = next else {
                break;
            };
            range.start = std::ops::Bound::Excluded(key);
            let index = table.uses_index(self.query.order);
            let bytes = if index {
                tx.get(&value)?.ok_or_else(corrupt)?
            } else {
                value
            };
            let mut row = decode(table, &bytes)?;
            if !self.query.matches(&row) {
                continue;
            }
            if skipped < self.query.offset {
                skipped += 1;
                continue;
            }
            if self.action == LocalAction::Delete {
                erase(tx, table, &row)?;
            } else {
                if self
                    .assignments
                    .iter()
                    .any(|a| Some(a.column) == self.query.order && index)
                {
                    return Err(Error::Invalid);
                }
                apply(&mut row)?;
                put(tx, table, &row)?;
            }
            count += 1;
        }
        Ok(count)
    }
}

#[cfg(test)]
#[path = "tests.rs"]
mod tests;
