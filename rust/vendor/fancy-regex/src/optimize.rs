// Copyright 2025 The Fancy Regex Authors.
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

//! Optimization of regex expressions.

use crate::parse::ExprTree;
use crate::Expr;
use crate::LookAround;

use alloc::boxed::Box;
use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;
use core::mem;

/// Rewrite the expression tree to help the VM compile an efficient program.
/// Returns a boolean to say whether the new tree explicitly contains capture group 0.
pub fn optimize(tree: &mut ExprTree) -> bool {
    // self recursion prevents us from moving the trailing lookahead out of group 0
    let requires_capture_group_fixup = if !tree.self_recursive {
        // KeepOut (\K) optimization is tried first; it also handles a trailing positive
        // lookahead when both are present, so we skip the standalone lookahead pass in
        // that case to avoid creating a second explicit capture group 0.
        if optimize_keepout(tree) {
            true
        } else {
            optimize_trailing_lookahead(tree)
        }
    } else {
        false
    };

    let has_backrefs = !tree.backrefs.is_empty();
    optimize_nested_repeats(&mut tree.expr, has_backrefs);
    optimize_ambiguous_concat_repeats(&mut tree.expr);

    requires_capture_group_fixup
}

/// Simplify nested repeat quantifiers (e.g. `(?:x+)+` → `x+`) to help
/// the VM avoid catastrophic backtracking from deeply nested quantifiers.
/// Repeats around capture groups get special consideration not to affect
/// capture semantics. This means that:
/// - if backreferences are used, the optimization is not valid.
/// - if the outer group is optional, the capture group can be skipped entirely,
///   so we need to preserve that behavior.
fn optimize_nested_repeats(expr: &mut Expr, has_backrefs: bool) {
    for child in expr.children_iter_mut() {
        optimize_nested_repeats(child, has_backrefs);
    }

    let replacement = if let Expr::Repeat {
        child,
        lo: outer_lo,
        hi: outer_hi,
        greedy: outer_greedy,
    } = expr
    {
        if let Expr::Repeat {
            child: inner_child,
            lo: inner_lo,
            hi: inner_hi,
            greedy: inner_greedy,
        } = child.as_ref()
        {
            // the outer repeat's child is itself a bare Repeat (no
            // capture group involved). Folding the two quantifiers together is
            // always safe.
            if let Some(result_kind) = can_simplify(
                *outer_lo,
                *outer_hi,
                *outer_greedy,
                *inner_lo,
                *inner_hi,
                *inner_greedy,
            ) {
                let result_kind =
                    downgrade_to_optional(result_kind, has_backrefs, inner_child.as_ref());
                Some(compose_repeat(
                    Box::new(inner_child.as_ref().clone()),
                    result_kind,
                ))
            } else {
                None
            }
        } else if let Expr::Group(group) = child.as_mut() {
            if let Expr::Repeat {
                child: inner_child,
                lo: inner_lo,
                hi: inner_hi,
                greedy: inner_greedy,
            } = group.as_ref()
            {
                if *outer_lo == 0 {
                    // outer_lo == 0 (ZeroOrMore or Optional). When there are no
                    // backreferences and the group's content is self-absorbing (an
                    // unbounded repeat like `x+`), downgrade the outer {0,} (ZeroOrMore)
                    // to ? (Optional). This is safe because (X+)* = (X+)? when X is
                    // self-absorbing, and without backreferences the differing group
                    // participation count is unobservable. The ? form is more efficient
                    // for the VM (single backtrack point instead of a loop).
                    if !has_backrefs && *outer_hi == usize::MAX && is_self_absorbing(group.as_ref())
                    {
                        *outer_hi = 1;
                    }
                    None
                } else {
                    // Here we only optimize when outer_lo is > 0 (i.e. not Optional or ZeroOrMore)
                    // This is because, when the outer repeat is optional, the capture group can be
                    // skipped entirely, correctly yielding an unmatched (None) capture.
                    // If we were to use the simplified form, it would always enter the group,
                    // yielding Some(""). So we must not simplify in that case to preserve
                    // capture semantics.

                    if let Some(result_kind) = can_simplify(
                        *outer_lo,
                        *outer_hi,
                        *outer_greedy,
                        *inner_lo,
                        *inner_hi,
                        *inner_greedy,
                    ) {
                        let result_kind =
                            downgrade_to_optional(result_kind, has_backrefs, inner_child.as_ref());
                        Some(Expr::Group(Arc::new(compose_repeat(
                            Box::new(inner_child.as_ref().clone()),
                            result_kind,
                        ))))
                    } else {
                        None
                    }
                }
            } else {
                None
            }
        } else {
            None
        }
    } else {
        None
    };

    if let Some(replacement) = replacement {
        *expr = replacement;
    }
}

