// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Linked-host identity, not an integrity checksum, signature, or proof that a
//! plugin used the correct server headers.
use crate::{INVALID, OK, STATE_MISMATCH};
use std::io::{self, Read, Seek, SeekFrom};
use std::path::Path;
use std::sync::OnceLock;
fn invalid() -> io::Error {
    io::Error::new(
        io::ErrorKind::InvalidData,
        "unsupported or malformed ELF build contract",
    )
}
fn u16_at(b: &[u8], i: usize) -> u16 {
    u16::from_le_bytes(b[i..i + 2].try_into().unwrap())
}
fn u32_at(b: &[u8], i: usize) -> u32 {
    u32::from_le_bytes(b[i..i + 4].try_into().unwrap())
}
fn u64_at(b: &[u8], i: usize) -> u64 {
    u64::from_le_bytes(b[i..i + 8].try_into().unwrap())
}
fn aligned(n: usize) -> io::Result<usize> {
    n.checked_add(3).map(|n| n & !3).ok_or_else(invalid)
}
fn from_elf<R: Read + Seek>(input: &mut R) -> io::Result<Vec<u8>> {
    let size = input.seek(SeekFrom::End(0))?;
    input.seek(SeekFrom::Start(0))?;
    let mut header = [0u8; 64];
    input.read_exact(&mut header)?;
    if &header[..7] != b"\x7fELF\x02\x01\x01"
        || u16_at(&header, 52) != 64
        || u16_at(&header, 54) != 56
        || !matches!(u16_at(&header, 16), 2 | 3)
        || u32_at(&header, 20) != 1
    {
        return Err(invalid());
    }
    let count = u16_at(&header, 56) as u64;
    let table = u64_at(&header, 32);
    if count == 0
        || count > 1024
        || table < 64
        || table
            .checked_add(count * 56)
            .filter(|end| *end <= size)
            .is_none()
    {
        return Err(invalid());
    }
    let mut found = None;
    let mut note_bytes = 0u64;
    for i in 0..count {
        input.seek(SeekFrom::Start(table + i * 56))?;
        let mut ph = [0u8; 56];
        input.read_exact(&mut ph)?;
        if u32_at(&ph, 0) != 4 {
            continue;
        }
        let offset = u64_at(&ph, 8);
        let length = u64_at(&ph, 32);
        note_bytes = note_bytes.checked_add(length).ok_or_else(invalid)?;
        if note_bytes > 1024 * 1024
            || offset
                .checked_add(length)
                .filter(|end| *end <= size)
                .is_none()
        {
            return Err(invalid());
        }
        let mut notes = Vec::new();
        notes
            .try_reserve_exact(length as usize)
            .map_err(|_| io::Error::other("ELF note allocation failed"))?;
        notes.resize(length as usize, 0);
        input.seek(SeekFrom::Start(offset))?;
        input.read_exact(&mut notes)?;
        let mut pos = 0usize;
        while pos < notes.len() {
            if notes.len() - pos < 12 {
                return Err(invalid());
            }
            let namesz = u32_at(&notes, pos) as usize;
            let descsz = u32_at(&notes, pos + 4) as usize;
            let kind = u32_at(&notes, pos + 8);
            let name = pos + 12;
            let desc = name.checked_add(aligned(namesz)?).ok_or_else(invalid)?;
            let end = desc.checked_add(aligned(descsz)?).ok_or_else(invalid)?;
            if end > notes.len() {
                return Err(invalid());
            }
            if kind == 3 && &notes[name..name + namesz] == b"GNU\0" {
                if descsz == 0 || descsz > 64 || found.is_some() {
                    return Err(invalid());
                }
                found = Some(notes[desc..desc + descsz].to_vec());
            }
            pos = end;
        }
    }
    found.ok_or_else(invalid)
}
/// Bounded ELF64 little-endian PT_NOTE reader; no subprocess or whole-file read.
pub fn read_build_id(path: &Path) -> io::Result<Vec<u8>> {
    from_elf(&mut std::fs::File::open(path)?)
}
/// Match the running host, not a plugin-selected path. Public activations never
/// call this; only successful reads are cached. Non-Linux hosts fail closed.
/// # Safety
/// expected addresses a live immutable span of length bytes for this call.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_match_host_build_id(
    expected: *const u8,
    length: u32,
) -> i32 {
    if expected.is_null() || length == 0 || length > 64 {
        return INVALID;
    }
    static HOST: OnceLock<Vec<u8>> = OnceLock::new();
    if HOST.get().is_none() {
        #[cfg(target_os = "linux")]
        let current = read_build_id(Path::new("/proc/self/exe"));
        #[cfg(not(target_os = "linux"))]
        let current: io::Result<Vec<u8>> = Err(invalid());
        let Ok(current) = current else {
            return STATE_MISMATCH;
        };
        let _ = HOST.set(current);
    }
    if HOST.get().unwrap().as_slice()
        == unsafe { std::slice::from_raw_parts(expected, length as usize) }
    {
        OK
    } else {
        STATE_MISMATCH
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;
    fn fixture() -> Vec<u8> {
        let mut b = vec![0u8; 156];
        b[..7].copy_from_slice(b"\x7fELF\x02\x01\x01");
        b[16..18].copy_from_slice(&3u16.to_le_bytes());
        b[20..24].copy_from_slice(&1u32.to_le_bytes());
        b[32..40].copy_from_slice(&64u64.to_le_bytes());
        b[52..54].copy_from_slice(&64u16.to_le_bytes());
        b[54..56].copy_from_slice(&56u16.to_le_bytes());
        b[56..58].copy_from_slice(&1u16.to_le_bytes());
        b[64..68].copy_from_slice(&4u32.to_le_bytes());
        b[72..80].copy_from_slice(&120u64.to_le_bytes());
        b[96..104].copy_from_slice(&36u64.to_le_bytes());
        b[120..124].copy_from_slice(&4u32.to_le_bytes());
        b[124..128].copy_from_slice(&20u32.to_le_bytes());
        b[128..132].copy_from_slice(&3u32.to_le_bytes());
        b[132..136].copy_from_slice(b"GNU\0");
        b[136..156].fill(0xab);
        b
    }
    #[test]
    fn reads_linked_identity_without_section_headers() {
        assert_eq!(
            from_elf(&mut Cursor::new(fixture())).unwrap(),
            vec![0xab; 20]
        );
    }
    #[test]
    fn every_truncation_and_malformed_bound_is_rejected() {
        let valid = fixture();
        for end in 0..valid.len() {
            assert!(from_elf(&mut Cursor::new(&valid[..end])).is_err());
        }
        for index in [
            0, 4, 5, 6, 16, 20, 32, 39, 52, 54, 56, 57, 64, 79, 103, 120, 124, 128, 132,
        ] {
            let mut b = valid.clone();
            b[index] = 255;
            assert!(from_elf(&mut Cursor::new(b)).is_err(), "offset {index}");
        }
        let mut duplicate = valid.clone();
        duplicate.extend_from_slice(&valid[120..]);
        duplicate[96..104].copy_from_slice(&72u64.to_le_bytes());
        assert!(from_elf(&mut Cursor::new(duplicate)).is_err());
    }
    #[cfg(target_os = "linux")]
    #[test]
    fn matches_running_executable_and_rejects_changed_identity() {
        let mut id = read_build_id(Path::new("/proc/self/exe")).unwrap();
        assert_eq!(
            unsafe { seekdb_runtime_match_host_build_id(id.as_ptr(), id.len() as u32) },
            OK
        );
        id[0] ^= 1;
        assert_eq!(
            unsafe { seekdb_runtime_match_host_build_id(id.as_ptr(), id.len() as u32) },
            STATE_MISMATCH
        );
        assert_eq!(
            unsafe { seekdb_runtime_match_host_build_id(std::ptr::null(), 0) },
            INVALID
        );
    }
}
