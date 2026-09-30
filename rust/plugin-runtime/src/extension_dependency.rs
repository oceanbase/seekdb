// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Replace one Extension's declared providers in a transaction-fenced graph.
//! SQL locking, object permissions and durable publication remain host-owned.
use crate::dependency::{self, Edge, MAX_EDGES, MAX_NODES};
use crate::extension_install::valid_id;
use crate::registration::NO_MEMORY;
use crate::{INVALID, OK};
use std::{mem, slice};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Dependency {
    pub provider: u64,
    pub consumer: u64,
}

fn validate(consumer: u64, existing: &[Dependency], providers: &[u64]) -> Result<(), i32> {
    if !valid_id(consumer) || providers.iter().any(|id| !valid_id(*id)) {
        return Err(INVALID);
    }
    let mut ids = Vec::new();
    ids.try_reserve_exact(existing.len() * 2 + providers.len() + 1)
        .map_err(|_| NO_MEMORY)?;
    ids.push(consumer);
    ids.extend_from_slice(providers);
    for edge in existing {
        if !valid_id(edge.provider) || !valid_id(edge.consumer) {
            return Err(INVALID);
        }
        ids.extend_from_slice(&[edge.provider, edge.consumer]);
    }
    ids.sort_unstable();
    ids.dedup();
    if ids.len() > MAX_NODES as usize {
        return Err(dependency::LIMIT);
    }
    let mut edges = Vec::new();
    edges
        .try_reserve_exact(existing.len() + providers.len())
        .map_err(|_| NO_MEMORY)?;
    let ordinal = |id| ids.binary_search(&id).expect("validated graph ID") as u32;
    for edge in existing {
        if edge.consumer != consumer {
            edges.push(Edge {
                provider: ordinal(edge.provider),
                consumer: ordinal(edge.consumer),
            });
        }
    }
    for provider in providers {
        edges.push(Edge {
            provider: ordinal(*provider),
            consumer: ordinal(consumer),
        });
    }
    if edges.len() > MAX_EDGES as usize {
        return Err(dependency::LIMIT);
    }
    dependency::plan(ids.len() as u32, &edges, false)
        .map(|_| ())
        .map_err(|(status, _)| status)
}

/// Validate the resulting graph, replacing ALL old providers of `consumer`.
/// Uses stable SQL identities, not runtime generations; no retained buffers.
/// # Safety
/// Arrays are readable for their counts and remain immutable until return.
/// Host holds its dependency-update fence and provider locks through commit.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_dependency_replace_validate(
    consumer: u64,
    existing: *const Dependency,
    count: u32,
    providers: *const u64,
    provider_count: u32,
) -> i32 {
    if count > MAX_EDGES || provider_count > 64 {
        return dependency::LIMIT;
    }
    if (count != 0
        && (existing.is_null()
            || !(existing as usize).is_multiple_of(mem::align_of::<Dependency>())))
        || (provider_count != 0
            && (providers.is_null()
                || !(providers as usize).is_multiple_of(mem::align_of::<u64>())))
    {
        return INVALID;
    }
    let edges = if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(existing, count as usize) }
    };
    let providers = if provider_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(providers, provider_count as usize) }
    };
    validate(consumer, edges, providers).err().unwrap_or(OK)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn add_remove_and_replace_use_resulting_graph() {
        let graph = [
            Dependency {
                provider: 1,
                consumer: 2,
            },
            Dependency {
                provider: 2,
                consumer: 3,
            },
        ];
        assert_eq!(validate(1, &graph, &[3]), Err(dependency::CYCLE));
        assert_eq!(validate(2, &graph, &[]), Ok(()));
        assert_eq!(validate(2, &graph, &[3]), Err(dependency::CYCLE));
        assert_eq!(validate(3, &graph, &[1]), Ok(()));
        assert_eq!(validate(3, &graph, &[1, 2]), Ok(()));
        assert_eq!(validate(1, &[], &[1]), Err(dependency::CYCLE));
        // An explicit replacement can remove a corrupt cycle's offending edge.
        let cycle = [
            Dependency {
                provider: 1,
                consumer: 2,
            },
            Dependency {
                provider: 2,
                consumer: 1,
            },
        ];
        assert_eq!(validate(1, &cycle, &[]), Ok(()));
        assert_eq!(validate(3, &cycle, &[]), Err(dependency::CYCLE));
    }
    #[test]
    fn ffi_bounds_and_stable_ids() {
        unsafe {
            assert_eq!(
                seekdb_runtime_extension_dependency_replace_validate(
                    1,
                    std::ptr::null(),
                    0,
                    std::ptr::null(),
                    0
                ),
                OK
            );
            assert_eq!(
                seekdb_runtime_extension_dependency_replace_validate(
                    1,
                    std::ptr::null(),
                    1,
                    std::ptr::null(),
                    0
                ),
                INVALID
            );
            assert_eq!(
                seekdb_runtime_extension_dependency_replace_validate(
                    1,
                    std::ptr::null(),
                    0,
                    std::ptr::null(),
                    65
                ),
                dependency::LIMIT
            );
            assert_eq!(
                seekdb_runtime_extension_dependency_replace_validate(
                    1,
                    std::ptr::null(),
                    MAX_EDGES + 1,
                    std::ptr::null(),
                    0
                ),
                dependency::LIMIT
            );
        }
        for id in [0, u64::MAX, i64::MAX as u64 + 1] {
            assert_eq!(validate(id, &[], &[]), Err(INVALID));
            assert_eq!(
                validate(
                    1,
                    &[Dependency {
                        provider: id,
                        consumer: 1
                    }],
                    &[]
                ),
                Err(INVALID)
            );
            assert_eq!(validate(1, &[], &[id]), Err(INVALID));
        }
        assert_eq!(validate(i64::MAX as u64, &[], &[1]), Ok(()));
    }
}
