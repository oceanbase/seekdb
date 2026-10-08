// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Bind a native Rust manifest to the linked host selected by the build.
//! This is not a signature, C++ layout proof or permission to load untrusted code.
use crate::sys;
use std::mem::size_of;

/// Wrap an ordinary v1 manifest, retaining its callbacks and capability bits.
/// Supply the build-generated host ID, not a product version or plugin build ID.
/// The caller still owns all raw pointer lifetimes and the manifest's Sync proof.
pub const fn bind(mut manifest: sys::Manifest, host_id: &[u8]) -> sys::ServerDevManifest {
    assert!(manifest.struct_size == size_of::<sys::Manifest>() as u32);
    assert!(manifest.abi_major == 1 && manifest.abi_minor == 0);
    assert!(manifest.capabilities & sys::SERVER_DEV == 0);
    assert!(!host_id.is_empty() && host_id.len() <= 64);
    let mut bytes = [0; 64];
    let mut index = 0;
    while index < host_id.len() {
        bytes[index] = host_id[index];
        index += 1;
    }
    manifest.struct_size = size_of::<sys::ServerDevManifest>() as u32;
    manifest.capabilities |= sys::SERVER_DEV;
    sys::ServerDevManifest {
        v1: manifest,
        bridge_version: 1,
        host_build_id_size: host_id.len() as u32,
        host_build_id: bytes,
        reserved: [0; 4],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn manifest() -> sys::Manifest {
        sys::Manifest {
            struct_size: size_of::<sys::Manifest>() as u32,
            abi_major: 1,
            abi_minor: 0,
            plugin_id: c"test".as_ptr(),
            vendor: c"test".as_ptr(),
            version: sys::Version {
                major: 1,
                minor: 2,
                patch: 3,
            },
            build_id: c"test-build".as_ptr(),
            catalog_version: 5,
            data_format_version: 6,
            capabilities: sys::THREAD_SAFE,
            provides: std::ptr::null(),
            provides_count: 0,
            required_services: std::ptr::null(),
            required_services_count: 0,
            init: None,
            start: None,
            stop: None,
            deinit: None,
            reserved: [0; 8],
        }
    }
    #[test]
    fn binding_retains_prefix_and_zeroes_unused_suffix() {
        for size in [1, 20, 64] {
            let id = vec![42; size];
            let bound = bind(manifest(), &id);
            assert_eq!(
                bound.v1.struct_size as usize,
                size_of::<sys::ServerDevManifest>()
            );
            assert_eq!(bound.v1.capabilities, sys::THREAD_SAFE | sys::SERVER_DEV);
            assert_eq!(bound.v1.catalog_version, 5);
            assert_eq!(bound.v1.data_format_version, 6);
            assert_eq!(bound.v1.version.minor, 2);
            assert_eq!(bound.v1.plugin_id, manifest().plugin_id);
            assert_eq!(bound.host_build_id_size as usize, size);
            assert_eq!(&bound.host_build_id[..size], &id);
            assert!(bound.host_build_id[size..].iter().all(|b| *b == 0));
            assert_eq!(bound.bridge_version, 1);
            assert_eq!(bound.reserved, [0; 4]);
        }
    }
    #[test]
    fn invalid_binding_is_rejected() {
        for size in [0, 65] {
            assert!(std::panic::catch_unwind(|| bind(manifest(), &vec![1; size])).is_err());
        }
        for variant in 0..4 {
            assert!(std::panic::catch_unwind(|| {
                let mut base = manifest();
                match variant {
                    0 => base.struct_size -= 1,
                    1 => base.abi_major = 2,
                    2 => base.abi_minor = 1,
                    _ => base.capabilities |= sys::SERVER_DEV,
                }
                bind(base, &[1])
            })
            .is_err());
        }
    }
}