fn optimize_ambiguous_concat_repeats(expr: &mut Expr) {
    for child in expr.children_iter_mut() {
        optimize_ambiguous_concat_repeats(child);
    }

    if let Expr::Concat(children) = expr {
        rewrite_concat_repeat_windows(children);
    }

    let should_rewrite = if let Expr::Repeat {
        child,
        lo,
        hi,
        greedy,
    } = &*expr
    {
        *greedy && *hi == usize::MAX && check_repeated_concat(child.as_ref(), *lo)
    } else {
        false
    };

    if should_rewrite {
        let owned = mem::replace(expr, Expr::Empty);
        if let Expr::Repeat { child, lo, .. } = owned {
            *expr = build_repeated_concat(*child, lo);
        }
    }
}

fn rewrite_concat_repeat_windows(children: &mut Vec<Expr>) {
    let mut ix = 0;
    while ix + 2 < children.len() {
        if check_concat_repeat_triplet(&children[ix], &children[ix + 1], &children[ix + 2]) {
            let mut left = Expr::Empty;
            let mut middle = Expr::Empty;
            let mut right = Expr::Empty;
            mem::swap(&mut left, &mut children[ix]);
            mem::swap(&mut middle, &mut children[ix + 1]);
            mem::swap(&mut right, &mut children[ix + 2]);
            let (prefix, optional_tail) = build_concat_repeat_triplet(left, middle, right);
            children.splice(ix..ix + 3, [prefix, optional_tail]);
            ix += 2;
        } else {
            ix += 1;
        }
    }
}

fn check_concat_repeat_triplet(left: &Expr, middle: &Expr, right: &Expr) -> bool {
    let Expr::Repeat {
        child: left_inner,
        lo: left_lo,
        hi: left_hi,
        greedy: left_greedy,
    } = left
    else {
        return false;
    };
    let Expr::Repeat {
        child: right_inner,
        lo: right_lo,
        hi: right_hi,
        greedy: right_greedy,
    } = right
    else {
        return false;
    };
    let Expr::Repeat {
        lo: middle_lo,
        hi: middle_hi,
        ..
    } = middle
    else {
        return false;
    };

    is_unbounded_simple_repeat(*left_lo, *left_hi, *left_greedy)
        && is_unbounded_simple_repeat(*right_lo, *right_hi, *right_greedy)
        && compatible_edge_repeat_bounds(*left_lo, *right_lo)
        && *middle_lo == 0
        && *middle_hi != 0
        && left_inner.as_ref() == right_inner.as_ref()
}

fn build_concat_repeat_triplet(left: Expr, middle: Expr, right: Expr) -> (Expr, Expr) {
    let Expr::Repeat {
        child: left_inner,
        lo: left_lo,
        ..
    } = left
    else {
        unreachable!("check_concat_repeat_triplet guarantees left is a Repeat");
    };
    let Expr::Repeat {
        child: middle_inner,
        hi: middle_hi,
        greedy: middle_greedy,
        ..
    } = middle
    else {
        unreachable!("check_concat_repeat_triplet guarantees middle is a Repeat");
    };
    let right_lo = if let Expr::Repeat { lo, .. } = &right {
        *lo
    } else {
        unreachable!("check_concat_repeat_triplet guarantees right is a Repeat");
    };

    let mandatory_middle = Expr::Repeat {
        child: middle_inner,
        lo: 1,
        hi: middle_hi,
        greedy: middle_greedy,
    };

    if left_lo < right_lo {
        // The right repeat is the less permissive (higher lo) side — keep it as a mandatory
        // suffix. Make the left repeat and middle optional as a prefix instead, so that the
        // rewritten expression is no more permissive than the original.
        // E.g. `\w*\.?\w+` → `(?:\w*\.{1})?\w+`
        let left_repeat = Expr::Repeat {
            child: left_inner,
            lo: left_lo,
            hi: usize::MAX,
            greedy: true,
        };
        let head = Expr::Concat(vec![left_repeat, mandatory_middle]);
        let optional_head = Expr::Repeat {
            child: Box::new(head),
            lo: 0,
            hi: 1,
            greedy: true,
        };
        (optional_head, right)
    } else {
        // The left repeat is the less permissive (higher or equal lo) side — keep it as a
        // mandatory prefix. Make the middle and right repeat optional as a suffix.
        // E.g. `\w+\.?\w+` → `\w+(?:\.{1}\w+)?`
        // E.g. `\w+\.?\w*` → `\w+(?:\.{1}\w*)?`
        let prefix = Expr::Repeat {
            child: left_inner,
            lo: left_lo,
            hi: usize::MAX,
            greedy: true,
        };
        let tail = Expr::Concat(vec![mandatory_middle, right]);
        let optional_tail = Expr::Repeat {
            child: Box::new(tail),
            lo: 0,
            hi: 1,
            greedy: true,
        };
        (prefix, optional_tail)
    }
}

