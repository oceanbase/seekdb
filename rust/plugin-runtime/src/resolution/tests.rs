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

use super::*;

fn text(value: &'static str) -> Text {
    Text {
        data: value.as_ptr(),
        length: value.len() as u32,
    }
}
fn unknown() -> Text {
    Text {
        data: ptr::null(),
        length: 0,
    }
}
fn candidate(id: &'static str, types: &[Text], min: u32, max: u32) -> Candidate {
    Candidate {
        object_id: text(id),
        signature: types.as_ptr(),
        signature_count: types.len() as u32,
        minimum_arity: min,
        maximum_arity: max,
        reserved: 0,
    }
}
fn cast(source: &'static str, target: &'static str, cost: u32, context: u32) -> Cast {
    Cast {
        source: text(source),
        target: text(target),
        context,
        cost,
    }
}
fn choose(candidates: &[Candidate], casts: &[Cast], arguments: &[Text]) -> Result<usize, i32> {
    unsafe { resolve(candidates, casts, arguments, true) }
}

fn choose_cast(casts: &[Cast], source: Text, target: Text, context: u32) -> Result<u32, i32> {
    let mut selected = 123;
    let status = unsafe {
        seekdb_runtime_resolve_cast(
            casts.as_ptr(),
            casts.len() as u32,
            source,
            target,
            context,
            &mut selected,
        )
    };
    if status == OK {
        Ok(selected)
    } else {
        assert_eq!(selected, u32::MAX);
        Err(status)
    }
}

fn common(arguments: &[Text], casts: &[Cast]) -> Result<usize, i32> {
    let mut selected = 17;
    let status = unsafe {
        seekdb_runtime_resolve_common_type(
            arguments.as_ptr(),
            arguments.len() as u32,
            casts.as_ptr(),
            casts.len() as u32,
            &mut selected,
        )
    };
    if status == OK {
        Ok(selected as usize)
    } else {
        assert_eq!(selected, u32::MAX);
        Err(status)
    }
}

#[test]
fn common_type_identity_null_and_direct_implicit_contract() {
    assert_eq!(common(&[], &[]), Err(NOT_FOUND));
    assert_eq!(common(&[unknown(), unknown()], &[]), Err(NOT_FOUND));
    assert_eq!(
        common(&[unknown(), text("a"), text("a"), unknown()], &[]),
        Ok(1)
    );
    for context in 1..=3 {
        let edges = [cast("a", "b", u32::MAX, context)];
        assert_eq!(
            common(&[text("a"), text("b")], &edges),
            if context == IMPLICIT {
                Ok(1)
            } else {
                Err(NOT_FOUND)
            }
        );
    }
    let edges = [cast("a", "b", 0, 3), cast("b", "c", 0, 3)];
    assert_eq!(common(&[text("a"), text("c")], &edges), Err(NOT_FOUND));
    let edges = [cast("a", "c", 0, 3), cast("b", "c", 0, 3)];
    assert_eq!(common(&[text("a"), text("b")], &edges), Err(NOT_FOUND));
}

#[test]
fn common_type_cost_ties_and_duplicate_branches() {
    let mut edges = [
        cast("a", "b", 1, 3),
        cast("b", "a", 2, 3),
        cast("a", "b", 1, 2),
    ];
    assert_eq!(common(&[text("a"), text("b")], &edges), Ok(1));
    assert_eq!(
        common(&[text("a"), text("a"), text("a"), text("b")], &edges),
        Ok(3)
    );
    edges.reverse();
    assert_eq!(common(&[unknown(), text("b"), text("a")], &edges), Ok(1));
    let tied = [cast("a", "b", 1, 3), cast("b", "a", 1, 3)];
    assert_eq!(common(&[text("a"), text("b")], &tied), Err(AMBIGUOUS));
    let tied_cast = [
        cast("a", "b", 1, 3),
        cast("a", "b", 1, 3),
        cast("b", "a", 5, 3),
    ];
    assert_eq!(common(&[text("a"), text("b")], &tied_cast), Err(AMBIGUOUS));
    let cheaper = [
        cast("a", "b", 1, 3),
        cast("a", "b", 1, 3),
        cast("b", "a", 0, 3),
    ];
    assert_eq!(common(&[text("a"), text("b")], &cheaper), Ok(0));
    let self_tie = [cast("a", "a", 0, 3), cast("a", "a", 0, 3)];
    assert_eq!(common(&[text("a"), text("a")], &self_tie), Ok(0));
}

