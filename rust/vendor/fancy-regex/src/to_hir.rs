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

//! Translation of easy `Expr` subtrees directly into `regex_syntax::hir::Hir`.
//!
//! Delegating a subtree to regex-automata used to mean re-serializing it with
//! [`Expr::to_str`] and letting the engine parse the string all over again —
//! a full AST parse plus Hir translation per delegated engine. Building the
//! `Hir` directly from the `Expr` tree skips that second parse.
//!
//! The translation is defined to produce **exactly** the `Hir` that
//! `regex_syntax` would produce for the `to_str` output (there is an oracle
//! test asserting this equality). Node kinds outside the easy subset that
//! `to_str` handles — and any fragment whose semantics depend on parsing (a
//! `Delegate`'s inner string, a case-insensitive literal) that fails its
//! (fragment-sized) parse — return `None`, and the caller falls back to the
//! string path, preserving both behavior and error reporting.

use alloc::boxed::Box;
use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;
use core::convert::TryFrom;

use regex_syntax::hir::{Capture, Dot, Hir, HirKind, Look, Repetition};

use bit_set::BitSet;

use crate::{push_quoted, Assertion, BytesMode, Expr, RegexOptions};

/// Context threaded through a translation: the syntax options the string path
/// would hand to the engine's parser, plus the capture-group counter (groups
/// are numbered in the order their `(` appears, starting at 1, exactly like
/// the parser numbers the `to_str` output).
pub(crate) struct HirCtx {
    unicode: bool,
    utf8: bool,
    next_group: u32,
    /// Memoize case-insensitive fragments
    fragments: BTreeMap<String, Option<Hir>>,
    /// When true, capture groups inside {0} (zero-repetition) quantifiers are
    /// preserved in the HIR. This is needed for top-level delegation where
    /// regex-syntax's Hir::repetition constructor would otherwise simplify
    /// (x){0,0} to Hir::empty() and silently drop capture group slots, causing
    /// a mismatch between the parser's group numbering and regex-automata's.
    /// When false (the default, used for per-fragment VM delegates), {0}
    /// repetitions are simplified to empty, matching regex-syntax's behavior.
    preserve_zero_rep_captures: bool,
    /// Group indices that are inside {0} (zero-repetition) quantifiers,
    /// populated when preserve_zero_rep_captures is true. These groups never
    /// participate in matching, so their captures should return None even
    /// though they exist in the HIR for group-counting purposes.
    zero_rep_groups: BitSet,
}

impl HirCtx {
    pub(crate) fn new(unicode: bool, utf8: bool) -> Self {
        HirCtx {
            unicode,
            utf8,
            next_group: 1,
            fragments: BTreeMap::new(),
            preserve_zero_rep_captures: false,
            zero_rep_groups: BitSet::new(),
        }
    }

    /// Whether the UTF8 mode is enabled
    pub(crate) fn utf8(&self) -> bool {
        self.utf8
    }

    /// Enable preserving capture group slots for groups inside {0} (zero-
    /// repetition) quantifiers. See `preserve_zero_rep_captures` field.
    pub(crate) fn enable_preserve_zero_rep_captures(&mut self) {
        self.preserve_zero_rep_captures = true;
    }

    /// Returns the set of group indices that are inside {0} (zero-repetition)
    /// quantifiers. Only populated when preserve_zero_rep_captures is enabled.
    pub(crate) fn into_zero_rep_groups(self) -> BitSet {
        self.zero_rep_groups
    }
}

impl From<&RegexOptions> for HirCtx {
    fn from(options: &RegexOptions) -> Self {
        let unicode =
            options.syntaxc.get_unicode() && !matches!(options.bytes_mode, BytesMode::Ascii);
        let utf8 = matches!(options.bytes_mode, BytesMode::Unicode);
        Self::new(unicode, utf8)
    }
}

