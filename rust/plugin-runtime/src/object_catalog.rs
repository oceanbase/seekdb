// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Runtime object identity index. Published catalogs are immutable; the host
//! mutates a private clone and atomically publishes it with its service image.
//! This is not the durable Extension installation catalog or its transaction.

use crate::registration::{CONFLICT, LIMIT, NO_MEMORY};
use crate::{INVALID, OK};
use std::alloc::{alloc, Layout};
use std::ffi::c_void;
use std::ptr;
use std::sync::Arc;

const MAX_OBJECTS: usize = 4096;
type Release = unsafe extern "C" fn(*mut c_void);

struct Object {
    kind: u32,
    id: Vec<u8>,
    payload: *mut c_void,
    release: Release,
}
impl Drop for Object {
    fn drop(&mut self) {
        unsafe { (self.release)(self.payload) };
    }
}

#[derive(Default)]
pub struct ObjectCatalog {
    // Sorted identity index also supplies deterministic enumeration. The
    // immutable payload/key is shared by clones, not copied or indexed twice.
    objects: Vec<Arc<Object>>,
}

impl ObjectCatalog {
    fn find(&self, kind: u32, id: &[u8]) -> Result<usize, usize> {
        self.objects.binary_search_by(|object| {
            object
                .kind
                .cmp(&kind)
                .then_with(|| object.id.as_slice().cmp(id))
        })
    }

    fn try_clone(&self) -> Result<Self, i32> {
        let mut objects = Vec::new();
        objects
            .try_reserve_exact(self.objects.len())
            .map_err(|_| NO_MEMORY)?;
        objects.extend(self.objects.iter().cloned());
        Ok(Self { objects })
    }

    // All validation and fallible reservations precede ownership transfer.
    // Arc control-block allocation follows the host's abort-on-OOM strategy.
    fn insert(&mut self, kind: u32, id: &[u8], payload: *mut c_void, release: Release) -> i32 {
        if kind == 0 || !valid_id(id) || payload.is_null() {
            return INVALID;
        }
        let position = match self.find(kind, id) {
            Ok(_) => return CONFLICT,
            Err(position) => position,
        };
        if self.objects.len() >= MAX_OBJECTS {
            return LIMIT;
        }
        let mut key = Vec::new();
        if key.try_reserve_exact(id.len()).is_err() || self.objects.try_reserve(1).is_err() {
            return NO_MEMORY;
        }
        key.extend_from_slice(id);
        // The host contract permits concurrent read/clone on immutable images
        // and requires thread-safe payload destruction. Rust never accesses
        // the host payload; no blanket Send/Sync promise is made for pointers.
        #[allow(clippy::arc_with_non_send_sync)]
        let object = Arc::new(Object {
            kind,
            id: key,
            payload,
            release,
        });
        self.objects.insert(position, object);
        OK
    }
}

fn valid_id(id: &[u8]) -> bool {
    !id.is_empty()
        && id.len() <= 255
        && id
            .iter()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || b"._-".contains(c))
}

fn owned(catalog: ObjectCatalog) -> *mut ObjectCatalog {
    let allocation = unsafe { alloc(Layout::new::<ObjectCatalog>()).cast::<ObjectCatalog>() };
    if !allocation.is_null() {
        unsafe { allocation.write(catalog) };
    }
    allocation
}

/// # Safety
/// All catalog pointers must be live. Mutations/destruction require exclusive
/// access; reads/clone may share an immutable catalog. Payload release must not
/// unwind, reenter, or call plugin code; it may run on any host owner thread.
#[no_mangle]
pub extern "C" fn seekdb_runtime_objects_create() -> *mut ObjectCatalog {
    owned(ObjectCatalog::default())
}

/// # Safety
/// catalog must be null or an exclusively owned handle from this module.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_destroy(catalog: *mut ObjectCatalog) {
    if !catalog.is_null() {
        drop(unsafe { Box::from_raw(catalog) });
    }
}

/// # Safety
/// catalog is immutable and live for the call. A null result reports allocation
/// failure or invalid input; successful clones share payload ownership.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_clone(
    catalog: *const ObjectCatalog,
) -> *mut ObjectCatalog {
    let Some(catalog) = (unsafe { catalog.as_ref() }) else {
        return ptr::null_mut();
    };
    match catalog.try_clone() {
        Ok(copy) => owned(copy),
        Err(_) => ptr::null_mut(),
    }
}

/// # Safety
/// catalog is exclusive. id points to length readable bytes. On OK only,
/// payload transfers to the catalog; release obeys the module-level contract.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_insert(
    catalog: *mut ObjectCatalog,
    kind: u32,
    id: *const u8,
    length: u32,
    payload: *mut c_void,
    release: Option<Release>,
) -> i32 {
    if catalog.is_null() || id.is_null() || length == 0 || length > 255 {
        return INVALID;
    }
    let Some(release) = release else {
        return INVALID;
    };
    unsafe { &mut *catalog }.insert(
        kind,
        unsafe { std::slice::from_raw_parts(id, length as usize) },
        payload,
        release,
    )
}

