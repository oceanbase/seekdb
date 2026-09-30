// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Explicit ordered nested execution, retaining one owned row per input.
//! The planner supplies a topological schedule and authorizes rescans. Retained
//! children evaluate correlated predicates; this is not an arbitrary SQL join.
use super::*;

#[derive(Debug, PartialEq, Eq)]
struct Schedule {
    inputs: Vec<(u32, bool)>,
    outputs: Vec<(usize, usize)>,
}
impl Schedule {
    // SJD1, input count, (physical input, bind flag)*, output count,
    // (physical input, column)*. All words little-endian u32; no graph handles.
    fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() < 12 || bytes.len() > 65536 || &bytes[..4] != b"SJD1" {
            return Err(sys::INVALID);
        }
        let word = |at| -> Result<u32> {
            Ok(u32::from_le_bytes(
                bytes
                    .get(at..at + 4)
                    .ok_or(sys::INVALID)?
                    .try_into()
                    .unwrap(),
            ))
        };
        let count = word(4)? as usize;
        if !(1..=64).contains(&count) || bytes.len() < 12 + count * 8 {
            return Err(sys::INVALID);
        }
        let mut inputs = Vec::new();
        inputs
            .try_reserve_exact(count)
            .map_err(|_| sys::NO_MEMORY)?;
        let mut seen = 0u64;
        for i in 0..count {
            let input = word(8 + i * 8)?;
            let bound = word(12 + i * 8)?;
            if input as usize >= count
                || seen & (1 << input) != 0
                || bound > 1
                || (i == 0 && bound != 0)
            {
                return Err(sys::INVALID);
            }
            seen |= 1 << input;
            inputs.push((input, bound != 0));
        }
        let start = 12 + count * 8;
        let outputs = word(start - 4)? as usize;
        if outputs > 1024 || bytes.len() != start + outputs * 8 {
            return Err(sys::INVALID);
        }
        let mut mapping = Vec::new();
        mapping
            .try_reserve_exact(outputs)
            .map_err(|_| sys::NO_MEMORY)?;
        for i in 0..outputs {
            let input = word(start + i * 8)? as usize;
            let column = word(start + 4 + i * 8)? as usize;
            if input >= count || column >= 1024 {
                return Err(sys::INVALID);
            }
            mapping.push((input, column));
        }
        Ok(Self {
            inputs,
            outputs: mapping,
        })
    }
    fn validate(&self, c: &Context<'_>) -> Result<()> {
        if c.input_count() as usize != self.inputs.len() {
            return Err(sys::INVALID);
        }
        if !c.has_input_rescan() || (self.inputs.iter().any(|i| i.1) && !c.has_input_bindings()) {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let output = c.output_schema()?;
        if output.len() != self.outputs.len() {
            return Err(sys::INVALID);
        }
        // Validate before any child runs, including zero-row schedules.
        for &(input, _) in &self.inputs {
            c.input_schema(input)?;
        }
        for (slot, &(input, column)) in self.outputs.iter().enumerate() {
            let schema = c.input_schema(input as u32)?;
            let a = schema.column(column)?;
            let b = output.column(slot)?;
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
pub(in super::super) struct State {
    plan: Schedule,
    rows: Vec<Option<InputRow>>,
    charges: Vec<usize>,
    depth: usize,
    ready: bool,
    ended: bool,
}
impl State {
    pub(in super::super) fn open(bytes: &[u8]) -> Result<Self> {
        let plan = Schedule::parse(bytes)?;
        let count = plan.inputs.len();
        let mut rows = Vec::new();
        rows.try_reserve_exact(count).map_err(|_| sys::NO_MEMORY)?;
        rows.resize_with(count, || None);
        let mut charges = Vec::new();
        charges
            .try_reserve_exact(count)
            .map_err(|_| sys::NO_MEMORY)?;
        charges.resize(count, 0);
        Ok(Self {
            plan,
            rows,
            charges,
            depth: 0,
            ready: false,
            ended: false,
        })
    }
    pub(in super::super) fn next(&mut self, c: &mut Context<'_>) -> Result<Step> {
        if !self.ready {
            self.plan.validate(c)?;
            self.ready = true;
        }
        if self.ended {
            return Ok(Step::End);
        }
        loop {
            c.check_interrupt()?;
            let (input, _) = self.plan.inputs[self.depth];
            self.rows[input as usize] = None;
            self.charges[input as usize] = 0;
            let Some(row) = c.next_input(input)? else {
                if self.depth == 0 {
                    self.ended = true;
                    return Ok(Step::End);
                }
                self.depth -= 1;
                continue;
            };
            let charged: usize = self.charges.iter().sum();
            let (row, bytes) = streaming::own(row, None, streaming::BUDGET - charged)?;
            self.rows[input as usize] = Some(row);
            self.charges[input as usize] = bytes;
            if self.depth + 1 < self.plan.inputs.len() {
                self.depth += 1;
                let (next, bound) = self.plan.inputs[self.depth];
                if bound {
                    c.bind_rescan_input(next)?;
                } else {
                    c.rescan_input(next)?;
                }
                continue;
            }
            let mut output = Vec::new();
            output
                .try_reserve_exact(self.plan.outputs.len())
                .map_err(|_| sys::NO_MEMORY)?;
            for &(source, column) in &self.plan.outputs {
                let cell = &self.rows[source]
                    .as_ref()
                    .ok_or(sys::FAILED_PRECONDITION)?
                    .cells[column];
                output.push(Cell {
                    type_id: &cell.type_id,
                    bytes: cell.bytes.as_deref(),
                });
            }
            c.emit(&output)?;
            return Ok(Step::Row);
        }
    }
    pub(in super::super) fn rescan(&mut self) {
        for row in &mut self.rows {
            *row = None;
        }
        self.charges.fill(0);
        self.depth = 0;
        self.ready = false;
        self.ended = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn bytes() -> Vec<u8> {
        let mut result = b"SJD1".to_vec();
        for word in [3u32, 2, 0, 0, 1, 1, 1, 2, 2, 3, 1, 0] {
            result.extend_from_slice(&word.to_le_bytes());
        }
        result
    }
    #[test]
    fn schedule_owns_permutation_bind_flags_and_output_order() {
        let plan = Schedule::parse(&bytes()).unwrap();
        assert_eq!(plan.inputs, vec![(2, false), (0, true), (1, true)]);
        assert_eq!(plan.outputs, vec![(2, 3), (1, 0)]);
        let mut state = State::open(&bytes()).unwrap();
        state.depth = 2;
        state.ready = true;
        state.ended = true;
        state.rescan();
        assert_eq!(state.depth, 0);
        assert!(!state.ready && !state.ended);
    }
    #[test]
    fn schedule_rejects_truncation_cycles_in_order_and_invalid_fields() {
        let good = bytes();
        for end in 0..good.len() {
            assert!(Schedule::parse(&good[..end]).is_err());
        }
        let mut extra = good.clone();
        extra.push(0);
        assert!(Schedule::parse(&extra).is_err());
        for (at, value) in [
            (4, 0u32),
            (4, 65),
            (8, 3),
            (12, 1),
            (16, 2),
            (20, 2),
            (32, 1025),
            (36, 3),
            (40, 1024),
        ] {
            let mut bad = good.clone();
            bad[at..at + 4].copy_from_slice(&value.to_le_bytes());
            assert!(Schedule::parse(&bad).is_err(), "field {at}={value}");
        }
    }
}
