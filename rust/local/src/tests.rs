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

use super::*;
#[derive(Clone, Copy, PartialEq, Eq)]
enum Table {
    Composite,
    Indexed,
    Mixed,
}
impl Table {
    fn schema(self) -> Arc<Schema> {
        let (kinds, nullable, primary, indexes) = match self {
            Self::Composite => (vec![I, I, I], vec![], vec![0, 1], vec![2]),
            Self::Indexed => (vec![I; 6], vec![0, 1], vec![2], vec![3]),
            Self::Mixed => (
                vec![I, LocalKind::Text, LocalKind::Blob, I],
                vec![1, 2, 3],
                vec![0],
                vec![],
            ),
        };
        Arc::new(Schema {
            namespace_id: self as u8,
            kinds,
            nullable,
            primary,
            indexes,
        })
    }
}

use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
static NEXT: AtomicU64 = AtomicU64::new(0);
struct Directory(PathBuf);
impl Directory {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!(
            "local-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir(&path).unwrap();
        Self(path)
    }
}
impl Drop for Directory {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.0).unwrap();
    }
}
fn record(table: Table, id: i64) -> Vec<Cell> {
    let mut row: Vec<_> = table
        .schema()
        .kinds
        .iter()
        .map(|&kind| match kind {
            LocalKind::Integer => Cell::Integer(0),
            LocalKind::Text => Cell::Text(vec![]),
            LocalKind::Blob => Cell::Blob(vec![]),
            _ => unreachable!(),
        })
        .collect();
    row[table.schema().primary[0]] = Cell::Integer(id);
    row
}
fn insert(table: Table, row: Vec<Cell>, upsert: bool) -> Mutation {
    Mutation {
        action: if upsert {
            LocalAction::Upsert
        } else {
            LocalAction::Insert
        },
        query: Query::all(table.schema()),
        assignments: row
            .into_iter()
            .enumerate()
            .map(|(column, value)| Assignment {
                column,
                value,
                clamp_column: None,
            })
            .collect(),
    }
}
fn all(store: &kv::Store, query: Query) -> Vec<Vec<Cell>> {
    let mut cursor = RowCursor::new(store, query).unwrap();
    let mut rows = vec![];
    while let Some(row) = cursor.next_row().unwrap() {
        rows.push(row);
    }
    rows
}
fn predicate(column: usize, compare: LocalCompare, value: i64) -> Predicate {
    Predicate {
        column,
        compare,
        values: vec![Cell::Integer(value)],
    }
}
#[test]
fn rows_persist_with_binary_fields_and_preserve_other_namespaces() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    tx.put(b"other/key", b"value").unwrap();
    for table in [Table::Composite, Table::Indexed, Table::Mixed] {
        let mut row = record(table, 12);
        if table == Table::Mixed {
            row[2] = Cell::Blob(vec![0, 1, 255, 0, 2]);
            row[1] = Cell::Text(b"text'\0value".to_vec());
        }
        insert(table, row, false).execute(&mut tx).unwrap();
    }
    tx.commit().unwrap();
    drop(store);
    let store = kv::Store::open(&directory.0).unwrap();
    for table in [Table::Composite, Table::Indexed, Table::Mixed] {
        let rows = all(&store, Query::all(table.schema()));
        assert_eq!(rows.len(), 1);
        if table == Table::Mixed {
            assert_eq!(rows[0][2], Cell::Blob(vec![0, 1, 255, 0, 2]));
        }
    }
    assert_eq!(store.get(b"other/key").unwrap().unwrap(), b"value");
}
#[test]
fn cross_table_transaction_rolls_back_on_duplicate_and_preserves_primary_identity() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    insert(Table::Composite, record(Table::Composite, 9), false)
        .execute(&mut tx)
        .unwrap();
    tx.commit().unwrap();
    let mut tx = store.begin().unwrap();
    insert(Table::Indexed, record(Table::Indexed, 22), true)
        .execute(&mut tx)
        .unwrap();
    insert(Table::Mixed, record(Table::Mixed, 22), true)
        .execute(&mut tx)
        .unwrap();
    let mut duplicate = record(Table::Composite, 9);
    duplicate[2] = Cell::Integer(77);
    assert!(matches!(
        insert(Table::Composite, duplicate, false).execute(&mut tx),
        Err(Error::Duplicate)
    ));
    tx.rollback();
    assert!(all(&store, Query::all(Table::Indexed.schema())).is_empty());
    assert!(all(&store, Query::all(Table::Mixed.schema())).is_empty());
    let errors = all(&store, Query::all(Table::Composite.schema()));
    assert_eq!(errors[0][2], Cell::Integer(0));
}
#[test]
fn cleanup_uses_time_order_and_upserts_remove_old_index_keys() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    for (id, time) in [(1, 300), (2, 100), (3, 200)] {
        let mut row = record(Table::Composite, id);
        row[2] = Cell::Integer(time);
        insert(Table::Composite, row, false)
            .execute(&mut tx)
            .unwrap();
    }
    tx.commit().unwrap();
    let mut query = Query::all(Table::Composite.schema());
    query.order = Some(2);
    query.limit = Some(1);
    query.predicates.push(predicate(2, LocalCompare::Less, 250));
    let mut tx = store.begin().unwrap();
    assert_eq!(
        Mutation {
            action: LocalAction::Delete,
            query,
            assignments: vec![]
        }
        .execute(&mut tx)
        .unwrap(),
        1
    );
    let mut updated = record(Table::Composite, 3);
    updated[2] = Cell::Integer(400);
    insert(Table::Composite, updated, true)
        .execute(&mut tx)
        .unwrap();
    tx.commit().unwrap();
    let mut query = Query::all(Table::Composite.schema());
    query.order = Some(2);
    let rows = all(&store, query);
    assert_eq!(rows.len(), 2);
    assert_eq!(rows[0][0], Cell::Integer(1));
    assert_eq!(rows[1][0], Cell::Integer(3));
}
#[test]
fn long_cursor_keeps_its_view_while_other_tables_are_modified() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    for id in 0..5 {
        insert(Table::Indexed, record(Table::Indexed, id), true)
            .execute(&mut tx)
            .unwrap();
    }
    tx.commit().unwrap();
    let mut cursor = RowCursor::new(&store, Query::all(Table::Indexed.schema())).unwrap();
    assert_eq!(cursor.next_row().unwrap().unwrap()[2], Cell::Integer(0));
    let mut tx = store.begin().unwrap();
    Mutation {
        action: LocalAction::Delete,
        query: Query::all(Table::Indexed.schema()),
        assignments: vec![],
    }
    .execute(&mut tx)
    .unwrap();
    insert(Table::Indexed, record(Table::Indexed, 99), true)
        .execute(&mut tx)
        .unwrap();
    tx.commit().unwrap();
    for id in 1..5 {
        assert_eq!(cursor.next_row().unwrap().unwrap()[2], Cell::Integer(id));
    }
    assert!(cursor.next_row().unwrap().is_none());
    assert_eq!(all(&store, Query::all(Table::Indexed.schema())).len(), 1);
}
#[test]
fn conditional_updates_count_affected_rows_and_clamp_to_another_column() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    for (id, bound, status) in [(1, 50, 0), (2, 80, 1), (3, 120, 0)] {
        let mut row = record(Table::Indexed, id);
        row[3] = Cell::Integer(bound);
        row[5] = Cell::Integer(status);
        insert(Table::Indexed, row, true).execute(&mut tx).unwrap();
    }
    tx.commit().unwrap();
    let mut query = Query::all(Table::Indexed.schema());
    query.predicates = vec![
        predicate(3, LocalCompare::GreaterEqual, 60),
        predicate(4, LocalCompare::Less, 100),
        predicate(5, LocalCompare::NotEqual, 1),
    ];
    let mut tx = store.begin().unwrap();
    assert_eq!(
        Mutation {
            action: LocalAction::Update,
            query,
            assignments: vec![Assignment {
                column: 4,
                value: Cell::Integer(100),
                clamp_column: Some(3)
            }]
        }
        .execute(&mut tx)
        .unwrap(),
        1
    );
    tx.commit().unwrap();
    let rows = all(&store, Query::all(Table::Indexed.schema()));
    assert_eq!(
        rows.iter().map(|r| r[4].clone()).collect::<Vec<_>>(),
        vec![Cell::Integer(0), Cell::Integer(0), Cell::Integer(100)]
    );
    let mut query = Query::all(Table::Indexed.schema());
    query.order = Some(3);
    query.columns = vec![3];
    query.limit = Some(1);
    assert_eq!(all(&store, query), vec![vec![Cell::Integer(50)]]);
}
#[test]
fn ordering_by_an_indexed_primary_column_uses_row_keys() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let descriptor = ffi::Schema {
        namespace_id: 10,
        columns: vec![
            ffi::Column {
                kind: I,
                nullable: false,
            };
            2
        ],
        primary: vec![0],
        indexes: vec![0],
    };
    let schema = Arc::new(Schema::from_descriptor(&descriptor).unwrap());
    let mut tx = store.begin().unwrap();
    for id in [-3, 0, 9] {
        Mutation {
            action: LocalAction::Insert,
            query: Query::all(schema.clone()),
            assignments: vec![
                Assignment {
                    column: 0,
                    value: Cell::Integer(id),
                    clamp_column: None,
                },
                Assignment {
                    column: 1,
                    value: Cell::Integer(100),
                    clamp_column: None,
                },
            ],
        }
        .execute(&mut tx)
        .unwrap();
    }
    tx.commit().unwrap();
    let mut query = Query::all(schema);
    query.order = Some(0);
    query.predicates = vec![predicate(0, LocalCompare::GreaterEqual, 0)];
    let expected = vec![
        vec![Cell::Integer(0), Cell::Integer(100)],
        vec![Cell::Integer(9), Cell::Integer(100)],
    ];
    assert_eq!(all(&store, query.clone()), expected);
    let tx = store.begin().unwrap();
    let mut rows = vec![];
    query
        .visit_transaction(&tx, |row| {
            rows.push(row.to_vec());
            true
        })
        .unwrap();
    assert_eq!(rows, expected);
    tx.rollback();
    let mut tx = store.begin().unwrap();
    assert_eq!(
        Mutation {
            action: LocalAction::Delete,
            query,
            assignments: vec![],
        }
        .execute(&mut tx)
        .unwrap(),
        2
    );
    tx.commit().unwrap();
}

