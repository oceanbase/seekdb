// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Independent persistent TYPE: runtime UTF-8, storage RUT/v1 + inverted bytes.
//! Never changes the existing nonpersistent rust_utf8 identity or format.
use seekdb_extension::{
    boundary, sys, Call, CastContext, CastDefinition, Registration, TypeDefinition,
};
use std::ffi::CStr;
use std::mem::size_of;

pub const TYPE_ID: &CStr = c"org.seekdb.rust-text.stored-utf8";
const HEADER: &[u8; 4] = b"RUT\x01";
const MAX_BYTES: usize = 16 * 1024 * 1024;

pub fn register(registration: &mut Registration<'_>) -> seekdb_extension::Result<()> {
    registration.data_type(&TypeDefinition {
        object_id: TYPE_ID,
        sql_name: c"rust_stored_utf8",
        physical_format_id: c"org.seekdb.rust-text.stored-utf8.v1",
        physical_format_version: 1,
        flags: sys::PERSISTENT | sys::REQUIRES_CATALOG,
        codec: super::implementation(c"org.seekdb.rust-text.stored-codec"),
    })?;
    for (id, source, target, context, service) in [
        (
            c"org.seekdb.rust-text.bytes-to-stored",
            c"core.type.bytes",
            TYPE_ID,
            CastContext::Explicit,
            c"org.seekdb.rust-text.stored-from-bytes",
        ),
        (
            c"org.seekdb.rust-text.stored-to-bytes",
            TYPE_ID,
            c"core.type.bytes",
            CastContext::Implicit,
            c"org.seekdb.rust-text.stored-to-bytes",
        ),
    ] {
        registration.cast(&CastDefinition {
            object_id: id,
            source_type_id: source,
            target_type_id: target,
            context,
            cost: 1,
            flags: sys::DETERMINISTIC | sys::IMMUTABLE,
            implementation: super::implementation(service),
        })?;
    }
    Ok(())
}

fn encode_payload(input: &[u8]) -> seekdb_extension::Result<Vec<u8>> {
    if input.len() > MAX_BYTES - HEADER.len() {
        return Err(sys::INVALID);
    }
    std::str::from_utf8(input).map_err(|_| sys::INVALID)?;
    let mut encoded = Vec::new();
    encoded
        .try_reserve_exact(HEADER.len() + input.len())
        .map_err(|_| sys::NO_MEMORY)?;
    encoded.extend_from_slice(HEADER);
    encoded.extend(input.iter().map(|byte| !byte));
    Ok(encoded)
}

fn decode_payload(input: &[u8]) -> seekdb_extension::Result<Vec<u8>> {
    if input.len() > MAX_BYTES || !input.starts_with(HEADER) {
        return Err(sys::INVALID);
    }
    let mut decoded = Vec::new();
    decoded
        .try_reserve_exact(input.len() - HEADER.len())
        .map_err(|_| sys::NO_MEMORY)?;
    decoded.extend(input[HEADER.len()..].iter().map(|byte| !byte));
    std::str::from_utf8(&decoded).map_err(|_| sys::INVALID)?;
    Ok(decoded)
}

enum Operation {
    FromBytes,
    ToBytes,
    Encode,
    Decode,
}

unsafe fn transform(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
    operation: Operation,
) -> sys::Status {
    boundary(|| {
        super::validate_instance(handle)?;
        let mut call = unsafe { Call::from_raw(context, values, count) }?;
        if call.argument_count() != 1 {
            return Err(sys::INVALID);
        }
        let from_bytes = matches!(operation, Operation::FromBytes | Operation::Decode);
        let input = call.bytes(
            0,
            if from_bytes {
                c"core.type.bytes"
            } else {
                TYPE_ID
            },
        )?;
        let output_type = if from_bytes {
            TYPE_ID
        } else {
            c"core.type.bytes"
        };
        match operation {
            Operation::Encode | Operation::Decode => {
                let output = input
                    .map(|bytes| {
                        if matches!(operation, Operation::Encode) {
                            encode_payload(bytes)
                        } else {
                            decode_payload(bytes)
                        }
                    })
                    .transpose()?;
                call.emit_bytes(output_type, output.as_deref())
            }
            Operation::FromBytes | Operation::ToBytes => {
                if let Some(bytes) = input {
                    std::str::from_utf8(bytes).map_err(|_| sys::INVALID)?;
                }
                call.emit_bytes(output_type, input)
            }
        }
    })
}