/// # Safety
/// catalog is live and immutable. Result is borrowed only until this catalog
/// is mutated/destroyed; clone ownership independently retains the payload.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_at(
    catalog: *const ObjectCatalog,
    index: u32,
) -> *const c_void {
    unsafe { catalog.as_ref() }
        .and_then(|catalog| catalog.objects.get(index as usize))
        .map_or(ptr::null(), |object| object.payload.cast_const())
}

/// # Safety
/// Same borrowing rule as at; id points to length bytes for this call only.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_find(
    catalog: *const ObjectCatalog,
    kind: u32,
    id: *const u8,
    length: u32,
) -> *const c_void {
    if catalog.is_null() || id.is_null() || length == 0 || length > 255 {
        return ptr::null();
    }
    let catalog = unsafe { &*catalog };
    let id = unsafe { std::slice::from_raw_parts(id, length as usize) };
    catalog.find(kind, id).map_or(ptr::null(), |index| {
        catalog.objects[index].payload.cast_const()
    })
}

/// # Safety
/// catalog is live and immutable, or null (returns zero).
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_count(catalog: *const ObjectCatalog) -> u32 {
    unsafe { catalog.as_ref() }.map_or(0, |catalog| catalog.objects.len() as u32)
}

/// # Safety
/// catalog is exclusively owned. Removal affects this image only. Release
/// occurs only after the final cloned image retaining this object is dropped.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_remove(
    catalog: *mut ObjectCatalog,
    index: u32,
) -> i32 {
    let Some(catalog) = (unsafe { catalog.as_mut() }) else {
        return INVALID;
    };
    if index as usize >= catalog.objects.len() {
        return INVALID;
    }
    catalog.objects.remove(index as usize);
    OK
}

