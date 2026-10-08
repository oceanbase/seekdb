// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Reference custom algorithm: buffer child rows in Rust, then replay in order.
//! No host MATERIAL executor is called. A bounded example, not a spill engine.
use seekdb_extension::{
    custom_executor::{Context, Encoding, Executor, Service, Step},
    sys,
    table::{Cell, Number},
    Result,
};
use std::{
    collections::VecDeque,
    ffi::{CStr, CString},
    mem::size_of,
};
mod join;
mod sort;

fn compatible_precision(
    type_id: &CStr,
    encoding: Encoding,
    input: i32,
    output: i32,
    explicit: bool,
) -> bool {
    // SQL integer precision is display metadata, not a payload width/range.
    // A column may have unknown precision while ABS(column) reports 20, yet
    // both are the same BIGINT and wire i64. SQL type/encoding/scale and host
    // range checks remain exact. Do not infer this for opaque or float types.
    input == output
        || (explicit
            && Number::recognizes(type_id)
            && matches!(
                encoding,
                Encoding::Bool | Encoding::I32 | Encoding::U32 | Encoding::I64 | Encoding::U64
            ))
}

struct OwnedCell {
    type_id: CString,
    bytes: Option<Vec<u8>>,
}
pub struct State {
    rows: VecDeque<Vec<OwnedCell>>,
    loaded: bool,
    projection: Option<Vec<usize>>,
    multi_projection: Option<Vec<Vec<usize>>>,
    join: Option<join::State>,
    streaming_join: Option<join::Streaming>,
    dag_join: Option<join::Dag>,
    sort: Option<sort::State>,
}
// SMP1 + input count +, for each input, output count and source ordinals.
// A round-robin UNION ALL reference algorithm; never implicit concatenation of
// schemas or a claim that SQL join/union path construction is already provided.
fn multi_projection(plan: &[u8]) -> Result<Vec<Vec<usize>>> {
    if plan.len() < 8 || plan.len() > 64 * 1024 || &plan[..4] != b"SMP1" {
        return Err(sys::INVALID);
    }
    let count = u32::from_le_bytes(plan[4..8].try_into().unwrap()) as usize;
    if count > 64 {
        return Err(sys::INVALID);
    }
    let mut inputs = Vec::new();
    inputs
        .try_reserve_exact(count)
        .map_err(|_| sys::NO_MEMORY)?;
    let mut position = 8;
    for _ in 0..count {
        let bytes = plan.get(position..position + 4).ok_or(sys::INVALID)?;
        let count = u32::from_le_bytes(bytes.try_into().unwrap()) as usize;
        position += 4;
        if count > 1024 {
            return Err(sys::INVALID);
        }
        let bytes = plan
            .get(position..position + count * 4)
            .ok_or(sys::INVALID)?;
        position += count * 4;
        let mut columns = Vec::new();
        columns
            .try_reserve_exact(count)
            .map_err(|_| sys::NO_MEMORY)?;
        for value in bytes.chunks_exact(4) {
            let index = u32::from_le_bytes(value.try_into().unwrap()) as usize;
            if index >= 1024 {
                return Err(sys::INVALID);
            }
            columns.push(index);
        }
        inputs.push(columns);
    }
    if position != plan.len() {
        return Err(sys::INVALID);
    }
    Ok(inputs)
}
// Plugin-owned plan format, not a host SQL expression ID: SPJ1 + little-endian
// u32 count + count input ordinals. Empty bytes retain the legacy identity spool.
fn projection(plan: &[u8]) -> Result<Option<Vec<usize>>> {
    if plan.is_empty() {
        return Ok(None);
    }
    if plan.len() < 8 || &plan[..4] != b"SPJ1" {
        return Err(sys::INVALID);
    }
    let count = u32::from_le_bytes(plan[4..8].try_into().unwrap()) as usize;
    if count > 1024 || plan.len() != 8 + count * 4 {
        return Err(sys::INVALID);
    }
    let mut columns = Vec::new();
    columns
        .try_reserve_exact(count)
        .map_err(|_| sys::NO_MEMORY)?;
    for bytes in plan[8..].chunks_exact(4) {
        let index = u32::from_le_bytes(bytes.try_into().unwrap()) as usize;
        if index >= 1024 {
            return Err(sys::INVALID);
        }
        columns.push(index);
    }
    Ok(Some(columns))
}
pub struct Spool;
impl Executor for Spool {
    type State = State;
    const INPUT_RESCAN: bool = true;
    const INPUT_BINDINGS: bool = true;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        super::validate(instance)?;
        if super::ACTIVE.load(std::sync::atomic::Ordering::Acquire) {
            Ok(())
        } else {
            Err(sys::FAILED_PRECONDITION)
        }
    }
    fn open(_: *mut sys::Handle, plan: &[u8]) -> Result<State> {
        let multi = plan.starts_with(b"SMP1");
        let join = plan.starts_with(b"SJE1");
        let streaming = plan.starts_with(b"SJR1") || plan.starts_with(b"SJC1");
        let dag = plan.starts_with(b"SJD1");
        let sort = plan.starts_with(b"SSO1");
        Ok(State {
            rows: VecDeque::new(),
            loaded: false,
            projection: if multi || join || streaming || dag || sort {
                None
            } else {
                projection(plan)?
            },
            multi_projection: if multi {
                Some(multi_projection(plan)?)
            } else {
                None
            },
            join: if join {
                Some(join::State::open(plan)?)
            } else {
                None
            },
            streaming_join: if streaming {
                Some(join::Streaming::open(plan)?)
            } else {
                None
            },
            dag_join: if dag {
                Some(join::Dag::open(plan)?)
            } else {
                None
            },
            sort: if sort {
                Some(sort::State::open(plan)?)
            } else {
                None
            },
        })
    }
    fn next(state: &mut State, context: &mut Context<'_>) -> Result<Step> {
        if let Some(sort) = &mut state.sort {
            return sort.next(context);
        }
        if let Some(join) = &mut state.dag_join {
            return join.next(context);
        }
        if let Some(join) = &mut state.streaming_join {
            return join.next(context);
        }
        if let Some(join) = &mut state.join {
            return join.next(context);
        }
        if context.input_count() as usize != state.multi_projection.as_ref().map_or(1, Vec::len) {
            return Err(sys::INVALID);
        }
        if !state.loaded {
            // Validate mappings even when there are no input rows. Explicit
            // projection requires schema; an absent v1 description is not empty.
            let explicit = state.projection.is_some() || state.multi_projection.is_some();
            if explicit && !context.has_schema() {
                return Err(sys::UNSUPPORTED_ABI);
            }
            // Validate EVERY branch before reading any input, including empty
            // branches and zero-column projections. No late schema discovery.
            for input_index in 0..context.input_count() {
                if context.has_schema() {
                    let projection = state
                        .multi_projection
                        .as_ref()
                        .map(|p| &p[input_index as usize])
                        .or(state.projection.as_ref());
                    let input = context.input_schema(input_index)?;
                    let output = context.output_schema()?;
                    let count = projection.map_or(input.len(), Vec::len);
                    if count != output.len() {
                        return Err(sys::INVALID);
                    }
                    for index in 0..count {
                        let source = projection.map_or(index, |p| p[index]);
                        let a = input.column(source)?;
                        let b = output.column(index)?;
                        if a.type_id() != b.type_id()
                            || a.encoding() != b.encoding()
                            || (a.nullable() && !b.nullable())
                            || (!explicit
                                && (a.nullable() != b.nullable() || a.stored() != b.stored()))
                            || a.sql_type() != b.sql_type()
                            || a.collation() != b.collation()
                            || !compatible_precision(
                                a.type_id(),
                                a.encoding(),
                                a.precision(),
                                b.precision(),
                                explicit,
                            )
                            || a.scale() != b.scale()
                        {
                            return Err(sys::INVALID);
                        }
                    }
                }
            }
            let mut charged = 0usize;
            let mut ended = [false; 64];
            let mut remaining = context.input_count();
            while remaining > 0 {
                for input_index in 0..context.input_count() {
                    if ended[input_index as usize] {
                        continue;
                    }
                    context.check_interrupt()?;
                    let Some(row) = context.next_input(input_index)? else {
                        ended[input_index as usize] = true;
                        remaining -= 1;
                        continue;
                    };
                    if state.rows.len() >= 65536 {
                        return Err(sys::NO_MEMORY);
                    }
                    let mut owned = Vec::new();
                    let projection = state
                        .multi_projection
                        .as_ref()
                        .map(|p| &p[input_index as usize])
                        .or(state.projection.as_ref());
                    let count = projection.map_or(row.len(), Vec::len);
                    owned.try_reserve_exact(count).map_err(|_| sys::NO_MEMORY)?;
                    charged += size_of::<Vec<OwnedCell>>() + count * size_of::<OwnedCell>();
                    for index in 0..count {
                        let source = projection.map_or(index, |p| p[index]);
                        let cell = row.cell(source)?;
                        // Exercise the typed SDK contract as well as byte replay.
                        // Never interpret a custom type merely by a builtin suffix.
                        if Number::recognizes(cell.type_id) {
                            let _number = cell.number()?;
                        }
                        charged += cell.type_id.to_bytes_with_nul().len()
                            + cell.bytes.map_or(0, |v| v.len());
                        if charged > 16 * 1024 * 1024 {
                            return Err(sys::NO_MEMORY);
                        }
                        let mut type_id = Vec::new();
                        type_id
                            .try_reserve_exact(cell.type_id.to_bytes_with_nul().len())
                            .map_err(|_| sys::NO_MEMORY)?;
                        type_id.extend_from_slice(cell.type_id.to_bytes_with_nul());
                        let bytes = if let Some(value) = cell.bytes {
                            let mut copy = Vec::new();
                            copy.try_reserve_exact(value.len())
                                .map_err(|_| sys::NO_MEMORY)?;
                            copy.extend_from_slice(value);
                            Some(copy)
                        } else {
                            None
                        };
                        owned.push(OwnedCell {
                            type_id: CString::from_vec_with_nul(type_id)
                                .map_err(|_| sys::INVALID)?,
                            bytes,
                        });
                    }
                    if charged > 16 * 1024 * 1024 {
                        return Err(sys::NO_MEMORY);
                    }
                    state.rows.try_reserve(1).map_err(|_| sys::NO_MEMORY)?;
                    state.rows.push_back(owned);
                }
            }
            state.loaded = true;
        }
        let Some(row) = state.rows.front() else {
            return Ok(Step::End);
        };
        let mut cells = Vec::new();
        cells
            .try_reserve_exact(row.len())
            .map_err(|_| sys::NO_MEMORY)?;
        cells.extend(row.iter().map(|cell| Cell {
            type_id: &cell.type_id,
            bytes: cell.bytes.as_deref(),
        }));
        context.emit(&cells)?;
        state.rows.pop_front();
        Ok(Step::Row)
    }
    fn rescan(state: &mut State) -> Result<()> {
        if let Some(sort) = &mut state.sort {
            sort.rescan();
        }
        if let Some(join) = &mut state.dag_join {
            join.rescan();
        }
        if let Some(join) = &mut state.streaming_join {
            join.rescan();
        }
        if let Some(join) = &mut state.join {
            join.rescan();
        }
        state.rows.clear();
        state.loaded = false;
        Ok(())
    }
}
pub static SERVICE: sys::CustomExecutor = Service::<Spool>::ABI;

