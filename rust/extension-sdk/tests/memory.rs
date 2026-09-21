// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{memory::HostAllocator, sys};
use std::alloc::{alloc, dealloc, Layout};
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::c_void;
use std::mem::size_of;
use std::ptr;

#[derive(Default)]
struct Ledger {
    live: HashMap<usize, (u64, u32)>,
    bytes: u64,
    allocations: u32,
    frees: u32,
}
struct Host {
    ledger: RefCell<Ledger>,
    limit: u64,
}
impl Host {
    fn new(limit: u64) -> Self {
        Self {
            ledger: RefCell::default(),
            limit,
        }
    }
    fn api(&self) -> sys::HostApiV1 {
        let mut api: sys::HostApiV1 = unsafe { std::mem::zeroed() };
        api.struct_size = size_of::<sys::HostApiV1>() as u32;
        api.abi_major = 1;
        api.host_handle = (self as *const Self).cast_mut().cast();
        api.alloc = Some(allocate);
        api.free = Some(release);
        api
    }
}
unsafe extern "C" fn allocate(host: *mut c_void, bytes: u64, alignment: u32) -> *mut c_void {
    let host = unsafe { &*host.cast::<Host>() };
    let mut ledger = host.ledger.borrow_mut();
    ledger.allocations += 1;
    if bytes > host.limit - ledger.bytes {
        return ptr::null_mut();
    }
    let memory =
        unsafe { alloc(Layout::from_size_align(bytes as usize, alignment as usize).unwrap()) };
    if !memory.is_null() {
        assert!(ledger
            .live
            .insert(memory as usize, (bytes, alignment))
            .is_none());
        ledger.bytes += bytes;
    }
    memory.cast()
}
unsafe extern "C" fn release(host: *mut c_void, memory: *mut c_void, bytes: u64, alignment: u32) {
    let host = unsafe { &*host.cast::<Host>() };
    let mut ledger = host.ledger.borrow_mut();
    assert_eq!(
        ledger.live.remove(&(memory as usize)),
        Some((bytes, alignment))
    );
    unsafe {
        dealloc(
            memory.cast(),
            Layout::from_size_align(bytes as usize, alignment as usize).unwrap(),
        )
    };
    ledger.bytes -= bytes;
    ledger.frees += 1;
}

#[test]
fn initialized_aligned_bytes_and_matching_drop() {
    let host = Host::new(4096);
    let api = host.api();
    let allocator = unsafe { HostAllocator::from_raw(&api) }.unwrap();
    let mut buffer = allocator.zeroed(17, 128).unwrap();
    assert_eq!(buffer.as_ptr() as usize % 128, 0);
    assert_eq!(&*buffer, &[0; 17]);
    buffer[16] = 9;
    assert_eq!(buffer[16], 9);
    assert_eq!(host.ledger.borrow().bytes, 17);
    drop(buffer);
    assert_eq!(host.ledger.borrow().bytes, 0);
    assert_eq!(host.ledger.borrow().frees, 1);
}

#[test]
fn copied_sources_can_change_and_empty_parts_need_no_allocation() {
    let host = Host::new(100);
    let api = host.api();
    let allocator = unsafe { HostAllocator::from_raw(&api) }.unwrap();
    let mut source = vec![1, 0, 255];
    let first = allocator.copy_from_slice(&source).unwrap();
    source.fill(9);
    let second = allocator
        .copy_from_slices(&[&first, &[], "中".as_bytes(), &first])
        .unwrap();
    assert_eq!(&*first, &[1, 0, 255]);
    assert_eq!(&*second, &[1, 0, 255, 0xe4, 0xb8, 0xad, 1, 0, 255]);
    for empty in [
        allocator.zeroed(0, 64),
        allocator.copy_from_slices(&[]),
        allocator.copy_from_slice(&[]),
    ] {
        assert!(empty.unwrap().is_empty());
    }
    for alignment in [1, 8, 64, 4096, 1 << 31] {
        let empty = allocator.zeroed(0, alignment).unwrap();
        assert_eq!(empty.as_ptr() as usize % alignment as usize, 0);
    }
    assert_eq!(host.ledger.borrow().allocations, 2);
    drop((first, second));
    assert_eq!(host.ledger.borrow().frees, 2);
}

#[test]
fn quota_failure_preserves_live_bytes_and_can_be_retried_after_drop() {
    let host = Host::new(8);
    let api = host.api();
    let allocator = unsafe { HostAllocator::from_raw(&api) }.unwrap();
    let first = allocator.copy_from_slice(b"12345678").unwrap();
    assert!(matches!(allocator.zeroed(1, 1), Err(sys::NO_MEMORY)));
    assert_eq!(&*first, b"12345678");
    assert_eq!(host.ledger.borrow().frees, 0);
    drop(first);
    drop(allocator.zeroed(8, 8).unwrap());
    assert_eq!(host.ledger.borrow().bytes, 0);
    assert_eq!(host.ledger.borrow().frees, 2);
}

#[test]
fn invalid_layouts_are_rejected_before_host_callback() {
    let host = Host::new(u64::MAX);
    let api = host.api();
    let allocator = unsafe { HostAllocator::from_raw(&api) }.unwrap();
    for (length, alignment) in [(0, 0), (1, 3), (usize::MAX, 1), (isize::MAX as usize, 4096)] {
        assert!(matches!(
            allocator.zeroed(length, alignment),
            Err(sys::INVALID)
        ));
    }
    assert_eq!(host.ledger.borrow().allocations, 0);
}

#[test]
fn early_error_and_unwind_release_each_buffer_once() {
    let host = Host::new(100);
    let api = host.api();
    let allocator = unsafe { HostAllocator::from_raw(&api) }.unwrap();
    let fail = || -> seekdb_extension::Result<()> {
        let _bytes = allocator.copy_from_slice(b"error").unwrap();
        Err(sys::INVALID)
    };
    assert_eq!(fail(), Err(sys::INVALID));
    assert_eq!(
        seekdb_extension::boundary(|| {
            let _bytes = allocator.zeroed(32, 16)?;
            panic!("unwind-mode fixture");
        }),
        sys::INTERNAL
    );
    assert_eq!(host.ledger.borrow().bytes, 0);
    assert_eq!(host.ledger.borrow().frees, 2);
}

#[test]
fn tables_require_valid_prefix_version_owner_and_callback_pair() {
    assert!(matches!(
        unsafe { HostAllocator::from_raw(ptr::null()) },
        Err(sys::INVALID)
    ));
    #[repr(C, align(8))]
    struct Prefix(u32);
    let prefix = Prefix(4);
    assert!(matches!(
        unsafe { HostAllocator::from_raw((&prefix as *const Prefix).cast()) },
        Err(sys::UNSUPPORTED_ABI)
    ));
    let host = Host::new(1);
    for variant in 0..4 {
        let mut api = host.api();
        match variant {
            0 => api.abi_major = 2,
            1 => api.alloc = None,
            2 => api.free = None,
            _ => api.host_handle = ptr::null_mut(),
        }
        assert!(
            matches!(unsafe { HostAllocator::from_raw(&api) }, Err(status) if status ==
            if variant == 3 { sys::INVALID } else { sys::UNSUPPORTED_ABI })
        );
    }
    assert_eq!(host.ledger.borrow().allocations, 0);
}
