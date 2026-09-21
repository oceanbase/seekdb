use seekdb_extension::{
    sys,
    type_comparison::{Comparator, Service},
    Result,
};
use std::{cmp::Ordering, ffi::CStr, mem::size_of, ptr};

struct Probe {
    calls: usize,
    mode: u8,
}
struct Compare;
impl Comparator for Compare {
    fn compare(
        instance: *mut sys::Handle,
        id: &CStr,
        left: &[u8],
        right: &[u8],
    ) -> Result<Ordering> {
        assert_eq!(id, c"test.type");
        // Test owns the instance exclusively; no reference spans reentry.
        let probe = unsafe { &mut *instance.cast::<Probe>() };
        probe.calls += 1;
        match probe.mode {
            1 => Err(sys::TIMEOUT),
            2 => panic!("comparison panic"),
            _ => Ok(left.cmp(right)),
        }
    }
}
const SERVICE: sys::TypeCodecServiceV2 = Service::<Compare>::with_codec(sys::TypeCodecService {
    struct_size: size_of::<sys::TypeCodecService>() as u32,
    spi_major: 1,
    spi_minor: 0,
    reserved_word: 0,
    decode: None,
    encode: None,
    reserved: [0; 8],
});
fn value(bytes: &[u8]) -> sys::Value {
    sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"test.type".as_ptr(),
        data: bytes.as_ptr(),
        data_size: bytes.len() as u64,
        is_null: 0,
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    }
}
fn output() -> sys::TypeComparison {
    sys::TypeComparison {
        struct_size: size_of::<sys::TypeComparison>() as u32,
        ordering: 99,
        reserved: [42; 4],
    }
}
#[test]
fn ordering_errors_and_panics_have_initialized_outputs() {
    assert_eq!(
        SERVICE.v1.struct_size,
        size_of::<sys::TypeCodecServiceV2>() as u32
    );
    assert_eq!(SERVICE.v1.spi_minor, 1);
    let mut probe = Probe { calls: 0, mode: 0 };
    for (left, right, expected) in [
        (b"a".as_slice(), b"b".as_slice(), -1),
        (b"x", b"x", 0),
        (b"b", b"a", 1),
        (b"", b"", 0),
    ] {
        let mut out = output();
        let status = unsafe {
            SERVICE.compare.unwrap()(
                (&mut probe as *mut Probe).cast(),
                &value(left),
                &value(right),
                &mut out,
            )
        };
        assert_eq!(status, sys::OK);
        assert_eq!(out.ordering, expected);
        assert_eq!(out.reserved, [0; 4]);
    }
    for (mode, expected) in [(1, sys::TIMEOUT), (2, sys::INTERNAL)] {
        probe.mode = mode;
        let mut out = output();
        let status = unsafe {
            SERVICE.compare.unwrap()(
                (&mut probe as *mut Probe).cast(),
                &value(b"a"),
                &value(b"b"),
                &mut out,
            )
        };
        assert_eq!(status, expected);
        assert_eq!(out.ordering, 0);
        assert_eq!(out.reserved, [0; 4]);
    }
    assert_eq!(probe.calls, 6);
}
#[test]
fn invalid_metadata_never_reaches_comparator() {
    let mut probe = Probe { calls: 0, mode: 0 };
    for scenario in 0..11 {
        let mut left = value(b"a");
        let mut right = value(b"b");
        let mut out = output();
        match scenario {
            0 => left.struct_size = 0,
            1 => left.is_null = 1,
            2 => left.type_id = ptr::null(),
            3 => left.reserved[1] = 1,
            4 => left.reserved_bytes[3] = 1,
            5 => left.data_size = 16 * 1024 * 1024 + 1,
            6 => left.data = ptr::null(),
            7 => right.type_id = c"other.type".as_ptr(),
            8 => left.type_id = c"INVALID".as_ptr(),
            9 => out.struct_size = 0,
            _ => (),
        }
        let status = unsafe {
            SERVICE.compare.unwrap()(
                (&mut probe as *mut Probe).cast(),
                if scenario == 10 { ptr::null() } else { &left },
                &right,
                &mut out,
            )
        };
        assert_eq!(status, sys::INVALID);
        assert_eq!(probe.calls, 0);
        assert_eq!(out.ordering, if scenario == 9 { 99 } else { 0 });
    }
}