/// Translate an easy `Expr` subtree to an `Hir`, or `None` when it isn't
/// covered. `None` is always safe: the caller falls back to the string path.
pub(crate) fn expr_to_hir(expr: &Expr, ctx: &mut HirCtx) -> Option<Hir> {
    Some(match *expr {
        Expr::Empty | Expr::DefineGroup { .. } => Hir::empty(),
        Expr::Any { newline, crlf } => {
            let dot = match (newline, crlf, ctx.unicode, ctx.utf8) {
                (true, _, true, _) => Dot::AnyChar,
                (true, _, false, false) => Dot::AnyByte,
                (false, false, true, _) => Dot::AnyCharExceptLF,
                (false, false, false, false) => Dot::AnyByteExceptLF,
                (false, true, true, _) => Dot::AnyCharExceptCRLF,
                (false, true, false, false) => Dot::AnyByteExceptCRLF,
                // A byte dot under a UTF-8-only haystack is rejected by the
                // parser; let the string path produce that error.
                (_, _, false, true) => return None,
            };
            Hir::dot(dot)
        }
        Expr::Literal { ref val, casei } => {
            if !casei {
                Hir::literal(val.as_bytes())
            } else {
                // Case-insensitive literals expand to case-folded classes with
                // semantics owned by regex-syntax (Unicode simple folding vs
                // ASCII folding, error cases in non-Unicode modes). Parse just
                // this literal instead of replicating those rules.
                let mut cooked = String::with_capacity(val.len() + 5);
                cooked.push_str("(?i:");
                push_quoted(&mut cooked, val);
                cooked.push(')');
                parse_fragment(&cooked, ctx)?
            }
        }
        Expr::LiteralBytes { ref bytes, .. } => {
            if ctx.utf8 {
                let s: String = bytes
                    .iter()
                    .map(|&b| char::from_u32(b as u32).unwrap())
                    .collect();
                Hir::literal(s.as_bytes())
            } else {
                Hir::literal(bytes.as_slice())
            }
        }
        Expr::Assertion(assertion) => Hir::look(match assertion {
            Assertion::StartText => Look::Start,
            Assertion::EndText => Look::End,
            Assertion::StartLine { crlf: false }
            | Assertion::StartLineOniguruma { crlf: false } => Look::StartLF,
            Assertion::EndLine { crlf: false } => Look::EndLF,
            Assertion::StartLine { crlf: true } | Assertion::StartLineOniguruma { crlf: true } => {
                Look::StartCRLF
            }
            Assertion::EndLine { crlf: true } => Look::EndCRLF,
            // Word boundaries and half boundaries are const-size and the
            // regex-automata engine supports them, so they can be delegated.
            // They are only hard inside a lookbehind (see
            // `Assertion::is_hard_in_lookaround`); if they reach here from a
            // lookbehind, the caller is about to build a reverse DFA, which
            // can't run them — bail out so the caller falls back to the VM.
            Assertion::WordBoundary => {
                if ctx.unicode {
                    Look::WordUnicode
                } else {
                    Look::WordAscii
                }
            }
            Assertion::NotWordBoundary => {
                if ctx.unicode {
                    Look::WordUnicodeNegate
                } else {
                    Look::WordAsciiNegate
                }
            }
            Assertion::LeftWordBoundary => {
                if ctx.unicode {
                    Look::WordStartUnicode
                } else {
                    Look::WordStartAscii
                }
            }
            Assertion::RightWordBoundary => {
                if ctx.unicode {
                    Look::WordEndUnicode
                } else {
                    Look::WordEndAscii
                }
            }
            Assertion::LeftWordHalfBoundary => {
                if ctx.unicode {
                    Look::WordStartHalfUnicode
                } else {
                    Look::WordStartHalfAscii
                }
            }
            Assertion::RightWordHalfBoundary => {
                if ctx.unicode {
                    Look::WordEndHalfUnicode
                } else {
                    Look::WordEndHalfAscii
                }
            }
            // `\Z` is always hard and never reaches a delegated subtree.
            _ => return None,
        }),
        Expr::Concat(ref children) => {
            let subs = children
                .iter()
                .map(|child| expr_to_hir(child, ctx))
                .collect::<Option<Vec<_>>>()?;
            Hir::concat(subs)
        }
        Expr::Alt(ref children) => {
            let subs = children
                .iter()
                .map(|child| expr_to_hir(child, ctx))
                .collect::<Option<Vec<_>>>()?;
            Hir::alternation(subs)
        }
        Expr::Group(ref child) => {
            let index = ctx.next_group;
            ctx.next_group += 1;
            let sub = expr_to_hir(child, ctx)?;
            Hir::capture(Capture {
                index,
                name: None,
                sub: Box::new(sub),
            })
        }
        Expr::Repeat {
            ref child,
            lo,
            hi,
            greedy,
        } => {
            // When the repetition is {0,0} (zero repetitions), regex-syntax's
            // Hir::repetition constructor simplifies it to Hir::empty(),
            // silently dropping any capture groups inside. When
            // preserve_zero_rep_captures is true (for top-level delegation),
            // we keep those capture slots so group numbering stays consistent.
            // When false (for per-fragment VM delegates), we let the
            // simplification happen as it does for the string path.
            if lo == 0 && hi == 0 && ctx.preserve_zero_rep_captures {
                let sub = expr_to_hir(child, ctx)?;
                return Some(make_zero_rep_empty_with_captures(
                    sub,
                    &mut ctx.zero_rep_groups,
                ));
            }
            let min = u32::try_from(lo).ok()?;
            let max = if hi == usize::MAX {
                None
            } else {
                Some(u32::try_from(hi).ok()?)
            };
            let sub = expr_to_hir(child, ctx)?;
            Hir::repetition(Repetition {
                min,
                max,
                greedy,
                sub: Box::new(sub),
            })
        }
        Expr::Delegate { ref inner, casei } => {
            // Delegate nodes carry raw pattern fragments (character classes
            // and the like); their meaning is defined by the parser.
            if casei {
                let mut cooked = String::with_capacity(inner.len() + 5);
                cooked.push_str("(?i:");
                cooked.push_str(inner);
                cooked.push(')');
                parse_fragment(&cooked, ctx)?
            } else {
                parse_fragment(inner, ctx)?
            }
        }
        _ => return None,
    })
}