#[test]
fn common_type_validates_unused_metadata_and_ffi_bounds() {
    for arguments in [&[][..], &[unknown()][..], &[text("a")][..]] {
        assert_eq!(
            common(arguments, &[cast("unused", "invalid\0id", 0, 3)]),
            Err(INVALID)
        );
        assert_eq!(
            common(arguments, &[cast("unused", "b", 0, 4)]),
            Err(INVALID)
        );
    }
    assert_eq!(common(&[text("a"), text("INVALID")], &[]), Err(INVALID));
    let mut selected = 1;
    unsafe {
        assert_eq!(
            seekdb_runtime_resolve_common_type(ptr::null(), 1, ptr::null(), 0, &mut selected),
            INVALID
        );
        assert_eq!(selected, u32::MAX);
        selected = 1;
        assert_eq!(
            seekdb_runtime_resolve_common_type(ptr::null(), 1025, ptr::null(), 0, &mut selected),
            INVALID
        );
        assert_eq!(selected, u32::MAX);
        selected = 1;
        assert_eq!(
            seekdb_runtime_resolve_common_type(ptr::null(), 0, ptr::null(), 4097, &mut selected),
            INVALID
        );
        assert_eq!(selected, u32::MAX);
        assert_eq!(
            seekdb_runtime_resolve_common_type(ptr::null(), 0, ptr::null(), 0, ptr::null_mut()),
            INVALID
        );
    }
}

#[test]
fn common_type_matches_exhaustive_three_type_reference() {
    let ids = ["a", "b", "c"];
    let pairs = [(0, 1), (0, 2), (1, 0), (1, 2), (2, 0), (2, 1)];
    for graph in 0..4096usize {
        let mut costs = [[None; 3]; 3];
        let mut edges = Vec::new();
        for (position, (from, to)) in pairs.iter().copied().enumerate() {
            let option = (graph >> (2 * position)) & 3;
            if option != 0 {
                let cost = [0, 0, 1, u32::MAX][option];
                costs[from][to] = Some(cost);
                edges.push(cast(ids[from], ids[to], cost, 3));
            }
        }
        let mut ranking = Vec::new();
        for (target, id) in ids.iter().enumerate() {
            let total: Option<u64> = (0..3)
                .filter(|source| *source != target)
                .map(|source| costs[source][target].map(|cost| 1 + u64::from(cost)))
                .sum();
            if let Some(total) = total {
                ranking.push((total, *id));
            }
        }
        ranking.sort_unstable();
        let expected = if ranking.is_empty() {
            Err(NOT_FOUND)
        } else if ranking.len() > 1 && ranking[0].0 == ranking[1].0 {
            Err(AMBIGUOUS)
        } else {
            Ok(ranking[0].1)
        };
        for order in [
            [0, 1, 2],
            [0, 2, 1],
            [1, 0, 2],
            [1, 2, 0],
            [2, 0, 1],
            [2, 1, 0],
        ] {
            let input = order.map(|i| text(ids[i]));
            assert_eq!(
                common(&input, &edges).map(|index| ids[order[index]]),
                expected,
                "graph={graph}"
            );
            edges.reverse();
        }
    }
}

#[test]
fn common_type_handles_maximum_inputs_and_cost_without_overflow() {
    let names: Vec<_> = (0..MAX_ARGUMENTS)
        .map(|i| format!("type.t{i:04}"))
        .collect();
    let arguments: Vec<_> = names
        .iter()
        .map(|name| Text {
            data: name.as_ptr(),
            length: name.len() as u32,
        })
        .collect();
    let target = *arguments.last().unwrap();
    let mut casts: Vec<_> = arguments[..arguments.len() - 1]
        .iter()
        .map(|source| Cast {
            source: *source,
            target,
            context: IMPLICIT,
            cost: u32::MAX,
        })
        .collect();
    casts.resize_with(MAX_OBJECTS, || Cast {
        source: target,
        target,
        context: IMPLICIT,
        cost: 0,
    });
    assert_eq!(common(&arguments, &casts), Ok(MAX_ARGUMENTS - 1));
}

#[test]
fn direct_cast_context_matrix_and_known_identities() {
    for declared in 1..=3 {
        for requested in 1..=3 {
            let casts = [cast("core.bytes", "plugin.text", u32::MAX, declared)];
            assert_eq!(
                choose_cast(&casts, text("core.bytes"), text("plugin.text"), requested),
                if declared >= requested {
                    Ok(0)
                } else {
                    Err(NOT_FOUND)
                }
            );
        }
    }
    let casts = [
        cast("type.a", "type.b", 0, 3),
        cast("type.b", "type.c", 0, 3),
    ];
    assert_eq!(
        choose_cast(&casts, text("type.a"), text("type.c"), 2),
        Err(NOT_FOUND)
    );
    assert_eq!(
        choose_cast(&[], text("type.a"), text("type.a"), 2),
        Err(NOT_FOUND)
    );
    assert_eq!(
        choose_cast(&casts, unknown(), text("type.b"), 2),
        Err(INVALID)
    );
    assert_eq!(
        choose_cast(&casts, text("type.a"), unknown(), 2),
        Err(INVALID)
    );
}

