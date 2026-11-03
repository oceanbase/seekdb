// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Bounded in-process integer sort; no native SORT call and no hidden spill.
use super::*;
use crate::sort_plan::{Key, Plan};
use std::cmp::Ordering;

const BYTES: usize = 16 * 1024 * 1024;
const ROWS: usize = 65536;
fn charge(used: &mut usize, bytes: usize) -> Result<()> {
    let next = used.checked_add(bytes).ok_or(sys::NO_MEMORY)?;
    if next > BYTES {
        return Err(sys::NO_MEMORY);
    }
    *used = next;
    Ok(())
}
struct Row {
    keys: Vec<Option<i128>>,
    cells: Vec<OwnedCell>,
    ordinal: usize,
}
fn compare(a: &Row, b: &Row, keys: &[Key]) -> Ordering {
    for (i, key) in keys.iter().enumerate() {
        let ordering = match (a.keys[i], b.keys[i]) {
            (None, None) => Ordering::Equal,
            (None, Some(_)) => {
                if key.nulls_first {
                    Ordering::Less
                } else {
                    Ordering::Greater
                }
            }
            (Some(_), None) => {
                if key.nulls_first {
                    Ordering::Greater
                } else {
                    Ordering::Less
                }
            }
            (Some(a), Some(b)) => {
                if key.descending {
                    b.cmp(&a)
                } else {
                    a.cmp(&b)
                }
            }
        };
        if ordering != Ordering::Equal {
            return ordering;
        }
    }
    // Deterministic ties without relying on allocation-heavy stable sorting.
    a.ordinal.cmp(&b.ordinal)
}
fn sift<T>(
    rows: &mut [T],
    mut root: usize,
    end: usize,
    compare: &mut impl FnMut(&T, &T) -> Result<Ordering>,
) -> Result<()> {
    loop {
        let left = root * 2 + 1;
        if left >= end {
            break;
        }
        let mut child = left;
        if left + 1 < end && compare(&rows[left], &rows[left + 1])? == Ordering::Less {
            child += 1;
        }
        if compare(&rows[root], &rows[child])? != Ordering::Less {
            break;
        }
        rows.swap(root, child);
        root = child;
    }
    Ok(())
}
// Fallible in-place heapsort: O(n log n), no allocation in the sorting phase.
// Cancellation returns an error without publishing a partially sorted result.
fn heap_sort<T>(
    rows: &mut [T],
    compare: impl Fn(&T, &T) -> Ordering,
    mut poll: impl FnMut() -> Result<()>,
) -> Result<()> {
    poll()?;
    let size = rows.len();
    {
        let mut comparisons = 0usize;
        let mut checked = |a: &T, b: &T| {
            if comparisons % 256 == 0 {
                poll()?;
            }
            comparisons += 1;
            Ok(compare(a, b))
        };
        for root in (0..size / 2).rev() {
            sift(rows, root, size, &mut checked)?;
        }
        for end in (1..size).rev() {
            rows.swap(0, end);
            sift(rows, 0, end, &mut checked)?;
        }
    }
    poll()
}
pub(super) struct State {
    plan: Plan,
    rows: Vec<Row>,
    loaded: bool,
    position: usize,
}
impl State {
    pub(super) fn open(bytes: &[u8]) -> Result<Self> {
        Ok(Self {
            plan: Plan::parse(bytes)?,
            rows: Vec::new(),
            loaded: false,
            position: 0,
        })
    }
    fn validate(&self, context: &Context<'_>) -> Result<()> {
        if context.input_count() != 1 {
            return Err(sys::INVALID);
        }
        let input = context.input_schema(0)?;
        let output = context.output_schema()?;
        for key in &self.plan.keys {
            let column = input.column(key.column)?;
            if !Number::recognizes(column.type_id())
                || !matches!(
                    column.encoding(),
                    Encoding::Bool | Encoding::I32 | Encoding::U32 | Encoding::I64 | Encoding::U64
                )
            {
                return Err(sys::INVALID);
            }
        }
        if output.len() != self.plan.outputs.len() {
            return Err(sys::INVALID);
        }
        for (index, &slot) in self.plan.outputs.iter().enumerate() {
            let a = input.column(slot)?;
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
    pub(super) fn next(&mut self, context: &mut Context<'_>) -> Result<Step> {
        if !self.loaded {
            self.validate(context)?; // Includes empty inputs and all payload columns.
            let mut charged = self.rows.capacity() * size_of::<Row>()
                + self.plan.keys.capacity() * size_of::<Key>()
                + self.plan.outputs.capacity() * size_of::<usize>();
            charge(&mut charged, 0)?;
            loop {
                context.check_interrupt()?;
                let Some(row) = context.next_input(0)? else {
                    break;
                };
                if self.rows.len() == ROWS {
                    return Err(sys::NO_MEMORY);
                }
                if self.rows.len() == self.rows.capacity() {
                    let wanted = (self.rows.len() + 256).min(ROWS);
                    charge(
                        &mut charged,
                        (wanted - self.rows.capacity()) * size_of::<Row>(),
                    )?;
                    self.rows
                        .try_reserve_exact(wanted - self.rows.len())
                        .map_err(|_| sys::NO_MEMORY)?;
                }
                charge(
                    &mut charged,
                    self.plan.keys.len() * size_of::<Option<i128>>()
                        + self.plan.outputs.len() * size_of::<OwnedCell>(),
                )?;
                let mut keys = Vec::new();
                keys.try_reserve_exact(self.plan.keys.len())
                    .map_err(|_| sys::NO_MEMORY)?;
                for key in &self.plan.keys {
                    keys.push(super::join::integer(row.cell(key.column)?)?);
                }
                let mut cells = Vec::new();
                cells
                    .try_reserve_exact(self.plan.outputs.len())
                    .map_err(|_| sys::NO_MEMORY)?;
                for &slot in &self.plan.outputs {
                    let cell = row.cell(slot)?;
                    charge(&mut charged, cell.type_id.to_bytes_with_nul().len())?;
                    charge(&mut charged, cell.bytes.map_or(0, |b| b.len()))?;
                    let mut name = Vec::new();
                    name.try_reserve_exact(cell.type_id.to_bytes_with_nul().len())
                        .map_err(|_| sys::NO_MEMORY)?;
                    name.extend_from_slice(cell.type_id.to_bytes_with_nul());
                    let bytes = if let Some(bytes) = cell.bytes {
                        let mut copy = Vec::new();
                        copy.try_reserve_exact(bytes.len())
                            .map_err(|_| sys::NO_MEMORY)?;
                        copy.extend_from_slice(bytes);
                        Some(copy)
                    } else {
                        None
                    };
                    cells.push(OwnedCell {
                        type_id: CString::from_vec_with_nul(name).map_err(|_| sys::INVALID)?,
                        bytes,
                    });
                }
                self.rows.push(Row {
                    keys,
                    cells,
                    ordinal: self.rows.len(),
                });
            }
            let keys = &self.plan.keys;
            heap_sort(
                &mut self.rows,
                |a, b| compare(a, b, keys),
                || context.check_interrupt(),
            )?;
            self.loaded = true;
        }
        context.check_interrupt()?;
        let Some(row) = self.rows.get(self.position) else {
            return Ok(Step::End);
        };
        let mut cells = Vec::new();
        cells
            .try_reserve_exact(row.cells.len())
            .map_err(|_| sys::NO_MEMORY)?;
        cells.extend(row.cells.iter().map(|cell| Cell {
            type_id: &cell.type_id,
            bytes: cell.bytes.as_deref(),
        }));
        context.emit(&cells)?;
        self.rows[self.position].cells.clear();
        self.rows[self.position].keys.clear();
        self.position += 1;
        Ok(Step::Row)
    }
    pub(super) fn rescan(&mut self) {
        self.rows.clear();
        self.position = 0;
        self.loaded = false;
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn sort_integer_extremes_null_placement_and_ties_are_independent_of_direction() {
        for flags in 0..4 {
            let keys = [Key {
                column: 0,
                descending: flags & 1 != 0,
                nulls_first: flags & 2 != 0,
            }];
            let values = [
                Some(0),
                None,
                Some(u64::MAX as i128),
                Some(i64::MIN as i128),
                None,
                Some(0),
            ];
            let mut rows: Vec<_> = values
                .into_iter()
                .enumerate()
                .map(|(ordinal, key)| Row {
                    keys: vec![key],
                    cells: vec![],
                    ordinal,
                })
                .collect();
            heap_sort(&mut rows, |a, b| compare(a, b, &keys), || Ok(())).unwrap();
            let expected = match flags {
                0 => vec![3, 0, 5, 2, 1, 4],
                1 => vec![2, 0, 5, 3, 1, 4],
                2 => vec![1, 4, 3, 0, 5, 2],
                _ => vec![1, 4, 2, 0, 5, 3],
            };
            assert_eq!(rows.iter().map(|r| r.ordinal).collect::<Vec<_>>(), expected);
        }
    }
    #[test]
    fn sort_multikey_is_lexicographic_and_keeps_payload_attached() {
        let keys = [
            Key {
                column: 7,
                descending: false,
                nulls_first: false,
            },
            Key {
                column: 2,
                descending: true,
                nulls_first: true,
            },
        ];
        let data = [
            (Some(1), Some(2)),
            (Some(0), None),
            (Some(1), None),
            (Some(1), Some(3)),
        ];
        let mut rows: Vec<_> = data
            .into_iter()
            .enumerate()
            .map(|(ordinal, (a, b))| Row {
                keys: vec![a, b],
                ordinal,
                cells: vec![OwnedCell {
                    type_id: c"core.type.bytes".to_owned(),
                    bytes: Some(vec![ordinal as u8]),
                }],
            })
            .collect();
        heap_sort(&mut rows, |a, b| compare(a, b, &keys), || Ok(())).unwrap();
        assert_eq!(
            rows.iter().map(|r| r.ordinal).collect::<Vec<_>>(),
            [1, 2, 3, 0]
        );
        for row in rows {
            assert_eq!(row.cells[0].bytes.as_ref().unwrap(), &[row.ordinal as u8]);
        }
    }
    #[test]
    fn heap_sort_matches_reference_and_cancels_during_comparisons() {
        for size in [0, 1, 2, 17, 257, 4096] {
            let mut rows: Vec<_> = (0..size).map(|i| (i * 7919 + 17) % 103).collect();
            let mut expected = rows.clone();
            expected.sort();
            heap_sort(&mut rows, Ord::cmp, || Ok(())).unwrap();
            assert_eq!(rows, expected);
        }
        let mut rows: Vec<_> = (0..4096).rev().collect();
        let mut polls = 0;
        assert_eq!(
            heap_sort(&mut rows, Ord::cmp, || {
                polls += 1;
                if polls == 4 {
                    Err(sys::FAILED_PRECONDITION)
                } else {
                    Ok(())
                }
            }),
            Err(sys::FAILED_PRECONDITION)
        );
        assert_eq!(polls, 4);
        let mut used = BYTES - 1;
        charge(&mut used, 1).unwrap();
        assert!(charge(&mut used, 1).is_err());
        let mut overflow = usize::MAX;
        assert!(charge(&mut overflow, 1).is_err());
    }
}
