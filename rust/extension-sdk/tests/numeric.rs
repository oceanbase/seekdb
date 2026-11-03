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

use seekdb_extension::{
    sys,
    table::{Cell, Number},
};

#[test]
fn exact_native_widths_null_and_unaligned_buffers() {
    let cases = [
        (c"core.type.bool", vec![1], Number::Bool(true)),
        (
            c"core.type.int32",
            i32::MIN.to_ne_bytes().to_vec(),
            Number::I32(i32::MIN),
        ),
        (
            c"core.type.uint32",
            u32::MAX.to_ne_bytes().to_vec(),
            Number::U32(u32::MAX),
        ),
        (
            c"core.type.int64",
            i64::MIN.to_ne_bytes().to_vec(),
            Number::I64(i64::MIN),
        ),
        (
            c"core.type.uint64",
            u64::MAX.to_ne_bytes().to_vec(),
            Number::U64(u64::MAX),
        ),
        (
            c"core.type.float64",
            1.25f64.to_ne_bytes().to_vec(),
            Number::F64(1.25),
        ),
    ];
    for (type_id, mut bytes, want) in cases {
        assert!(Number::recognizes(type_id));
        assert_eq!(
            Cell {
                type_id,
                bytes: None
            }
            .number(),
            Ok(None)
        );
        bytes.insert(0, 0);
        assert_eq!(
            Cell {
                type_id,
                bytes: Some(&bytes[1..])
            }
            .number(),
            Ok(Some(want))
        );
        for bad in [&bytes[..], &bytes[2..], &[]] {
            assert_eq!(
                Cell {
                    type_id,
                    bytes: Some(bad)
                }
                .number(),
                Err(sys::INVALID)
            );
        }
    }
}

#[test]
fn canonical_bool_exact_ids_and_floating_bits() {
    for value in [2, 128, 255] {
        assert_eq!(
            Cell {
                type_id: c"core.type.bool",
                bytes: Some(&[value])
            }
            .number(),
            Err(sys::INVALID)
        );
    }
    assert_eq!(
        Cell {
            type_id: c"core.type.bool",
            bytes: Some(&[0])
        }
        .number(),
        Ok(Some(Number::Bool(false)))
    );
    for type_id in [
        c"org.example.bool",
        c"core.type.bytes",
        c"core.type.float32",
        c"core.type.null",
    ] {
        assert!(!Number::recognizes(type_id));
        for bytes in [None, Some(&[0][..])] {
            assert_eq!(Cell { type_id, bytes }.number(), Err(sys::INVALID));
        }
    }
    for bits in [
        (-0.0f64).to_bits(),
        f64::INFINITY.to_bits(),
        0x7ff8_0000_0000_0123,
    ] {
        let bytes = bits.to_ne_bytes();
        let Some(Number::F64(value)) = (Cell {
            type_id: c"core.type.float64",
            bytes: Some(&bytes),
        })
        .number()
        .unwrap() else {
            panic!("wrong numeric variant");
        };
        assert_eq!(value.to_bits(), bits);
    }
}

#[test]
fn gis_compatibility_is_explicit_not_an_arbitrary_suffix() {
    for (type_id, bytes, want) in [
        (c"org.seekdb.gis.scalar.bool", vec![1], Number::Bool(true)),
        (
            c"org.seekdb.gis.scalar.int32",
            i32::MIN.to_ne_bytes().to_vec(),
            Number::I32(i32::MIN),
        ),
        (
            c"org.seekdb.gis.scalar.uint32",
            u32::MAX.to_ne_bytes().to_vec(),
            Number::U32(u32::MAX),
        ),
        (
            c"org.seekdb.gis.scalar.int64",
            i64::MIN.to_ne_bytes().to_vec(),
            Number::I64(i64::MIN),
        ),
        (
            c"org.seekdb.gis.scalar.uint64",
            u64::MAX.to_ne_bytes().to_vec(),
            Number::U64(u64::MAX),
        ),
        (
            c"org.seekdb.gis.scalar.float64",
            1.25f64.to_ne_bytes().to_vec(),
            Number::F64(1.25),
        ),
    ] {
        assert!(Number::recognizes(type_id));
        assert_eq!(
            Cell {
                type_id,
                bytes: Some(&bytes)
            }
            .number(),
            Ok(Some(want))
        );
    }
    for type_id in [
        c"org.seekdb.gis.scalar.fake.bool",
        c"org.seekdb.gis.scalar.boolx",
        c"org.other.int32",
    ] {
        assert!(!Number::recognizes(type_id));
        assert_eq!(
            Cell {
                type_id,
                bytes: None
            }
            .number(),
            Err(sys::INVALID)
        );
    }
}