fn check_repeated_concat(child: &Expr, outer_lo: usize) -> bool {
    if outer_lo != 0 && outer_lo != 1 {
        return false;
    }
    let Expr::Concat(children) = child else {
        return false;
    };
    let [prefix, optional_tail] = children.as_slice() else {
        return false;
    };
    let Expr::Repeat {
        child: tail_inner,
        lo: tail_lo,
        hi: tail_hi,
        greedy: tail_greedy,
    } = optional_tail
    else {
        return false;
    };
    if !*tail_greedy || *tail_lo != 0 || *tail_hi != 1 {
        return false;
    }
    let Expr::Concat(tail_children) = tail_inner.as_ref() else {
        return false;
    };
    let [_middle_part, right] = tail_children.as_slice() else {
        return false;
    };
    let Expr::Repeat {
        child: prefix_inner,
        lo: prefix_lo,
        hi: prefix_hi,
        greedy: prefix_greedy,
    } = prefix
    else {
        return false;
    };
    let Expr::Repeat {
        child: right_inner,
        lo: right_lo,
        hi: right_hi,
        greedy: right_greedy,
    } = right
    else {
        return false;
    };

    is_unbounded_simple_repeat(*prefix_lo, *prefix_hi, *prefix_greedy)
        && is_unbounded_simple_repeat(*right_lo, *right_hi, *right_greedy)
        && compatible_edge_repeat_bounds(*prefix_lo, *right_lo)
        && prefix_inner.as_ref() == right_inner.as_ref()
}

fn build_repeated_concat(child: Expr, outer_lo: usize) -> Expr {
    let Expr::Concat(mut children) = child else {
        unreachable!("check_repeated_concat guarantees child is a Concat");
    };
    // children = [prefix_repeat, optional_tail]
    let optional_tail = children.pop().unwrap();
    let prefix = children.pop().unwrap();

    let Expr::Repeat {
        child: tail_inner, ..
    } = optional_tail
    else {
        unreachable!("check_repeated_concat guarantees tail is a Repeat");
    };
    let Expr::Concat(mut tail_children) = *tail_inner else {
        unreachable!("check_repeated_concat guarantees tail inner is a Concat");
    };
    // tail_children = [middle_part, right_repeat]
    let right = tail_children.pop().unwrap();
    let middle_part = tail_children.pop().unwrap();

    let repeated_tail = Expr::Repeat {
        child: Box::new(Expr::Concat(vec![middle_part, right])),
        lo: 0,
        hi: usize::MAX,
        greedy: true,
    };
    let core = Expr::Concat(vec![prefix, repeated_tail]);
    match outer_lo {
        1 => core,
        0 => Expr::Repeat {
            child: Box::new(core),
            lo: 0,
            hi: 1,
            greedy: true,
        },
        _ => unreachable!("check_repeated_concat guarantees outer_lo is 0 or 1"),
    }
}

fn is_unbounded_simple_repeat(lo: usize, hi: usize, greedy: bool) -> bool {
    greedy && hi == usize::MAX && matches!(lo, 0 | 1)
}

