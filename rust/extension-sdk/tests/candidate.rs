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
    candidate::{Context, Hook, Mode, Service},
    sys, Result,
};
use std::{ffi::c_void, mem::size_of, ptr};

#[derive(Default)]
struct Host {
    nexts: u32,
    selected: Option<u32>,
    bad_info: bool,
    failed_select: bool,
    built: u32,
    bad_build: u32,
    database_error: i32,
    custom: bool,
}
unsafe extern "C" fn get(host: *mut c_void, _: u32, info: *mut sys::CandidateInfo) -> sys::Status {
    unsafe {
        (*info).cost = 17.0;
        if (*host.cast::<Host>()).bad_info {
            (*info).reserved[0] = 1;
        }
    }
    sys::OK
}
unsafe extern "C" fn select(host: *mut c_void, index: u32) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    if host.failed_select {
        return sys::NO_MEMORY;
    }
    host.selected = Some(index);
    sys::OK
}
unsafe extern "C" fn next(host: *mut c_void, error: *mut i32) -> sys::Status {
    unsafe {
        (*host.cast::<Host>()).nexts += 1;
        *error = 0;
    }
    sys::OK
}
fn raw(host: &mut Host) -> sys::CandidateContext {
    sys::CandidateContext {
        struct_size: size_of::<sys::CandidateContext>() as u32,
        candidate_count: 2,
        host_context: (host as *mut Host).cast(),
        get: Some(get),
        select: Some(select),
        continuation: (host as *mut Host).cast(),
        next: Some(next),
        reserved: [0; 4],
    }
}
struct Policy<const ACTION: u32>;
impl<const ACTION: u32> Hook for Policy<ACTION> {
    const MODE: Mode = if ACTION == 0 {
        Mode::Around
    } else {
        Mode::Replace
    };
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        match ACTION {
            0 => {
                context.call_next()?;
                assert_eq!(context.database_error(), 0);
            }
            2 => {
                let _ = context.get(0);
                return Ok(());
            }
            3 => {
                let _ = context.select(0);
                return Ok(());
            }
            4 => {
                let _ = context.get(2);
                return Ok(());
            }
            _ => {}
        }
        assert_eq!(context.get(1)?.cost, 17.0);
        context.select(1)
    }
}
fn invoke<const ACTION: u32>(view: &sys::CandidateContext) -> sys::Status {
    unsafe { Service::<Policy<ACTION>>::ABI.invoke.unwrap()(ptr::null_mut(), view) }
}
unsafe extern "C" fn count(host: *mut c_void) -> u32 {
    unsafe { 2 + (*host.cast::<Host>()).built }
}
unsafe extern "C" fn database_error(host: *mut c_void) -> i32 {
    unsafe { (*host.cast::<Host>()).database_error }
}
unsafe extern "C" fn build(
    host: *mut c_void,
    request: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    let request = unsafe { &*request };
    assert_eq!(
        (
            request.kind,
            request.input_index,
            request.reserved_word,
            request.reserved
        ),
        (if host.custom { 2 } else { 1 }, 0, 0, [0; 4])
    );
    if host.custom {
        assert_eq!(
            request.struct_size,
            size_of::<sys::CustomPathRequest>() as u32
        );
        let custom =
            unsafe { &*(request as *const sys::PathRequest).cast::<sys::CustomPathRequest>() };
        assert_eq!(
            unsafe { std::ffi::CStr::from_ptr(custom.service_id) },
            c"test.executor"
        );
        assert_eq!(
            (custom.service_major, custom.minimum_minor, custom.flags),
            (1, 2, 3)
        );
        assert_eq!(custom.operator_cost, 3.5);
        assert_eq!(
            unsafe { std::slice::from_raw_parts(custom.plan, custom.plan_size as usize) },
            b"owned plan"
        );
        assert_eq!(custom.reserved, [0; 4]);
    }
    if host.bad_build == 3 {
        host.database_error = -4012;
        return sys::INTERNAL;
    }
    unsafe {
        *out = if host.bad_build == 1 {
            99
        } else {
            2 + host.built
        };
    }
    host.built += if host.bad_build == 2 { 2 } else { 1 };
    sys::OK
}
struct Builder;
struct CustomBuilder<const CASE: u8>;
impl<const CASE: u8> Hook for CustomBuilder<CASE> {
    const MODE: Mode = Mode::Replace;
    const BUILDERS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        let large = vec![0u8; 65537];
        let long_name = std::ffi::CString::new(vec![b'x'; 256]).unwrap();
        let path = seekdb_extension::candidate::CustomPath {
            input: if CASE == 6 { 2 } else { 0 },
            service_id: if CASE == 7 {
                c""
            } else if CASE == 8 {
                &long_name
            } else {
                c"test.executor"
            },
            service_major: if CASE == 5 { 0 } else { 1 },
            minimum_minor: 2,
            plan: if CASE == 4 { &large } else { b"owned plan" },
            operator_cost: match CASE {
                1 => f64::NAN,
                2 => f64::INFINITY,
                3 => -1.0,
                _ => 3.5,
            },
            preserves_order: true,
            blocking: true,
        };
        match context.custom(&path) {
            Ok(index) => context.select(index),
            Err(_) => Ok(()), // Errors cannot be swallowed, including host errors.
        }
    }
}
fn custom_result<const CASE: u8>(bad_build: u32) -> (sys::Status, Host) {
    let mut host = Host {
        custom: true,
        bad_build,
        ..Default::default()
    };
    let mut view = sys::CandidateContextV2 {
        v1: raw(&mut host),
        current_count: Some(count),
        build: Some(build),
        get_error: Some(database_error),
        reserved: [0; 4],
    };
    view.v1.struct_size = size_of::<sys::CandidateContextV2>() as u32;
    let status =
        unsafe { Service::<CustomBuilder<CASE>>::ABI.invoke.unwrap()(ptr::null_mut(), &view.v1) };
    (status, host)
}
#[test]
fn custom_path_transports_owned_request_and_preserves_host_failure() {
    let (status, host) = custom_result::<0>(0);
    assert_eq!(status, sys::OK);
    assert_eq!((host.built, host.selected), (1, Some(2)));
    for fault in 1..4 {
        let (status, host) = custom_result::<0>(fault);
        assert_ne!(status, sys::OK);
        assert_eq!(host.selected, None);
    }
}
#[test]
fn invalid_custom_paths_never_reach_host_or_select() {
    for (status, host) in [
        custom_result::<1>(0),
        custom_result::<2>(0),
        custom_result::<3>(0),
        custom_result::<4>(0),
        custom_result::<5>(0),
        custom_result::<6>(0),
        custom_result::<7>(0),
        custom_result::<8>(0),
    ] {
        assert_eq!(status, sys::INVALID);
        assert_eq!((host.built, host.selected), (0, None));
    }
}
impl Hook for Builder {
    const MODE: Mode = Mode::Around;
    const BUILDERS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        match context.materialize(0) {
            Ok(index) => {
                assert_eq!(context.count(), 3);
                context.call_next()?;
                context.select(index)
            }
            Err(_) => Ok(()), // Safe wrapper must preserve the failure.
        }
    }
}
#[test]
fn construction_checks_dynamic_indices_and_host_error() {
    for variant in 0..4 {
        let mut host = Host {
            bad_build: variant,
            ..Default::default()
        };
        let mut view = sys::CandidateContextV2 {
            v1: raw(&mut host),
            current_count: Some(count),
            build: Some(build),
            get_error: Some(database_error),
            reserved: [0; 4],
        };
        view.v1.struct_size = size_of::<sys::CandidateContextV2>() as u32;
        let result = unsafe { Service::<Builder>::ABI.invoke.unwrap()(ptr::null_mut(), &view.v1) };
        let expected = if variant == 0 {
            sys::OK
        } else if variant == 3 {
            sys::INTERNAL
        } else {
            sys::FAILED_PRECONDITION
        };
        assert_eq!(result, expected);
        assert_eq!(host.nexts, if variant == 0 { 1 } else { 0 });
        assert_eq!(host.selected, if variant == 0 { Some(2) } else { None });
    }
}
#[test]
fn construction_requires_complete_v2_context() {
    for variant in 0..5 {
        let mut host = Host::default();
        let mut view = sys::CandidateContextV2 {
            v1: raw(&mut host),
            current_count: Some(count),
            build: Some(build),
            get_error: Some(database_error),
            reserved: [0; 4],
        };
        view.v1.struct_size = size_of::<sys::CandidateContextV2>() as u32;
        match variant {
            0 => view.current_count = None,
            1 => view.build = None,
            2 => view.get_error = None,
            3 => view.reserved[0] = 1,
            _ => view.v1.struct_size = size_of::<sys::CandidateContext>() as u32,
        }
        assert_eq!(
            unsafe { Service::<Builder>::ABI.invoke.unwrap()(ptr::null_mut(), &view.v1) },
            sys::INVALID
        );
        assert_eq!((host.built, host.nexts), (0, 0));
    }
}
#[test]
fn around_and_replacement_access_host_without_cpp_layout() {
    let mut host = Host::default();
    assert_eq!(invoke::<0>(&raw(&mut host)), sys::OK);
    assert_eq!((host.nexts, host.selected), (1, Some(1)));
    let mut host = Host::default();
    assert_eq!(invoke::<1>(&raw(&mut host)), sys::OK);
    assert_eq!((host.nexts, host.selected), (0, Some(1)));
}
#[test]
fn malformed_host_values_and_failed_operations_are_sticky() {
    let mut host = Host {
        bad_info: true,
        ..Default::default()
    };
    assert_eq!(invoke::<2>(&raw(&mut host)), sys::INVALID);
    let mut host = Host {
        failed_select: true,
        ..Default::default()
    };
    assert_eq!(invoke::<3>(&raw(&mut host)), sys::NO_MEMORY);
    let mut host = Host::default();
    assert_eq!(invoke::<4>(&raw(&mut host)), sys::INVALID);
    assert_eq!((host.nexts, host.selected), (0, None));
}
#[test]
fn invalid_context_never_calls_user_hook() {
    for variant in 0..8 {
        let mut host = Host::default();
        let mut view = raw(&mut host);
        match variant {
            0 => view.struct_size = 0,
            1 => view.candidate_count = 0,
            2 => view.host_context = ptr::null_mut(),
            3 => view.get = None,
            4 => view.select = None,
            5 => view.next = None,
            6 => view.reserved[0] = 1,
            _ => {
                assert_eq!(
                    unsafe {
                        Service::<Policy<0>>::ABI.invoke.unwrap()(ptr::null_mut(), ptr::null())
                    },
                    sys::INVALID
                );
                continue;
            }
        }
        assert_eq!(invoke::<0>(&view), sys::INVALID);
        assert_eq!((host.nexts, host.selected), (0, None));
    }
}
