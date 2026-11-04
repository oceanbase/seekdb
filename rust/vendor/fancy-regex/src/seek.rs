// Copyright 2026 The Fancy Regex Authors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//! Seek pre-filter: build a simplified approximation of a pattern used to
//! skip to plausible match positions before handing off to the backtracking VM.
//!
//! Every quantifier in the approximation is emitted lazily because we only care about
//! the start and being lazy doesn't change the result.

use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;

#[cfg(not(feature = "std"))]
use alloc::collections::BTreeMap as Map;
#[cfg(feature = "std")]
use std::collections::HashMap as Map;

use crate::analyze::Info;
use crate::compile::MAX_SUBROUTINE_RECURSION_DEPTH;
use crate::{write_quantifier, Absent, Assertion, Expr};

/// Returns `true` if the expression tree contains a `StartText` or `EndText` assertion anywhere.
///
/// This is used to decide whether it is safe to inline a group body when approximating a backref:
/// text-anchor assertions in an inlined body would be evaluated at the wrong string position and
/// could cause false negatives (missing valid match positions).
pub(crate) fn expr_contains_positional_anchor(expr: &Expr) -> bool {
    match expr {
        // StartText (^), EndText ($), StartLine ((?m)^), EndLine ((?m)$) are positional anchors
        // that depend on absolute position in the string.  When a group containing such anchors
        // is inlined for a backref approximation, the anchors would be evaluated at the wrong
        // position (the backref position rather than the original capture position), potentially
        // producing false negatives.
        Expr::Assertion(
            Assertion::StartText
            | Assertion::EndText
            | Assertion::EndTextIgnoreTrailingNewlines { .. }
            | Assertion::StartLine { .. }
            | Assertion::StartLineOniguruma { .. }
            | Assertion::EndLine { .. },
        ) => true,
        _ => expr.children_iter().any(expr_contains_positional_anchor),
    }
}

/// Cap on the seek-pattern buffer length. Inlining a backref/subroutine
/// substitutes the referenced group's body, and when that body transitively
/// references the same group (e.g. `(\s+((\3|\4|\5)))?` where `\4`/`\5` point
/// at ancestors of the backref) the substitution branches at every level and,
/// bounded only by the recursion depth, expands the buffer exponentially — so
/// a short pattern can produce a multi-megabyte seek string. Once the buffer
/// is already this large, further inlining stops and a permissive placeholder
/// is emitted instead: the seek pattern only narrows candidate start positions
/// and never decides matches, so a less-selective approximation stays correct.
pub(crate) const MAX_SEEK_PATTERN_LEN: usize = 4096;

/// Emit a lazy permissive size placeholder — `(?s:.)*?`, `(?s:.)+?`, or `(?s:.){N,}?` — into `buf`.
///
/// Used when a hard node cannot be represented in the seek pattern but has a known minimum
/// size.  The placeholder is an over-approximation that avoids false negatives: it admits any
/// run of characters of at least `min_size` length.
pub(crate) fn emit_min_size_placeholder(buf: &mut String, min_size: usize, precedence: u8) {
    if precedence > 2 {
        buf.push_str("(?:");
    }
    buf.push_str("(?s:.)");
    write_quantifier(buf, min_size, usize::MAX, false);
    if precedence > 2 {
        buf.push(')');
    }
}