fn compatible_edge_repeat_bounds(left_lo: usize, right_lo: usize) -> bool {
    matches!((left_lo, right_lo), (0, 0) | (0, 1) | (1, 0) | (1, 1))
}

fn compose_repeat(child: Box<Expr>, result_kind: QuantifierKind) -> Expr {
    let (lo, hi) = result_kind.bounds();
    Expr::Repeat {
        child,
        lo,
        hi,
        greedy: true,
    }
}

/// Checks whether `(expr)*` is equivalent to `(expr)?`, i.e. whether the language
/// of `expr` is closed under concatenation (L(expr)·L(expr) ⊆ L(expr)).
///
/// This is true for unbounded repeats (`X+` and `X*`) and `Empty`, but not for
/// bounded repeats like `X?` (where two iterations can match `X{2}` which a single
/// iteration cannot) or for simple atoms like a `Literal`.
fn is_self_absorbing(expr: &Expr) -> bool {
    match expr {
        Expr::Repeat { hi, .. } => *hi == usize::MAX,
        Expr::Group(inner) => is_self_absorbing(inner.as_ref()),
        Expr::Empty => true,
        _ => false,
    }
}

/// When the pattern has no backreferences, a `ZeroOrMore` result can be downgraded
/// to `Optional` if the inner child is self-absorbing (its language is closed under
/// concatenation). This produces a more efficient VM program because `?` creates a
/// single backtrack point instead of a loop, and the match result is unchanged.
fn downgrade_to_optional(
    result_kind: QuantifierKind,
    has_backrefs: bool,
    inner_child: &Expr,
) -> QuantifierKind {
    if !has_backrefs && result_kind == QuantifierKind::ZeroOrMore && is_self_absorbing(inner_child)
    {
        QuantifierKind::Optional
    } else {
        result_kind
    }
}