#[test]
fn aggregates_and_paginated_queries_preserve_empty_and_null_semantics() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut query = Query::all(Table::Mixed.schema());
    query.columns = vec![0];
    query.aggregate = LocalAggregate::Max;
    assert_eq!(all(&store, query.clone()), vec![vec![Cell::Null]]);
    let mut tx = store.begin().unwrap();
    for id in [9, -3, 0, 2] {
        insert(Table::Mixed, record(Table::Mixed, id), false)
            .execute(&mut tx)
            .unwrap();
    }
    tx.commit().unwrap();
    assert_eq!(all(&store, query), vec![vec![Cell::Integer(9)]]);
    let mut query = Query::all(Table::Mixed.schema());
    query.columns = vec![0];
    query.order = Some(0);
    query.offset = 1;
    query.limit = Some(2);
    assert_eq!(
        all(&store, query),
        vec![vec![Cell::Integer(0)], vec![Cell::Integer(2)]]
    );
    let mut query = Query::all(Table::Mixed.schema());
    query.aggregate = LocalAggregate::Count;
    query.predicates = vec![predicate(0, LocalCompare::Greater, 100)];
    assert_eq!(all(&store, query), vec![vec![Cell::Integer(0)]]);
}
#[test]
fn primary_ranges_and_in_lists_handle_signed_boundaries_without_duplicates() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    for id in [i64::MIN, -3, 0, 2, 9, i64::MAX] {
        insert(Table::Mixed, record(Table::Mixed, id), false)
            .execute(&mut tx)
            .unwrap();
    }
    tx.commit().unwrap();
    let mut query = Query::all(Table::Mixed.schema());
    query.columns = vec![0];
    query.predicates = vec![
        predicate(0, LocalCompare::Greater, -3),
        predicate(0, LocalCompare::LessEqual, 9),
    ];
    assert_eq!(
        all(&store, query),
        vec![
            vec![Cell::Integer(0)],
            vec![Cell::Integer(2)],
            vec![Cell::Integer(9)]
        ]
    );
    let mut query = Query::all(Table::Mixed.schema());
    query.columns = vec![0];
    query.predicates = vec![predicate(0, LocalCompare::Greater, i64::MAX)];
    assert!(all(&store, query).is_empty());
    let mut query = Query::all(Table::Mixed.schema());
    query.columns = vec![0];
    query.predicates = vec![Predicate {
        column: 0,
        compare: LocalCompare::In,
        values: vec![
            Cell::Integer(9),
            Cell::Integer(-3),
            Cell::Integer(9),
            Cell::Integer(5),
        ],
    }];
    assert_eq!(
        all(&store, query.clone()),
        vec![vec![Cell::Integer(-3)], vec![Cell::Integer(9)]]
    );
    let mut tx = store.begin().unwrap();
    assert_eq!(
        Mutation {
            action: LocalAction::Delete,
            query,
            assignments: vec![]
        }
        .execute(&mut tx)
        .unwrap(),
        2
    );
    tx.commit().unwrap();
    assert_eq!(all(&store, Query::all(Table::Mixed.schema())).len(), 4);
}
#[test]
fn transaction_reads_its_writes_and_can_compute_a_following_update() {
    let directory = Directory::new();
    let store = kv::Store::open(&directory.0).unwrap();
    let mut tx = store.begin().unwrap();
    insert(Table::Mixed, record(Table::Mixed, 22), false)
        .execute(&mut tx)
        .unwrap();
    let mut query = Query::all(Table::Mixed.schema());
    query.columns = vec![3];
    query.predicates = vec![predicate(0, LocalCompare::Equal, 22)];
    let mut old = None;
    query
        .visit_transaction(&tx, |row| {
            old = Some(row[0].clone());
            true
        })
        .unwrap();
    assert_eq!(old, Some(Cell::Integer(0)));
    Mutation {
        action: LocalAction::Update,
        query: query.clone(),
        assignments: vec![Assignment {
            column: 3,
            value: Cell::Integer(73),
            clamp_column: None,
        }],
    }
    .execute(&mut tx)
    .unwrap();
    query
        .visit_transaction(&tx, |row| {
            assert_eq!(row[0], Cell::Integer(73));
            true
        })
        .unwrap();
    tx.rollback();
    assert!(all(&store, Query::all(Table::Mixed.schema())).is_empty());
}
#[test]
fn large_payload_batch_fits_the_transaction_budget() {
    let directory = Directory::new();
    let store = kv::Store::open_with_options(
        &directory.0,
        kv::Options {
            max_transaction_pages: 16384,
        },
    )
    .unwrap();
    let mut tx = store.begin().unwrap();
    for id in 1..=128 {
        insert(Table::Indexed, record(Table::Indexed, id), true)
            .execute(&mut tx)
            .unwrap();
        let mut row = record(Table::Mixed, id);
        row[1] = Cell::Text(vec![b'x'; 84 * 1024]);
        row[2] = Cell::Blob(vec![37; 32 * 1024]);
        insert(Table::Mixed, row, true).execute(&mut tx).unwrap();
    }
    let commit = tx.commit().unwrap();
    assert!(commit.pages_written < 16384);
    assert_eq!(all(&store, Query::all(Table::Indexed.schema())).len(), 128);
    assert_eq!(all(&store, Query::all(Table::Mixed.schema())).len(), 128);
}

