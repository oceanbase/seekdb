// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Exercise the actual strategy through the SDK ABI, without process-global
//! plugin activation. The full kernel regression separately covers real SQL.
use super::*;
use std::{ffi::c_void, mem::size_of, slice};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
enum Case {
    #[default]
    Normal,
    SkipFirst,
    NullSafe,
    Outer,
    String,
    Signedness,
    Volatile,
    Independent,
    CrossInput,
    Filter,
    Parallel,
    TooManyColumns,
    SetOperation,
    MissingTargets,
    BuildError,
    MetadataError,
    NestedLoopBinding,
    LeftBinding,
    RightBinding,
    OutsideColumn,
    UnknownColumn,
}
#[derive(Default)]
struct Host {
    case: Case,
    subproblem: bool,
    correlated: bool,
    multi: bool,
    swap_inputs: bool,
    reverse_equality: bool,
    builds: u32,
    nexts: u32,
    error: i32,
}
unsafe fn host<'a>(p: *mut c_void) -> &'a mut Host {
    // SAFETY: run() keeps the exclusively borrowed Host alive for invocation.
    unsafe { &mut *p.cast::<Host>() }
}
unsafe extern "C" fn count(p: *mut c_void) -> u32 {
    2 + unsafe { host(p).builds }
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
    panic!("the strategy reads the graph, not host cost selection")
}
unsafe extern "C" fn select(_: *mut c_void, _: u32) -> sys::Status {
    panic!("contribution must never force a winner")
}
unsafe extern "C" fn root(_: *mut c_void, index: u32, out: *mut u32) -> sys::Status {
    assert!(index < 2); // Do not revisit our own addition.
    unsafe {
        *out = 10 + index;
    }
    sys::OK
}
unsafe extern "C" fn plan(p: *mut c_void, id: u32, out: *mut sys::PlanInfo) -> sys::Status {
    let h = unsafe { host(p) };
    let join = id < 100;
    unsafe {
        *out = sys::PlanInfo {
            struct_size: size_of::<sys::PlanInfo>() as u32,
            operator_type: 987, // Deliberately unrelated to the normalized tag.
            join_type: 876,
            child_count: if join { 2 } else { 0 },
            expression_counts: [
                u32::from(join && h.case == Case::Filter),
                0,
                0,
                u32::from(join && !h.correlated),
                0,
            ],
            rows: if id == 101 { 3.0 } else { 7.0 },
            cost: 1.0,
            width: 8.0,
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn plan_semantics(
    p: *mut c_void,
    id: u32,
    out: *mut sys::PlanSemantics,
) -> sys::Status {
    let h = unsafe { host(p) };
    let relation = if id >= 100 || (id == 10 && h.case == Case::SkipFirst) {
        RelationKind::Other
    } else if h.case == Case::Outer {
        RelationKind::LeftJoin
    } else {
        RelationKind::InnerJoin
    };
    unsafe {
        *out = sys::PlanSemantics {
            struct_size: size_of::<sys::PlanSemantics>() as u32,
            relation_kind: relation as u32,
            flags: u32::from(h.case != Case::Parallel),
            ..Default::default()
        };
    }
    if h.case == Case::MetadataError {
        h.error = -4012;
        return sys::INTERNAL;
    }
    sys::OK
}
unsafe extern "C" fn child(p: *mut c_void, id: u32, ordinal: u32, out: *mut u32) -> sys::Status {
    let h = unsafe { host(p) };
    assert!(ordinal < 2);
    unsafe {
        *out = if h.multi && id < 50 {
            [50, 303][ordinal as usize]
        } else {
            [101, 202][ordinal as usize ^ usize::from(h.swap_inputs)]
        };
    }
    sys::OK
}
unsafe extern "C" fn expression(
    _: *mut c_void,
    _: u32,
    role: u32,
    ordinal: u32,
    out: *mut u32,
    order: *mut u32,
) -> sys::Status {
    assert_eq!((role, ordinal), (Role::JoinCondition as u32, 0));
    unsafe {
        *out = 90;
        *order = u32::MAX;
    }
    sys::OK
}
unsafe extern "C" fn describe(_: *mut c_void, id: u32, out: *mut sys::ExprInfo) -> sys::Status {
    // All fields of ExprInfo are integer scalars/arrays.
    let mut value: sys::ExprInfo = unsafe { std::mem::zeroed() };
    value.struct_size = size_of::<sys::ExprInfo>() as u32;
    value.sql_type = 5;
    value.argument_count = if id == 90 { 2 } else { 0 };
    if id < 20 {
        value.flags = 4;
        value.table_id = if id == 12 {
            202
        } else if id == 14 {
            303
        } else {
            101
        };
        value.column_id = id as u64;
    }
    unsafe {
        *out = value;
    }
    sys::OK
}
unsafe extern "C" fn expression_semantics(
    p: *mut c_void,
    id: u32,
    out: *mut sys::ExprSemantics,
) -> sys::Status {
    let h = unsafe { host(p) };
    unsafe {
        *out = sys::ExprSemantics {
            struct_size: size_of::<sys::ExprSemantics>() as u32,
            comparison_kind: if id == 90 {
                if h.case == Case::NullSafe {
                    2
                } else {
                    1
                }
            } else {
                0
            },
            value_kind: if h.case == Case::String {
                0
            } else if h.case == Case::Signedness && id == 92 {
                2
            } else {
                1
            },
            flags: u32::from(h.case != Case::Volatile),
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn argument(p: *mut c_void, id: u32, ordinal: u32, out: *mut u32) -> sys::Status {
    assert_eq!(id, 90);
    assert!(ordinal < 2);
    let h = unsafe { host(p) };
    unsafe {
        *out = [91, 92][ordinal as usize ^ usize::from(h.reverse_equality)];
    }
    sys::OK
}
unsafe extern "C" fn scope(p: *mut c_void, plan: u32, expr: u32, out: *mut u32) -> sys::Status {
    let h = unsafe { host(p) };
    let external = expr == if h.multi { 15 } else { 14 };
    let scope = if external && h.case == Case::UnknownColumn {
        // Empty/unresolved relation dependencies do not prove an outside column.
        DependencyScope::Independent
    } else if external && h.case == Case::OutsideColumn {
        DependencyScope::Outside
    } else if expr == 91 && h.case == Case::Independent {
        DependencyScope::Independent
    } else if expr == 91 && h.case == Case::CrossInput {
        DependencyScope::Outside
    } else if (h.multi && plan == 50 && matches!(expr, 11 | 12 | 13))
        || plan
            == if expr == 92 || expr == 12 {
                202
            } else if expr == 14 {
                303
            } else {
                101
            }
    {
        DependencyScope::Contained
    } else {
        DependencyScope::Outside
    };
    unsafe {
        *out = scope as u32;
    }
    sys::OK
}
unsafe extern "C" fn query(p: *mut c_void, out: *mut sys::QueryInfo) -> sys::Status {
    let h = unsafe { host(p) };
    unsafe {
        *out = sys::QueryInfo {
            struct_size: size_of::<sys::QueryInfo>() as u32,
            flags: if h.case == Case::MissingTargets {
                0
            } else if h.case == Case::SetOperation {
                3
            } else {
                1
            },
            target_count: if h.case == Case::MissingTargets { 0 } else { 2 },
            ..Default::default()
        };
    }
    sys::OK
}
unsafe extern "C" fn target(_: *mut c_void, _: u32, _: *mut u32) -> sys::Status {
    panic!("SELECT ordinal is not an input layout")
}
unsafe extern "C" fn column_count(p: *mut c_void, out: *mut u32) -> sys::Status {
    unsafe {
        *out = if host(p).case == Case::TooManyColumns {
            1025
        } else {
            4 + u32::from(host(p).multi)
                + u32::from(matches!(
                    host(p).case,
                    Case::OutsideColumn | Case::UnknownColumn
                ))
        };
    }
    sys::OK
}
unsafe extern "C" fn column(p: *mut c_void, ordinal: u32, out: *mut u32) -> sys::Status {
    // A hidden left column (13), shuffled order, and a duplicate column.
    unsafe {
        *out = if host(p).multi {
            [14, 12, 13, 11, 14, 15][ordinal as usize]
        } else if matches!(host(p).case, Case::OutsideColumn | Case::UnknownColumn) {
            [12, 13, 11, 12, 14][ordinal as usize]
        } else {
            [12, 13, 11, 12][ordinal as usize]
        };
    }
    sys::OK
}
unsafe extern "C" fn build(
    p: *mut c_void,
    request: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    assert_eq!(h.builds, 0);
    if h.multi {
        return unsafe { build_multi(h, request, out) };
    }
    if h.correlated {
        return unsafe { build_correlated(h, request, out) };
    }
    let r = unsafe { &*request.cast::<sys::CustomPathRequestV3>() };
    assert_eq!(
        r.v2.v1.v1.struct_size,
        size_of::<sys::CustomPathRequestV3>() as u32
    );
    assert_eq!(r.v2.v1.v1.input_index, u32::from(h.case == Case::SkipFirst));
    assert_eq!((r.plan_count, r.execution, r.v2.output_count), (2, 1, 3));
    assert_eq!(r.v2.v1.flags, 2); // Blocking, does not claim retained ordering.
    assert!((r.v2.v1.operator_cost - 1.42).abs() < 1e-12);
    let plans = unsafe { slice::from_raw_parts(r.input_plans, 2) };
    let offsets = unsafe { slice::from_raw_parts(r.input_offsets, 3) };
    let inputs = unsafe { slice::from_raw_parts(r.v2.inputs, r.v2.input_count as usize) };
    assert_eq!(
        unsafe { slice::from_raw_parts(r.v2.outputs, 3) },
        &[12, 13, 11]
    );
    let encoded = unsafe { slice::from_raw_parts(r.v2.v1.plan, r.v2.v1.plan_size as usize) };
    assert_eq!(&encoded[..4], b"SJE1");
    let words: Vec<_> = encoded[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    if h.swap_inputs {
        assert_eq!(plans, &[202, 101]);
        assert_eq!(offsets, &[0, 2, 5]);
        assert_eq!(inputs, &[92, 12, 91, 13, 11]);
        assert_eq!(words, [0, 0, 3, 0, 1, 1, 1, 1, 2]);
    } else {
        assert_eq!(plans, &[101, 202]);
        assert_eq!(offsets, &[0, 3, 5]);
        assert_eq!(inputs, &[91, 13, 11, 92, 12]);
        assert_eq!(words, [0, 0, 3, 1, 1, 0, 1, 0, 2]);
    }
    if h.case == Case::BuildError {
        h.error = -4012;
        return sys::INTERNAL;
    }
    unsafe {
        *out = 2;
    }
    h.builds += 1;
    sys::OK
}
unsafe fn build_correlated(
    h: &mut Host,
    request: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let r = unsafe { &*request.cast::<sys::CustomPathRequestV4>() };
    let v3 = &r.v3;
    assert_eq!(
        v3.v2.v1.v1.struct_size,
        size_of::<sys::CustomPathRequestV4>() as u32
    );
    assert_eq!(v3.v2.v1.flags, 0); // Streaming; no retained ordering claim.
    assert_eq!(
        (v3.plan_count, v3.v2.output_count, r.binding_count),
        (2, 3, 2)
    );
    assert!((v3.v2.v1.operator_cost - 1.42).abs() < 1e-12);
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.input_plans, 2) },
        &[101, 202]
    );
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.input_offsets, 3) },
        &[0, 3, 4]
    );
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.v2.inputs, v3.v2.input_count as usize) },
        &[91, 13, 11, 12]
    );
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.v2.outputs, 3) },
        &[12, 13, 11]
    );
    for (i, b) in unsafe { slice::from_raw_parts(r.bindings, 2) }
        .iter()
        .enumerate()
    {
        assert_eq!(
            (b.parameter, b.source_input, b.source_column, b.target_input),
            (301 + i as u32, 0, i as u32, 1)
        );
    }
    let encoded = unsafe { slice::from_raw_parts(v3.v2.v1.plan, v3.v2.v1.plan_size as usize) };
    assert_eq!(&encoded[..4], b"SJC1");
    let words: Vec<_> = encoded[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(words, [0, 0, 3, 1, 0, 0, 1, 0, 2]);
    if h.case == Case::BuildError {
        h.error = -4012;
        return sys::INTERNAL;
    }
    unsafe { *out = 2 };
    h.builds += 1;
    sys::OK
}
struct Policy;
unsafe fn build_multi(
    h: &mut Host,
    request: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let r = unsafe { &*request.cast::<sys::CustomPathRequestV4>() };
    let v3 = &r.v3;
    assert_eq!(
        v3.v2.v1.v1.struct_size,
        size_of::<sys::CustomPathRequestV4>() as u32
    );
    assert_eq!(
        (v3.plan_count, v3.v2.output_count, r.binding_count),
        (3, 4, 3)
    );
    assert_eq!(v3.v2.v1.flags, 0);
    assert!((v3.v2.v1.operator_cost - 4.36).abs() < 1e-12);
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.input_plans, 3) },
        &[101, 202, 303]
    );
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.input_offsets, 4) },
        &[0, 2, 3, 4]
    );
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.v2.inputs, v3.v2.input_count as usize) },
        &[11, 13, 12, 14]
    );
    assert_eq!(
        unsafe { slice::from_raw_parts(v3.v2.outputs, 4) },
        &[14, 12, 13, 11]
    );
    let expected = [(303, 0, 0, 1), (301, 0, 1, 2), (302, 1, 0, 2)];
    for (b, expected) in unsafe { slice::from_raw_parts(r.bindings, 3) }
        .iter()
        .zip(expected)
    {
        assert_eq!(
            (b.parameter, b.source_input, b.source_column, b.target_input),
            expected
        );
    }
    let bytes = unsafe { slice::from_raw_parts(v3.v2.v1.plan, v3.v2.v1.plan_size as usize) };
    assert_eq!(&bytes[..4], b"SJD1");
    let words: Vec<_> = bytes[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(words, [3, 0, 0, 1, 1, 2, 1, 4, 2, 0, 1, 0, 0, 1, 0, 0]);
    if h.case == Case::BuildError {
        h.error = -4012;
        return sys::INTERNAL;
    }
    unsafe {
        *out = 2;
    }
    h.builds += 1;
    sys::OK
}
unsafe extern "C" fn binding_count(
    p: *mut c_void,
    id: u32,
    role: u32,
    out: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    let case = h.case;
    unsafe {
        *out = if id >= 100 {
            0
        } else if h.multi && id == 50 && role == 1 {
            1
        } else if h.correlated && role == 1 {
            2
        } else {
            u32::from(matches!(
                (case, role),
                (Case::NestedLoopBinding, 1) | (Case::LeftBinding, 2) | (Case::RightBinding, 3)
            ))
        };
    }
    sys::OK
}
unsafe extern "C" fn binding(
    p: *mut c_void,
    id: u32,
    role: u32,
    ordinal: u32,
    parameter: *mut u32,
    source: *mut u32,
) -> sys::Status {
    assert!(unsafe { host(p).correlated });
    assert_eq!(role, 1);
    assert!(ordinal < 2);
    unsafe {
        *parameter = if host(p).multi && id == 50 {
            303
        } else {
            301 + ordinal
        };
        *source = if host(p).multi {
            if id == 50 {
                11
            } else {
                [13, 12][ordinal as usize]
            }
        } else {
            [91, 13][ordinal as usize]
        };
    }
    sys::OK
}
impl Hook for Policy {
    const MODE: Mode = Mode::Around;
    const PARAMETERS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(p: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        if unsafe { host(p.cast()).subproblem } {
            return JoinSubproblem::invoke(p, c);
        }
        IntegerJoin::invoke(p, c)
    }
}
fn run(h: &mut Host) -> sys::Status {
    let raw = (h as *mut Host).cast();
    let v5 = sys::CandidateContextV5 {
        v4: sys::CandidateContextV4 {
            v3: sys::CandidateContextV3 {
                v2: sys::CandidateContextV2 {
                    v1: sys::CandidateContext {
                        struct_size: size_of::<sys::CandidateContextV6>() as u32,
                        candidate_count: 2,
                        host_context: raw,
                        get: Some(get),
                        select: Some(select),
                        continuation: raw,
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
        plan_semantics: Some(plan_semantics),
        expression_semantics: Some(expression_semantics),
        scope: Some(scope),
        column_count: Some(column_count),
        column: Some(column),
        reserved: [0; 4],
    };
    let c = sys::CandidateContextV6 {
        v5,
        binding_count: Some(binding_count),
        binding: Some(binding),
        reserved: [0; 4],
    };
    unsafe { Service::<Policy>::ABI.invoke.unwrap()(raw.cast(), &c.v5.v4.v3.v2.v1) }
}
#[test]
fn subproblem_omits_only_proven_outside_columns() {
    for (subproblem, case, builds) in [
        (true, Case::OutsideColumn, 1),
        (false, Case::OutsideColumn, 0),
        (true, Case::UnknownColumn, 0),
    ] {
        let mut h = Host {
            subproblem,
            case,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::OK);
        assert_eq!((h.builds, h.nexts, h.error), (builds, 1, 0));
    }
}
#[test]
fn correlated_subproblems_keep_complete_bindings_and_hidden_columns() {
    for multi in [false, true] {
        for (subproblem, case, builds) in [
            (true, Case::OutsideColumn, 1),
            (false, Case::OutsideColumn, 0),
            (true, Case::UnknownColumn, 0),
        ] {
            let mut h = Host {
                correlated: true,
                multi,
                subproblem,
                case,
                ..Default::default()
            };
            assert_eq!(run(&mut h), sys::OK);
            assert_eq!((h.builds, h.nexts, h.error), (builds, 1, 0));
        }
    }
}
#[test]
fn correlated_subproblems_do_not_hide_binding_or_host_errors() {
    for multi in [false, true] {
        for case in [Case::LeftBinding, Case::RightBinding, Case::Volatile] {
            let mut h = Host {
                correlated: true,
                multi,
                subproblem: true,
                case,
                ..Default::default()
            };
            assert_eq!(run(&mut h), sys::OK);
            assert_eq!((h.builds, h.nexts), (0, 1));
        }
        for case in [Case::BuildError, Case::MetadataError] {
            let mut h = Host {
                correlated: true,
                multi,
                subproblem: true,
                case,
                ..Default::default()
            };
            assert_eq!(run(&mut h), sys::INTERNAL);
            assert_eq!((h.builds, h.nexts, h.error), (0, 0, -4012));
        }
    }
}
#[test]
fn multi_owner_strategy_builds_three_inputs_and_preserves_hidden_columns() {
    let mut h = Host {
        correlated: true,
        multi: true,
        ..Default::default()
    };
    assert_eq!(run(&mut h), sys::OK);
    assert_eq!((h.builds, h.nexts, h.error), (1, 1, 0));
}
#[test]
fn multi_owner_strategy_declines_unsupported_semantics_and_keeps_host_errors() {
    for case in [
        Case::Outer,
        Case::Filter,
        Case::Volatile,
        Case::Parallel,
        Case::TooManyColumns,
    ] {
        let mut h = Host {
            case,
            correlated: true,
            multi: true,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::OK);
        assert_eq!((h.builds, h.nexts, h.error), (0, 1, 0));
    }
    for case in [Case::BuildError, Case::MetadataError] {
        let mut h = Host {
            case,
            correlated: true,
            multi: true,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::INTERNAL);
        assert_eq!((h.builds, h.nexts, h.error), (0, 0, -4012));
    }
}
#[test]
fn orients_physical_inputs_and_equality_independently_and_preserves_hidden_columns() {
    for case in [Case::Normal, Case::SkipFirst] {
        for swap_inputs in [false, true] {
            for reverse_equality in [false, true] {
                let mut h = Host {
                    case,
                    swap_inputs,
                    reverse_equality,
                    ..Default::default()
                };
                assert_eq!(run(&mut h), sys::OK);
                assert_eq!((h.builds, h.nexts, h.error), (1, 1, 0));
            }
        }
    }
}
#[test]
fn unsupported_semantics_decline_without_building_or_selecting() {
    for case in [
        Case::NullSafe,
        Case::Outer,
        Case::String,
        Case::Signedness,
        Case::Volatile,
        Case::Independent,
        Case::CrossInput,
        Case::Filter,
        Case::Parallel,
        Case::TooManyColumns,
        Case::SetOperation,
        Case::MissingTargets,
        Case::NestedLoopBinding,
        Case::LeftBinding,
        Case::RightBinding,
    ] {
        let mut h = Host {
            case,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::OK, "{case:?}");
        assert_eq!((h.builds, h.nexts, h.error), (0, 1, 0), "{case:?}");
    }
}
#[test]
fn host_failures_are_not_disguised_as_semantic_declines() {
    for case in [Case::BuildError, Case::MetadataError] {
        let mut h = Host {
            case,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::INTERNAL, "{case:?}");
        assert_eq!((h.builds, h.nexts, h.error), (0, 0, -4012));
    }
}
#[test]
fn correlated_strategy_transfers_all_bindings_and_deduplicates_source_columns() {
    let mut h = Host {
        correlated: true,
        ..Default::default()
    };
    assert_eq!(run(&mut h), sys::OK);
    assert_eq!((h.builds, h.nexts, h.error), (1, 1, 0));
}
#[test]
fn correlated_strategy_declines_unowned_sources_and_unsupported_relations() {
    for case in [
        Case::Independent,
        Case::CrossInput,
        Case::Volatile,
        Case::Filter,
        Case::Outer,
        Case::Parallel,
        Case::TooManyColumns,
        Case::LeftBinding,
        Case::RightBinding,
    ] {
        let mut h = Host {
            correlated: true,
            case,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::OK, "{case:?}");
        assert_eq!((h.builds, h.nexts, h.error), (0, 1, 0), "{case:?}");
    }
    let mut h = Host {
        correlated: true,
        swap_inputs: true,
        ..Default::default()
    };
    assert_eq!(run(&mut h), sys::OK);
    assert_eq!((h.builds, h.nexts), (0, 1));
}
#[test]
fn correlated_strategy_propagates_host_failures() {
    for case in [Case::BuildError, Case::MetadataError] {
        let mut h = Host {
            correlated: true,
            case,
            ..Default::default()
        };
        assert_eq!(run(&mut h), sys::INTERNAL);
        assert_eq!((h.builds, h.nexts, h.error), (0, 0, -4012));
    }
}