fn can_simplify(
    outer_lo: usize,
    outer_hi: usize,
    outer_greedy: bool,
    inner_lo: usize,
    inner_hi: usize,
    inner_greedy: bool,
) -> Option<QuantifierKind> {
    if !outer_greedy || !inner_greedy {
        return None;
    }

    let outer = quantifier_kind(outer_lo, outer_hi)?;
    let inner = quantifier_kind(inner_lo, inner_hi)?;

    match (inner, outer) {
        (QuantifierKind::OneOrMore, QuantifierKind::OneOrMore) => Some(QuantifierKind::OneOrMore),
        (QuantifierKind::ZeroOrMore, QuantifierKind::OneOrMore) => Some(QuantifierKind::ZeroOrMore),
        (QuantifierKind::Optional, QuantifierKind::OneOrMore) => Some(QuantifierKind::ZeroOrMore),
        (QuantifierKind::OneOrMore, QuantifierKind::ZeroOrMore) => Some(QuantifierKind::ZeroOrMore),
        (QuantifierKind::ZeroOrMore, QuantifierKind::ZeroOrMore) => {
            Some(QuantifierKind::ZeroOrMore)
        }
        (QuantifierKind::Optional, QuantifierKind::ZeroOrMore) => Some(QuantifierKind::ZeroOrMore),
        (QuantifierKind::OneOrMore, QuantifierKind::Optional) => Some(QuantifierKind::ZeroOrMore),
        (QuantifierKind::ZeroOrMore, QuantifierKind::Optional) => Some(QuantifierKind::ZeroOrMore),
        (QuantifierKind::Optional, QuantifierKind::Optional) => Some(QuantifierKind::Optional),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum QuantifierKind {
    Optional,
    ZeroOrMore,
    OneOrMore,
}

impl QuantifierKind {
    fn bounds(self) -> (usize, usize) {
        match self {
            QuantifierKind::Optional => (0, 1),
            QuantifierKind::ZeroOrMore => (0, usize::MAX),
            QuantifierKind::OneOrMore => (1, usize::MAX),
        }
    }
}

fn quantifier_kind(lo: usize, hi: usize) -> Option<QuantifierKind> {
    match (lo, hi) {
        (0, 1) => Some(QuantifierKind::Optional),
        (0, usize::MAX) => Some(QuantifierKind::ZeroOrMore),
        (1, usize::MAX) => Some(QuantifierKind::OneOrMore),
        _ => None,
    }
}

fn optimize_keepout(tree: &mut ExprTree) -> bool {
    // Rewrites a root-level `\K` (KeepOut) in a Concat into an explicit capture group 0.
    //
    // `\K` resets the reported match start to the current position, so whatever follows it
    // is the "real" match.  By wrapping the suffix in a capture group we can delegate the
    // whole expression to regex-automata and read the result from group 1.
    //
    // Example: `\w+:\s*"\K[^"]*`  →  `\w+:\s*"([^"]*)`
    //
    // When a trailing positive lookahead is also present the two optimizations are merged
    // into a single new capture group to avoid needing two explicit group-0 wrappers:
    //
    // Example: `\w+:\s*"\K[^"]*(?=")`  →  `\w+:\s*"([^"]*)"`
    //
    // The optimization requires that none of the prefix children (before the \K) contain
    // capture groups.  If they do, the new wrapper group would not be group 1, which
    // breaks the `explicit_capture_group_0` mechanism that reads the match span from group 1.

    let Expr::Concat(ref mut children) = tree.expr else {
        return false;
    };

    // Find the first KeepOut at the root concat level.
    let Some(keepout_pos) = children.iter().position(|e| matches!(e, Expr::KeepOut)) else {
        return false;
    };

    // Safety check: any capture group in the prefix would be numbered before our new
    // wrapper group, so it would become group 1 instead of our wrapper.
    // Note: `has_descendant` checks only strict descendants, not the node itself,
    // so we need the explicit `matches!(child, Expr::Group(_))` check alongside it.
    let prefix_has_capture_groups = children[..keepout_pos].iter().any(|child| {
        matches!(child, Expr::Group(_)) || child.has_descendant(|e| matches!(e, Expr::Group(_)))
    });
    if prefix_has_capture_groups {
        return false;
    }

    // Drain KeepOut and everything after it.
    let mut tail: Vec<Expr> = children.drain(keepout_pos..).collect();
    // Remove the KeepOut itself (first element of `tail`).
    tail.remove(0);

    // Check whether the last remaining child is a trailing positive lookahead.
    // If so, pull its inner expression out so we can keep it outside the new group.
    let lookahead_inner = if let Some(Expr::LookAround(_, LookAround::LookAhead)) = tail.last() {
        let last = tail.pop().expect("just checked last is Some");
        if let Expr::LookAround(inner, LookAround::LookAhead) = last {
            Some(*inner)
        } else {
            unreachable!("already checked it is a lookahead");
        }
    } else {
        None
    };

    // Build the content of the new capture group from the remaining tail.
    let group_content = match tail.len() {
        0 => Expr::Empty,
        1 => tail.remove(0),
        _ => Expr::Concat(tail),
    };
    let group0 = Expr::Group(Arc::new(group_content));

    // Append: Group(suffix) and then the lookahead's inner expression (if any).
    children.push(group0);
    if let Some(la_inner) = lookahead_inner {
        children.push(la_inner);
    }

    tree.total_groups += 1;
    true
}

fn optimize_trailing_lookahead(tree: &mut ExprTree) -> bool {
    // returns a boolean to say whether the optimization was applied.
    // - if it was applied, capture group 0 is no longer implicit, but explicit
    //   if/when the whole expression gets delegated to regex-automata
    // converts i.e. original pattern `a(?=b)` when wrapped in the capture group 0
    // as `(a(?=b))`
    // to `(a)b`

    if let Expr::Concat(ref mut root_concat_children) = tree.expr {
        // we get the last child if it is a positive lookahead
        if let Some(Expr::LookAround(_, LookAround::LookAhead)) = root_concat_children.last() {
            // then pop the lookahead
            let lookahead_expr = root_concat_children
                .pop()
                .expect("lookaround should be popped");
            // take the rest of the children from the original Concat
            let group0_children = mem::take(root_concat_children);

            // extract the inner expression from the lookahead
            if let Expr::LookAround(inner, LookAround::LookAhead) = lookahead_expr {
                let group0 = Expr::Group(Arc::new(Expr::Concat(group0_children)));
                // compose new Concat: [Group0, lookahead inner expr]
                let new_concat = Expr::Concat(vec![group0, *inner]);
                tree.expr = new_concat;
                tree.total_groups += 1;
                return true;
            } else {
                unreachable!("already checked it is a lookahead");
            }
        }
    } else if let Expr::LookAround(ref mut inner, LookAround::LookAhead) = &mut tree.expr {
        let group0 = Expr::Group(Arc::new(Expr::Empty));
        let mut swap = Expr::Empty;
        mem::swap(&mut swap, inner);
        // compose new Concat: [Group0, lookahead inner expr]
        tree.expr = Expr::Concat(vec![group0, swap]);
        tree.total_groups += 1;
        return true;
    }
    false
}

#[cfg(test)]
mod tests {
    use super::optimize;
    use super::vec;
    use super::Arc;
    use super::Box;
    use crate::parse::make_literal;
    use crate::Expr;
    use alloc::string::String;

    fn optimized_pattern(pattern: &str) -> String {
        optimized_pattern_with_flags(pattern, crate::parse_flags::FLAG_UNICODE)
    }

    fn optimized_pattern_with_flags(pattern: &str, flags: u32) -> String {
        let mut tree = Expr::parse_tree_with_flags(pattern, flags).unwrap();
        optimize(&mut tree);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        s
    }

    fn oniguruma_flags() -> u32 {
        crate::parse_flags::FLAG_ONIGURUMA_MODE | crate::parse_flags::FLAG_UNICODE
    }

    #[test]
    fn trailing_positive_lookahead_optimized() {
        let mut tree = Expr::parse_tree("a(?=b)").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        assert_eq!(s, "(a)b");
    }

    #[test]
    fn standalone_positive_lookahead_optimized() {
        let mut tree = Expr::parse_tree("(?=b)").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        assert_eq!(s, "()b");
    }

    #[test]
    fn trailing_positive_lookahead_with_alternative_optimized() {
        let mut tree = Expr::parse_tree("a(?=b|c)").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        assert_eq!(s, "(a)(?:b|c)");
    }

    #[test]
    fn trailing_positive_lookahead_moved_even_if_not_easy() {
        let mut tree = Expr::parse_tree(r"(a)\1(?=c)").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        assert_eq!(
            tree.expr,
            Expr::Concat(vec![
                Expr::Group(Arc::new(Expr::Concat(vec![
                    Expr::Group(Arc::new(make_literal("a"))),
                    Expr::Backref {
                        group: 1,
                        casei: false
                    }
                ]))),
                make_literal("c"),
            ])
        );
    }

    #[test]
    fn trailing_positive_lookahead_left_alone_when_self_recursive() {
        let tree = Expr::parse_tree(r"ab?\g<0>?(?=a|$)").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn trailing_negative_lookahead_left_alone() {
        let tree = Expr::parse_tree(r"a(?!b)").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn trailing_positive_lookbehind_left_alone() {
        let tree = Expr::parse_tree(r"(?<=b)").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn non_trailing_positive_lookahead_left_alone() {
        let tree = Expr::parse_tree(r"a(?=(b))\1").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);

        let tree = Expr::parse_tree(r"(?=(b))\1").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn keepout_optimized() {
        let mut tree = Expr::parse_tree(r"ab\Kcd").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        assert_eq!(s, "ab(cd)");
    }

    #[test]
    fn keepout_at_start_of_concat_optimized() {
        let mut tree = Expr::parse_tree(r"\Kcd").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        assert_eq!(s, "(cd)");
    }

    #[test]
    fn keepout_with_trailing_lookahead_optimized_into_single_group() {
        // \K and trailing positive lookahead should be merged into one capture group,
        // not two separate group-0 wrappers.
        let mut tree = Expr::parse_tree(r#"ab\Kcd(?=e)"#).unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, true);
        let mut s = String::new();
        tree.expr.to_str(&mut s, 0);
        assert_eq!(s, "ab(cd)e");
    }

    #[test]
    fn keepout_left_alone_when_self_recursive() {
        let tree = Expr::parse_tree(r"ab?\g<0>?\Kc").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn keepout_inside_group_left_alone() {
        // \K inside a capturing group (not the root concat) should not be optimized
        let tree = Expr::parse_tree(r"c(a\Kb)d").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn keepout_with_prefix_capture_group_left_alone() {
        // When there's a capture group before \K, applying the optimization would make
        // that group become the first group (group 1) instead of our new wrapper, so
        // we must leave the pattern unchanged.
        let tree = Expr::parse_tree(r"(\w+)\K\s*(\w+)").unwrap();
        let mut optimized_tree = tree.clone();
        let requires_capture_group_fixup = optimize(&mut optimized_tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(&optimized_tree.expr, &tree.expr);
    }

    #[test]
    fn nested_plus_plus_simplified() {
        assert_eq!(optimized_pattern(r"(x+){1,}"), "(x+)");
    }

    #[test]
    fn nested_star_plus_simplified() {
        assert_eq!(optimized_pattern(r"(x*){1,}"), "(x*)");
    }

    #[test]
    fn nested_optional_plus_simplified() {
        assert_eq!(optimized_pattern(r"(x?){1,}"), "(x*)");
    }

    #[test]
    fn nested_optional_optional_not_simplified() {
        // (x?)? — outer is Optional (lo=0), so the capture group can be
        // skipped entirely (→ None). Simplifying to (x?) would always enter
        // the group (→ Some("")), changing capture semantics.
        assert_eq!(optimized_pattern(r"(x?){0,1}"), "(x?)?");
    }

    #[test]
    fn nested_optional_star_not_simplified() {
        // Outer (x*){?} has lo=0, so the capture group can be
        // skipped (→ None). Simplifying would always enter the group.
        assert_eq!(optimized_pattern(r"(x*){0,1}"), "(x*)?");
    }

    #[test]
    fn nested_repeats_in_children_simplified() {
        // (x+){1,} is simplified (outer lo=1, group always participates).
        // (y*){0,} has outer lo=0, so the group can be skipped → None.
        // With no backreferences and a self-absorbing group content (y*),
        // the outer {0,} (*) is downgraded to ? since (y*)* = (y*)? = y*.
        assert_eq!(optimized_pattern(r"(x+){1,}(y*){0,}"), "(x+)(y*)?");
    }

    #[test]
    fn capture_group_preserved_when_nested_repeat_simplified() {
        assert_eq!(optimized_pattern(r"(x+){1,}"), "(x+)");
    }

    #[test]
    fn non_greedy_nested_repeats_left_alone() {
        assert_eq!(optimized_pattern(r"(x+?){1,}"), "(x+?)+");
        assert_eq!(optimized_pattern(r"(x+){0,1}?"), "(x+)??");
    }

    #[test]
    fn non_capturing_group_nested_repeats_still_simplified() {
        // Non-capturing groups are transparent (unwrapped by the parser), so
        // they hit the bare-Repeat branch which has no outer_lo guard.
        // These are safe because there is no capture group to preserve.
        assert_eq!(optimized_pattern(r"(?:x+){1,}"), "x+");
        assert_eq!(optimized_pattern(r"(?:x*){0,}"), "x*");
        assert_eq!(optimized_pattern(r"(?:x+)+"), "x+");
        assert_eq!(optimized_pattern(r"(?:x*)*"), "x*");
        assert_eq!(optimized_pattern(r"(?:x*)+"), "x*");
        assert_eq!(optimized_pattern(r"(?:x+)*"), "x*");
    }

    #[test]
    fn bounded_quantifier_left_alone() {
        let max = usize::MAX;
        assert_eq!(
            super::can_simplify(1, max, true, 1, max, true),
            Some(super::QuantifierKind::OneOrMore)
        );
        assert_eq!(super::can_simplify(2, max, true, 1, max, true), None);
    }

    #[test]
    fn nested_repeat_does_not_affect_capture_group_fixup_return() {
        let mut tree = Expr::parse_tree(r"(x+){1,}").unwrap();
        let requires_capture_group_fixup = optimize(&mut tree);
        assert_eq!(requires_capture_group_fixup, false);
        assert_eq!(optimized_pattern(r"(x+){1,}"), "(x+)");
    }

    #[test]
    fn nested_repeats_from_oniguruma_adjacent_quantifiers_not_simplified() {
        // (x+){1,}{0,} — the outer {0,} has lo=0 so the capture group can be
        // skipped (→ None). Simplifying to (x*) would always enter the group.
        // With no backreferences, the trailing * is downgraded to ? since
        // (x+)* and (x+)? are equivalent (both match zero or more x) and
        // (x+)? is more efficient for the VM (single backtrack point).
        assert_eq!(
            optimized_pattern_with_flags(r"(x+){1,}{0,}", oniguruma_flags()),
            "(x+)?"
        );
    }

    #[test]
    fn backref_prevents_star_to_optional_downgrade() {
        // When there's a backreference, the * → ? downgrade must NOT happen
        // because the group participation count is observable via \1.
        let mut tree = Expr::parse_tree_with_flags(r"(x+){1,}{0,}\1", oniguruma_flags()).unwrap();
        optimize(&mut tree);
        // The inner {1,} is simplified to (x+), but the outer {0,} stays
        // as ZeroOrMore (not downgraded to Optional) because has_backrefs is true.
        assert_eq!(
            tree.expr,
            Expr::Concat(vec![
                Expr::Repeat {
                    child: Box::new(Expr::Group(Arc::new(Expr::Repeat {
                        child: Box::new(make_literal("x")),
                        lo: 1,
                        hi: usize::MAX,
                        greedy: true,
                    }))),
                    lo: 0,
                    hi: usize::MAX,
                    greedy: true,
                },
                Expr::Backref {
                    group: 1,
                    casei: false,
                },
            ])
        );
    }

    #[test]
    fn non_self_absorbing_group_not_downgraded() {
        // (x?){0,} — inner is Optional (lo=0, hi=1), not self-absorbing.
        // Even without backreferences, * is NOT downgraded to ? because
        // (x?)* = x* ≠ (x?)? = x? (they match different strings).
        assert_eq!(optimized_pattern(r"(x?){0,}"), "(x?)*");
    }

    #[test]
    fn ambiguous_concat_repeats_simplified_basic() {
        assert_eq!(optimized_pattern(r"\s*\w?\s*"), r"\s*(?:\w{1}\s*)?");
    }

    #[test]
    fn ambiguous_concat_repeats_simplified_with_bounded_middle() {
        // left=\w* (lo=0) < right=\w+ (lo=1): right is mandatory suffix, left+middle become optional prefix
        assert_eq!(
            optimized_pattern(r"foo\w*\s{0,5}\w+"),
            r"foo(?:\w*\s{1,5})?\w+"
        );
    }

    #[test]
    fn ambiguous_concat_repeats_simplified_with_nongreedy_middle() {
        // left=\w* (lo=0) < right=\w+ (lo=1): same as above with non-greedy middle
        assert_eq!(
            optimized_pattern(r"foo\w*\s{0,5}?\w+"),
            r"foo(?:\w*\s{1,5}?)?\w+"
        );
    }

    #[test]
    fn ambiguous_concat_repeats_simplified_with_plus_and_star() {
        // left=\s+ (lo=1) >= right=\s* (lo=0): left is mandatory prefix, right+middle become optional suffix
        assert_eq!(optimized_pattern(r"\s+\w{0,1}\s*"), r"\s+(?:\w{1}\s*)?");
        assert_eq!(optimized_pattern(r"^\s+\w{0,1}\s*$"), r"^\s+(?:\w{1}\s*)?$");
    }

    #[test]
    fn ambiguous_concat_repeats_inside_plus_simplified() {
        assert_eq!(optimized_pattern(r"(?:\s*\w?\s*)+"), r"\s*(?:\w{1}\s*)*");
    }

    #[test]
    fn ambiguous_concat_repeats_inside_star_simplified() {
        assert_eq!(
            optimized_pattern(r"(?:\s*\w?\s*)*"),
            r"(?:\s*(?:\w{1}\s*)*)?"
        );
    }

    #[test]
    fn ambiguous_concat_repeats_star_optional_plus_uses_suffix() {
        // left=\w* (lo=0) < right=\w+ (lo=1): the more restrictive side becomes the mandatory
        // suffix to avoid making the rewritten expression more permissive than the original.
        // \w*\.?\w+ cannot match an empty string (requires at least one word char from \w+),
        // so the rewritten form must also require at least one word char.
        assert_eq!(optimized_pattern(r"\w*\.?\w+"), r"(?:\w*\.{1})?\w+");
    }

    #[test]
    fn ambiguous_concat_repeats_plus_optional_star_uses_prefix() {
        // left=\w+ (lo=1) >= right=\w* (lo=0): left is the mandatory prefix
        assert_eq!(optimized_pattern(r"\w+\.?\w*"), r"\w+(?:\.{1}\w*)?");
    }
}
