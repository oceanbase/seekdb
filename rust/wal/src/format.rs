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

use std::fmt;

pub const PHYSICAL_BLOCK_SIZE: u64 = 1 << 24;
pub const SEGMENT_HEADER_SIZE: u64 = 4096;
pub const BLOCK_SIZE: u64 = PHYSICAL_BLOCK_SIZE - SEGMENT_HEADER_SIZE;
pub const GROUP_HEADER_SIZE: usize = 64;
pub const ENTRY_HEADER_SIZE: usize = 32;
pub const MAX_LOG_BODY_SIZE: usize = 3 * 1024 * 1024 + 512 * 1024;
pub const GROUP_MAGIC: u16 = 0x4752;
pub const ENTRY_MAGIC: u16 = 0x4c48;
pub const GROUP_VERSION: u16 = 1;
pub const ENTRY_VERSION: u16 = 1;
pub const PADDING_FLAG: u32 = 1;
const SEGMENT_MAGIC: &[u8; 8] = b"PFALSEG2";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    NotInit,
    NotFound,
    End,
    OutLower,
    OutUpper,
    Partial,
    Duplicate,
    Corrupt,
    UnknownVersion,
    Io,
    NoSpace,
    NoMemory,
    Closed,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for Error {}

impl From<std::io::Error> for Error {
    fn from(value: std::io::Error) -> Self {
        if value.kind() == std::io::ErrorKind::WriteZero || value.raw_os_error() == Some(28) {
            Self::NoSpace
        } else {
            Self::Io
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;

pub(crate) fn zeroed_bytes(len: usize) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    bytes.try_reserve_exact(len).map_err(|_| Error::NoMemory)?;
    bytes.resize(len, 0);
    Ok(bytes)
}

pub(crate) fn copied_bytes(source: &[u8]) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    bytes
        .try_reserve_exact(source.len())
        .map_err(|_| Error::NoMemory)?;
    bytes.extend_from_slice(source);
    Ok(bytes)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EntryHeader {
    pub data_len: u32,
    pub scn: u64,
    pub data_checksum: u64,
    pub padding: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GroupHeader {
    pub data_len: u32,
    pub committed_end_lsn: u64,
    pub max_scn: u64,
    pub log_id: i64,
    pub data_checksum: u64,
    pub accumulated_checksum: u64,
    pub padding: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SegmentHeader {
    pub start_lsn: u64,
    pub prev_lsn: u64,
    pub prev_scn: u64,
    pub prev_log_id: i64,
    pub prev_accumulated_checksum: u64,
    pub first_scn: u64,
}

#[derive(Clone, Debug)]
pub struct EntryMeta {
    pub offset: usize,
    pub lsn: u64,
    pub header: EntryHeader,
}

pub fn block_id(lsn: u64) -> u64 {
    lsn / BLOCK_SIZE
}

pub fn block_offset(lsn: u64) -> u64 {
    lsn % BLOCK_SIZE
}

pub(crate) fn block_start(lsn: u64) -> u64 {
    lsn - block_offset(lsn)
}

pub fn physical_offset(lsn: u64) -> u64 {
    SEGMENT_HEADER_SIZE + block_offset(lsn)
}

pub fn segment_path(directory: &std::path::Path, id: u64) -> std::path::PathBuf {
    directory.join(format!("{id:016x}.log"))
}

pub fn crc64(bytes: &[u8]) -> u64 {
    crc64_with_seed(0, bytes)
}

pub fn crc64_with_seed(seed: u64, bytes: &[u8]) -> u64 {
    let mut crc = seed;
    let (chunks, remainder) = bytes.as_chunks::<8>();
    for chunk in chunks {
        let value = u64::from_le_bytes(*chunk) ^ crc;
        crc = CRC_TABLES[7][(value & 0xff) as usize]
            ^ CRC_TABLES[6][((value >> 8) & 0xff) as usize]
            ^ CRC_TABLES[5][((value >> 16) & 0xff) as usize]
            ^ CRC_TABLES[4][((value >> 24) & 0xff) as usize]
            ^ CRC_TABLES[3][((value >> 32) & 0xff) as usize]
            ^ CRC_TABLES[2][((value >> 40) & 0xff) as usize]
            ^ CRC_TABLES[1][((value >> 48) & 0xff) as usize]
            ^ CRC_TABLES[0][(value >> 56) as usize];
    }
    for byte in remainder {
        crc = CRC_TABLES[0][((crc ^ u64::from(*byte)) & 0xff) as usize] ^ (crc >> 8);
    }
    crc
}

const fn make_crc_tables() -> [[u64; 256]; 8] {
    let mut tables = [[0; 256]; 8];
    let mut index = 0;
    while index < 256 {
        let mut value = index as u64;
        let mut bit = 0;
        while bit < 8 {
            value = if value & 1 != 0 {
                (value >> 1) ^ 0xc96c5795d7870f42
            } else {
                value >> 1
            };
            bit += 1;
        }
        tables[0][index] = value;
        index += 1;
    }
    let mut table = 1;
    while table < 8 {
        let mut index = 0;
        while index < 256 {
            let previous = tables[table - 1][index];
            tables[table][index] = (previous >> 8) ^ tables[0][(previous & 0xff) as usize];
            index += 1;
        }
        table += 1;
    }
    tables
}

const CRC_TABLES: [[u64; 256]; 8] = make_crc_tables();

fn get_u16(bytes: &[u8], offset: usize) -> Result<u16> {
    let value = bytes.get(offset..offset + 2).ok_or(Error::Corrupt)?;
    Ok(u16::from_le_bytes(value.try_into().unwrap()))
}

fn get_u32(bytes: &[u8], offset: usize) -> Result<u32> {
    let value = bytes.get(offset..offset + 4).ok_or(Error::Corrupt)?;
    Ok(u32::from_le_bytes(value.try_into().unwrap()))
}

fn get_i64(bytes: &[u8], offset: usize) -> Result<i64> {
    let value = bytes.get(offset..offset + 8).ok_or(Error::Corrupt)?;
    Ok(i64::from_le_bytes(value.try_into().unwrap()))
}

fn get_u64(bytes: &[u8], offset: usize) -> Result<u64> {
    let value = bytes.get(offset..offset + 8).ok_or(Error::Corrupt)?;
    Ok(u64::from_le_bytes(value.try_into().unwrap()))
}

fn put_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn put_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn put_i64(bytes: &mut [u8], offset: usize, value: i64) {
    bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
}

fn put_u64(bytes: &mut [u8], offset: usize, value: u64) {
    bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
}

pub fn encode_entry(payload: &[u8], scn: u64, padding: bool) -> Result<Vec<u8>> {
    let mut bytes = zeroed_bytes(ENTRY_HEADER_SIZE + payload.len())?;
    put_u16(&mut bytes, 0, ENTRY_MAGIC);
    put_u16(&mut bytes, 2, ENTRY_VERSION);
    put_u32(&mut bytes, 4, ENTRY_HEADER_SIZE as u32);
    put_u32(&mut bytes, 8, payload.len() as u32);
    put_u32(&mut bytes, 12, if padding { PADDING_FLAG } else { 0 });
    put_u64(&mut bytes, 16, scn);
    put_u64(&mut bytes, 24, crc64(payload));
    bytes[ENTRY_HEADER_SIZE..].copy_from_slice(payload);
    Ok(bytes)
}

pub fn decode_entry_header(bytes: &[u8]) -> Result<EntryHeader> {
    if bytes.len() < ENTRY_HEADER_SIZE {
        return Err(Error::Corrupt);
    }
    if get_u16(bytes, 0)? != ENTRY_MAGIC {
        return Err(Error::Corrupt);
    }
    if get_u16(bytes, 2)? != ENTRY_VERSION {
        return Err(Error::UnknownVersion);
    }
    if get_u32(bytes, 4)? as usize != ENTRY_HEADER_SIZE {
        return Err(Error::Corrupt);
    }
    let data_len = get_u32(bytes, 8)?;
    let flags = get_u32(bytes, 12)?;
    if flags & !PADDING_FLAG != 0 {
        return Err(Error::Corrupt);
    }
    Ok(EntryHeader {
        data_len,
        scn: get_u64(bytes, 16)?,
        data_checksum: get_u64(bytes, 24)?,
        padding: flags & PADDING_FLAG != 0,
    })
}

pub fn group_entries(bytes: &[u8], group_lsn: u64) -> Result<Vec<EntryMeta>> {
    let mut entries = Vec::new();
    let mut offset = 0;
    while offset < bytes.len() {
        if bytes.len() - offset < ENTRY_HEADER_SIZE {
            return Err(Error::Corrupt);
        }
        let header = decode_entry_header(&bytes[offset..])?;
        let end = offset
            .checked_add(ENTRY_HEADER_SIZE)
            .and_then(|value| value.checked_add(header.data_len as usize))
            .ok_or(Error::Corrupt)?;
        if end > bytes.len() {
            return Err(Error::Corrupt);
        }
        if crc64(&bytes[offset + ENTRY_HEADER_SIZE..end]) != header.data_checksum {
            return Err(Error::Corrupt);
        }
        entries.try_reserve(1).map_err(|_| Error::NoMemory)?;
        entries.push(EntryMeta {
            offset,
            lsn: group_lsn + GROUP_HEADER_SIZE as u64 + offset as u64,
            header,
        });
        offset = end;
    }
    if entries.is_empty() {
        return Err(Error::Corrupt);
    }
    Ok(entries)
}

pub fn encode_group(
    payload: &[u8],
    committed_end_lsn: u64,
    max_scn: u64,
    log_id: i64,
    previous_accumulated_checksum: u64,
    padding: bool,
) -> Result<Vec<u8>> {
    let entries = group_entries(payload, 0)?;
    validate_entries(&entries, max_scn, padding)?;
    let data_checksum = fold_entry_checksums(&entries);
    let accumulated_checksum =
        crc64_with_seed(previous_accumulated_checksum, &data_checksum.to_le_bytes());
    let mut bytes = zeroed_bytes(GROUP_HEADER_SIZE + payload.len())?;
    put_u16(&mut bytes, 0, GROUP_MAGIC);
    put_u16(&mut bytes, 2, GROUP_VERSION);
    put_u32(&mut bytes, 4, GROUP_HEADER_SIZE as u32);
    put_u32(&mut bytes, 8, payload.len() as u32);
    put_u32(&mut bytes, 12, if padding { PADDING_FLAG } else { 0 });
    put_u64(&mut bytes, 16, committed_end_lsn);
    put_u64(&mut bytes, 24, max_scn);
    put_i64(&mut bytes, 32, log_id);
    put_u64(&mut bytes, 40, data_checksum);
    put_u64(&mut bytes, 48, accumulated_checksum);
    let header_checksum = crc64(&bytes[..56]);
    put_u64(&mut bytes, 56, header_checksum);
    bytes[GROUP_HEADER_SIZE..].copy_from_slice(payload);
    Ok(bytes)
}

fn fold_entry_checksums(entries: &[EntryMeta]) -> u64 {
    let mut checksum = 0;
    for entry in entries {
        checksum = crc64_with_seed(checksum, &entry.header.data_checksum.to_le_bytes());
    }
    checksum
}

fn validate_entries(entries: &[EntryMeta], max_scn: u64, padding: bool) -> Result<()> {
    let mut previous_scn = None;
    for entry in entries {
        if entry.header.padding != padding || previous_scn.is_some_and(|scn| entry.header.scn < scn)
        {
            return Err(Error::Corrupt);
        }
        previous_scn = Some(entry.header.scn);
    }
    if entries.last().map(|entry| entry.header.scn) != Some(max_scn) {
        return Err(Error::Corrupt);
    }
    Ok(())
}

pub fn decode_group_header(bytes: &[u8]) -> Result<GroupHeader> {
    if bytes.len() < GROUP_HEADER_SIZE {
        return Err(Error::Corrupt);
    }
    if get_u16(bytes, 0)? != GROUP_MAGIC {
        return Err(Error::Corrupt);
    }
    if get_u16(bytes, 2)? != GROUP_VERSION {
        return Err(Error::UnknownVersion);
    }
    if get_u32(bytes, 4)? as usize != GROUP_HEADER_SIZE {
        return Err(Error::Corrupt);
    }
    if crc64(&bytes[..56]) != get_u64(bytes, 56)? {
        return Err(Error::Corrupt);
    }
    let flags = get_u32(bytes, 12)?;
    if flags & !PADDING_FLAG != 0 {
        return Err(Error::Corrupt);
    }
    Ok(GroupHeader {
        data_len: get_u32(bytes, 8)?,
        committed_end_lsn: get_u64(bytes, 16)?,
        max_scn: get_u64(bytes, 24)?,
        log_id: get_i64(bytes, 32)?,
        data_checksum: get_u64(bytes, 40)?,
        accumulated_checksum: get_u64(bytes, 48)?,
        padding: flags & PADDING_FLAG != 0,
    })
}

pub fn validate_group(bytes: &[u8], group_lsn: u64) -> Result<GroupHeader> {
    let header = decode_group_header(bytes)?;
    let expected_len = GROUP_HEADER_SIZE
        .checked_add(header.data_len as usize)
        .ok_or(Error::Corrupt)?;
    if expected_len != bytes.len() || block_offset(group_lsn) + expected_len as u64 > BLOCK_SIZE {
        return Err(Error::Corrupt);
    }
    let entries = group_entries(&bytes[GROUP_HEADER_SIZE..], group_lsn)?;
    validate_entries(&entries, header.max_scn, header.padding)?;
    if fold_entry_checksums(&entries) != header.data_checksum {
        return Err(Error::Corrupt);
    }
    Ok(header)
}

pub fn encode_segment_header(id: u64, header: &SegmentHeader) -> Result<Vec<u8>> {
    let mut bytes = zeroed_bytes(SEGMENT_HEADER_SIZE as usize)?;
    bytes[..8].copy_from_slice(SEGMENT_MAGIC);
    put_u64(&mut bytes, 8, id);
    put_u64(&mut bytes, 16, header.start_lsn);
    put_u64(&mut bytes, 24, header.prev_lsn);
    put_u64(&mut bytes, 32, header.prev_scn);
    put_i64(&mut bytes, 40, header.prev_log_id);
    put_u64(&mut bytes, 48, header.prev_accumulated_checksum);
    put_u64(&mut bytes, 56, header.first_scn);
    let header_checksum = crc64(&bytes[..64]);
    put_u64(&mut bytes, 64, header_checksum);
    Ok(bytes)
}

pub fn validate_segment_header(bytes: &[u8], id: u64) -> Result<SegmentHeader> {
    if bytes.len() < SEGMENT_HEADER_SIZE as usize
        || bytes.get(..8) != Some(SEGMENT_MAGIC)
        || get_u64(bytes, 8)? != id
        || crc64(&bytes[..64]) != get_u64(bytes, 64)?
    {
        return Err(Error::Corrupt);
    }
    Ok(SegmentHeader {
        start_lsn: get_u64(bytes, 16)?,
        prev_lsn: get_u64(bytes, 24)?,
        prev_scn: get_u64(bytes, 32)?,
        prev_log_id: get_i64(bytes, 40)?,
        prev_accumulated_checksum: get_u64(bytes, 48)?,
        first_scn: get_u64(bytes, 56)?,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bytewise_crc64_with_seed(seed: u64, bytes: &[u8]) -> u64 {
        let mut crc = seed;
        for byte in bytes {
            crc = CRC_TABLES[0][((crc ^ u64::from(*byte)) & 0xff) as usize] ^ (crc >> 8);
        }
        crc
    }

    #[test]
    fn sliced_crc_matches_bytewise_crc_at_every_alignment() {
        let bytes: Vec<u8> = (0..131).map(|index| (index * 37 + 11) as u8).collect();
        for seed in [0, 1, u64::MAX, 0x1234_5678_9abc_def0] {
            for len in 0..=bytes.len() {
                assert_eq!(
                    crc64_with_seed(seed, &bytes[..len]),
                    bytewise_crc64_with_seed(seed, &bytes[..len]),
                    "seed={seed:#x} len={len}"
                );
            }
        }
    }

    #[test]
    fn impossible_allocation_is_an_error() {
        assert_eq!(zeroed_bytes(usize::MAX).unwrap_err(), Error::NoMemory);
    }

    #[test]
    fn segment_headers_round_trip_recovery_metadata() {
        let header = SegmentHeader {
            start_lsn: BLOCK_SIZE,
            prev_lsn: BLOCK_SIZE - 128,
            prev_scn: 42,
            prev_log_id: 9,
            prev_accumulated_checksum: 1234,
            first_scn: 43,
        };
        let bytes = encode_segment_header(1, &header).unwrap();
        assert_eq!(validate_segment_header(&bytes, 1), Ok(header));
        let mut damaged = bytes;
        damaged[24] ^= 1;
        assert_eq!(validate_segment_header(&damaged, 1), Err(Error::Corrupt));
    }

    #[test]
    fn records_round_trip_and_detect_damage() {
        let entry = encode_entry(b"transaction", 42, false).unwrap();
        let group = encode_group(&entry, 0, 42, 1, 0, false).unwrap();
        let header = validate_group(&group, 0).unwrap();
        assert_eq!(header.max_scn, 42);
        assert_eq!(
            group_entries(&group[GROUP_HEADER_SIZE..], 0).unwrap()[0].lsn,
            64
        );
        let mut damaged = group;
        damaged[GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE] ^= 1;
        assert_eq!(validate_group(&damaged, 0), Err(Error::Corrupt));
    }

    #[test]
    fn groups_fold_every_entry_and_require_ordered_scns() {
        let mut payload = encode_entry(b"one", 42, false).unwrap();
        payload.extend(encode_entry(b"two", 42, false).unwrap());
        payload.extend(encode_entry(b"three", 43, false).unwrap());
        let group = encode_group(&payload, 0, 43, 1, 0, false).unwrap();
        assert_eq!(
            group_entries(&group[GROUP_HEADER_SIZE..], 0).unwrap().len(),
            3
        );
        assert_eq!(
            encode_group(&payload, 0, 42, 1, 0, false),
            Err(Error::Corrupt)
        );

        let mut reversed = encode_entry(b"two", 43, false).unwrap();
        reversed.extend(encode_entry(b"one", 42, false).unwrap());
        assert_eq!(
            encode_group(&reversed, 0, 43, 1, 0, false),
            Err(Error::Corrupt)
        );
    }

    #[test]
    fn unknown_versions_are_not_torn_writes() {
        let entry = encode_entry(b"x", 1, false).unwrap();
        let mut group = encode_group(&entry, 0, 1, 1, 0, false).unwrap();
        group[2] = 2;
        assert_eq!(decode_group_header(&group), Err(Error::UnknownVersion));
    }
}