#[test]
fn direct_cast_cost_and_ambiguity_are_order_independent() {
    let mut casts = [
        cast("a", "b", 10, 2),
        cast("a", "b", 10, 3),
        cast("a", "b", 2, 1),
    ];
    assert_eq!(choose_cast(&casts, text("a"), text("b"), 2), Err(AMBIGUOUS));
    assert_eq!(choose_cast(&casts, text("a"), text("b"), 1), Ok(2));
    casts.reverse();
    assert_eq!(choose_cast(&casts, text("a"), text("b"), 2), Err(AMBIGUOUS));
    assert_eq!(choose_cast(&casts, text("a"), text("b"), 1), Ok(0));
    assert_eq!(choose_cast(&casts, text("a"), text("b"), 3), Ok(1));
}

#[test]
fn direct_cast_rejects_malformed_snapshot_even_after_a_match() {
    for context in [0, 4, u32::MAX] {
        assert_eq!(
            choose_cast(&[], text("a"), text("b"), context),
            Err(INVALID)
        );
        let casts = [cast("a", "b", 0, 3), cast("other", "type", 1, context)];
        assert_eq!(choose_cast(&casts, text("a"), text("b"), 2), Err(INVALID));
    }
    let casts = [cast("a", "b", 0, 3), cast("invalid\0id", "type", 1, 3)];
    assert_eq!(choose_cast(&casts, text("a"), text("b"), 2), Err(INVALID));
    let mut selected = 123;
    for count in [1, MAX_OBJECTS as u32 + 1] {
        assert_eq!(
            unsafe {
                seekdb_runtime_resolve_cast(
                    ptr::null(),
                    count,
                    text("a"),
                    text("b"),
                    2,
                    &mut selected,
                )
            },
            INVALID
        );
        assert_eq!(selected, u32::MAX);
    }
    assert_eq!(
        unsafe {
            seekdb_runtime_resolve_cast(ptr::null(), 0, text("a"), text("b"), 2, ptr::null_mut())
        },
        INVALID
    );
}

#[test]
fn exact_then_cast_then_legacy_independent_of_order() {
    let integer = [text("core.int")];
    let number = [text("core.number")];
    let conversions = [cast("core.int", "core.number", 0, IMPLICIT)];
    let choices = [
        candidate("legacy", &[], 1, 1),
        candidate("number", &number, 1, 1),
        candidate("integer", &integer, 1, 1),
    ];
    assert_eq!(choose(&choices, &conversions, &integer), Ok(2));
    assert_eq!(choose(&choices[..2], &conversions, &integer), Ok(1));
    assert_eq!(choose(&choices[..2], &[], &integer), Ok(0));
    let reordered = [
        candidate("integer", &integer, 1, 1),
        candidate("legacy", &[], 1, 1),
    ];
    assert_eq!(choose(&reordered, &[], &integer), Ok(0));
}

#[test]
fn maximum_cast_cost_is_valid_and_cannot_lose_to_legacy() {
    let arguments = [text("core.int"), text("core.int")];
    let signature = [text("core.number"), text("core.number")];
    let conversions = [cast("core.int", "core.number", u32::MAX, IMPLICIT)];
    let choices = [
        candidate("legacy", &[], 2, 2),
        candidate("typed", &signature, 2, 2),
    ];
    assert_eq!(choose(&choices, &conversions, &arguments), Ok(1));
}

#[test]
fn ambiguity_is_not_resolved_by_registration_order() {
    let input = [text("core.int")];
    let a = [text("type.a")];
    let b = [text("type.b")];
    let conversions = [
        cast("core.int", "type.a", 4, IMPLICIT),
        cast("core.int", "type.b", 4, IMPLICIT),
    ];
    let choices = [candidate("a", &a, 1, 1), candidate("b", &b, 1, 1)];
    assert_eq!(choose(&choices, &conversions, &input), Err(AMBIGUOUS));
    let reverse = [candidate("b", &b, 1, 1), candidate("a", &a, 1, 1)];
    assert_eq!(choose(&reverse, &conversions, &input), Err(AMBIGUOUS));
    // A cheaper candidate encountered later clears an earlier tie.
    let choices = [
        candidate("a", &a, 1, 1),
        candidate("b", &b, 1, 1),
        candidate("exact", &input, 1, 1),
    ];
    assert_eq!(choose(&choices, &conversions, &input), Ok(2));
}