/// Recursively replace the sub-expression of every [`Capture`] with
/// [`Hir::empty`] while preserving the capture group structure (index, name).
///
/// This is used for `{0}` (zero-repetition) quantifiers: regex-syntax's
/// [`Hir::repetition`] constructor simplifies `(x){0,0}` to
/// [`Hir::empty`], silently dropping any capture groups inside. By preserving
/// the capture group slots (with empty content), regex-automata allocates the
/// correct number of capture group slots and assigns correct group numbers,
/// keeping the HIR's group numbering consistent with the parser's.
fn make_zero_rep_empty_with_captures(hir: Hir, zero_rep_groups: &mut BitSet) -> Hir {
    match hir.into_kind() {
        HirKind::Capture(capture) => {
            zero_rep_groups.insert(capture.index as usize);
            let sub = make_zero_rep_empty_with_captures(*capture.sub, zero_rep_groups);
            Hir::capture(Capture {
                index: capture.index,
                name: capture.name,
                sub: Box::new(sub),
            })
        }
        HirKind::Concat(subs) => {
            let new_subs: Vec<Hir> = subs
                .into_iter()
                .map(|s| make_zero_rep_empty_with_captures(s, zero_rep_groups))
                .collect();
            Hir::concat(new_subs)
        }
        HirKind::Alternation(subs) => {
            let new_subs: Vec<Hir> = subs
                .into_iter()
                .map(|s| make_zero_rep_empty_with_captures(s, zero_rep_groups))
                .collect();
            Hir::alternation(new_subs)
        }
        HirKind::Repetition(rep) => {
            let sub = make_zero_rep_empty_with_captures(*rep.sub, zero_rep_groups);
            Hir::repetition(Repetition {
                min: rep.min,
                max: rep.max,
                greedy: rep.greedy,
                sub: Box::new(sub),
            })
        }
        _ => Hir::empty(),
    }
}

