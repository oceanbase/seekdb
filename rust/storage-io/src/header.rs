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

use crate::BlockError;

pub const HEADER_PAGE_SIZE: usize = 4096;
const MAGIC: u64 = 0x424c_4f43_4b46_494c;
const VERSION: u64 = 1;

pub(crate) fn check_block_size(block_size: u64) -> Result<(), BlockError> {
    if block_size < HEADER_PAGE_SIZE as u64
        || !block_size.is_multiple_of(HEADER_PAGE_SIZE as u64)
        || block_size > i64::MAX as u64
    {
        return Err(BlockError::InvalidInput("invalid physical block size"));
    }
    Ok(())
}

fn raw_crc32c(bytes: &[u8]) -> u64 {
    let mut crc = 0u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0x82f6_3b78 & 0u32.wrapping_sub(crc & 1));
        }
    }
    u64::from(crc)
}

pub fn encode_block_header(block_size: u64, page: &mut [u8]) -> Result<(), BlockError> {
    check_block_size(block_size)?;
    if page.len() != HEADER_PAGE_SIZE {
        return Err(BlockError::InvalidInput("header requires one aligned page"));
    }
    page.fill(0);
    page[..8].copy_from_slice(&MAGIC.to_ne_bytes());
    page[8..16].copy_from_slice(&VERSION.to_ne_bytes());
    page[16..24].copy_from_slice(&block_size.to_ne_bytes());
    let crc = raw_crc32c(&page[..24]);
    page[24..32].copy_from_slice(&crc.to_ne_bytes());
    Ok(())
}

pub fn validate_block_header(block_size: u64, page: &[u8]) -> Result<(), BlockError> {
    check_block_size(block_size)?;
    if page.len() != HEADER_PAGE_SIZE {
        return Err(BlockError::InvalidHeader);
    }
    let field = |offset: usize| {
        u64::from_ne_bytes(page[offset..offset + 8].try_into().expect("header field"))
    };
    if field(0) != MAGIC || field(8) != VERSION || field(16) != block_size {
        return Err(BlockError::InvalidHeader);
    }
    if field(24) != raw_crc32c(&page[..24]) {
        return Err(BlockError::ChecksumMismatch);
    }
    Ok(())
}