#[test]
fn unknown_probe_is_stable_but_partly_typed_call_can_be_ambiguous() {
    let a = [text("core.int"), text("type.a")];
    let b = [text("core.int"), text("type.b")];
    let choices = [candidate("b", &b, 2, 2), candidate("a", &a, 2, 2)];
    assert_eq!(choose(&choices, &[], &[unknown(), unknown()]), Ok(1));
    assert_eq!(
        choose(&choices, &[], &[text("core.int"), unknown()]),
        Err(AMBIGUOUS)
    );
    let zero = [
        candidate("zero.a", &[], 0, 0),
        candidate("zero.b", &[], 0, 0),
    ];
    assert_eq!(choose(&zero, &[], &[]), Err(AMBIGUOUS));
}

#[test]
fn optional_and_variadic_arity_use_the_normalized_tail_type() {
    let signature = [text("core.int"), text("core.bytes")];
    let choices = [candidate("variadic", &signature, 1, 4)];
    assert_eq!(choose(&choices, &[], &[text("core.int")]), Ok(0));
    assert_eq!(
        choose(
            &choices,
            &[],
            &[text("core.int"), text("core.bytes"), text("core.bytes")]
        ),
        Ok(0)
    );
    assert_eq!(
        choose(
            &choices,
            &[],
            &[text("core.int"), text("core.bytes"), text("core.int")]
        ),
        Err(NOT_FOUND)
    );
    assert_eq!(choose(&choices, &[], &[]), Err(NOT_FOUND));
    assert_eq!(
        choose(&choices, &[], &[text("core.int"); 5]),
        Err(NOT_FOUND)
    );
}

#[test]
fn casts_are_direct_and_implicit_only() {
    let signature = [text("type.c")];
    let choices = [candidate("c", &signature, 1, 1)];
    let input = [text("type.a")];
    for context in [1, 2] {
        assert_eq!(
            choose(&choices, &[cast("type.a", "type.c", 0, context)], &input),
            Err(NOT_FOUND)
        );
    }
    let chain = [
        cast("type.a", "type.b", 0, IMPLICIT),
        cast("type.b", "type.c", 0, IMPLICIT),
    ];
    assert_eq!(choose(&choices, &chain, &input), Err(NOT_FOUND));
    assert_eq!(
        choose(&choices, &[cast("type.a", "type.c", 1, IMPLICIT)], &input),
        Ok(0)
    );
}

#[test]
fn validation_does_not_depend_on_matching_a_candidate() {
    assert_eq!(choose(&[], &[], &[text("INVALID")]), Err(INVALID));
    assert_eq!(choose(&[], &[], &[text("")]), Err(INVALID));
    let signature = [text("core.int")];
    let mut choices = [candidate("f", &signature, 1, 1)];
    choices[0].reserved = 1;
    assert_eq!(choose(&choices, &[], &signature), Err(INVALID));
    assert_eq!(
        choose(&[], &[cast("core.int", "core.number", 0, 99)], &[]),
        Err(INVALID)
    );
}

#[test]
fn ffi_bounds_outputs_and_type_lookup() {
    let choices = [candidate("type.example", &[], 1, 1)];
    let mut output = 7;
    unsafe {
        assert_eq!(
            seekdb_runtime_resolve_sql(
                choices.as_ptr(),
                1,
                ptr::null(),
                0,
                ptr::null(),
                0,
                0,
                &mut output
            ),
            OK
        );
        assert_eq!(output, 0);
        assert_eq!(
            seekdb_runtime_resolve_sql(
                choices.as_ptr(),
                1,
                ptr::null(),
                0,
                ptr::null(),
                0,
                1,
                &mut output
            ),
            NOT_FOUND
        );
        assert_eq!(output, u32::MAX);
        assert_eq!(
            seekdb_runtime_resolve_sql(
                ptr::null(),
                1,
                ptr::null(),
                0,
                ptr::null(),
                0,
                1,
                &mut output
            ),
            INVALID
        );
        assert_eq!(output, u32::MAX);
        assert_eq!(
            seekdb_runtime_resolve_sql(
                ptr::null(),
                0,
                ptr::null(),
                0,
                ptr::null(),
                1025,
                1,
                &mut output
            ),
            INVALID
        );
        assert_eq!(
            seekdb_runtime_resolve_sql(
                choices.as_ptr(),
                4097,
                ptr::null(),
                0,
                ptr::null(),
                0,
                1,
                &mut output
            ),
            INVALID
        );
        assert_eq!(
            seekdb_runtime_resolve_sql(
                ptr::null(),
                0,
                ptr::null(),
                0,
                ptr::null(),
                0,
                1,
                ptr::null_mut()
            ),
            INVALID
        );
    }
}
