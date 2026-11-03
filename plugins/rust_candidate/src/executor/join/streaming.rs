// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Explicit rescanning integer join. Retains at most one row from each input.
//! SJR1 authorizes repeated right-side execution; the default policy keeps SJE1.
//! Input control alone does not bind parameters or guarantee repeatable rows.
use super::*;
use seekdb_extension::custom_executor::Row;

pub(super) const BUDGET: usize = 16 * 1024 * 1024;
pub(super) fn own(row: Row<'_>, key: Option<usize>, budget: usize) -> Result<(InputRow, usize)> {
    let key = match key {
        Some(key) => integer(row.cell(key)?)?,
        None => None,
    };
    let mut charged = size_of::<InputRow>() + row.len() * size_of::<OwnedCell>();
    if charged > budget {
        return Err(sys::NO_MEMORY);
    }
    let mut cells = Vec::new();
    cells
        .try_reserve_exact(row.len())
        .map_err(|_| sys::NO_MEMORY)?;
    for index in 0..row.len() {
        let cell = row.cell(index)?;
        let id = cell.type_id.to_bytes_with_nul();
        let bytes = cell.bytes.unwrap_or_default();
        if id.len() > budget - charged {
            return Err(sys::NO_MEMORY);
        }
        charged += id.len();
        if bytes.len() > budget - charged {
            return Err(sys::NO_MEMORY);
        }
        charged += bytes.len();
        let mut owned_id = Vec::new();
        owned_id
            .try_reserve_exact(id.len())
            .map_err(|_| sys::NO_MEMORY)?;
        owned_id.extend_from_slice(id);
        let owned_bytes = if cell.bytes.is_some() {
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
            type_id: CString::from_vec_with_nul(owned_id).map_err(|_| sys::INVALID)?,
            bytes: owned_bytes,
        });
    }
    Ok((InputRow { key, cells }, charged))
}
pub(in super::super) struct State {
    plan: Plan,
    left: Option<InputRow>,
    charged: usize,
    ready: bool,
    ended: bool,
}
impl State {
    pub(in super::super) fn open(bytes: &[u8]) -> Result<Self> {
        if !bytes.starts_with(b"SJR1") && !bytes.starts_with(b"SJC1") {
            return Err(sys::INVALID);
        }
        Ok(Self {
            plan: Plan::parse(bytes)?,
            left: None,
            charged: 0,
            ready: false,
            ended: false,
        })
    }
    pub(in super::super) fn next(&mut self, c: &mut Context<'_>) -> Result<Step> {
        if !self.ready {
            if !c.has_input_rescan() || (self.plan.bound && !c.has_input_bindings()) {
                return Err(sys::UNSUPPORTED_ABI);
            }
            self.plan.validate(c)?;
            self.ready = true;
        }
        if self.ended {
            return Ok(Step::End);
        }
        loop {
            c.check_interrupt()?;
            if self.left.is_none() {
                let Some(row) = c.next_input(0)? else {
                    self.ended = true;
                    return Ok(Step::End);
                };
                let (left, charged) =
                    own(row, (!self.plan.bound).then_some(self.plan.keys[0]), BUDGET)?;
                if !self.plan.bound && left.key.is_none() {
                    continue;
                }
                self.left = Some(left);
                self.charged = charged;
                if self.plan.bound {
                    c.bind_rescan_input(1)?;
                } else {
                    c.rescan_input(1)?;
                }
            }
            let Some(row) = c.next_input(1)? else {
                self.left = None;
                self.charged = 0;
                continue;
            };
            let left = self.left.as_ref().unwrap();
            if !self.plan.bound && integer(row.cell(self.plan.keys[1])?)? != left.key {
                continue;
            }
            // emit is another host callback: copy the borrowed right row first.
            // Both retained rows share the same aggregate allocation budget.
            let (right, _) = own(
                row,
                (!self.plan.bound).then_some(self.plan.keys[1]),
                BUDGET - self.charged,
            )?;
            let rows = [left, &right];
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
            c.emit(&output)?;
            return Ok(Step::Row);
        }
    }
    pub(in super::super) fn rescan(&mut self) {
        self.left = None;
        self.charged = 0;
        self.ready = false;
        self.ended = false;
    }
}