pub(crate) fn parse_fragment(fragment: &str, ctx: &mut HirCtx) -> Option<Hir> {
    // Only case-insensitive fragments are worth memoizing since folding
    // is the expensive piece
    let memoize = fragment.starts_with("(?i:");
    if memoize {
        if let Some(hir) = ctx.fragments.get(fragment) {
            return hir.clone();
        }
    }
    let hir = regex_syntax::ParserBuilder::new()
        .utf8(ctx.utf8)
        .unicode(ctx.unicode)
        .build()
        .parse(fragment)
        .ok();
    if memoize {
        ctx.fragments.insert(fragment.into(), hir.clone());
    }
    hir
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyze::{analyze, AnalyzeContext};
    use crate::{BytesMode, RegexOptions};
    use alloc::string::ToString;

    /// Patterns covering the easy subset: literals (plain, quoted, casei,
    /// non-ASCII), dots in all newline/CRLF modes, anchors in all line modes,
    /// concat/alt/group nesting, all quantifier shapes, classes and other
    /// delegated fragments, and mixed inline flags.
    const PATTERNS: &[&str] = &[
        "a",
        "abc",
        "a.c",
        ".",
        "(?s).",
        "(?s:.)x",
        "^abc$",
        "(?m)^abc$",
        "(?m:^)foo",
        "(?Rm)^x$",
        "(?i)abc",
        "(?i)aBc(?-i)d",
        "(?i)δΔ",
        "αβγ",
        "a+b*c?",
        "a{2,5}",
        "a{3}",
        "a{2,}?",
        "a+?b??",
        "(a)(b(c))",
        "(?:ab|cd)e",
        "a|b|",
        "(a|)",
        "()",
        "[a-z]+",
        r"\d\w\s",
        "[^a-c]{2,3}",
        r"\p{L}+",
        "(?i)[a-k]x",
        r"\x61\n\t",
        "(a+)(?:b|c)*",
        r"\.\*\+#",
        r"(?i)ſ",
        // Hard patterns must be skipped by the harness (info.hard).
        r"\bword\b",
        r"(a)\1",
        // Word boundaries are easy outside a lookbehind, so they exercise the
        // `expr_to_hir` word-boundary arms in all three byte modes.
        r"\b(?:at)\b",
        r"\B(?:foo)\B",
        r"\b(?:foo|bar)\b",
    ];

    /// The translator must produce exactly the Hir the parser produces for
    /// the `to_str` serialization — or `None` where the parser errors, so the
    /// string path can report the error.
    fn check_oracle(bytes_mode: BytesMode) {
        for pattern in PATTERNS {
            let options = RegexOptions {
                bytes_mode,
                ..RegexOptions::default()
            };
            let Ok(tree) = crate::Expr::parse_tree_with_flags(pattern, options.compute_flags())
            else {
                continue;
            };
            let Ok(info) = analyze(&tree, AnalyzeContext::default()) else {
                continue;
            };
            if info.hard {
                continue;
            }
            let mut cooked = String::new();
            tree.expr.to_str(&mut cooked, 0);
            let unicode =
                options.syntaxc.get_unicode() && !matches!(options.bytes_mode, BytesMode::Ascii);
            let utf8 = matches!(options.bytes_mode, BytesMode::Unicode);
            let expected = regex_syntax::ParserBuilder::new()
                .utf8(utf8)
                .unicode(unicode)
                .build()
                .parse(&cooked);
            let got = expr_to_hir(&tree.expr, &mut HirCtx::new(unicode, utf8));
            match expected {
                Ok(hir) => assert_eq!(
                    Some(hir),
                    got,
                    "Hir mismatch for {:?} (cooked {:?}) in {:?} mode",
                    pattern,
                    cooked,
                    bytes_mode
                ),
                Err(_) => assert_eq!(
                    None, got,
                    "expected fallback for {:?} (cooked {:?}) in {:?} mode: the parser \
                     rejects it, so the translator must not accept it",
                    pattern, cooked, bytes_mode
                ),
            }
        }
    }

    #[test]
    fn oracle_unicode_mode() {
        check_oracle(BytesMode::Unicode);
    }

    #[test]
    fn oracle_unicode_bytes_mode() {
        check_oracle(BytesMode::UnicodeBytes);
    }

    #[test]
    fn oracle_ascii_mode() {
        check_oracle(BytesMode::Ascii);
    }

    #[test]
    fn group_numbering_matches_textual_order() {
        let tree = crate::Expr::parse_tree("(a)((b)(c))").unwrap();
        let hir = expr_to_hir(&tree.expr, &mut HirCtx::new(true, true)).unwrap();
        let cooked = {
            let mut s = String::new();
            tree.expr.to_str(&mut s, 0);
            s
        };
        let expected = regex_syntax::ParserBuilder::new().build().parse(&cooked);
        assert_eq!(expected.unwrap().to_string(), hir.to_string());
    }
}
