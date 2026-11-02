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

use crate::records;
use std::{
    cell::{Cell as Flag, RefCell},
    path::Path,
};

#[cxx::bridge(namespace = "oceanbase::local")]
pub mod ffi {
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Code {
        Ok,
        End,
        NotFound,
        Duplicate,
        Invalid,
        Io,
        Corrupt,
        Busy,
        Aborted,
        Limit,
        NotInit,
        Poisoned,
    }
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Kind {
        Null,
        Integer,
        Text,
        Blob,
    }
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Compare {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        In,
    }
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Aggregate {
        None,
        Count,
        Min,
        Max,
    }
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum Action {
        Insert,
        Upsert,
        Update,
        Delete,
    }
    #[derive(Clone)]
    struct Column {
        kind: Kind,
        nullable: bool,
    }
    #[derive(Clone)]
    struct Schema {
        namespace_id: u8,
        columns: Vec<Column>,
        primary: Vec<u32>,
        indexes: Vec<u32>,
    }
    #[derive(Clone)]
    struct Value {
        kind: Kind,
        integer: i64,
        bytes: Vec<u8>,
    }
    struct Condition {
        column: u32,
        compare: Compare,
        values: Vec<Value>,
    }
    struct Field {
        column: u32,
        value: Value,
        clamp_column: i32,
    }
    struct Query {
        schema: Schema,
        columns: Vec<u32>,
        conditions: Vec<Condition>,
        order_column: i32,
        limit: i64,
        offset: u64,
        aggregate: Aggregate,
    }
    struct Mutation {
        action: Action,
        query: Query,
        fields: Vec<Field>,
    }
    struct WriteResult {
        code: i32,
        affected: u64,
    }