unsafe extern "C" fn from_bytes(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
) -> sys::Status {
    unsafe { transform(handle, context, values, count, Operation::FromBytes) }
}
unsafe extern "C" fn to_bytes(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
) -> sys::Status {
    unsafe { transform(handle, context, values, count, Operation::ToBytes) }
}
unsafe extern "C" fn encode(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    value: *const sys::Value,
) -> sys::Status {
    if context.is_null() || unsafe { (*context).struct_size } != size_of::<sys::ContextV1>() as u32
    {
        return sys::UNSUPPORTED_ABI;
    }
    unsafe { transform(handle, context, value, 1, Operation::Encode) }
}
unsafe extern "C" fn decode(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    encoded: *const u8,
    size: u64,
) -> sys::Status {
    if context.is_null() || unsafe { (*context).struct_size } != size_of::<sys::ContextV1>() as u32
    {
        return sys::UNSUPPORTED_ABI;
    }
    let value = sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"core.type.bytes".as_ptr(),
        data: encoded,
        data_size: size,
        is_null: 0,
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    };
    unsafe { transform(handle, context, &value, 1, Operation::Decode) }
}

struct Comparator;
impl seekdb_extension::type_comparison::Comparator for Comparator {
    fn compare(
        handle: *mut sys::Handle,
        logical_type: &CStr,
        left: &[u8],
        right: &[u8],
    ) -> seekdb_extension::Result<std::cmp::Ordering> {
        super::validate_instance(handle)?;
        if logical_type != TYPE_ID {
            return Err(sys::INVALID);
        }
        let left = std::str::from_utf8(left).map_err(|_| sys::INVALID)?;
        let right = std::str::from_utf8(right).map_err(|_| sys::INVALID)?;
        Ok(left
            .chars()
            .count()
            .cmp(&right.chars().count())
            .then_with(|| left.cmp(right)))
    }
}
static CODEC: sys::TypeCodecServiceV2 =
    seekdb_extension::type_comparison::Service::<Comparator>::with_codec(sys::TypeCodecService {
        struct_size: size_of::<sys::TypeCodecService>() as u32,
        spi_major: 1,
        spi_minor: 0,
        reserved_word: 0,
        decode: Some(decode),
        encode: Some(encode),
        reserved: [0; 8],
    });
static FROM: sys::FunctionService = sys::FunctionService {
    struct_size: size_of::<sys::FunctionService>() as u32,
    spi_major: 1,
    spi_minor: 0,
    reserved_word: 0,
    execute: Some(from_bytes),
    reserved: [0; 8],
};
static TO: sys::FunctionService = sys::FunctionService {
    struct_size: size_of::<sys::FunctionService>() as u32,
    spi_major: 1,
    spi_minor: 0,
    reserved_word: 0,
    execute: Some(to_bytes),
    reserved: [0; 8],
};

pub const fn provide(index: usize) -> sys::ServiceProvide {
    let (id, service) = match index {
        0 => (
            c"org.seekdb.rust-text.stored-codec",
            (&CODEC as *const sys::TypeCodecServiceV2).cast(),
        ),
        1 => (
            c"org.seekdb.rust-text.stored-from-bytes",
            (&FROM as *const sys::FunctionService).cast(),
        ),
        2 => (
            c"org.seekdb.rust-text.stored-to-bytes",
            (&TO as *const sys::FunctionService).cast(),
        ),
        _ => panic!("invalid stored-text service"),
    };
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: id.as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service,
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn codec_roundtrip_and_rejection() {
        for bytes in [b"".as_slice(), b"z", b"aa", "中🙂".as_bytes(), b"a\0b"] {
            let encoded = encode_payload(bytes).unwrap();
            assert!(encoded.starts_with(HEADER));
            assert_eq!(encoded.len(), bytes.len() + 4);
            assert_eq!(decode_payload(&encoded).unwrap(), bytes);
        }
        assert!(encode_payload(&[255]).is_err());
        for invalid in [b"".as_slice(), b"RUT", b"RUT\x02abc", b"RUT\x01\x00"] {
            assert!(decode_payload(invalid).is_err());
        }
        assert!(encode_payload(b"a").unwrap() > encode_payload(b"z").unwrap());
    }
    #[test]
    fn codec_size_bound() {
        let maximum = vec![b'a'; MAX_BYTES - HEADER.len()];
        let encoded = encode_payload(&maximum).unwrap();
        assert_eq!(encoded.len(), MAX_BYTES);
        assert_eq!(decode_payload(&encoded).unwrap(), maximum);
        assert!(encode_payload(&vec![b'a'; MAX_BYTES - HEADER.len() + 1]).is_err());
        assert!(decode_payload(&vec![0; MAX_BYTES + 1]).is_err());
    }
}