/// Write the seek-pattern approximation for `info` into `buf`.
///
/// Easy subtrees without capture groups are serialised verbatim via [`Expr::to_str`].
/// Hard nodes are handled as follows:
/// - `Backref` / `SubroutineCall`: inline the referenced group's body (up to
///   `MAX_SUBROUTINE_RECURSION_DEPTH`). For backrefs, positional anchors (`^`, `$`, `\b`) etc.
///   are dropped from the inlined body because backreferences only capture text, not positions.
///   If inlining is not possible, a permissive `(?s:.){min_size,}` placeholder is emitted
///   instead of silently dropping, to maintain correctness.
/// - `LookAround`, `KeepOut`, `ContinueFromPreviousMatchEnd`, `BacktrackingControlVerb`,
///   `BackrefExistsCondition`, `Absent`: dropped (emit nothing — safe over-approximation,
///   their `min_size` is 0).
/// - `AtomicGroup`: keep the contents, discard the atomicity wrapper.
/// - `Group`: keep the contents, discard the capture group wrapper.
/// - `Conditional`: emit the union of the true and false branches.
/// - `GeneralNewline`: replaced with an explicit alternation.
/// - `Assertion`: anchors and word-boundary assertions, half boundaries included, are emitted.
pub(crate) fn build_seek_pattern<'a>(
    info: &Info<'a>,
    group_info_map: &Map<usize, &'a Info<'a>>,
    depth: usize,
    buf: &mut String,
    precedence: u8,
) {
    let mut inlined_groups = Vec::new();
    build_seek_pattern_impl(
        info,
        group_info_map,
        depth,
        buf,
        precedence,
        false,
        &mut inlined_groups,
    );
}