#[test]
fn concurrent_bridge_cursors_keep_snapshots_and_release_them_at_end() {
    let directory = Directory::new();
    let store = crate::bridge::open_storage(directory.0.to_str().unwrap());
    assert_eq!(store.status(), 0);
    let schema = Table::Mixed.schema();
    let query = ffi::Query {
        schema: ffi::Schema {
            namespace_id: schema.namespace_id,
            columns: schema
                .kinds
                .iter()
                .enumerate()
                .map(|(i, &kind)| ffi::Column {
                    kind,
                    nullable: schema.nullable.contains(&i),
                })
                .collect(),
            primary: schema.primary.iter().map(|&i| i as u32).collect(),
            indexes: vec![],
        },
        columns: vec![0],
        conditions: vec![],
        order_column: -1,
        limit: -1,
        offset: 0,
        aggregate: ffi::Aggregate::None,
    };
    let mut tx = store.kv().unwrap().begin().unwrap();
    insert(Table::Mixed, record(Table::Mixed, 7), false)
        .execute(&mut tx)
        .unwrap();
    tx.commit().unwrap();
    let mut cursors: Vec<_> = (0..65)
        .map(|_| {
            let cursor = store.cursor_open(&query);
            assert_eq!(cursor.status(), 0);
            cursor
        })
        .collect();
    assert!(store.kv().unwrap().get(b"unrelated").is_ok());
    let mut tx = store.kv().unwrap().begin().unwrap();
    insert(Table::Mixed, record(Table::Mixed, 8), false)
        .execute(&mut tx)
        .unwrap();
    tx.commit().unwrap();
    for cursor in &mut cursors {
        assert_eq!(cursor.next(), 0);
        assert_eq!(cursor.row()[0].integer, 7);
        assert_eq!(cursor.next(), ffi::Code::End.repr as i32);
        assert_eq!(cursor.next(), ffi::Code::End.repr as i32);
    }
    drop(store);
    // Exhausted C++ bridge handles may remain alive without pinning the store.
    let reopened = kv::Store::open(&directory.0).unwrap();
    assert_eq!(all(&reopened, Query::all(schema)).len(), 2);
}
