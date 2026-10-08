// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
use std::{ffi::c_void, mem::size_of, ptr, slice};
struct Host {
    case: u32,
    builds: u32,
    nexts: u32,
    error: i32,
}
unsafe fn host<'a>(p: *mut c_void) -> &'a mut Host {
    unsafe { &mut *p.cast::<Host>() }
}
unsafe extern "C" fn count(p: *mut c_void) -> u32 {
    1 + unsafe { host(p).builds }
}
unsafe extern "C" fn error(p: *mut c_void) -> i32 {
    unsafe { host(p).error }
}
unsafe extern "C" fn next(p: *mut c_void, out: *mut i32) -> sys::Status {
    unsafe {
        host(p).nexts += 1;
        *out = 0;
    }
    sys::OK
}
unsafe extern "C" fn get(_: *mut c_void, _: u32, _: *mut sys::CandidateInfo) -> sys::Status {
    sys::INTERNAL
}
unsafe extern "C" fn select(_: *mut c_void, _: u32) -> sys::Status {
    sys::INTERNAL
}
unsafe extern "C" fn root(_: *mut c_void, _: u32, out: *mut u32) -> sys::Status {
    unsafe {
        *out = 10;
    }
    sys::OK
}
unsafe extern "C" fn child(_: *mut c_void, id: u32, index: u32, out: *mut u32) -> sys::Status {
    assert_eq!((id, index), (10, 0));
    unsafe {
        *out = 11;
    }
    sys::OK
}
unsafe extern "C" fn plan(p: *mut c_void, id: u32, out: *mut sys::PlanInfo) -> sys::Status {
    unsafe {
        *out = sys::PlanInfo {
            struct_size: size_of::<sys::PlanInfo>() as u32,
            operator_type: 999,
            child_count: if id == 10 { 1 } else { 0 },
            join_type: u32::MAX,
            expression_counts: [u32::from(host(p).case == 8 && id == 10), 0, 0, 0, 0],
            cost: 1.0,
            rows: 8.0,
            width: 16.0,
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn semantics(
    p: *mut c_void,
    _: u32,
    out: *mut sys::PlanSemantics,
) -> sys::Status {
    unsafe {
        *out = sys::PlanSemantics {
            struct_size: size_of::<sys::PlanSemantics>() as u32,
            flags: u32::from(host(p).case != 9),
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn expr_semantics(
    p: *mut c_void,
    _: u32,
    out: *mut sys::ExprSemantics,
) -> sys::Status {
    let case = unsafe { host(p).case };
    unsafe {
        *out = sys::ExprSemantics {
            struct_size: size_of::<sys::ExprSemantics>() as u32,
            value_kind: if case == 10 { 0 } else { 1 },
            flags: u32::from(case != 11),
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn describe(p: *mut c_void, id: u32, out: *mut sys::ExprInfo) -> sys::Status {
    let case = unsafe { host(p).case };
    unsafe {
        *out = std::mem::zeroed();
        (*out).struct_size = size_of::<sys::ExprInfo>() as u32;
        (*out).flags = if id == 50 {
            if case == 12 {
                0
            } else if case == 13 {
                8
            } else {
                4
            }
        } else {
            0
        };
    }
    sys::OK
}
unsafe extern "C" fn scope(p: *mut c_void, _: u32, _: u32, out: *mut u32) -> sys::Status {
    unsafe {
        *out = if host(p).case == 14 { 1 } else { 2 };
    }
    sys::OK
}
unsafe extern "C" fn query(p: *mut c_void, out: *mut sys::QueryInfo) -> sys::Status {
    unsafe {
        *out = sys::QueryInfo {
            struct_size: size_of::<sys::QueryInfo>() as u32,
            flags: if host(p).case == 15 { 3 } else { 1 },
            target_count: 2,
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn target(p: *mut c_void, _: u32, out: *mut u32) -> sys::Status {
    unsafe {
        *out = if host(p).case == 16 { 60 } else { 50 };
    }
    sys::OK
}
unsafe extern "C" fn column_count(_: *mut c_void, out: *mut u32) -> sys::Status {
    unsafe {
        *out = 0;
    }
    sys::OK
}
unsafe extern "C" fn argument(_: *mut c_void, _: u32, _: u32, _: *mut u32) -> sys::Status {
    sys::INTERNAL
}
unsafe extern "C" fn value_info(
    p: *mut c_void,
    plan: u32,
    expr: u32,
    out: *mut sys::ValueInfo,
) -> sys::Status {
    assert_eq!(plan, 11);
    let case = unsafe { host(p).case };
    unsafe {
        *out = sys::ValueInfo {
            struct_size: size_of::<sys::ValueInfo>() as u32,
            flags: u32::from(case != 14 && !(case == 12 && expr == 50)),
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn expression(
    _: *mut c_void,
    _: u32,
    _: u32,
    _: u32,
    _: *mut u32,
    _: *mut u32,
) -> sys::Status {
    sys::INTERNAL
}
unsafe extern "C" fn binding_count(_: *mut c_void, _: u32, _: u32, _: *mut u32) -> sys::Status {
    sys::INTERNAL
}
unsafe extern "C" fn sort_info(p: *mut c_void, _: u32, out: *mut sys::SortInfo) -> sys::Status {
    let h = unsafe { host(p) };
    let mut v = sys::SortInfo {
        struct_size: size_of::<sys::SortInfo>() as u32,
        flags: 1,
        key_count: 2,
        topn_expression: u32::MAX,
        topk_limit_expression: u32::MAX,
        topk_offset_expression: u32::MAX,
        hash_expression: u32::MAX,
        ..Default::default()
    };
    match h.case {
        1 => v.topn_expression = 60,
        2 => v.prefix_key_count = 1,
        3 => v.partition_key_count = 1,
        4 => v.flags |= 4,
        5 => v.flags |= 8,
        6 => v.flags |= 16,
        7 => v.topk_limit_expression = 60,
        17 => {
            h.error = -4012;
            return sys::INTERNAL;
        }
        19 => v.flags |= 2,
        20 => v.topk_offset_expression = 60,
        21 => v.hash_expression = 60,
        _ => (),
    }
    unsafe {
        *out = v;
    }
    sys::OK
}
unsafe extern "C" fn sort_key(
    _: *mut c_void,
    _: u32,
    i: u32,
    expr: *mut u32,
    flags: *mut u32,
) -> sys::Status {
    unsafe {
        *expr = 60;
        *flags = i + 1;
    }
    sys::OK
}
unsafe extern "C" fn build(
    p: *mut c_void,
    raw: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    if h.case == 18 {
        h.error = -4012;
        return sys::INTERNAL;
    }
    let r = unsafe { &*raw.cast::<sys::CustomPathRequestV3>() };
    assert_eq!(
        r.v2.v1.v1.struct_size,
        size_of::<sys::CustomPathRequestV3>() as u32
    );
    assert_eq!(
        (r.plan_count, r.v2.v1.v1.input_index, r.v2.v1.flags),
        (1, 0, 3)
    );
    assert_eq!(unsafe { *r.input_plans }, 11); // Consume the child, not the SORT result.
    let inputs = unsafe { slice::from_raw_parts(r.v2.inputs, r.v2.input_count as usize) };
    let outputs = unsafe { slice::from_raw_parts(r.v2.outputs, r.v2.output_count as usize) };
    if h.case == 13 || h.case == 16 {
        assert_eq!(inputs, &[60]);
        assert_eq!(outputs, &[60]);
    } else {
        assert_eq!(inputs, &[60, 50]);
        assert_eq!(outputs, &[60, 50]);
    }
    let plan =
        SortPlan::parse(unsafe { slice::from_raw_parts(r.v2.v1.plan, r.v2.v1.plan_size as usize) })
            .unwrap();
    assert_eq!(
        plan.keys,
        vec![
            Key {
                column: 0,
                descending: true,
                nulls_first: false
            },
            Key {
                column: 0,
                descending: false,
                nulls_first: true
            }
        ]
    );
    assert_eq!(plan.outputs, (0..outputs.len()).collect::<Vec<_>>());
    unsafe {
        *out = 1;
    }
    h.builds += 1;
    sys::OK
}
struct Probe;
impl Hook for Probe {
    const MODE: Mode = Mode::Around;
    const VALUES: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let root = c.root(0)?;
        let sort = c.sort(root)?.unwrap();
        contribute(c, 0, root, &sort)?;
        c.call_next()
    }
}
fn run(case: u32) -> (sys::Status, Host) {
    let mut h = Host {
        case,
        builds: 0,
        nexts: 0,
        error: 0,
    };
    let p = (&mut h as *mut Host).cast();
    let c = sys::CandidateContextV7 {
        v6: sys::CandidateContextV6 {
            v5: sys::CandidateContextV5 {
                v4: sys::CandidateContextV4 {
                    v3: sys::CandidateContextV3 {
                        v2: sys::CandidateContextV2 {
                            v1: sys::CandidateContext {
                                struct_size: size_of::<sys::CandidateContextV8>() as u32,
                                candidate_count: 1,
                                host_context: p,
                                get: Some(get),
                                select: Some(select),
                                continuation: p,
                                next: Some(next),
                                reserved: [0; 4],
                            },
                            current_count: Some(count),
                            build: Some(build),
                            get_error: Some(error),
                            reserved: [0; 4],
                        },
                        root: Some(root),
                        plan: Some(plan),
                        child: Some(child),
                        expression: Some(expression),
                        describe_expression: Some(describe),
                        argument: Some(argument),
                        reserved: [0; 4],
                    },
                    query: Some(query),
                    target: Some(target),
                    reserved: [0; 4],
                },
                plan_semantics: Some(semantics),
                expression_semantics: Some(expr_semantics),
                scope: Some(scope),
                column_count: Some(column_count),
                column: Some(target),
                reserved: [0; 4],
            },
            binding_count: Some(binding_count),
            binding: Some(expression),
            reserved: [0; 4],
        },
        sort_info: Some(sort_info),
        sort_key: Some(sort_key),
        reserved: [0; 4],
    };
    let c = sys::CandidateContextV8 {
        v7: c,
        value_info: Some(value_info),
        reserved: [0; 4],
    };
    let result =
        unsafe { Service::<Probe>::ABI.invoke.unwrap()(ptr::null_mut(), &c.v7.v6.v5.v4.v3.v2.v1) };
    (result, h)
}
#[test]
fn sorter_builds_child_fragment_and_preserves_hidden_keys_constants_and_aliases() {
    for case in [0, 11, 13, 16, 19] {
        let (result, h) = run(case);
        assert_eq!(result, sys::OK, "case {case}");
        assert_eq!((h.builds, h.nexts), (1, 1));
    }
}
#[test]
fn sorter_declines_special_sorts_and_unproven_projection_without_hiding_errors() {
    for case in (1..=21).filter(|c| ![11, 13, 16, 19].contains(c)) {
        let (result, h) = run(case);
        if case == 17 || case == 18 {
            assert_eq!(result, sys::INTERNAL);
            assert_eq!(h.nexts, 0);
        } else {
            assert_eq!(result, sys::OK, "case {case}");
            assert_eq!(h.nexts, 1);
        }
        assert_eq!(h.builds, 0, "case {case}");
    }
}