pub(crate) fn build_seek_pattern_impl<'a>(
    info: &Info<'a>,
    group_info_map: &Map<usize, &'a Info<'a>>,
    depth: usize,
    buf: &mut String,
    precedence: u8,
    drop_positional_anchors: bool,
    inlined_groups: &mut Vec<usize>,
) {
    // Drop positional anchors at this node when requested (used when inlining for a backref).
    if drop_positional_anchors {
        if let Expr::Assertion(
            Assertion::StartText
            | Assertion::EndText
            | Assertion::StartLine { .. }
            | Assertion::StartLineOniguruma { .. }
            | Assertion::EndLine { .. },
        ) = info.expr
        {
            return;
        }
        // \Z is an odd one out, although it is a zero-width assertion, for the purposes of seeking
        // it still needs to consume the newlines
        if let Expr::Assertion(Assertion::EndTextIgnoreTrailingNewlines { crlf }) = info.expr {
            if *crlf {
                buf.push_str(r"[\r\n]*");
            } else {
                buf.push_str(r"\n*");
            }
            return;
        }
    }

    let has_capture_groups = info.start_group() != info.end_group();
    if !info.hard && !has_capture_groups {
        // Easy subtree — use to_str directly when no positional-anchor dropping is needed,
        // or when the subtree contains no positional anchors (so dropping is a no-op).
        if !drop_positional_anchors || !expr_contains_positional_anchor(info.expr) {
            info.expr.to_str(buf, precedence);
            return;
        }
        // The easy subtree contains positional anchors that need to be stripped.
        // Fall through to the match below, which recurses via build_seek_pattern_impl.
    }

    match info.expr {
        Expr::Empty | Expr::DefineGroup { .. } => {}
        Expr::Assertion(assertion) => {
            // Emit word boundaries, including half boundaries, as `to_str` writes them.
            // Note: easy positional assertions (StartText, EndText, StartLine, EndLine) are
            // handled by the to_str early return above (drop_positional_anchors=false) or by
            // the early drop at the top of this function (drop_positional_anchors=true).
            // Only hard assertions reach this arm.
            match assertion {
                Assertion::EndTextIgnoreTrailingNewlines { crlf: false } => buf.push_str(r"\n*$"),
                Assertion::EndTextIgnoreTrailingNewlines { crlf: true } => {
                    buf.push_str(r"[\r\n]*$")
                }
                Assertion::WordBoundary
                | Assertion::NotWordBoundary
                | Assertion::LeftWordBoundary
                | Assertion::RightWordBoundary
                | Assertion::LeftWordHalfBoundary
                | Assertion::RightWordHalfBoundary => info.expr.to_str(buf, precedence),
                // Easy positional anchors — handled by early return / early drop above.
                // They can only reach here when drop_positional_anchors=false and the subtree
                // is easy (handled by to_str) but the Group/Concat parent fell through to match.
                // Emit them faithfully.
                Assertion::StartText
                | Assertion::EndText
                | Assertion::StartLine { .. }
                | Assertion::StartLineOniguruma { .. }
                | Assertion::EndLine { .. } => info.expr.to_str(buf, precedence),
            }
        }
        Expr::Concat(_) => {
            if precedence > 1 {
                buf.push_str("(?:");
            }
            for child in &info.children {
                build_seek_pattern_impl(
                    child,
                    group_info_map,
                    depth,
                    buf,
                    2,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
            if precedence > 1 {
                buf.push(')');
            }
        }
        Expr::Alt(_) => {
            if precedence > 0 {
                buf.push_str("(?:");
            }
            let mut first = true;
            for child in &info.children {
                if !first {
                    buf.push('|');
                }
                build_seek_pattern_impl(
                    child,
                    group_info_map,
                    depth,
                    buf,
                    1,
                    drop_positional_anchors,
                    inlined_groups,
                );
                first = false;
            }
            if precedence > 0 {
                buf.push(')');
            }
        }
        Expr::Group(_) => {
            // Drop the capture group wrapper; keep the contents.
            if !info.children.is_empty() {
                build_seek_pattern_impl(
                    &info.children[0],
                    group_info_map,
                    depth,
                    buf,
                    precedence,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
        }
        Expr::Repeat { lo, hi, .. } => {
            if precedence > 2 {
                buf.push_str("(?:");
            }
            if !info.children.is_empty() {
                build_seek_pattern_impl(
                    &info.children[0],
                    group_info_map,
                    depth,
                    buf,
                    3,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
            write_quantifier(buf, *lo, *hi, false);
            if precedence > 2 {
                buf.push(')');
            }
        }
        Expr::Backref { group, casei }
        | Expr::BackrefWithRelativeRecursionLevel { group, casei, .. } => {
            // Inline the body of the referenced capture group, wrapping with (?i:...) when
            // the backref is case-insensitive so the approximation remains correct.
            //
            // Positional anchors are dropped from the inlined body: a backref matches the
            // captured text, not a position, so anchors from the group definition do not hold
            // at the backref's position.
            //
            // For BackrefWithRelativeRecursionLevel, the relative_level is ignored here:
            // for seeking purposes, the group body is the same regardless of recursion level.
            //
            // If inlining is not possible (depth limit, group not in map, or recursive cycle), emit a permissive
            // placeholder so that no match positions are incorrectly skipped.
            if depth < MAX_SUBROUTINE_RECURSION_DEPTH && buf.len() < MAX_SEEK_PATTERN_LEN {
                if let Some(group_info) = group_info_map.get(group) {
                    if inlined_groups.contains(group) {
                        emit_min_size_placeholder(buf, group_info.min_size, precedence);
                        return;
                    }
                    if !group_info.children.is_empty() {
                        let child = &group_info.children[0];
                        inlined_groups.push(*group);
                        if *casei {
                            let mut inner = String::new();
                            build_seek_pattern_impl(
                                child,
                                group_info_map,
                                depth + 1,
                                &mut inner,
                                // Precedence 0 (alternation) so content inside (?i:...) is unambiguous.
                                0,
                                true,
                                inlined_groups,
                            );
                            inlined_groups.pop();
                            if !inner.is_empty() {
                                buf.push_str("(?i:");
                                buf.push_str(&inner);
                                buf.push(')');
                            }
                        } else {
                            build_seek_pattern_impl(
                                child,
                                group_info_map,
                                depth + 1,
                                buf,
                                precedence,
                                true,
                                inlined_groups,
                            );
                            inlined_groups.pop();
                        }
                        return;
                    }
                    // Empty group (no children): min_size=0, nothing to emit.
                    return;
                }
            }
            // Could not inline (depth limit or group not found): emit a permissive placeholder
            // so we don't falsely skip positions where the backref's target could have matched.
            emit_min_size_placeholder(buf, info.min_size, precedence);
        }
        Expr::SubroutineCall(target_group) => {
            // Inline the body of the target group once, honouring the recursion depth limit.
            if depth < MAX_SUBROUTINE_RECURSION_DEPTH && buf.len() < MAX_SEEK_PATTERN_LEN {
                if let Some(group_info) = group_info_map.get(target_group) {
                    if inlined_groups.contains(target_group) {
                        let placeholder_min = usize::from(group_info.min_size > 0);
                        emit_min_size_placeholder(buf, placeholder_min, precedence);
                        return;
                    }

                    if !group_info.children.is_empty() {
                        inlined_groups.push(*target_group);
                        build_seek_pattern_impl(
                            &group_info.children[0],
                            group_info_map,
                            depth + 1,
                            buf,
                            precedence,
                            drop_positional_anchors,
                            inlined_groups,
                        );
                        inlined_groups.pop();
                        return;
                    }
                    return;
                }
            }
            emit_min_size_placeholder(buf, info.min_size, precedence);
        }
        // LookAround is zero-width — drop it.
        Expr::LookAround(_, _) => {}
        Expr::AtomicGroup(_) => {
            // Keep the contents; atomicity is invisible to the seek approximation.
            if !info.children.is_empty() {
                build_seek_pattern_impl(
                    &info.children[0],
                    group_info_map,
                    depth,
                    buf,
                    precedence,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
        }
        Expr::GeneralNewline { unicode } => {
            // Replace \R with an explicit alternation accepted by regex-automata.
            if *unicode {
                buf.push_str(r"(?:\r\n|[\n\x0B\x0C\r\x85\u{2028}\u{2029}])");
            } else {
                buf.push_str(r"(?:\r\n|[\n\x0B\x0C\r])");
            }
        }
        Expr::Conditional { .. } => {
            // Emit the union of (condition + true_branch) and (false_branch).
            //
            // Including the condition prefix in the true alternative ensures that consuming
            // conditions (e.g. `(?(a)b|c)` where `a` is matched and consumed) are
            // over-approximated correctly.  For zero-width conditions (e.g. backref-exists
            // checks) the condition emits nothing and the result degenerates to
            // `true_branch | false_branch`, preserving the original behaviour.
            let mut cond_pat = String::new();
            let mut true_pat = String::new();
            let mut false_pat = String::new();
            if !info.children.is_empty() {
                build_seek_pattern_impl(
                    &info.children[0],
                    group_info_map,
                    depth,
                    &mut cond_pat,
                    2,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
            if info.children.len() >= 2 {
                build_seek_pattern_impl(
                    &info.children[1],
                    group_info_map,
                    depth,
                    &mut true_pat,
                    2,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
            if info.children.len() >= 3 {
                build_seek_pattern_impl(
                    &info.children[2],
                    group_info_map,
                    depth,
                    &mut false_pat,
                    1,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
            // Build the "condition then true-branch" alternative.
            let cond_true = if cond_pat.is_empty() {
                true_pat.clone()
            } else if true_pat.is_empty() {
                cond_pat.clone()
            } else {
                format!("(?:{}{})", cond_pat, true_pat)
            };
            if cond_true.is_empty() && false_pat.is_empty() {
                return;
            }
            // An empty branch still matches the empty string. Keep it in the union
            // so the seek pattern does not require the other branch to match.
            if precedence > 0 {
                buf.push_str("(?:");
            }
            buf.push_str(&cond_true);
            buf.push('|');
            buf.push_str(&false_pat);
            if precedence > 0 {
                buf.push(')');
            }
        }
        // Absent repeater `(?~expr)` matches any content not containing `expr`.
        // Approximate with `(?s:.*?)` (any characters, including newline) since the repeater
        // can consume an arbitrary number of characters.
        Expr::Absent(Absent::Repeater(_)) => buf.push_str("(?s:.*?)"),
        // Absent expression `(?~|absent|exp)` matches `exp` subject to the absent constraint.
        // Approximate by seeking only for `exp`, dropping the constraint — this is a safe
        // over-approximation since any position where `exp` matches (ignoring the constraint)
        // is a superset of positions where the full expression matches.
        Expr::Absent(crate::Absent::Expression { .. }) => {
            if info.children.len() >= 2 {
                build_seek_pattern_impl(
                    &info.children[1],
                    group_info_map,
                    depth,
                    buf,
                    precedence,
                    drop_positional_anchors,
                    inlined_groups,
                );
            }
        }
        // Zero-width / control nodes — drop them (all have min_size = 0).
        Expr::KeepOut
        | Expr::ContinueFromPreviousMatchEnd
        | Expr::BacktrackingControlVerb(_)
        | Expr::BackrefExistsCondition { .. }
        | Expr::Absent(_) => {}
        // Approximate since we don't want to have to deal with ascii vs unicode handling here
        Expr::LiteralBytes { bytes } => buf.push_str(&format!("(?s:.{{0,{}}}?)", bytes.len())),
        // Easy leaf nodes (Literal, Any, Delegate) are always handled by the easy no-captures
        // early return above and never reach here. Listed explicitly so that adding a new
        // Expr variant produces a compile error until the seek-pattern case is handled.
        Expr::Literal { .. } | Expr::Any { .. } | Expr::Delegate { .. } => {
            info.expr.to_str(buf, precedence)
        }
        // These variants cause a compile error during analysis and are therefore unreachable
        // after a successful `analyze()` call.
        Expr::AstNode(..) => {
            unreachable!("unexpected expr variant after analysis")
        }
    }
}

/// Returns `true` if `pattern` contains at least one character that provides useful filtering
/// (i.e. a literal character, character class, or anchor rather than just wildcards/quantifiers).
///
/// This is a conservative approximation: any pattern containing `[`, `\`, `^`, `$`, or an
/// alphanumeric character is considered useful. The primary goal is to reject fully
/// unconstrained patterns such as `.*`.
pub fn seek_pattern_is_useful(pattern: &str) -> bool {
    pattern
        .bytes()
        .any(|b| matches!(b, b'[' | b'\\' | b'^' | b'$' | b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9'))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyze::{analyze, AnalyzeContext};
    use crate::compile::populate_group_info_map;
    use crate::optimize;
    use crate::ExprTree;
    use crate::Regex;
    use alloc::string::ToString;

    /// Build the seek pattern for a regex string and return it.
    fn get_seek_pattern(re: &str) -> String {
        let mut tree = Expr::parse_tree(re).unwrap();
        get_seek_pattern_for_tree(&mut tree)
    }

    /// Build the seek pattern for a regex string parsed with the given flags.
    fn get_seek_pattern_with_flags(re: &str, flags: u32) -> String {
        let mut tree = Expr::parse_tree_with_flags(re, flags).unwrap();
        get_seek_pattern_for_tree(&mut tree)
    }

    // Build the seek pattern for the given ExprTree
    fn get_seek_pattern_for_tree(tree: &mut ExprTree) -> String {
        let requires_capture_group_fixup = optimize(tree);

        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: requires_capture_group_fixup,
                ..AnalyzeContext::default()
            },
        )
        .unwrap();

        let mut group_info_map = Map::new();
        populate_group_info_map(&mut group_info_map, &info);
        let mut buf = String::new();
        build_seek_pattern(&info, &group_info_map, 0, &mut buf, 0);
        buf
    }

    #[test]
    fn seek_pattern_conditionals_preserve_empty_alternatives() {
        for (pattern, haystack) in [
            (r"(a)?b(?(1)c)", "b"),
            (r"(a)?b(?(1)c|)", "b"),
            (r"(a)?b(?(1)|c)", "ab"),
        ] {
            let seek_pattern = get_seek_pattern(pattern);
            assert!(
                regex::Regex::new(&seek_pattern).unwrap().is_match(haystack),
                "{pattern:?} produced {seek_pattern:?}, excluding {haystack:?}"
            );
        }
    }

    #[test]
    fn seek_pattern_backref_no_anchor() {
        // Simple backref with no positional anchors: group body is inlined verbatim.
        // The `(?:...)` wrapping comes from Concat precedence handling when stripping groups.
        assert_eq!(get_seek_pattern(r"(abc)\1"), "(?:abc)(?:abc)");
        assert_eq!(
            get_seek_pattern(r"(?i)(abc)\1"),
            "(?:(?i:a)(?i:b)(?i:c))(?i:(?i:a)(?i:b)(?i:c))"
        );
    }

    #[test]
    fn seek_pattern_backref_with_start_anchor() {
        // Backref to a group whose body starts with `^`: the anchor should be dropped from the
        // inlined backref position but kept at the group definition site.
        // `(^a)\1` — seek = `(?:^a)` (group) + `(?:a)` (backref, anchor dropped)
        assert_eq!(get_seek_pattern(r"(^a)\1"), "(?:^a)(?:a)");
    }

    #[test]
    fn seek_pattern_backref_with_start_anchor_variable_length() {
        // The group has a start anchor plus a variable-length content.
        // Anchor is dropped when inlining for the backref.
        assert_eq!(get_seek_pattern(r"(^a+)\1"), "(?:^a+)(?:a+)");
    }

    #[test]
    fn seek_pattern_backref_with_end_anchor() {
        // Backref to a group ending with `$`: the anchor is dropped in the inline position.
        assert_eq!(get_seek_pattern(r"(a$)\1"), "(?:a$)(?:a)");
    }

    #[test]
    fn seek_pattern_backref_only_anchor() {
        // Backref to a group that is only a positional anchor.
        // The group emits `^` (min_size=0); the backref drops `^` and emits nothing.
        assert_eq!(get_seek_pattern(r"(^)\1"), "^");
    }

    #[test]
    fn seek_pattern_lookahead_dropped() {
        // Lookahead is zero-width and is dropped; only the literal is kept.
        assert_eq!(get_seek_pattern(r"(?=foo)bar"), "bar");
    }

    #[test]
    fn seek_pattern_casei_literal_backref_with_anchor() {
        // Group contains a case-insensitive literal and a start anchor.
        // The anchor is dropped during backref inlining; the casei literal emits (?i:...).
        assert_eq!(get_seek_pattern(r"(?i:(^a))\1"), "(?:^(?i:a))(?:(?i:a))");
    }

    #[test]
    fn seek_pattern_easy_expr_preserved() {
        // Easy (non-hard) expressions without capture groups are serialised verbatim via to_str.
        assert_eq!(get_seek_pattern(r"abc"), "abc");
        assert_eq!(get_seek_pattern(r"a|b"), "a|b");
        assert_eq!(get_seek_pattern(r"a+b*c?"), "a+b*c?");
        assert_eq!(get_seek_pattern(r"\ba+b*c?\b"), r"\ba+b*c?\b");
        assert_eq!(
            get_seek_pattern(r"\b{start-half}foo\b{end-half}"),
            r"\b{start-half}foo\b{end-half}"
        );
    }

    #[test]
    fn seek_pattern_easy_expr_with_capture_group_strips_group_wrapper() {
        // Easy expressions with capture groups drop the capture group wrappings.
        assert_eq!(get_seek_pattern(r"(abc)"), "abc");
        assert_eq!(
            get_seek_pattern(r"(abc(def))(?<named>ghi)"),
            "(?:abc(?:def))(?:ghi)"
        );
    }

    #[test]
    fn seek_pattern_optimized_easy_expr_strips_group_wrapper() {
        // Optimized patterns lose the capture group wrappings
        assert_eq!(get_seek_pattern(r"(?=abc)"), "(?:abc)");
        assert_eq!(
            get_seek_pattern(r"(h)(e)(l)(l)(o)\s*(?=world)"),
            r"(?:hello\s*)(?:world)"
        );
    }

    #[test]
    fn seek_pattern_end_text_ignore_trailing_newlines_non_crlf() {
        // \Z (non-CRLF mode) — approximate with `\n*$` so the seek only visits positions
        // near end-of-text.
        assert_eq!(get_seek_pattern(r"abc\Z"), r"abc\n*$");
    }

    #[test]
    fn seek_pattern_end_text_ignore_trailing_newlines_crlf() {
        // \Z in CRLF mode — approximate with `(?:\r?\n)*$`.
        assert_eq!(get_seek_pattern(r"(?R)abc\Z"), r"abc[\r\n]*$");
    }

    #[test]
    fn seek_pattern_end_text_ignore_trailing_newlines_only() {
        // \Z alone (hard assertion) — just the seek pattern with no surrounding literal.
        assert_eq!(get_seek_pattern(r"\Z"), r"\n*$");
    }

    #[test]
    fn seek_pattern_backref_with_end_text_ignore_trailing_newlines() {
        // Backref to a group that ends with \Z: \Z is a positional anchor and should be
        // dropped when inlining the group body for the backref.
        // `(a\Z)\1` — seek = `(?:a\n*$)` (group) + `(?:a\n*)` (backref, \Z anchor dropped, newline matching kept)
        assert_eq!(get_seek_pattern(r"(a\Z)\1"), r"(?:a\n*$)(?:a\n*)");
    }

    #[test]
    fn seek_pattern_subroutine_call_inlined() {
        assert_eq!(
            get_seek_pattern(r"([A-Z][a-z0-9]*)::\g<1>"),
            r"(?:[A-Z][a-z0-9]*)::(?:[A-Z][a-z0-9]*)"
        );
    }

    #[test]
    fn seek_pattern_subroutine_call_inlined_and_anchors_are_preserved() {
        assert_eq!(
            get_seek_pattern(r"((?m:^)[A-Z][a-z0-9]*)\n\g<1>"),
            "(?:(?m:^)[A-Z][a-z0-9]*)\\n(?:(?m:^)[A-Z][a-z0-9]*)"
        );
    }

    #[test]
    fn seek_pattern_self_referential_backref_is_bounded() {
        // Inlining a backref whose target transitively references the backref's
        // own ancestors would expand exponentially with recursion depth if we
        // didn't have cycle detection or a length cap to cause it to fall back
        // to a permissive placeholder instead of continuing to recurse.
        // Together these help to keep the seek pattern small (and, therefore, compilation
        // fast) rather than producing a multi-megabyte string which would likely not help
        // to narrow candidate start positions much better anyway.
        let seek = get_seek_pattern(r"(end)(\s+(function))?(\s+((\3|\4|\5)))?");
        assert!(
            seek.len() <= MAX_SEEK_PATTERN_LEN,
            "seek pattern should stay bounded, got {} bytes",
            seek.len()
        );
        // Compiling it must succeed and stay well-formed.
        Regex::new(r"(end)(\s+(function))?(\s+((\3|\4|\5)))?").unwrap();
    }

    #[test]
    fn seek_recursive_subroutine_with_empty_base_preserves_leftmost_match() {
        let pattern = r"(?<g>(?:a\g<g>)?)b";
        let haystack = "aab";

        let mut options_no_seek = crate::RegexOptionsBuilder::new();
        let expected = options_no_seek
            .seek(false)
            .build(pattern.to_string())
            .unwrap()
            .find(haystack)
            .unwrap();

        let mut options = crate::RegexOptionsBuilder::new();
        options.seek(true);
        let actual = options
            .build(pattern.to_string())
            .unwrap()
            .find(haystack)
            .unwrap();

        assert_eq!(
            expected.map(|m| (m.start(), m.end())),
            actual.map(|m| (m.start(), m.end()))
        );
        assert_eq!(expected.unwrap().start, 0);
    }

    #[test]
    fn seek_pattern_recursive_subroutine_is_inlined_once() {
        let pattern = r"(?<tuple>\((?:[^()]|\g<tuple>)+\))\s*(?!=[=>])(?==)";
        let seek = get_seek_pattern(pattern);
        assert_eq!(
            seek,
            r#"(?:(?:\((?:[^()]|\((?:[^()]|(?s:.)+?)+?\))+?\))\s*)="#
        );
    }

    #[test]
    fn seek_pattern_literal_bytes() {
        // In Unicode mode \xFF parses to a `Literal` (handled by the easy to_str path).
        assert_eq!(get_seek_pattern(r"\xFF"), r"ÿ");
    }

    #[test]
    fn seek_pattern_literal_bytes_ascii_mode() {
        // In non-Unicode mode \xFF parses to `LiteralBytes`, which is hard and so
        // reaches the seek-pattern arm that interpolates the byte length.
        assert_eq!(get_seek_pattern_with_flags(r"\xFF", 0), r"(?s:.{0,1}?)");
        // Two separate byte escapes are a Concat of two LiteralBytes nodes.
        assert_eq!(
            get_seek_pattern_with_flags(r"\xFF\xFF", 0),
            r"(?s:.{0,1}?)(?s:.{0,1}?)"
        );
    }
}
