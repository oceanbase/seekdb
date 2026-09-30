// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Reference SQL-in-open/next flow, deliberately not an efficient series generator.
use seekdb_extension::{
    sql, sys,
    table::{Arguments, Cell, Column, Cursor, Definition, QueryContext, Rows, Service},
    Result,
};
use std::mem::size_of;

static COLUMNS: [Column<'static>; 1] = [Column {
    name: c"ordinal",
    type_id: c"core.type.int64",
    nullable: false,
}];
pub fn definition() -> Definition<'static> {
    Definition {
        object_id: c"org.seekdb.rust-text.sql-series",
        sql_name: c"seekdb_rust_sql_series",
        argument_types: &super::ARGUMENT_TYPES,
        columns: &COLUMNS,
        flags: 0,
        implementation: super::implementation(c"org.seekdb.rust-text.sql-series"),
    }
}
pub const fn provide() -> sys::ServiceProvide {
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.sql-series".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&Service::<SqlSeries>::WITH_SQL as *const sys::TableFunctionServiceV2).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    }
}
struct SqlSeries {
    length: i64,
    position: i64,
}
impl Cursor for SqlSeries {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        super::validate_instance(instance)
    }
    fn open(_: *mut sys::Handle, _: &Arguments<'_>) -> Result<Self> {
        Err(sys::UNAVAILABLE) // Raw rescan has no query context; close/reopen instead.
    }
    fn open_with_context(
        _: *mut sys::Handle,
        args: &Arguments<'_>,
        query: &mut QueryContext<'_>,
    ) -> Result<Self> {
        if args.len() != 1 {
            return Err(sys::INVALID);
        }
        let text = args
            .bytes(0, c"core.type.bytes")?
            .map(std::str::from_utf8)
            .transpose()
            .map_err(|_| sys::INVALID)?;
        let mut length = None;
        let outcome = query
            .execute_sql(
                "SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4))",
                &[text.map_or(sql::Value::Null, sql::Value::Text)],
                1,
                |row| {
                    if row.len() != 1 {
                        return Err(sys::INVALID);
                    }
                    length = Some(match row.get(0)? {
                        sql::Value::Null => 0,
                        sql::Value::I64(n) if n >= 0 => n,
                        _ => return Err(sys::INVALID),
                    });
                    Ok(())
                },
            )
            .map_err(|error| error.status)?;
        if outcome.returned_rows != 1 {
            return Err(sys::INVALID);
        }
        Ok(Self {
            length: length.ok_or(sys::INVALID)?,
            position: 0,
        })
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        while rows.remaining() != 0 && self.position < self.length {
            let mut next = None;
            let outcome = rows
                .execute_sql(
                    "SELECT CAST(? AS SIGNED) + 1",
                    &[sql::Value::I64(self.position)],
                    1,
                    |row| {
                        if row.len() != 1 {
                            return Err(sys::INVALID);
                        }
                        match row.get(0)? {
                            sql::Value::I64(n) => next = Some(n),
                            _ => return Err(sys::INVALID),
                        }
                        Ok(())
                    },
                )
                .map_err(|error| error.status)?;
            let expected = self.position.checked_add(1).ok_or(sys::INVALID)?;
            if outcome.returned_rows != 1 || next != Some(expected) {
                return Err(sys::INVALID);
            }
            rows.emit(&[Cell {
                type_id: c"core.type.int64",
                bytes: Some(&expected.to_ne_bytes()),
            }])?;
            self.position = expected;
        }
        Ok(())
    }
}
