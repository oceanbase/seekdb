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

const CRC32C_POLYNOMIAL: u32 = 0x82f6_3b78;

const fn crc32c_table() -> [u32; 256] {
    let mut table = [0; 256];
    let mut index = 0;
    while index < table.len() {
        let mut value = index as u32;
        let mut bit = 0;
        while bit < 8 {
            value = if value & 1 == 1 {
                (value >> 1) ^ CRC32C_POLYNOMIAL
            } else {
                value >> 1
            };
            bit += 1;
        }
        table[index] = value;
        index += 1;
    }
    table
}

static CRC32C_TABLE: [u32; 256] = crc32c_table();

/// Raw Castagnoli state used by ob_crc64_sse42. There is no final XOR.
pub(crate) fn crc32c(state: u64, bytes: &[u8]) -> u64 {
    let mut crc = state as u32;
    for &byte in bytes {
        crc = (crc >> 8) ^ CRC32C_TABLE[((crc ^ u32::from(byte)) & 0xff) as usize];
    }
    u64::from(crc)
}

pub(crate) fn fold_u32(checksum: &mut u16, value: u32) {
    *checksum ^= value as u16;
    *checksum ^= (value >> 16) as u16;
}

pub(crate) fn fold_u64(checksum: &mut u16, value: u64) {
    fold_u32(checksum, value as u32);
    fold_u32(checksum, (value >> 32) as u32);
}

#[cfg(test)]
mod tests {
    use super::crc32c;

    #[test]
    fn crc32c_matches_standard_vector_without_final_xor() {
        assert_eq!(crc32c(0, b"123456789"), 0x58e3_fa20);
        assert_eq!(crc32c(0x58e3_fa20, b""), 0x58e3_fa20);
    }
}