#[cfg(test)]
mod tests {
    use super::*;
    fn multi_plan(inputs: &[Vec<u32>]) -> Vec<u8> {
        let mut bytes = b"SMP1".to_vec();
        bytes.extend_from_slice(&(inputs.len() as u32).to_le_bytes());
        for columns in inputs {
            bytes.extend_from_slice(&(columns.len() as u32).to_le_bytes());
            for column in columns {
                bytes.extend_from_slice(&column.to_le_bytes());
            }
        }
        bytes
    }
    #[test]
    fn multi_plan_preserves_branch_boundaries_and_zero_columns() {
        let columns = vec![vec![2, 0], vec![], vec![1, 1]];
        assert_eq!(
            multi_projection(&multi_plan(&columns)).unwrap(),
            vec![vec![2usize, 0], vec![], vec![1, 1]]
        );
        assert_eq!(
            multi_projection(&multi_plan(&[])).unwrap(),
            Vec::<Vec<usize>>::new()
        );
        assert_eq!(
            multi_projection(&multi_plan(&vec![vec![1023]; 64]))
                .unwrap()
                .len(),
            64
        );
    }
    #[test]
    fn multi_plan_rejects_every_truncation_and_bounds() {
        let valid = multi_plan(&[vec![2, 0], vec![], vec![1]]);
        for end in 0..valid.len() {
            assert_eq!(multi_projection(&valid[..end]), Err(sys::INVALID));
        }
        let mut trailing = valid.clone();
        trailing.push(0);
        assert_eq!(multi_projection(&trailing), Err(sys::INVALID));
        assert_eq!(
            multi_projection(&multi_plan(&vec![vec![]; 65])),
            Err(sys::INVALID)
        );
        assert_eq!(
            multi_projection(&multi_plan(&[vec![0; 1025]])),
            Err(sys::INVALID)
        );
        assert_eq!(
            multi_projection(&multi_plan(&[vec![1024]])),
            Err(sys::INVALID)
        );
        assert_eq!(
            multi_projection(&multi_plan(&vec![vec![0; 1024]; 64])),
            Err(sys::INVALID)
        );
    }
    fn plan(columns: &[u32]) -> Vec<u8> {
        let mut bytes = b"SPJ1".to_vec();
        bytes.extend_from_slice(&(columns.len() as u32).to_le_bytes());
        for index in columns {
            bytes.extend_from_slice(&index.to_le_bytes());
        }
        bytes
    }
    #[test]
    fn explicit_integer_projection_distinguishes_display_precision_from_encoding() {
        for (id, encoding) in [
            (c"core.type.bool", Encoding::Bool),
            (c"core.type.int32", Encoding::I32),
            (c"core.type.uint32", Encoding::U32),
            (c"core.type.int64", Encoding::I64),
            (c"core.type.uint64", Encoding::U64),
            (c"org.seekdb.gis.scalar.int64", Encoding::I64),
        ] {
            assert!(compatible_precision(id, encoding, -1, 20, true));
            assert!(!compatible_precision(id, encoding, -1, 20, false));
            assert!(compatible_precision(id, encoding, 20, 20, false));
        }
        for (id, encoding) in [
            (c"core.type.float64", Encoding::F64),
            (c"core.type.bytes", Encoding::Bytes),
            (c"org.example.int64", Encoding::I64),
            (c"org.seekdb.gis.scalar.float64", Encoding::F64),
        ] {
            assert!(!compatible_precision(id, encoding, -1, 20, true));
            assert!(compatible_precision(id, encoding, 20, 20, true));
        }
    }
    #[test]
    fn projection_plan_distinguishes_identity_empty_and_repeated_columns() {
        assert_eq!(projection(&[]).unwrap(), None);
        assert_eq!(projection(&plan(&[])).unwrap(), Some(vec![]));
        assert_eq!(projection(&plan(&[1, 0, 1])).unwrap(), Some(vec![1, 0, 1]));
        assert_eq!(
            projection(&plan(&vec![1023; 1024])).unwrap().unwrap().len(),
            1024
        );
    }
    #[test]
    fn projection_plan_rejects_truncation_trailing_and_out_of_range_indices() {
        let valid = plan(&[1, 0]);
        for end in 1..valid.len() {
            assert_eq!(projection(&valid[..end]), Err(sys::INVALID));
        }
        let mut trailing = valid.clone();
        trailing.push(0);
        assert_eq!(projection(&trailing), Err(sys::INVALID));
        let mut version = valid;
        version[3] = b'2';
        assert_eq!(projection(&version), Err(sys::INVALID));
        assert_eq!(projection(&plan(&[1024])), Err(sys::INVALID));
        assert_eq!(projection(&plan(&vec![0; 1025])), Err(sys::INVALID));
    }
}
