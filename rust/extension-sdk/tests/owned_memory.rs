// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    memory::{HostAllocator, OwnedHostBuffer},
    sys,
};
use std::alloc::{alloc, dealloc, Layout};
use std::mem::size_of;
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

struct Token {
    data: *mut u8,
    layout: Layout,
    released: Arc<AtomicUsize>,
}
struct Host {
    released: Arc<AtomicUsize>,
    mode: u32,
}
unsafe extern "C" fn release(owner: *mut sys::Handle) {
    let token = unsafe { Box::from_raw(owner.cast::<Token>()) };
    unsafe { dealloc(token.data, token.layout) };
    token.released.fetch_add(1, Ordering::SeqCst);
}
unsafe extern "C" fn allocate(
    host: *mut sys::Handle,
    size: u64,
    alignment: u32,
    out: *mut sys::OwnedBytesV1,
) -> sys::Status {
    let host = unsafe { &*host.cast::<Host>() };
    unsafe { *out = sys::OwnedBytesV1::default() };
    if host.mode == 1 {
        return sys::NO_MEMORY;
    }
    let layout = Layout::from_size_align(size as usize, alignment as usize).unwrap();
    let data = unsafe { alloc(layout) };
    if data.is_null() {
        return sys::NO_MEMORY;
    }
    let token = Box::new(Token {
        data,
        layout,
        released: host.released.clone(),
    });
    unsafe {
        *out = sys::OwnedBytesV1 {
            struct_size: size_of::<sys::OwnedBytesV1>() as u32,
            alignment,
            size: size + u64::from(host.mode == 2),
            data,
            owner: Box::into_raw(token).cast(),
            release: Some(release),
            reserved: [0; 4],
        }
    };
    sys::OK
}
unsafe extern "C" fn raw_alloc(_: *mut sys::Handle, _: u64, _: u32) -> *mut std::ffi::c_void {
    panic!("owned request used borrowed alloc");
}
unsafe extern "C" fn raw_free(_: *mut sys::Handle, _: *mut std::ffi::c_void, _: u64, _: u32) {
    panic!("owned request used borrowed free");
}
fn api(host: &Host) -> sys::HostApiV3 {
    let mut api: sys::HostApiV3 = unsafe { std::mem::zeroed() };
    api.v2.host.struct_size = size_of::<sys::HostApiV3>() as u32;
    api.v2.host.abi_major = 1;
    api.v2.host.host_handle = (host as *const Host).cast_mut().cast();
    api.v2.host.alloc = Some(raw_alloc);
    api.v2.host.free = Some(raw_free);
    api.memory_spi_major = 1;
    api.allocate_owned_bytes = Some(allocate);
    api
}

#[test]
fn bytes_outlive_allocator_table_and_host_and_drop_on_another_thread() {
    fn require_send_sync<T: Send + Sync>() {}
    require_send_sync::<OwnedHostBuffer>();
    let released = Arc::new(AtomicUsize::new(0));
    let mut bytes = {
        let host = Host {
            released: released.clone(),
            mode: 0,
        };
        let api = api(&host);
        let allocator =
            unsafe { HostAllocator::from_raw((&api as *const sys::HostApiV3).cast()) }.unwrap();
        let mut source = vec![1, 0, 255];
        let result = allocator.owned_copy_from_slice(&source).unwrap();
        source.fill(7);
        result
    };
    assert_eq!(&*bytes, &[1, 0, 255]);
    bytes[2] = 9;
    std::thread::spawn(move || {
        assert_eq!(&*bytes, &[1, 0, 9]);
        drop(bytes);
    })
    .join()
    .unwrap();
    assert_eq!(released.load(Ordering::SeqCst), 1);
}

#[test]
fn zeroed_alignment_empty_storage_and_layout_validation() {
    let host = Host {
        released: Arc::new(AtomicUsize::new(0)),
        mode: 0,
    };
    let api = api(&host);
    let allocator =
        unsafe { HostAllocator::from_raw((&api as *const sys::HostApiV3).cast()) }.unwrap();
    let bytes = allocator.owned_zeroed(19, 128).unwrap();
    assert_eq!(&*bytes, &[0; 19]);
    assert_eq!(bytes.as_ptr() as usize % 128, 0);
    drop(bytes);
    let empty = allocator.owned_zeroed(0, 4096).unwrap();
    assert!(empty.is_empty());
    assert_eq!(empty.as_ptr() as usize % 4096, 0);
    drop(empty);
    for (size, align) in [(0, 0), (1, 3), (usize::MAX, 1)] {
        assert!(matches!(
            allocator.owned_zeroed(size, align),
            Err(sys::INVALID)
        ));
    }
    assert_eq!(host.released.load(Ordering::SeqCst), 1);
}

#[test]
fn unavailable_abi_and_failed_or_malformed_allocation_never_fall_back() {
    for mode in 0..5 {
        let host = Host {
            released: Arc::new(AtomicUsize::new(0)),
            mode,
        };
        let mut api = api(&host);
        if mode == 0 {
            api.v2.host.struct_size = size_of::<sys::HostApiV2>() as u32;
        }
        if mode == 3 {
            api.memory_spi_major = 2;
        }
        if mode == 4 {
            api.allocate_owned_bytes = None;
        }
        let allocator =
            unsafe { HostAllocator::from_raw((&api as *const sys::HostApiV3).cast()) }.unwrap();
        let expected = match mode {
            1 => sys::NO_MEMORY,
            2 => sys::INVALID,
            _ => sys::UNSUPPORTED_ABI,
        };
        assert!(
            matches!(allocator.owned_copy_from_slice(b"test"), Err(status) if status == expected)
        );
        assert_eq!(host.released.load(Ordering::SeqCst), usize::from(mode == 2));
    }
}

#[test]
fn unwind_releases_owned_bytes_without_a_borrowed_host_free() {
    let host = Host {
        released: Arc::new(AtomicUsize::new(0)),
        mode: 0,
    };
    let api = api(&host);
    let allocator =
        unsafe { HostAllocator::from_raw((&api as *const sys::HostApiV3).cast()) }.unwrap();
    assert_eq!(
        seekdb_extension::boundary(|| {
            let _bytes = allocator.owned_zeroed(8, 8)?;
            panic!("owned token unwind fixture");
        }),
        sys::INTERNAL
    );
    assert_eq!(host.released.load(Ordering::SeqCst), 1);
}