    unsafe extern "C++" {
        include!("rust/local/include/callbacks.h");
        type WriteCallback;
        type RowCallback;
        fn run<'a>(self: &WriteCallback, transaction: &Transaction<'a>) -> i32;
        fn visit(self: &RowCallback, row: &[Value]) -> i32;
    }
    extern "Rust" {
        type Store;
        fn open_storage(path: &str) -> Box<Store>;
        fn status(self: &Store) -> i32;
        fn write(self: &Store, callback: &WriteCallback) -> i32;
        type Transaction<'a>;
        fn execute(self: &Transaction<'_>, mutation: &Mutation) -> WriteResult;
        fn query(self: &Transaction<'_>, query: &Query, callback: &RowCallback) -> i32;
        fn abort(self: &Transaction<'_>, status: i32) -> i32;
        type Cursor;
        fn cursor_open(self: &Store, query: &Query) -> Box<Cursor>;
        fn status(self: &Cursor) -> i32;
        fn next(self: &mut Cursor) -> i32;
        fn row(self: &Cursor) -> &[Value];
    }
}

fn code(error: records::Error) -> i32 {
    use records::Error;
    (match error {
        Error::Invalid => ffi::Code::Invalid,
        Error::Duplicate => ffi::Code::Duplicate,
        Error::Storage(error) => return storage_code(&error),
    })
    .repr as i32
}
fn storage_code(error: &kv::Error) -> i32 {
    (match error {
        kv::Error::Io(_) => ffi::Code::Io,
        kv::Error::Corrupt(_) => ffi::Code::Corrupt,
        kv::Error::Busy => ffi::Code::Busy,
        kv::Error::Poisoned => ffi::Code::Poisoned,
        kv::Error::TransactionAborted => ffi::Code::Aborted,
        kv::Error::InvalidOptions => ffi::Code::NotInit,
        _ => ffi::Code::Limit,
    })
    .repr as i32
}

fn cell(value: &ffi::Value) -> records::Result<records::Cell> {
    use ffi::Kind;
    if value.bytes.len() > kv::MAX_VALUE_SIZE {
        return Err(records::Error::Invalid);
    }
    Ok(match value.kind {
        Kind::Null => records::Cell::Null,
        Kind::Integer => records::Cell::Integer(value.integer),
        Kind::Text => records::Cell::Text(value.bytes.clone()),
        Kind::Blob => records::Cell::Blob(value.bytes.clone()),
        _ => return Err(records::Error::Invalid),
    })
}
fn values(row: Vec<records::Cell>) -> Vec<ffi::Value> {
    row.into_iter()
        .map(|cell| {
            let kind = cell.kind();
            let (integer, bytes) = match cell {
                records::Cell::Integer(n) => (n, vec![]),
                records::Cell::Text(bytes) | records::Cell::Blob(bytes) => (0, bytes),
                records::Cell::Null => (0, vec![]),
            };
            ffi::Value {
                kind,
                integer,
                bytes,
            }
        })
        .collect()
}
fn query(input: &ffi::Query) -> records::Result<records::Query> {
    Ok(records::Query {
        table: records::Schema::from_descriptor(&input.schema)?.into(),
        columns: input.columns.iter().map(|&c| c as usize).collect(),
        predicates: input
            .conditions
            .iter()
            .map(|condition| {
                Ok(records::Predicate {
                    column: condition.column as usize,
                    compare: condition.compare,
                    values: condition
                        .values
                        .iter()
                        .map(cell)
                        .collect::<records::Result<_>>()?,
                })
            })
            .collect::<records::Result<_>>()?,
        order: (input.order_column >= 0).then_some(input.order_column as usize),
        limit: (input.limit >= 0).then_some(input.limit as usize),
        offset: usize::try_from(input.offset).map_err(|_| records::Error::Invalid)?,
        aggregate: input.aggregate,
    })
}

thread_local! { static WRITING: Flag<bool> = const { Flag::new(false) }; }
pub struct Store {
    inner: kv::Result<kv::Store>,
}

// Other bridges borrow this same opaque Rust type through its CXX declaration.
unsafe impl cxx::ExternType for Store {
    type Id = cxx::type_id!("oceanbase::local::Store");
    type Kind = cxx::kind::Opaque;
}

pub fn open_storage(path: &str) -> Box<Store> {
    Box::new(Store {
        inner: kv::Store::open_with_options(
            Path::new(path),
            kv::Options {
                // Cover a full tablet queue batch, including both table indexes.
                max_transaction_pages: 16384,
            },
        ),
    })
}

impl Store {
    pub fn kv(&self) -> Result<&kv::Store, &kv::Error> {
        self.inner.as_ref()
    }

    pub fn status(&self) -> i32 {
        self.kv().map_or_else(storage_code, |_| 0)
    }
}

// The C++ callback borrows this transaction synchronously. Its Rust lifetime
// remains tied to the store's writer guard; no pointer cast extends it.
pub struct Transaction<'a> {
    tx: RefCell<kv::Transaction<'a>>,
    failure: Flag<i32>,
}
impl Store {
    pub fn write(&self, callback: &ffi::WriteCallback) -> i32 {
        if WRITING.with(|flag| flag.replace(true)) {
            return ffi::Code::Busy.repr as i32;
        }
        struct Reset;
        impl Drop for Reset {
            fn drop(&mut self) {
                WRITING.with(|flag| flag.set(false));
            }
        }
        let _reset = Reset;
        let store = match self.kv() {
            Ok(s) => s,
            Err(e) => return storage_code(e),
        };
        let tx = match store.begin() {
            Ok(tx) => tx,
            Err(e) => return code(e.into()),
        };
        let transaction = Transaction {
            tx: RefCell::new(tx),
            failure: Flag::new(0),
        };
        let status = callback.run(&transaction);
        if status != 0 {
            return status;
        }
        if transaction.failure.get() != 0 {
            return transaction.failure.get();
        }
        transaction
            .tx
            .into_inner()
            .commit()
            .map_or_else(|e| code(e.into()), |_| 0)
    }
}
impl Transaction<'_> {
    pub fn abort(&self, status: i32) -> i32 {
        if self.failure.get() == 0 {
            self.failure.set(status);
        }
        self.failure.get()
    }
    pub fn execute(&self, input: &ffi::Mutation) -> ffi::WriteResult {
        if self.failure.get() != 0 {
            return ffi::WriteResult {
                code: self.failure.get(),
                affected: 0,
            };
        }
        let Ok(mut tx) = self.tx.try_borrow_mut() else {
            return ffi::WriteResult {
                code: self.abort(ffi::Code::Busy.repr as i32),
                affected: 0,
            };
        };
        let result = (|| {
            let assignments = input
                .fields
                .iter()
                .map(|field| {
                    Ok(records::Assignment {
                        column: field.column as usize,
                        value: cell(&field.value)?,
                        clamp_column: (field.clamp_column >= 0)
                            .then_some(field.clamp_column as usize),
                    })
                })
                .collect::<records::Result<_>>()?;
            records::Mutation {
                action: input.action,
                query: query(&input.query)?,
                assignments,
            }
            .execute(&mut tx)
        })();
        match result {
            Ok(affected) => ffi::WriteResult { code: 0, affected },
            Err(e) => ffi::WriteResult {
                code: self.abort(code(e)),
                affected: 0,
            },
        }
    }
    pub fn query(&self, input: &ffi::Query, callback: &ffi::RowCallback) -> i32 {
        if self.failure.get() != 0 {
            return self.failure.get();
        }
        let Ok(tx) = self.tx.try_borrow_mut() else {
            return self.abort(ffi::Code::Busy.repr as i32);
        };
        let mut status = 0;
        let result = (|| {
            query(input)?.visit_transaction(&tx, |row| {
                status = callback.visit(&values(row.to_vec()));
                status == 0
            })
        })();
        if let Err(e) = result {
            status = code(e);
        }
        if status == ffi::Code::End.repr as i32 {
            status = 0;
        }
        self.abort(status)
    }
}

pub struct Cursor {
    cursor: Option<records::RowCursor>,
    row: Vec<ffi::Value>,
    status: i32,
}
impl Store {
    pub fn cursor_open(&self, input: &ffi::Query) -> Box<Cursor> {
        let result = self.kv().map_err(storage_code).and_then(|store| {
            query(input)
                .and_then(|query| records::RowCursor::new(store, query))
                .map_err(code)
        });
        Box::new(match result {
            Ok(cursor) => Cursor {
                cursor: Some(cursor),
                row: vec![],
                status: 0,
            },
            Err(e) => Cursor {
                cursor: None,
                row: vec![],
                status: e,
            },
        })
    }
}
impl Cursor {
    pub fn status(&self) -> i32 {
        self.status
    }
    pub fn row(&self) -> &[ffi::Value] {
        &self.row
    }
    pub fn next(&mut self) -> i32 {
        self.row.clear();
        if self.status != 0 {
            return self.status;
        }
        let result = self.cursor.as_mut().expect("valid cursor").next_row();
        self.status = match result {
            Ok(Some(row)) => {
                self.row = values(row);
                0
            }
            Ok(None) => ffi::Code::End.repr as i32,
            Err(e) => code(e),
        };
        if self.status != 0 {
            self.cursor = None;
        }
        self.status
    }
}
