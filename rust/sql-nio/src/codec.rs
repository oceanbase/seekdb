// Copyright (c) 2025 OceanBase.
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

pub fn read_lenenc(buf: &[u8], at: usize) -> Option<(u64, usize)> {
    let s = *buf.get(at)?;
    if s < 251 {
        Some((s as u64, 1))
    } else if s == 252 {
        Some((
            u16::from_le_bytes([*buf.get(at + 1)?, *buf.get(at + 2)?]) as u64,
            3,
        ))
    } else if s == 253 {
        let v = (*buf.get(at + 1)? as u64)
            | ((*buf.get(at + 2)? as u64) << 8)
            | ((*buf.get(at + 3)? as u64) << 16);
        Some((v, 4))
    } else if s == 254 {
        let start = at.checked_add(1)?;
        let end = start.checked_add(8)?;
        let encoded: [u8; 8] = buf.get(start..end)?.try_into().ok()?;
        let v = u64::from_le_bytes(encoded);
        Some((v, 9))
    } else {
        None
    }
}