/// # Safety
/// catalog is exclusive. The predicate only reads immutable host metadata and
/// does not unwind, mutate this image or reenter it. context is callback-scoped.
/// Removal is one linear compaction, not repeated shifting of a sorted vector.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_objects_remove_if(
    catalog: *mut ObjectCatalog,
    context: *const c_void,
    predicate: Option<unsafe extern "C" fn(*const c_void, *const c_void) -> u8>,
) -> i32 {
    let Some(catalog) = (unsafe { catalog.as_mut() }) else {
        return INVALID;
    };
    let Some(predicate) = predicate else {
        return INVALID;
    };
    catalog
        .objects
        .retain(|object| unsafe { predicate(context, object.payload.cast_const()) } == 0);
    OK
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    struct Payload {
        released: Arc<AtomicUsize>,
        value: usize,
    }
    unsafe extern "C" fn release(payload: *mut c_void) {
        let payload = unsafe { Box::from_raw(payload.cast::<Payload>()) };
        payload.released.fetch_add(1, Ordering::SeqCst);
    }
    fn payload(released: &Arc<AtomicUsize>, value: usize) -> *mut c_void {
        Box::into_raw(Box::new(Payload {
            released: released.clone(),
            value,
        }))
        .cast()
    }
    fn value(catalog: &ObjectCatalog, index: usize) -> usize {
        unsafe { (*catalog.objects[index].payload.cast::<Payload>()).value }
    }

    #[test]
    fn snapshot_clone_and_removal_preserve_borrowed_objects_until_last_owner() {
        let count = Arc::new(AtomicUsize::new(0));
        let mut catalog = ObjectCatalog::default();
        assert_eq!(
            catalog.insert(2, b"org.test.a", payload(&count, 42), release),
            OK
        );
        let mut next = catalog.try_clone().unwrap();
        assert_eq!(value(&next, 0), 42);
        assert!(Arc::ptr_eq(&next.objects[0], &catalog.objects[0]));
        next.objects.remove(0);
        assert_eq!(count.load(Ordering::SeqCst), 0);
        assert_eq!(value(&catalog, 0), 42);
        drop(catalog);
        assert_eq!(count.load(Ordering::SeqCst), 1);
        drop(next);
        assert_eq!(count.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn duplicate_and_invalid_insert_do_not_take_ownership() {
        let count = Arc::new(AtomicUsize::new(0));
        let mut catalog = ObjectCatalog::default();
        assert_eq!(
            catalog.insert(2, b"org.test.a", payload(&count, 1), release),
            OK
        );
        let rejected = payload(&count, 2);
        assert_eq!(
            catalog.insert(2, b"org.test.a", rejected, release),
            CONFLICT
        );
        assert_eq!(catalog.insert(0, b"org.test.b", rejected, release), INVALID);
        assert_eq!(catalog.insert(2, b"bad/name", rejected, release), INVALID);
        assert_eq!(count.load(Ordering::SeqCst), 0);
        assert_eq!(catalog.objects.len(), 1);
        unsafe { release(rejected) };
        drop(catalog);
        assert_eq!(count.load(Ordering::SeqCst), 2);
    }

    #[test]
    fn identity_is_owned_and_enumeration_is_kind_then_id() {
        let count = Arc::new(AtomicUsize::new(0));
        let mut catalog = ObjectCatalog::default();
        let mut id = b"org.test.z".to_vec();
        assert_eq!(catalog.insert(2, &id, payload(&count, 3), release), OK);
        id.fill(b'x');
        assert_eq!(
            catalog.insert(1, b"org.test.z", payload(&count, 1), release),
            OK
        );
        assert_eq!(
            catalog.insert(2, b"org.test.a", payload(&count, 2), release),
            OK
        );
        assert_eq!(
            (value(&catalog, 0), value(&catalog, 1), value(&catalog, 2)),
            (1, 2, 3)
        );
        assert_eq!(catalog.find(2, b"org.test.z"), Ok(2));
        assert!(catalog.find(3, b"org.test.z").is_err());
        drop(catalog);
        assert_eq!(count.load(Ordering::SeqCst), 3);
    }

    #[test]
    fn bounded_catalog_rejects_without_consuming_and_recovers_after_removal() {
        let count = Arc::new(AtomicUsize::new(0));
        let mut catalog = ObjectCatalog::default();
        for i in 0..MAX_OBJECTS {
            assert_eq!(
                catalog.insert(
                    2,
                    format!("org.test.{i:04}").as_bytes(),
                    payload(&count, i),
                    release
                ),
                OK
            );
        }
        let extra = payload(&count, MAX_OBJECTS);
        assert_eq!(catalog.insert(2, b"org.test.extra", extra, release), LIMIT);
        catalog.objects.remove(0);
        assert_eq!(catalog.insert(2, b"org.test.extra", extra, release), OK);
        drop(catalog);
        assert_eq!(count.load(Ordering::SeqCst), MAX_OBJECTS + 1);
    }

    #[test]
    fn ffi_clone_find_and_null_contract() {
        let count = Arc::new(AtomicUsize::new(0));
        unsafe {
            assert!(seekdb_runtime_objects_clone(ptr::null()).is_null());
            assert!(seekdb_runtime_objects_at(ptr::null(), 0).is_null());
            assert_eq!(seekdb_runtime_objects_count(ptr::null()), 0);
            assert_eq!(seekdb_runtime_objects_remove(ptr::null_mut(), 0), INVALID);
            let catalog = seekdb_runtime_objects_create();
            assert!(!catalog.is_null());
            let object = payload(&count, 42);
            let id = b"org.test.a";
            assert_eq!(
                seekdb_runtime_objects_insert(
                    catalog,
                    2,
                    id.as_ptr(),
                    id.len() as u32,
                    object,
                    None
                ),
                INVALID
            );
            assert_eq!(
                seekdb_runtime_objects_insert(
                    catalog,
                    2,
                    id.as_ptr(),
                    id.len() as u32,
                    object,
                    Some(release)
                ),
                OK
            );
            let copy = seekdb_runtime_objects_clone(catalog);
            assert!(!copy.is_null());
            assert_eq!(
                seekdb_runtime_objects_find(copy, 2, id.as_ptr(), id.len() as u32),
                object.cast_const()
            );
            assert!(seekdb_runtime_objects_at(copy, 1).is_null());
            assert_eq!(seekdb_runtime_objects_remove(catalog, 1), INVALID);
            assert_eq!(seekdb_runtime_objects_remove(catalog, 0), OK);
            seekdb_runtime_objects_destroy(catalog);
            assert_eq!(count.load(Ordering::SeqCst), 0);
            assert_eq!(seekdb_runtime_objects_count(copy), 1);
            seekdb_runtime_objects_destroy(copy);
            seekdb_runtime_objects_destroy(ptr::null_mut());
        }
        assert_eq!(count.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn bulk_remove_preserves_order_and_other_snapshot_ownership() {
        unsafe extern "C" fn even(_: *const c_void, payload: *const c_void) -> u8 {
            u8::from(unsafe { (*payload.cast::<Payload>()).value } % 2 == 0)
        }
        let count = Arc::new(AtomicUsize::new(0));
        let mut catalog = ObjectCatalog::default();
        for i in 0..100 {
            assert_eq!(
                catalog.insert(
                    2,
                    format!("test.{i:03}").as_bytes(),
                    payload(&count, i),
                    release
                ),
                OK
            );
        }
        let retained = catalog.try_clone().unwrap();
        assert_eq!(
            unsafe { seekdb_runtime_objects_remove_if(&mut catalog, ptr::null(), None) },
            INVALID
        );
        assert_eq!(catalog.objects.len(), 100);
        assert_eq!(
            unsafe { seekdb_runtime_objects_remove_if(&mut catalog, ptr::null(), Some(even)) },
            OK
        );
        assert_eq!(catalog.objects.len(), 50);
        for i in 0..50 {
            assert_eq!(value(&catalog, i), i * 2 + 1);
        }
        assert_eq!(count.load(Ordering::SeqCst), 0);
        drop(retained);
        assert_eq!(count.load(Ordering::SeqCst), 50);
        drop(catalog);
        assert_eq!(count.load(Ordering::SeqCst), 100);
    }
}
