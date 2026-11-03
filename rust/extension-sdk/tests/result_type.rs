// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{boundary, sys, TypeResolution};
use std::{
    ffi::{CStr, CString},
    mem::size_of,
    ptr,
};

fn output() -> sys::ResolvedType {
    sys::ResolvedType {
        struct_size: size_of::<sys::ResolvedType>() as u32,
        type_id: [0; sys::MAX_IDENTIFIER_BYTES + 1],
        reserved: [0; 4],
    }
}

#[test]
fn borrowed_types_and_owned_result() {
    let name = CString::new("org.test.type").unwrap();
    let inputs = [name.as_ptr(), ptr::null()];
    let mut output = output();
    let call = unsafe { TypeResolution::from_raw(inputs.as_ptr(), 2, &mut output) }.unwrap();
    assert_eq!(call.argument_count(), 2);
    assert_eq!(call.argument(0), Ok(Some(name.as_c_str())));
    assert_eq!(call.argument(1), Ok(None));
    assert_eq!(call.argument(2), Err(sys::INVALID));
    call.finish(&name).unwrap();
    drop(name);
    assert_eq!(
        unsafe { CStr::from_ptr(output.type_id.as_ptr()) },
        c"org.test.type"
    );
    assert_eq!(output.reserved, [0; 4]);
}

#[test]
fn invalid_metadata_and_bounded_result() {
    let mut out = output();
    assert!(matches!(
        unsafe { TypeResolution::from_raw(ptr::null(), 1, &mut out) },
        Err(sys::INVALID)
    ));
    assert!(matches!(
        unsafe { TypeResolution::from_raw(ptr::null(), 1025, &mut out) },
        Err(sys::INVALID)
    ));
    assert!(matches!(
        unsafe { TypeResolution::from_raw(ptr::null(), 0, ptr::null_mut()) },
        Err(sys::INVALID)
    ));
    out.struct_size = 0;
    assert!(matches!(
        unsafe { TypeResolution::from_raw(ptr::null(), 0, &mut out) },
        Err(sys::UNSUPPORTED_ABI)
    ));
    out = output();
    out.reserved[3] = 1;
    assert!(matches!(
        unsafe { TypeResolution::from_raw(ptr::null(), 0, &mut out) },
        Err(sys::UNSUPPORTED_ABI)
    ));
    for text in [
        "".to_owned(),
        "Bad.Type".to_owned(),
        "bad type".to_owned(),
        "x".repeat(256),
    ] {
        out = output();
        let bad = CString::new(text).unwrap();
        let pointers = [bad.as_ptr()];
        assert!(matches!(
            unsafe { TypeResolution::from_raw(pointers.as_ptr(), 1, &mut out) },
            Err(sys::INVALID)
        ));
        let call = unsafe { TypeResolution::from_raw(ptr::null(), 0, &mut out) }.unwrap();
        assert_eq!(call.finish(&bad), Err(sys::INVALID));
        assert_eq!(out.type_id[0], 0);
    }
    let longest = CString::new("x".repeat(255)).unwrap();
    unsafe { TypeResolution::from_raw(ptr::null(), 0, &mut out) }
        .unwrap()
        .finish(&longest)
        .unwrap();
    assert_eq!(
        unsafe { CStr::from_ptr(out.type_id.as_ptr()) },
        longest.as_c_str()
    );
}

#[test]
fn resolver_panic_stays_inside_boundary_without_a_result() {
    let mut out = output();
    assert_eq!(
        boundary(|| {
            let _call = unsafe { TypeResolution::from_raw(ptr::null(), 0, &mut out) }?;
            panic!("planning failure");
        }),
        sys::INTERNAL
    );
    assert_eq!(out.type_id[0], 0);
}
