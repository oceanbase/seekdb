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
    candidate::{self, Context, Role},
    sys, Result,
};
use std::{ffi::c_void, mem::size_of, ptr};
struct Host {
    mode: u32,
    calls: u32,
    error: i32,
    built: u32,
}
unsafe fn host<'a>(p: *mut c_void) -> &'a mut Host {
    unsafe { &mut *p.cast::<Host>() }
}
unsafe extern "C" fn root(p: *mut c_void, index: u32, out: *mut u32) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    assert_eq!(index, 0);
    unsafe { *out = if h.mode == 1 { u32::MAX } else { 10 } };
    if h.mode == 2 {
        h.error = -4012;
        return sys::INTERNAL;
    }
    if h.mode == 3 {
        h.error = -4012;
    }
    sys::OK
}
unsafe extern "C" fn plan(p: *mut c_void, id: u32, out: *mut sys::PlanInfo) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    assert_eq!(id, 10);
    let mut value = sys::PlanInfo {
        struct_size: size_of::<sys::PlanInfo>() as u32,
        operator_type: 100,
        child_count: 1,
        join_type: 5,
        expression_counts: [1, 0, 1, 0, 0],
        cost: 2.5,
        rows: 9.0,
        width: 16.0,
        ..Default::default()
    };
    if h.mode == 4 {
        value.struct_size -= 1;
    }
    if h.mode == 5 {
        value.reserved[2] = 1;
    }
    unsafe { *out = value };
    sys::OK
}
unsafe extern "C" fn child(_: *mut c_void, id: u32, index: u32, out: *mut u32) -> sys::Status {
    assert_eq!((id, index), (10, 0));
    unsafe { *out = 20 };
    sys::OK
}
unsafe extern "C" fn expression(
    p: *mut c_void,
    id: u32,
    role: u32,
    index: u32,
    out: *mut u32,
    order: *mut u32,
) -> sys::Status {
    assert_eq!((id, index), (10, 0));
    let h = unsafe { host(p) };
    h.calls += 1;
    unsafe {
        *out = 50;
        *order = if role == 3 { 1 } else { u32::MAX };
    }
    if h.mode == 6 {
        unsafe { *order = 0 };
    }
    sys::OK
}
unsafe extern "C" fn describe(p: *mut c_void, id: u32, out: *mut sys::ExprInfo) -> sys::Status {
    assert_eq!(id, 50);
    let h = unsafe { host(p) };
    h.calls += 1;
    let mut value: sys::ExprInfo = unsafe { std::mem::zeroed() };
    value.struct_size = size_of::<sys::ExprInfo>() as u32;
    value.expression_type = 42;
    value.sql_type = 5;
    value.argument_count = 1;
    value.flags = 1 | 4 | 16;
    value.table_id = 101;
    value.column_id = 7;
    value.collation = 63;
    value.precision = 10;
    value.scale = 0;
    for (a, b) in value
        .type_id
        .iter_mut()
        .zip(c"org.example.value".to_bytes_with_nul())
    {
        *a = *b as _;
    }
    match h.mode {
        7 => value.struct_size -= 1,
        8 => value.flags |= 32,
        9 => value.reserved[1] = 1,
        10 => value.type_id.fill(120),
        11 => value.type_id[0] = 0,
        12 => value.flags &= !1,
        13 => value.flags &= !4,
        14 => {
            value.flags = 2;
            value.type_id.fill(0);
            value.table_id = 0;
            value.column_id = 0;
        }
        _ => (),
    }
    unsafe { *out = value };
    sys::OK
}
unsafe extern "C" fn argument(p: *mut c_void, id: u32, index: u32, out: *mut u32) -> sys::Status {
    assert_eq!((id, index), (50, 0));
    let h = unsafe { host(p) };
    h.calls += 1;
    unsafe { *out = if h.mode == 15 { u32::MAX } else { 50 } };
    sys::OK
}
unsafe extern "C" fn count(p: *mut c_void) -> u32 {
    1 + unsafe { host(p).built }
}
unsafe extern "C" fn error(p: *mut c_void) -> i32 {
    unsafe { host(p).error }
}
unsafe extern "C" fn get(_: *mut c_void, _: u32, _: *mut sys::CandidateInfo) -> sys::Status {
    panic!("unused get")
}
unsafe extern "C" fn build(
    p: *mut c_void,
    r: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    assert!(h.mode >= 20);
    h.calls += 1;
    if h.mode >= 60 {
        let r = unsafe { &*r.cast::<sys::CustomPathRequestV3>() };
        assert_eq!(
            r.v2.v1.v1.struct_size,
            size_of::<sys::CustomPathRequestV3>() as u32
        );
        assert_eq!(
            (r.v2.v1.v1.input_index, r.execution, r.reserved),
            (0, 1, [0; 4])
        );
        assert_eq!(r.v2.reserved, [0; 4]);
        let plans = unsafe { std::slice::from_raw_parts(r.input_plans, r.plan_count as usize) };
        let offsets =
            unsafe { std::slice::from_raw_parts(r.input_offsets, r.plan_count as usize + 1) };
        let values = unsafe { std::slice::from_raw_parts(r.v2.inputs, r.v2.input_count as usize) };
        if h.mode == 61 {
            assert!(plans.is_empty() && values.is_empty());
            assert_eq!(offsets, &[0]);
        } else if h.mode == 62 {
            assert_eq!(plans, &[10, 20]);
            assert_eq!(offsets, &[0, 0, 2]);
            assert_eq!(values, &[50, 50]);
        } else {
            assert_eq!(plans, &[10, 20]);
            assert_eq!(offsets, &[0, 1024, 2048]);
            assert_eq!(values, vec![50; 2048]);
        }
        assert_eq!(r.v2.output_count, 1);
        assert_eq!(unsafe { *r.v2.outputs }, 50);
        if h.mode == 67 {
            h.error = -4012;
        }
        unsafe { *out = 1 };
        h.built += 1;
        return sys::OK;
    }
    let r = unsafe { &*r.cast::<sys::CustomPathRequestV2>() };
    assert_eq!(
        r.v1.v1.struct_size,
        size_of::<sys::CustomPathRequestV2>() as u32
    );
    assert_eq!(
        (r.v1.v1.kind, r.v1.v1.input_index, r.v1.v1.reserved_word),
        (2, 0, 0)
    );
    assert_eq!(r.v1.v1.reserved, [0; 4]);
    assert_eq!(r.v1.reserved, [0; 4]);
    assert_eq!(r.reserved, [0; 4]);
    assert_eq!(
        unsafe { std::ffi::CStr::from_ptr(r.v1.service_id) },
        c"test.layout"
    );
    assert_eq!(
        (
            r.v1.service_major,
            r.v1.minimum_minor,
            r.v1.operator_cost,
            r.v1.flags
        ),
        (1, 0, 2.0, 3)
    );
    assert_eq!(
        unsafe { std::slice::from_raw_parts(r.v1.plan, r.v1.plan_size as usize) },
        b"opaque"
    );
    if h.mode == 21 {
        assert_eq!((r.input_count, r.output_count), (0, 0));
    } else {
        assert_eq!(
            unsafe { std::slice::from_raw_parts(r.inputs, r.input_count as usize) },
            &[50, 50]
        );
        assert_eq!(
            unsafe { std::slice::from_raw_parts(r.outputs, r.output_count as usize) },
            &[50]
        );
    }
    if h.mode == 30 {
        h.error = -4012;
    }
    unsafe { *out = if h.mode == 31 { 0 } else { 1 } };
    if h.mode != 32 {
        h.built += 1;
    }
    sys::OK
}
unsafe extern "C" fn select(p: *mut c_void, id: u32) -> sys::Status {
    assert_eq!(id, unsafe { host(p).built });
    sys::OK
}
unsafe extern "C" fn next(_: *mut c_void, _: *mut i32) -> sys::Status {
    panic!("replacement does not call next")
}
struct Probe;
struct FragmentProbe<const MODE: u32>;
impl<const MODE: u32> candidate::Hook for FragmentProbe<MODE> {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const INSPECT: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let root = c.root(0)?;
        let child = c.child(root, 0)?;
        let value = c.expression(root, Role::Filter, 0)?.0;
        let values = vec![
            value;
            if MODE == 63 {
                1025
            } else if MODE == 62 {
                2
            } else {
                1024
            }
        ];
        let mut inputs = vec![
            candidate::FragmentInput {
                plan: root,
                expressions: if MODE == 62 { &[] } else { &values },
            },
            candidate::FragmentInput {
                plan: child,
                expressions: &values,
            },
        ];
        if MODE == 61 {
            inputs.clear();
        }
        if MODE == 64 {
            inputs[1].plan = root;
        }
        if MODE == 65 {
            inputs.clear();
            for _ in 0..65 {
                inputs.push(candidate::FragmentInput {
                    plan: root,
                    expressions: &[],
                });
            }
        }
        let outputs = vec![value; if MODE == 66 { 2 } else { 1 }];
        let result = c.custom_fragment(
            &candidate::CustomPath {
                input: 0,
                service_id: c"test.fragment",
                service_major: 1,
                minimum_minor: 0,
                plan: b"opaque",
                operator_cost: 2.0,
                preserves_order: false,
                blocking: true,
            },
            &inputs,
            &outputs,
        );
        if let Ok(index) = result {
            c.select(index)?;
        }
        // Local errors and exact host failures must remain sticky when swallowed.
        Ok(())
    }
}
#[test]
fn fragment_submission_partitions_inputs_and_preserves_failures() {
    fn run<const MODE: u32>(expected: sys::Status, built: u32) {
        let mut h = Host {
            mode: MODE,
            calls: 0,
            error: 0,
            built: 0,
        };
        let context = context(&mut h);
        assert_eq!(
            unsafe {
                candidate::Service::<FragmentProbe<MODE>>::ABI
                    .invoke
                    .unwrap()(ptr::null_mut(), &context.v2.v1)
            },
            expected
        );
        assert_eq!(h.built, built);
    }
    run::<60>(sys::OK, 1);
    run::<61>(sys::OK, 1);
    run::<62>(sys::OK, 1);
    run::<63>(sys::INVALID, 0);
    run::<64>(sys::INVALID, 0);
    run::<65>(sys::INVALID, 0);
    run::<66>(sys::INVALID, 0);
    run::<67>(sys::FAILED_PRECONDITION, 1);
}
fn inspect(c: &mut Context<'_>) -> Result<()> {
    let id = c.root(0)?;
    assert_eq!(c.root(0)?, id);
    let p = c.plan(id)?;
    assert_eq!(
        (p.operator_type, p.child_count, p.join_type),
        (100, 1, Some(5))
    );
    assert_eq!((p.cost, p.rows, p.width), (2.5, 9.0, 16.0));
    assert_eq!(p.expression_count(Role::Filter), 1);
    assert_ne!(id, c.child(id, 0)?);
    let (e, ordering) = c.expression(id, Role::Filter, 0)?;
    assert_eq!(ordering, None);
    let (same, ordering) = c.expression(id, Role::Ordering, 0)?;
    assert_eq!(ordering, Some(1));
    assert_eq!(e, same);
    let expr = c.describe_expression(e)?;
    assert_eq!(
        (
            expr.expression_type(),
            expr.sql_type(),
            expr.argument_count()
        ),
        (42, 5, 1)
    );
    assert_eq!(
        (expr.collation(), expr.precision(), expr.scale()),
        (63, 10, 0)
    );
    assert!(expr.not_null() && !expr.constant() && !expr.stored());
    assert_eq!(expr.column(), Some((101, 7)));
    assert_eq!(expr.plugin_type(), Some(c"org.example.value"));
    assert_eq!(c.argument(e, 0)?, e);
    c.select(0)
}
impl candidate::Hook for Probe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const INSPECT: bool = true; // implicitly enables v2; no BUILDERS opt-in needed
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let _ = inspect(c); // ABI/protocol failures must survive a swallowed Result.
        Ok(())
    }
}
fn context(host: &mut Host) -> sys::CandidateContextV3 {
    sys::CandidateContextV3 {
        v2: sys::CandidateContextV2 {
            v1: sys::CandidateContext {
                struct_size: size_of::<sys::CandidateContextV3>() as u32,
                candidate_count: 1,
                host_context: (host as *mut Host).cast(),
                get: Some(get),
                select: Some(select),
                continuation: ptr::null_mut(),
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
    }
}
unsafe extern "C" fn query(p: *mut c_void, out: *mut sys::QueryInfo) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    let mut value = sys::QueryInfo {
        struct_size: size_of::<sys::QueryInfo>() as u32,
        statement_type: 7,
        flags: 1,
        target_count: 3,
        ..Default::default()
    };
    match h.mode {
        41 => value.flags = 3,
        42 => value.target_count = 0,
        43 => {
            value.flags = 0;
            value.target_count = 0;
        }
        44 => value.struct_size -= 1,
        45 => value.reserved[2] = 1,
        46 => value.flags = 4,
        47 => value.flags = 0,
        48 => {
            value.flags = 2;
            value.target_count = 0;
        }
        49 | 50 => h.error = -4012,
        _ => (),
    }
    unsafe { *out = value };
    if h.mode == 50 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
unsafe extern "C" fn target(p: *mut c_void, ordinal: u32, out: *mut u32) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    assert!(ordinal < 3);
    unsafe {
        *out = if h.mode == 51 {
            u32::MAX
        } else if ordinal == 1 {
            51
        } else {
            50
        }
    };
    if h.mode == 52 || h.mode == 53 {
        h.error = -4012;
    }
    if h.mode == 53 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
fn query_context(host: &mut Host) -> sys::CandidateContextV4 {
    let mut c = sys::CandidateContextV4 {
        v3: context(host),
        query: Some(query),
        target: Some(target),
        reserved: [0; 4],
    };
    c.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV4>() as u32;
    c
}
unsafe extern "C" fn plan_semantics(
    p: *mut c_void,
    _: u32,
    out: *mut sys::PlanSemantics,
) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    let mut value = sys::PlanSemantics {
        struct_size: size_of::<sys::PlanSemantics>() as u32,
        relation_kind: 1,
        flags: 1,
        ..Default::default()
    };
    match h.mode {
        81 => value.flags = 2,
        82 => value.relation_kind = 9,
        83 => value.struct_size -= 1,
        84 => value.reserved[0] = 1,
        91 | 92 => h.error = -4012,
        _ => {}
    }
    unsafe { *out = value };
    if h.mode == 91 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
unsafe extern "C" fn expression_semantics(
    p: *mut c_void,
    _: u32,
    out: *mut sys::ExprSemantics,
) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    let mut value = sys::ExprSemantics {
        struct_size: size_of::<sys::ExprSemantics>() as u32,
        comparison_kind: 1,
        value_kind: 2,
        flags: 1,
        ..Default::default()
    };
    match h.mode {
        85 => value.comparison_kind = 3,
        86 => value.value_kind = 3,
        87 => value.flags = 2,
        88 => value.reserved[0] = 1,
        _ => {}
    }
    unsafe { *out = value };
    sys::OK
}
unsafe extern "C" fn scope(p: *mut c_void, _: u32, _: u32, out: *mut u32) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    unsafe { *out = if h.mode == 89 { u32::MAX } else { 2 } };
    sys::OK
}
unsafe extern "C" fn column_count(p: *mut c_void, out: *mut u32) -> sys::Status {
    unsafe {
        host(p).calls += 1;
        *out = 2;
    }
    sys::OK
}
unsafe extern "C" fn column(p: *mut c_void, index: u32, out: *mut u32) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    assert_eq!(index, 0);
    unsafe { *out = if h.mode == 90 { u32::MAX } else { 50 } };
    sys::OK
}
fn semantics_context(h: &mut Host) -> sys::CandidateContextV5 {
    let mut c = sys::CandidateContextV5 {
        v4: query_context(h),
        plan_semantics: Some(plan_semantics),
        expression_semantics: Some(expression_semantics),
        scope: Some(scope),
        column_count: Some(column_count),
        column: Some(column),
        reserved: [0; 4],
    };
    c.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV5>() as u32;
    c
}
unsafe extern "C" fn sort_info(p: *mut c_void, id: u32, out: *mut sys::SortInfo) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    assert_eq!(id, 10);
    let mut info = sys::SortInfo {
        struct_size: size_of::<sys::SortInfo>() as u32,
        flags: 31,
        key_count: 4,
        prefix_key_count: 1,
        partition_key_count: 2,
        topn_expression: 50,
        topk_limit_expression: 50,
        topk_offset_expression: 50,
        hash_expression: 50,
        ..Default::default()
    };
    match h.mode {
        201 | 208 | 209 => {
            info.flags = 0;
            info.key_count = 0;
            info.prefix_key_count = 0;
            info.partition_key_count = 0;
            info.topn_expression = u32::MAX;
            info.topk_limit_expression = u32::MAX;
            info.topk_offset_expression = u32::MAX;
            info.hash_expression = u32::MAX;
            if h.mode == 208 {
                info.flags = 2;
            }
            if h.mode == 209 {
                info.topn_expression = 50;
            }
        }
        202 => info.struct_size -= 1,
        203 => info.flags |= 32,
        204 => info.reserved_word = 1,
        205 => info.reserved[0] = 1,
        206 => info.prefix_key_count = 5,
        207 => info.partition_key_count = 5,
        210 => return sys::INTERNAL,
        211 => h.error = -4012,
        216 => {
            info.flags = 1;
            info.prefix_key_count = 0;
            info.partition_key_count = 0;
            info.topn_expression = u32::MAX;
            info.topk_limit_expression = u32::MAX;
            info.topk_offset_expression = u32::MAX;
            info.hash_expression = u32::MAX;
        }
        _ => (),
    }
    unsafe {
        *out = info;
    }
    sys::OK
}
unsafe extern "C" fn sort_key(
    p: *mut c_void,
    id: u32,
    ordinal: u32,
    expr: *mut u32,
    flags: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    h.calls += 1;
    assert_eq!(id, 10);
    assert!(ordinal < 4);
    unsafe {
        *expr = if h.mode == 213 { u32::MAX } else { 50 };
        *flags = if h.mode == 212 { 4 } else { ordinal };
    }
    if h.mode == 214 {
        return sys::INTERNAL;
    }
    if h.mode == 215 {
        h.error = -4012;
    }
    sys::OK
}
fn sorts_context(h: &mut Host) -> sys::CandidateContextV7 {
    let mut c = sys::CandidateContextV7 {
        v6: bindings_context(h),
        sort_info: Some(sort_info),
        sort_key: Some(sort_key),
        reserved: [0; 4],
    };
    c.v6.v5.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV7>() as u32;
    c
}
struct SortProbe;
unsafe extern "C" fn value_info(
    p: *mut c_void,
    plan: u32,
    expr: u32,
    out: *mut sys::ValueInfo,
) -> sys::Status {
    assert_eq!(plan, 10);
    let h = unsafe { host(p) };
    h.calls += 1;
    let mut value = sys::ValueInfo {
        struct_size: size_of::<sys::ValueInfo>() as u32,
        flags: if expr == 50 {
            2
        } else if expr == 51 {
            1
        } else {
            0
        },
        ..Default::default()
    };
    match h.mode {
        301 => value.flags |= 4,
        302 => value.reserved[0] = 1,
        303 => value.struct_size -= 1,
        304 => {
            h.error = -4012;
            return sys::INTERNAL;
        }
        305 => h.error = -4012,
        306 => value.flags = 0,
        _ => (),
    }
    unsafe {
        *out = value;
    }
    sys::OK
}
unsafe extern "C" fn value_describe(
    _: *mut c_void,
    id: u32,
    out: *mut sys::ExprInfo,
) -> sys::Status {
    unsafe {
        *out = std::mem::zeroed();
        (*out).struct_size = size_of::<sys::ExprInfo>() as u32;
        (*out).argument_count = if id == 50 { 3 } else { 0 };
        (*out).flags = if id == 52 { 8 } else { 0 };
    }
    sys::OK
}
unsafe extern "C" fn value_argument(
    p: *mut c_void,
    id: u32,
    index: u32,
    out: *mut u32,
) -> sys::Status {
    assert_eq!(id, 50);
    let mode = unsafe { host(p).mode };
    unsafe {
        *out = if mode == 307 {
            50
        } else if index == 1 {
            52
        } else {
            51
        };
    }
    sys::OK
}
fn values_context(h: &mut Host) -> sys::CandidateContextV8 {
    let mut c = sys::CandidateContextV8 {
        v7: sorts_context(h),
        value_info: Some(value_info),
        reserved: [0; 4],
    };
    c.v7.v6.v5.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV8>() as u32;
    c.v7.v6.v5.v4.v3.describe_expression = Some(value_describe);
    c.v7.v6.v5.v4.v3.argument = Some(value_argument);
    c
}
struct ValueProbe;
impl candidate::Hook for ValueProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const VALUES: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let _ignored = (|| -> Result<()> {
            let plan = c.root(0)?;
            let expr = c.target(0)?;
            if let Some(values) = c.required_values(plan, expr)? {
                assert_eq!(values, vec![c.argument(expr, 0)?]);
            }
            Ok(())
        })();
        Ok(()) // Ignoring an error must not erase it.
    }
}
#[test]
fn value_dependencies_preserve_existing_results_deduplicate_and_reject_cycles_and_host_errors() {
    assert_eq!(candidate::Service::<ValueProbe>::ABI.spi_minor, 7);
    for mode in 300..=307 {
        let mut h = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = values_context(&mut h);
        let result = unsafe {
            candidate::Service::<ValueProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v7.v6.v5.v4.v3.v2.v1,
            )
        };
        assert_eq!(
            result,
            match mode {
                300 | 306 => sys::OK,
                304 => sys::INTERNAL,
                305 => sys::FAILED_PRECONDITION,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
    }
    for fault in 0..4 {
        let mut h = Host {
            mode: 300,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut c = values_context(&mut h);
        match fault {
            0 => c.value_info = None,
            1 => c.reserved[0] = 1,
            2 => c.v7.sort_info = None,
            _ => c.v7.v6.v5.v4.v3.v2.v1.struct_size -= 1,
        }
        assert_eq!(
            unsafe {
                candidate::Service::<ValueProbe>::ABI.invoke.unwrap()(
                    ptr::null_mut(),
                    &c.v7.v6.v5.v4.v3.v2.v1,
                )
            },
            sys::INVALID
        );
        assert_eq!(h.calls, 0);
    }
}
struct MissingValueProbe;
impl candidate::Hook for MissingValueProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const SORTS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let plan = c.root(0)?;
        let expr = c.target(0)?;
        let _ignored = c.required_values(plan, expr);
        Ok(())
    }
}
#[test]
fn missing_value_capability_does_not_mean_no_dependencies() {
    let mut h = Host {
        mode: 300,
        calls: 0,
        error: 0,
        built: 0,
    };
    let c = sorts_context(&mut h);
    assert_eq!(
        unsafe {
            candidate::Service::<MissingValueProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v6.v5.v4.v3.v2.v1,
            )
        },
        sys::UNSUPPORTED_ABI
    );
}
struct MissingSortProbe;
impl candidate::Hook for MissingSortProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const PARAMETERS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let root = c.root(0)?;
        let _ignored = c.sort(root);
        Ok(())
    }
}
#[test]
fn missing_sort_capability_is_not_reported_as_non_sort() {
    let mut h = Host {
        mode: 200,
        calls: 0,
        error: 0,
        built: 0,
    };
    let c = bindings_context(&mut h);
    assert_eq!(
        unsafe {
            candidate::Service::<MissingSortProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v5.v4.v3.v2.v1,
            )
        },
        sys::UNSUPPORTED_ABI
    );
    assert_eq!(h.calls, 1); // Only root reached the host.
}
impl candidate::Hook for SortProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const SORTS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let _ignored = (|| -> Result<()> {
            let root = c.root(0)?;
            let Some(sort) = c.sort(root)? else {
                return Ok(());
            };
            assert_eq!(sort.key_count, 4);
            if sort.topn.is_some() {
                assert!(
                    sort.encoded_keys && sort.local_merge && sort.with_ties && sort.runtime_filter
                );
                assert_eq!((sort.prefix_key_count, sort.partition_key_count), (1, 2));
            } else {
                assert!(
                    !sort.encoded_keys
                        && !sort.local_merge
                        && !sort.with_ties
                        && !sort.runtime_filter
                );
                assert_eq!((sort.prefix_key_count, sort.partition_key_count), (0, 0));
            }
            let column = c.column(0)?;
            for expr in [sort.topn, sort.topk_limit, sort.topk_offset, sort.hash]
                .into_iter()
                .flatten()
            {
                assert_eq!(expr, column);
                c.describe_expression(expr)?;
            }
            for i in 0..4 {
                let key = c.sort_key(root, i)?;
                assert_eq!(key.expression, column);
                assert_eq!((key.descending, key.nulls_first), (i & 1 != 0, i & 2 != 0));
            }
            Ok(())
        })();
        Ok(()) // All protocol failures must remain sticky despite this success.
    }
}
#[test]
fn sort_semantics_normalize_keys_and_reject_malformed_or_failed_metadata() {
    assert_eq!(candidate::Service::<SortProbe>::ABI.spi_minor, 6);
    for mode in 200..=216 {
        let mut h = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = sorts_context(&mut h);
        let result = unsafe {
            candidate::Service::<SortProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v6.v5.v4.v3.v2.v1,
            )
        };
        assert_eq!(
            result,
            match mode {
                200 | 201 | 216 => sys::OK,
                210 | 214 => sys::INTERNAL,
                211 | 215 => sys::FAILED_PRECONDITION,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
        assert_eq!(h.built, 0);
    }
}
#[test]
fn sorts_require_complete_v7_and_do_not_reinterpret_older_prefixes() {
    for fault in 0..7 {
        let mut h = Host {
            mode: 200,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut c = sorts_context(&mut h);
        match fault {
            0 => c.sort_info = None,
            1 => c.sort_key = None,
            2 => c.reserved[3] = 1,
            3 => c.v6.v5.v4.v3.v2.v1.struct_size -= 1,
            4 => c.v6.v5.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV6>() as u32,
            5 => c.v6.binding = None,
            _ => c.v6.v5.scope = None,
        }
        assert_eq!(
            unsafe {
                candidate::Service::<SortProbe>::ABI.invoke.unwrap()(
                    ptr::null_mut(),
                    &c.v6.v5.v4.v3.v2.v1,
                )
            },
            sys::INVALID
        );
        assert_eq!(h.calls, 0);
    }
}
struct SemanticsProbe;
unsafe extern "C" fn binding_count(
    p: *mut c_void,
    id: u32,
    role: u32,
    out: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    assert_eq!(id, 10);
    assert!((1..=3).contains(&role));
    h.calls += 1;
    unsafe {
        *out = 1;
    }
    if h.mode == 103 || h.mode == 104 {
        h.error = -4012;
    }
    if h.mode == 103 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
unsafe extern "C" fn binding(
    p: *mut c_void,
    id: u32,
    role: u32,
    index: u32,
    parameter: *mut u32,
    source: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    assert_eq!((id, index), (10, 0));
    assert!((1..=3).contains(&role));
    h.calls += 1;
    unsafe {
        *parameter = if h.mode == 101 { u32::MAX } else { 60 };
        *source = if h.mode == 102 { u32::MAX } else { 50 };
    }
    if h.mode == 105 || h.mode == 106 {
        h.error = -4012;
    }
    if h.mode == 105 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
fn bindings_context(h: &mut Host) -> sys::CandidateContextV6 {
    let mut c = sys::CandidateContextV6 {
        v5: semantics_context(h),
        binding_count: Some(binding_count),
        binding: Some(binding),
        reserved: [0; 4],
    };
    c.v5.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV6>() as u32;
    c
}
struct BindingsProbe;
impl candidate::Hook for BindingsProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const PARAMETERS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let _ignored = (|| -> Result<()> {
            let root = c.root(0)?;
            assert_eq!(
                c.plan_semantics(root)?.relation,
                candidate::RelationKind::InnerJoin
            );
            for role in [
                candidate::BindingRole::NestedLoop,
                candidate::BindingRole::LeftPushDown,
                candidate::BindingRole::RightPushDown,
            ] {
                assert_eq!(c.binding_count(root, role)?, 1);
                let binding = c.binding(root, role, 0)?;
                assert_ne!(binding.parameter, binding.source);
                assert_eq!(binding.source, c.column(0)?);
                assert_eq!(binding.source, c.target(0)?);
            }
            Ok(())
        })();
        Ok(()) // A plugin cannot suppress a failed graph callback.
    }
}
#[test]
fn parameter_bindings_share_expression_identity_and_preserve_errors() {
    assert_eq!(candidate::Service::<BindingsProbe>::ABI.spi_minor, 5);
    for mode in 100..=106 {
        let mut h = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = bindings_context(&mut h);
        let result = unsafe {
            candidate::Service::<BindingsProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v5.v4.v3.v2.v1,
            )
        };
        assert_eq!(
            result,
            match mode {
                100 => sys::OK,
                103 | 105 => sys::INTERNAL,
                104 | 106 => sys::FAILED_PRECONDITION,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
        assert_eq!(h.built, 0);
    }
}
#[test]
fn incomplete_bindings_context_is_rejected_before_callback() {
    for fault in 0..6 {
        let mut h = Host {
            mode: 100,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut c = bindings_context(&mut h);
        match fault {
            0 => c.binding_count = None,
            1 => c.binding = None,
            2 => c.reserved[0] = 1,
            3 => c.v5.v4.v3.v2.v1.struct_size -= 1,
            4 => c.v5.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV5>() as u32,
            _ => c.v5.scope = None,
        }
        assert_eq!(
            unsafe {
                candidate::Service::<BindingsProbe>::ABI.invoke.unwrap()(
                    ptr::null_mut(),
                    &c.v5.v4.v3.v2.v1,
                )
            },
            sys::INVALID
        );
        assert_eq!(h.calls, 0);
    }
}
struct NoBindingsProbe;
struct BoundFragmentProbe<const PARAMETERS: bool>;
impl<const PARAMETERS: bool> candidate::Hook for BoundFragmentProbe<PARAMETERS> {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const SEMANTICS: bool = true;
    const PARAMETERS: bool = PARAMETERS;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(instance: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let mode = unsafe { *instance.cast::<u32>() };
        let root = c.root(0)?;
        let right = c.child(root, 0)?;
        let source = c.column(0)?;
        let parameter = if PARAMETERS {
            c.binding(root, candidate::BindingRole::NestedLoop, 0)?
                .parameter
        } else {
            source
        };
        let make_binding = || candidate::InputBinding {
            parameter,
            source_input: if mode == 122 {
                2
            } else if mode == 124 {
                1
            } else {
                0
            },
            source_column: u32::from(mode == 125),
            target_input: if mode == 123 { 2 } else { 1 },
        };
        let bindings: Vec<_> = (0..match mode {
            121 => 0,
            126 => 2,
            127 => 1025,
            _ => 1,
        })
            .map(|_| make_binding())
            .collect();
        let _ = c.custom_bound_fragment(
            &candidate::CustomPath {
                input: 0,
                service_id: c"test.bound",
                service_major: 1,
                minimum_minor: 0,
                plan: b"bound",
                operator_cost: 2.0,
                preserves_order: false,
                blocking: false,
            },
            &[
                candidate::FragmentInput {
                    plan: root,
                    expressions: &[source],
                },
                candidate::FragmentInput {
                    plan: right,
                    expressions: &[source],
                },
            ],
            &[source],
            &bindings,
        ); // Deliberately swallow validation and host failures.
        Ok(())
    }
}
unsafe extern "C" fn build_bound(
    p: *mut c_void,
    raw: *const sys::PathRequest,
    out: *mut u32,
) -> sys::Status {
    let h = unsafe { host(p) };
    let r = unsafe { &*raw.cast::<sys::CustomPathRequestV4>() };
    assert_eq!(
        r.v3.v2.v1.v1.struct_size,
        size_of::<sys::CustomPathRequestV4>() as u32
    );
    assert_eq!((r.v3.v2.v1.v1.kind, r.v3.v2.v1.v1.input_index), (2, 0));
    assert_eq!(r.v3.v2.v1.v1.reserved_word, 0);
    assert_eq!(r.v3.v2.v1.v1.reserved, [0; 4]);
    assert_eq!(r.v3.v2.v1.reserved, [0; 4]);
    assert_eq!(r.v3.v2.reserved, [0; 4]);
    assert_eq!(r.v3.reserved, [0; 4]);
    assert_eq!((r.reserved_word, r.reserved), (0, [0; 4]));
    assert_eq!(
        unsafe { std::ffi::CStr::from_ptr(r.v3.v2.v1.service_id) },
        c"test.bound"
    );
    assert_eq!(
        unsafe { std::slice::from_raw_parts(r.v3.v2.v1.plan, r.v3.v2.v1.plan_size as usize) },
        b"bound"
    );
    assert_eq!(
        (r.v3.plan_count, r.v3.execution, r.binding_count),
        (2, 1, 1)
    );
    assert_eq!(
        unsafe { std::slice::from_raw_parts(r.v3.input_plans, 2) },
        &[10, 20]
    );
    assert_eq!(
        unsafe { std::slice::from_raw_parts(r.v3.input_offsets, 3) },
        &[0, 1, 2]
    );
    assert_eq!((r.v3.v2.input_count, r.v3.v2.output_count), (2, 1));
    assert_eq!(
        unsafe { std::slice::from_raw_parts(r.v3.v2.inputs, 2) },
        &[50, 50]
    );
    assert_eq!(unsafe { *r.v3.v2.outputs }, 50);
    let b = unsafe { &*r.bindings };
    assert_eq!(
        (b.parameter, b.source_input, b.source_column, b.target_input),
        (60, 0, 0, 1)
    );
    h.built += 1;
    unsafe { *out = 1 };
    if h.mode >= 128 {
        h.error = -4012;
    }
    if h.mode == 128 {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
#[test]
fn bound_fragment_transports_owned_binding_declarations_and_sticky_failures() {
    for mode in 120..=129 {
        let mut h = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut context = bindings_context(&mut h);
        context.v5.v4.v3.v2.build = Some(build_bound);
        let mut mode = mode;
        let result = unsafe {
            candidate::Service::<BoundFragmentProbe<true>>::ABI
                .invoke
                .unwrap()((&mut mode as *mut u32).cast(), &context.v5.v4.v3.v2.v1)
        };
        assert_eq!(
            result,
            match mode {
                120 => sys::OK,
                128 => sys::INTERNAL,
                129 => sys::FAILED_PRECONDITION,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
        assert_eq!(h.built, u32::from(mode == 120 || mode >= 128));
    }
}
#[test]
fn bound_fragment_requires_parameter_capability_even_when_graph_is_available() {
    let mut h = Host {
        mode: 130,
        calls: 0,
        error: 0,
        built: 0,
    };
    let mut context = semantics_context(&mut h);
    context.v4.v3.v2.build = Some(build_bound);
    let mut mode = h.mode;
    assert_eq!(
        unsafe {
            candidate::Service::<BoundFragmentProbe<false>>::ABI
                .invoke
                .unwrap()((&mut mode as *mut u32).cast(), &context.v4.v3.v2.v1)
        },
        sys::UNSUPPORTED_ABI
    );
    assert_eq!(h.built, 0);
}
impl candidate::Hook for NoBindingsProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const SEMANTICS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let root = c.root(0)?;
        let _ = c.binding_count(root, candidate::BindingRole::NestedLoop);
        Ok(())
    }
}
#[test]
fn missing_parameter_capability_does_not_masquerade_as_independent_inputs() {
    let mut h = Host {
        mode: 100,
        calls: 0,
        error: 0,
        built: 0,
    };
    let c = semantics_context(&mut h);
    assert_eq!(
        unsafe {
            candidate::Service::<NoBindingsProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v4.v3.v2.v1,
            )
        },
        sys::UNSUPPORTED_ABI
    );
    assert_eq!(h.calls, 1);
}
impl candidate::Hook for SemanticsProbe {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const SEMANTICS: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let _ignored = (|| -> Result<()> {
            let root = c.root(0)?;
            let p = c.plan_semantics(root)?;
            assert_eq!(p.relation, candidate::RelationKind::InnerJoin);
            assert!(p.local_serial);
            assert_eq!(c.query()?.target_count, Some(3));
            assert_eq!(c.column_count()?, 2);
            let column = c.column(0)?;
            assert_eq!(column, c.target(0)?);
            let e = c.expression_semantics(column)?;
            assert_eq!(e.comparison, candidate::ComparisonKind::Equal);
            assert_eq!(e.value, candidate::ValueKind::UnsignedInteger);
            assert!(e.scalar_deterministic);
            assert_eq!(
                c.dependency_scope(root, column)?,
                candidate::DependencyScope::Contained
            );
            Ok(())
        })();
        Ok(())
    }
}
#[test]
fn normalized_semantics_scope_and_columns_preserve_protocol_errors() {
    assert_eq!(candidate::Service::<SemanticsProbe>::ABI.spi_minor, 4);
    for mode in 80..=92 {
        let mut h = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = semantics_context(&mut h);
        let status = unsafe {
            candidate::Service::<SemanticsProbe>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v4.v3.v2.v1,
            )
        };
        assert_eq!(
            status,
            match mode {
                80 => sys::OK,
                91 => sys::INTERNAL,
                92 => sys::FAILED_PRECONDITION,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
        assert_eq!(h.built, 0);
    }
}
#[test]
fn incomplete_semantics_context_is_rejected_before_callback() {
    for fault in 0..9 {
        let mut h = Host {
            mode: 80,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut c = semantics_context(&mut h);
        match fault {
            0 => c.plan_semantics = None,
            1 => c.expression_semantics = None,
            2 => c.scope = None,
            3 => c.column_count = None,
            4 => c.column = None,
            5 => c.reserved[0] = 1,
            6 => c.v4.target = None,
            7 => c.v4.v3.v2.v1.struct_size -= 1,
            _ => c.v4.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV4>() as u32,
        }
        assert_eq!(
            unsafe {
                candidate::Service::<SemanticsProbe>::ABI.invoke.unwrap()(
                    ptr::null_mut(),
                    &c.v4.v3.v2.v1,
                )
            },
            sys::INVALID
        );
        assert_eq!(h.calls, 0);
    }
}
struct QueryProbe<const ENABLE: bool>;
impl<const ENABLE: bool> candidate::Hook for QueryProbe<ENABLE> {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const QUERY_TARGETS: bool = ENABLE;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let _ignored = (|| -> Result<()> {
            let query = c.query()?;
            assert_eq!(query.statement_type, 7);
            if let Some(count) = query.target_count {
                if count > 0 {
                    assert_eq!(count, 3);
                    let first = c.target(0)?;
                    assert_ne!(first, c.target(1)?);
                    assert_eq!(first, c.target(2)?);
                    let root = c.root(0)?;
                    assert_eq!(first, c.expression(root, Role::Filter, 0)?.0);
                    let _ = c.describe_expression(first)?;
                }
            } else {
                assert!(!query.set_operation);
            }
            c.select(0)
        })();
        // Swallow deliberately: local validation and host errors stay sticky.
        Ok(())
    }
}
#[test]
fn query_targets_order_identity_and_sticky_errors() {
    assert_eq!(candidate::Service::<QueryProbe<true>>::ABI.spi_minor, 3);
    for mode in 40..54 {
        let mut h = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = query_context(&mut h);
        let result = unsafe {
            candidate::Service::<QueryProbe<true>>::ABI.invoke.unwrap()(
                ptr::null_mut(),
                &c.v3.v2.v1,
            )
        };
        assert_eq!(
            result,
            match mode {
                40..=43 => sys::OK,
                49 | 52 => sys::FAILED_PRECONDITION,
                50 | 53 => sys::INTERNAL,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
        assert!(h.calls > 0);
    }
}
#[test]
fn incomplete_query_context_rejected_before_invocation() {
    for fault in 0..7 {
        let mut h = Host {
            mode: 40,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut c = query_context(&mut h);
        match fault {
            0 => c.v3.v2.v1.struct_size = size_of::<sys::CandidateContextV3>() as u32,
            1 => c.query = None,
            2 => c.target = None,
            3 => c.reserved[0] = 1,
            4 => c.v3.argument = None,
            5 => c.v3.v2.get_error = None,
            _ => c.v3.v2.v1.struct_size -= 1,
        }
        assert_eq!(
            unsafe {
                candidate::Service::<QueryProbe<true>>::ABI.invoke.unwrap()(
                    ptr::null_mut(),
                    &c.v3.v2.v1,
                )
            },
            sys::INVALID
        );
        assert_eq!(h.calls, 0);
    }
}
#[test]
fn query_api_requires_declared_capability() {
    let mut h = Host {
        mode: 40,
        calls: 0,
        error: 0,
        built: 0,
    };
    let mut c = context(&mut h);
    c.v2.v1.struct_size = size_of::<sys::CandidateContext>() as u32;
    assert_eq!(
        unsafe {
            candidate::Service::<QueryProbe<false>>::ABI.invoke.unwrap()(ptr::null_mut(), &c.v2.v1)
        },
        sys::UNSUPPORTED_ABI
    );
    assert_eq!(h.calls, 0);
}
#[test]
fn graph_protocol_and_sticky_failures() {
    let service = candidate::Service::<Probe>::ABI;
    assert_eq!(service.spi_minor, 2);
    for mode in 0..16 {
        let mut host = Host {
            mode,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = context(&mut host);
        let result = unsafe { service.invoke.unwrap()(ptr::null_mut(), &c.v2.v1) };
        assert_eq!(
            result,
            match mode {
                0 => sys::OK,
                2 => sys::INTERNAL,
                3 => sys::FAILED_PRECONDITION,
                _ => sys::INVALID,
            },
            "mode {mode}"
        );
        assert!(host.calls > 0);
    }
}
#[test]
fn truncated_or_incomplete_graph_never_reaches_plugin() {
    for mode in 0..10 {
        let mut host = Host {
            mode: 0,
            calls: 0,
            error: 0,
            built: 0,
        };
        let mut c = context(&mut host);
        match mode {
            0 => c.v2.v1.struct_size -= 1,
            1 => c.root = None,
            2 => c.plan = None,
            3 => c.child = None,
            4 => c.expression = None,
            5 => c.describe_expression = None,
            6 => c.argument = None,
            7 => c.reserved[3] = 1,
            8 => c.v2.reserved[2] = 1,
            _ => c.v2.get_error = None,
        }
        assert_eq!(
            unsafe { candidate::Service::<Probe>::ABI.invoke.unwrap()(ptr::null_mut(), &c.v2.v1) },
            sys::INVALID
        );
        assert_eq!(host.calls, 0);
    }
}

struct Layout<const ACTION: u32>;
impl<const ACTION: u32> candidate::Hook for Layout<ACTION> {
    const MODE: candidate::Mode = candidate::Mode::Replace;
    const INSPECT: bool = true;
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn invoke(_: *mut sys::Handle, c: &mut Context<'_>) -> Result<()> {
        let root = c.root(0)?;
        let (id, _) = c.expression(root, Role::Ordering, 0)?;
        let inputs = vec![
            id;
            if ACTION == 21 {
                0
            } else if ACTION == 23 {
                1025
            } else {
                2
            }
        ];
        let outputs = vec![
            id;
            if ACTION == 21 {
                0
            } else if ACTION == 22 {
                2
            } else if ACTION == 24 {
                1025
            } else {
                1
            }
        ];
        let path = candidate::CustomPath {
            input: 0,
            service_id: c"test.layout",
            service_major: 1,
            minimum_minor: 0,
            plan: b"opaque",
            operator_cost: 2.0,
            preserves_order: true,
            blocking: true,
        };
        // Deliberately swallow errors: bad requests/host replies stay sticky.
        if let Ok(index) = c.custom_with_layout(&path, &inputs, &outputs) {
            c.select(index)?;
        }
        Ok(())
    }
}
#[test]
fn explicit_layout_owns_ordered_ids_and_preserves_sticky_failures() {
    fn check<const ACTION: u32>(expected: sys::Status, builds: u32) {
        let mut host = Host {
            mode: ACTION,
            calls: 0,
            error: 0,
            built: 0,
        };
        let c = context(&mut host);
        assert_eq!(
            unsafe {
                candidate::Service::<Layout<ACTION>>::ABI.invoke.unwrap()(ptr::null_mut(), &c.v2.v1)
            },
            expected,
            "action {ACTION}"
        );
        assert_eq!(host.built, builds);
        if (22..=24).contains(&ACTION) {
            assert_eq!(host.calls, 2);
        }
    }
    check::<20>(sys::OK, 1);
    check::<21>(sys::OK, 1);
    check::<22>(sys::INVALID, 0);
    check::<23>(sys::INVALID, 0);
    check::<24>(sys::INVALID, 0);
    check::<30>(sys::FAILED_PRECONDITION, 1);
    check::<31>(sys::FAILED_PRECONDITION, 1);
    check::<32>(sys::FAILED_PRECONDITION, 0);
}
