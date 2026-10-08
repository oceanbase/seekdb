// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use crate::{
    result_type, sys,
    table::{Cell, Number},
    Result,
};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, rc::Rc};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum Encoding {
    Bytes = 0,
    Null = 1,
    Bool = 2,
    I32 = 3,
    U32 = 4,
    I64 = 5,
    U64 = 6,
    F64 = 7,
}
impl TryFrom<u32> for Encoding {
    type Error = sys::Status;
    fn try_from(value: u32) -> Result<Self> {
        Ok(match value {
            0 => Self::Bytes,
            1 => Self::Null,
            2 => Self::Bool,
            3 => Self::I32,
            4 => Self::U32,
            5 => Self::I64,
            6 => Self::U64,
            7 => Self::F64,
            _ => return Err(sys::INVALID),
        })
    }
}
/// Execution column metadata, borrowed for this callback. sql_type and
/// collation are identifiers from the matched server build, not Rust layouts.
#[derive(Clone, Copy)]
pub struct Column<'a> {
    raw: &'a sys::CustomColumn,
    _thread: PhantomData<Rc<()>>,
}
impl Column<'_> {
    pub fn type_id(&self) -> &CStr {
        // Validated, fixed-size NUL-terminated ID; host keeps schema immutable.
        unsafe { CStr::from_ptr(self.raw.type_id.as_ptr()) }
    }
    pub fn encoding(&self) -> Encoding {
        Encoding::try_from(self.raw.encoding).unwrap()
    }
    pub fn nullable(&self) -> bool {
        self.raw.flags & 1 != 0
    }
    pub fn stored(&self) -> bool {
        self.raw.flags & 2 != 0
    }
    pub fn sql_type(&self) -> u32 {
        self.raw.sql_type
    }
    pub fn collation(&self) -> i32 {
        self.raw.collation
    }
    pub fn precision(&self) -> i32 {
        self.raw.precision
    }
    pub fn scale(&self) -> i32 {
        self.raw.scale
    }
    fn accepts(&self, cell: &Cell<'_>) -> bool {
        if cell.type_id != self.type_id() {
            return false;
        }
        let Some(bytes) = cell.bytes else {
            return self.nullable();
        };
        match self.encoding() {
            Encoding::Null => false,
            Encoding::Bytes => true,
            Encoding::Bool => matches!(bytes, [0] | [1]),
            Encoding::I32 | Encoding::U32 => bytes.len() == 4,
            Encoding::I64 | Encoding::U64 | Encoding::F64 => bytes.len() == 8,
        }
    }
}
/// Zero columns is known empty schema; missing v2 metadata is UNSUPPORTED_ABI.
/// This view never exposes a mutable host pointer and must not outlive next().
/// ```compile_fail
/// use seekdb_extension::custom_executor::{Context, Schema};
/// fn escape(context: &Context<'_>) -> Schema<'static> {
///     context.output_schema().unwrap()
/// }
/// ```
/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::custom_executor::Schema<'static>>();
/// ```
#[derive(Clone, Copy)]
pub struct Schema<'a> {
    columns: &'a [sys::CustomColumn],
    _thread: PhantomData<Rc<()>>,
}
impl<'a> Schema<'a> {
    pub fn len(&self) -> usize {
        self.columns.len()
    }
    pub fn is_empty(&self) -> bool {
        self.columns.is_empty()
    }
    pub fn column(&self, index: usize) -> Result<Column<'a>> {
        Ok(Column {
            raw: self.columns.get(index).ok_or(sys::INVALID)?,
            _thread: PhantomData,
        })
    }
    pub(super) fn accepts_cells(&self, cells: &[Cell<'_>]) -> bool {
        cells.len() == self.len()
            && cells
                .iter()
                .enumerate()
                .all(|(i, c)| self.column(i).unwrap().accepts(c))
    }
    pub(super) fn accepts_values(&self, values: &[sys::Value]) -> bool {
        values.len() == self.len()
            && values.iter().enumerate().all(|(i, v)| {
                // The caller already validated the row ABI and borrowed buffers.
                let cell = Cell {
                    type_id: unsafe { CStr::from_ptr(v.type_id) },
                    bytes: if v.is_null != 0 {
                        None
                    } else if v.data_size == 0 {
                        Some(&[])
                    } else {
                        Some(unsafe { std::slice::from_raw_parts(v.data, v.data_size as usize) })
                    },
                };
                self.column(i).unwrap().accepts(&cell)
            })
    }
    pub(super) unsafe fn from_validated(raw: &'a sys::CustomSchema) -> Self {
        Self {
            columns: if raw.column_count == 0 {
                &[]
            } else {
                unsafe { std::slice::from_raw_parts(raw.columns, raw.column_count as usize) }
            },
            _thread: PhantomData,
        }
    }
}
pub(super) unsafe fn validate(raw: &sys::CustomSchema) -> Result<()> {
    if raw.struct_size != size_of::<sys::CustomSchema>() as u32
        || raw.column_count > 1024
        || (raw.column_count != 0 && raw.columns.is_null())
        || raw.reserved != [0; 4]
    {
        return Err(sys::INVALID);
    }
    let schema = unsafe { Schema::from_validated(raw) };
    for raw in schema.columns {
        if raw.struct_size != size_of::<sys::CustomColumn>() as u32
            || raw.flags & !3 != 0
            || raw.reserved_word != 0
            || raw.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        let encoding = Encoding::try_from(raw.encoding)?;
        // Validate termination within the fixed buffer before constructing CStr.
        let bytes = unsafe {
            std::slice::from_raw_parts(raw.type_id.as_ptr().cast::<u8>(), raw.type_id.len())
        };
        let id = CStr::from_bytes_until_nul(bytes).map_err(|_| sys::INVALID)?;
        result_type::validate_type(id)?;
        let required = if id == c"core.type.bytes" {
            Some(Encoding::Bytes)
        } else if id == c"core.type.null" {
            Some(Encoding::Null)
        } else if Number::recognizes(id) {
            Some(match id.to_bytes().rsplit(|b| *b == b'.').next().unwrap() {
                b"bool" => Encoding::Bool,
                b"int32" => Encoding::I32,
                b"uint32" => Encoding::U32,
                b"int64" => Encoding::I64,
                b"uint64" => Encoding::U64,
                b"float64" => Encoding::F64,
                _ => return Err(sys::INVALID),
            })
        } else {
            None
        };
        if required.is_some_and(|required| required != encoding)
            || (encoding == Encoding::Null && raw.flags & 1 == 0)
        {
            return Err(sys::INVALID);
        }
    }
    Ok(())
}
