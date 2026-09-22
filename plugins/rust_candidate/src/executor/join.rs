// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Bounded integer equijoin reference. Own both inputs, stream matching pairs;
//! do not allocate the potentially quadratic result or call a host join operator.
use super::*;
mod dag;
mod streaming;
pub(super) use dag::State as Dag;
pub(super) use streaming::State as Streaming;

#[derive(Debug, PartialEq, Eq)]
struct Plan {
    keys: [usize; 2],
    outputs: Vec<(usize, usize)>,
    bound: bool,
}
impl Plan {
    fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() < 16 || !matches!(&bytes[..4], b"SJE1" | b"SJR1" | b"SJC1") {
            return Err(sys::INVALID);
        }
        let word = |at: usize| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as usize;
        let keys = [word(4), word(8)];
        let count = word(12);
        let bound = &bytes[..4] == b"SJC1";
        if bound && keys != [0, 0] {
            return Err(sys::INVALID);
        }
        if keys.iter().any(|k| *k >= 1024) || count > 1024 || bytes.len() != 16 + count * 8 {
            return Err(sys::INVALID);
        }
        let mut outputs = Vec::new();
        outputs
            .try_reserve_exact(count)
            .map_err(|_| sys::NO_MEMORY)?;
        for index in 0..count {
            let source = word(16 + index * 8);
            let column = word(20 + index * 8);
            if source >= 2 || column >= 1024 {
                return Err(sys::INVALID);
            }
            outputs.push((source, column));
        }
        Ok(Self {
            keys,
            outputs,
            bound,
        })
    }
    fn validate(&self, context: &Context<'_>) -> Result<()> {
        if context.input_count() != 2 {
            return Err(sys::INVALID);
        }
        let inputs = [context.input_schema(0)?, context.input_schema(1)?];
        if !self.bound {
            let a = inputs[0].column(self.keys[0])?;
            let b = inputs[1].column(self.keys[1])?;
            // Equality is defined here only for matching builtin integer contracts.
            // No byte equality for opaque types, text collation or floating NaNs.
            if !Number::recognizes(a.type_id())
                || a.type_id() != b.type_id()
                || a.encoding() != b.encoding()
                || a.sql_type() != b.sql_type()
                || !matches!(
                    a.encoding(),
                    Encoding::Bool | Encoding::I32 | Encoding::U32 | Encoding::I64 | Encoding::U64
                )
            {
                return Err(sys::INVALID);
            }
        }
        let output = context.output_schema()?;
        if output.len() != self.outputs.len() {
            return Err(sys::INVALID);
        }
        for (index, &(source, column)) in self.outputs.iter().enumerate() {
            let a = inputs[source].column(column)?;
            let b = output.column(index)?;
            if a.type_id() != b.type_id()
                || a.encoding() != b.encoding()
                || (a.nullable() && !b.nullable())
                || a.sql_type() != b.sql_type()
                || a.collation() != b.collation()
                || a.scale() != b.scale()
                || !compatible_precision(
                    a.type_id(),
                    a.encoding(),
                    a.precision(),
                    b.precision(),
                    true,
                )
            {
                return Err(sys::INVALID);
            }
        }
        Ok(())
    }
}
pub(super) fn integer(cell: Cell<'_>) -> Result<Option<i128>> {
    Ok(match cell.number()? {
        None => None,
        Some(Number::Bool(value)) => Some(i128::from(value)),
        Some(Number::I32(value)) => Some(i128::from(value)),
        Some(Number::U32(value)) => Some(i128::from(value)),
        Some(Number::I64(value)) => Some(i128::from(value)),
        Some(Number::U64(value)) => Some(i128::from(value)),
        Some(Number::F64(_)) => return Err(sys::INVALID),
    })
}
struct InputRow {
    key: Option<i128>,
    cells: Vec<OwnedCell>,
}
pub(super) struct State {
    plan: Plan,
    inputs: [Vec<InputRow>; 2],
    loaded: bool,
    position: [usize; 2],
}
impl State {
    pub(super) fn open(bytes: &[u8]) -> Result<Self> {
        Ok(Self {
            plan: Plan::parse(bytes)?,
            inputs: [Vec::new(), Vec::new()],
            loaded: false,
            position: [0; 2],
        })
    }
    pub(super) fn next(&mut self, context: &mut Context<'_>) -> Result<Step> {
        if !self.loaded {
            self.plan.validate(context)?; // Both inputs and all outputs before any read.
            let mut charged = 0usize;
            let mut count = 0usize;
            for input in 0..2 {
                loop {
                    context.check_interrupt()?;
                    let Some(row) = context.next_input(input as u32)? else {
                        break;
                    };
                    if count == 65536 {
                        return Err(sys::NO_MEMORY);
                    }
                    count += 1;
                    let key = integer(row.cell(self.plan.keys[input])?)?;
                    let mut cells = Vec::new();
                    cells
                        .try_reserve_exact(row.len())
                        .map_err(|_| sys::NO_MEMORY)?;
                    charged += size_of::<InputRow>() + row.len() * size_of::<OwnedCell>();
                    for index in 0..row.len() {
                        let cell = row.cell(index)?;
                        charged += cell.type_id.to_bytes_with_nul().len()
                            + cell.bytes.map_or(0, |v| v.len());
                        if charged > 16 * 1024 * 1024 {
                            return Err(sys::NO_MEMORY);
                        }
                        let mut id = Vec::new();
                        id.try_reserve_exact(cell.type_id.to_bytes_with_nul().len())
                            .map_err(|_| sys::NO_MEMORY)?;
                        id.extend_from_slice(cell.type_id.to_bytes_with_nul());
                        let bytes = if let Some(bytes) = cell.bytes {
                            let mut owned = Vec::new();
                            owned
                                .try_reserve_exact(bytes.len())
                                .map_err(|_| sys::NO_MEMORY)?;
                            owned.extend_from_slice(bytes);
                            Some(owned)
                        } else {
                            None
                        };
                        cells.push(OwnedCell {
                            type_id: CString::from_vec_with_nul(id).map_err(|_| sys::INVALID)?,
                            bytes,
                        });
                    }
                    if charged > 16 * 1024 * 1024 {
                        return Err(sys::NO_MEMORY);
                    }
                    self.inputs[input]
                        .try_reserve(1)
                        .map_err(|_| sys::NO_MEMORY)?;
                    self.inputs[input].push(InputRow { key, cells });
                }
            }
            self.loaded = true;
        }
        while self.position[0] < self.inputs[0].len() {
            context.check_interrupt()?;
            if self.position[1] == self.inputs[1].len() {
                self.position[0] += 1;
                self.position[1] = 0;
                continue;
            }
            let left = &self.inputs[0][self.position[0]];
            let right = &self.inputs[1][self.position[1]];
            self.position[1] += 1;
            if left.key.is_none() || left.key != right.key {
                continue;
            }
            let rows = [left, right];
            let mut output = Vec::new();
            output
                .try_reserve_exact(self.plan.outputs.len())
                .map_err(|_| sys::NO_MEMORY)?;
            output.extend(self.plan.outputs.iter().map(|&(input, column)| {
                let cell = &rows[input].cells[column];
                Cell {
                    type_id: &cell.type_id,
                    bytes: cell.bytes.as_deref(),
                }
            }));
            context.emit(&output)?;
            return Ok(Step::Row);
        }
        Ok(Step::End)
    }
    pub(super) fn rescan(&mut self) {
        self.inputs = [Vec::new(), Vec::new()];
        self.loaded = false;
        self.position = [0; 2];
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn bytes(keys: [u32; 2], columns: &[(u32, u32)]) -> Vec<u8> {
        let mut result = b"SJE1".to_vec();
        for word in [keys[0], keys[1], columns.len() as u32] {
            result.extend_from_slice(&word.to_le_bytes());
        }
        for &(source, index) in columns {
            result.extend_from_slice(&source.to_le_bytes());
            result.extend_from_slice(&index.to_le_bytes());
        }
        result
    }
    #[test]
    fn join_plan_preserves_independent_keys_and_repeated_projections() {
        let plan = Plan::parse(&bytes([0, 1023], &[(1, 2), (0, 0), (1, 2)])).unwrap();
        assert_eq!(plan.keys, [0, 1023]);
        assert_eq!(plan.outputs, [(1, 2), (0, 0), (1, 2)]);
        assert!(Plan::parse(&bytes([0, 0], &[])).unwrap().outputs.is_empty());
    }
    #[test]
    fn join_plan_rejects_truncations_trailing_and_invalid_slots() {
        let valid = bytes([0, 1], &[(0, 0), (1, 2)]);
        for end in 0..valid.len() {
            assert_eq!(Plan::parse(&valid[..end]), Err(sys::INVALID));
        }
        let mut trailing = valid;
        trailing.push(0);
        assert_eq!(Plan::parse(&trailing), Err(sys::INVALID));
        for invalid in [
            bytes([1024, 0], &[]),
            bytes([0, 1024], &[]),
            bytes([0, 0], &[(2, 0)]),
            bytes([0, 0], &[(0, 1024)]),
            bytes([0, 0], &vec![(0, 0); 1025]),
        ] {
            assert_eq!(Plan::parse(&invalid), Err(sys::INVALID));
        }
    }
    #[test]
    fn integer_keys_keep_signed_unsigned_extremes_and_null_distinct() {
        assert_eq!(
            integer(Cell {
                type_id: c"core.type.int64",
                bytes: Some(&i64::MIN.to_ne_bytes())
            })
            .unwrap(),
            Some(i64::MIN as i128)
        );
        assert_eq!(
            integer(Cell {
                type_id: c"core.type.uint64",
                bytes: Some(&u64::MAX.to_ne_bytes())
            })
            .unwrap(),
            Some(u64::MAX as i128)
        );
        assert_eq!(
            integer(Cell {
                type_id: c"core.type.int64",
                bytes: None
            })
            .unwrap(),
            None
        );
        assert_eq!(
            integer(Cell {
                type_id: c"core.type.float64",
                bytes: Some(&0f64.to_ne_bytes())
            }),
            Err(sys::INVALID)
        );
    }
}
