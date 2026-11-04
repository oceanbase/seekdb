use regex_syntax::hir::{Class, ClassUnicode, ClassUnicodeRange, Hir, HirKind};

use crate::optimize::optimize;
use crate::to_hir::{parse_fragment, HirCtx};
use crate::{Expr, LookAround, RegexOptions, Result};

/// A set of bytes, stored as a 256 bits bitmap.
/// Useful if you want to write your own prefilter.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct ByteSet([u64; 4]);

impl ByteSet {
    /// Adds a byte to the set
    pub fn insert(&mut self, byte: u8) {
        self.0[(byte >> 6) as usize] |= 1u64 << (byte & 63);
    }

    /// Whether the set contains any bytes at all
    pub fn is_empty(&self) -> bool {
        self.0 == [0; 4]
    }

    /// Whether the set contains that byte
    #[inline]
    pub fn contains(&self, byte: u8) -> bool {
        self.0[(byte >> 6) as usize] & (1u64 << (byte & 63)) != 0
    }

    /// Adds every byte of `other` to the set
    pub fn union(&mut self, other: &Self) {
        for (a, b) in self.0.iter_mut().zip(&other.0) {
            *a |= *b;
        }
    }

    /// Removes every byte not in `other`
    pub fn intersection(&mut self, other: &Self) {
        for (a, b) in self.0.iter_mut().zip(&other.0) {
            *a &= *b;
        }
    }

    /// Returns whether the current byte set is a subset of the given one
    #[inline]
    pub fn is_subset(&self, other: &Self) -> bool {
        self.0
            .iter()
            .zip(other.0)
            .all(|(self_word, other_word)| self_word & !other_word == 0)
    }

    /// Iterates over the bytes in the set, in ascending order
    pub fn iter(&self) -> impl Iterator<Item = u8> + '_ {
        set_bits(&self.0).map(|i| i as u8)
    }

    /// The underlying bitmap.
    pub fn words(&self) -> &[u64; 4] {
        &self.0
    }

    /// Builds a set from a bitmap returned by [`ByteSet::words`].
    pub const fn from_words(words: [u64; 4]) -> Self {
        Self(words)
    }
}

/// Get an iterator for the bits set, starting from the lowest
fn bits_of(mut bits: u64) -> impl Iterator<Item = usize> {
    core::iter::from_fn(move || {
        (bits != 0).then(|| {
            let bit = bits.trailing_zeros() as usize;
            // clear the lowest set bit so the next call gets the following one
            bits &= bits - 1;
            bit
        })
    })
}

/// Get an iterator for the indices of the bits set, starting from the lowest
fn set_bits(words: &[u64]) -> impl Iterator<Item = usize> + '_ {
    words
        .iter()
        .enumerate()
        .flat_map(|(i, &word)| bits_of(word).map(move |bit| i * 64 + bit))
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum Start {
    /// Every match must start with a byte from the set
    Definite(ByteSet),
    /// The match can be empty but otherwise it starts with a byte from the set
    MaybeEmpty(ByteSet),
    /// Could be anything.
    Bail,
}

impl Start {
    fn concat(starts: impl Iterator<Item = Start>) -> Start {
        let mut set = ByteSet::default();
        for start in starts {
            match start {
                Start::Definite(mut s) => {
                    s.union(&set);
                    return Start::Definite(s);
                }
                Start::MaybeEmpty(s) => set.union(&s),
                Start::Bail => return Start::Bail,
            }
        }
        Start::MaybeEmpty(set)
    }

    fn alt(starts: impl Iterator<Item = Start>) -> Start {
        let mut set = ByteSet::default();
        // None until the first branch, then whether every branch so far was definite
        let mut definite = None;
        for start in starts {
            match start {
                Start::Definite(s) => {
                    set.union(&s);
                    definite.get_or_insert(true);
                }
                Start::MaybeEmpty(s) => {
                    set.union(&s);
                    definite = Some(false);
                }
                Start::Bail => return Start::Bail,
            }
        }
        match definite {
            Some(true) => Start::Definite(set),
            _ => Start::MaybeEmpty(set),
        }
    }

    fn optional(self) -> Start {
        match self {
            Start::Definite(set) | Start::MaybeEmpty(set) => Start::MaybeEmpty(set),
            Start::Bail => Start::Bail,
        }
    }
}

#[inline]
fn first_utf8_byte(c: char) -> u8 {
    let mut buf = [0u8; 4];
    c.encode_utf8(&mut buf).as_bytes()[0]
}

fn char_class(c: char) -> ClassUnicode {
    ClassUnicode::new([ClassUnicodeRange::new(c, c)])
}

/// Inserts the first byte of every char of the class, case folded first if `casei`.
/// Returns false if the folding tables are not available.
fn insert_class(set: &mut ByteSet, mut class: ClassUnicode, casei: bool) -> bool {
    if casei && class.try_case_fold_simple().is_err() {
        return false;
    }
    for range in class.ranges() {
        for c in range.start()..=range.end() {
            set.insert(first_utf8_byte(c));
        }
    }
    true
}

/// Same logic as fancy_start but on regex-syntax HIR instead.
///
/// The HIR comes from a case-sensitive parse and `casei` is applied here.
fn regex_syntax_start(expr: &Hir, casei: bool) -> Start {
    match expr.kind() {
        // 0 len match
        HirKind::Empty | HirKind::Look(_) => Start::MaybeEmpty(ByteSet::default()),
        HirKind::Literal(l) => {
            let Some(&first) = l.0.first() else {
                return Start::MaybeEmpty(ByteSet::default());
            };
            let mut set = ByteSet::default();
            if casei {
                let first_char = core::str::from_utf8(&l.0)
                    .ok()
                    .and_then(|s| s.chars().next());
                let Some(c) = first_char else {
                    return Start::Bail;
                };
                if !insert_class(&mut set, char_class(c), true) {
                    return Start::Bail;
                }
            } else {
                set.insert(first);
            }
            Start::Definite(set)
        }
        HirKind::Class(class) => {
            let mut set = ByteSet::default();
            match class {
                Class::Unicode(cls) => {
                    // Fold first: a non-ASCII char can fold to one with another lead byte,
                    // e.g. `ÿ` (0xC3) and `Ÿ` (0xC5)
                    let folded;
                    let cls = if casei {
                        let mut cls = cls.clone();
                        if cls.try_case_fold_simple().is_err() {
                            return Start::Bail;
                        }
                        folded = cls;
                        &folded
                    } else {
                        cls
                    };
                    let mut ascii = ClassUnicode::empty();
                    for r in cls.ranges() {
                        if r.start().is_ascii() {
                            ascii.push(ClassUnicodeRange::new(r.start(), r.end().min('\x7F')));
                        }
                        if !r.end().is_ascii() {
                            let start = first_utf8_byte(r.start().max('\u{80}'));
                            for b in start..=first_utf8_byte(r.end()) {
                                set.insert(b);
                            }
                        }
                    }
                    insert_class(&mut set, ascii, false);
                }
                Class::Bytes(cls) => {
                    for r in cls.ranges() {
                        for i in r.start()..=r.end() {
                            set.insert(i);
                            if casei && i.is_ascii_alphabetic() {
                                set.insert(i.to_ascii_uppercase());
                                set.insert(i.to_ascii_lowercase());
                            }
                        }
                    }
                }
            }

            if set.is_empty() {
                Start::Bail
            } else {
                Start::Definite(set)
            }
        }
        HirKind::Capture(c) => regex_syntax_start(&c.sub, casei),
        HirKind::Repetition(r) => {
            let child = regex_syntax_start(&r.sub, casei);
            if r.min == 0 {
                child.optional()
            } else {
                child
            }
        }
        HirKind::Concat(c) => Start::concat(c.iter().map(|e| regex_syntax_start(e, casei))),
        HirKind::Alternation(c) => Start::alt(c.iter().map(|e| regex_syntax_start(e, casei))),
    }
}

fn fancy_start(expr: &Expr, ctx: &mut HirCtx) -> Start {
    match expr {
        Expr::Empty | Expr::Assertion(_) | Expr::KeepOut | Expr::ContinueFromPreviousMatchEnd => {
            Start::MaybeEmpty(ByteSet::default())
        }
        Expr::Literal { val, casei } => {
            let Some(first) = val.chars().next() else {
                return Start::MaybeEmpty(ByteSet::default());
            };
            // Outside of UTF-8 haystacks the pattern chars don't map to haystack bytes
            // the way `first_utf8_byte` assumes, so only ASCII is safe
            if !ctx.utf8() && !first.is_ascii() {
                return Start::Bail;
            }
            let mut set = ByteSet::default();
            if !insert_class(&mut set, char_class(first), *casei) {
                return Start::Bail;
            }
            Start::Definite(set)
        }
        Expr::Delegate { inner, casei } => match parse_fragment(inner, ctx) {
            Some(hir) => regex_syntax_start(&hir, *casei),
            None => Start::Bail,
        },

        Expr::Concat(exprs) => Start::concat(exprs.iter().map(|e| fancy_start(e, ctx))),
        Expr::Alt(exprs) => Start::alt(exprs.iter().map(|e| fancy_start(e, ctx))),
        Expr::Group(child) => fancy_start(child, ctx),
        Expr::AtomicGroup(child) => fancy_start(child, ctx),
        Expr::Repeat { child, lo, .. } => {
            let child = fancy_start(child, ctx);
            if *lo == 0 {
                child.optional()
            } else {
                child
            }
        }
        Expr::LookAround(expr, lookaround) => match lookaround {
            LookAround::LookAhead => match fancy_start(expr, ctx) {
                Start::Definite(set) => Start::Definite(set),
                _ => Start::MaybeEmpty(ByteSet::default()),
            },
            _ => Start::MaybeEmpty(ByteSet::default()),
        },
        _ => Start::Bail,
    }
}

pub(crate) fn byte_set_from_expr(expr: &Expr, options: &RegexOptions) -> Option<ByteSet> {
    let mut hir_ctx = HirCtx::from(options);
    match fancy_start(expr, &mut hir_ctx) {
        Start::Definite(set) if !set.is_empty() => Some(set),
        _ => None,
    }
}

/// See [`crate::RegexOptionsBuilder::start_bytes`]
pub(crate) fn start_bytes(pattern: &str, options: &RegexOptions) -> Result<Option<ByteSet>> {
    let mut tree = Expr::parse_tree_with_flags(pattern, options.compute_flags())?;
    // Same rewrite `Regex::new_options` applies before compiling so we look at the tree
    // that actually gets compiled
    optimize(&mut tree);
    Ok(byte_set_from_expr(&tree.expr, options))
}

fn required_byte_set_from_expr(expr: &Expr) -> ByteSet {
    let mut byte_set = ByteSet::default();

    match expr {
        Expr::Literal { val, casei } => {
            for byte in val.bytes() {
                // When `casei == true`, we can't require a byte: `é` can show up as `É` and
                // this function is to detect the required bytes, not "one of those".
                // Non-alphabetic bytes are fine and we can insert those regardless.\
                // We could fold casing and add the shared `0xC3` byte from `é`/`É`
                // but I'm not sure it's worth it.
                if !casei || (byte.is_ascii() && !byte.is_ascii_alphabetic()) {
                    byte_set.insert(byte);
                }
            }
        }
        Expr::LiteralBytes { .. } => {
            // we don't know at this stage whether the compiler will be in Unicode mode or not
            // so there is no way to know whether the bytes specified here will be treated literally
            // vs with utf8 semantics
        }
        Expr::Concat(children) => {
            for child in children {
                byte_set.union(&required_byte_set_from_expr(child));
            }
        }
        Expr::Alt(children) => {
            if let Some((first, rest)) = children.split_first() {
                byte_set = required_byte_set_from_expr(first);
                for child in rest {
                    let other = required_byte_set_from_expr(child);
                    byte_set.intersection(&other);
                }
            }
        }
        Expr::Conditional {
            condition,
            true_branch,
            false_branch,
        } => {
            // If we reach the true branch, this means the condition matched so its bytes
            // should be included in the byteset as well
            byte_set = required_byte_set_from_expr(true_branch);
            byte_set.union(&required_byte_set_from_expr(condition));
            let other = required_byte_set_from_expr(false_branch);
            byte_set.intersection(&other);
        }
        Expr::Group(child) => return required_byte_set_from_expr(child),
        Expr::AtomicGroup(child)
        | Expr::LookAround(child, LookAround::LookAhead | LookAround::LookBehind)
        | Expr::Repeat { child, lo: 1.., .. } => return required_byte_set_from_expr(child),
        Expr::Empty
        | Expr::Assertion(_)
        | Expr::DefineGroup { .. }
        | Expr::KeepOut
        | Expr::ContinueFromPreviousMatchEnd
        | Expr::BackrefExistsCondition { .. }
        | Expr::BacktrackingControlVerb(_)
        | Expr::Any { .. }
        | Expr::GeneralNewline { .. }
        | Expr::Delegate { .. }
        | Expr::Repeat { .. }
        | Expr::LookAround(..)
        | Expr::Absent(_)
        | Expr::Backref { .. }
        | Expr::BackrefWithRelativeRecursionLevel { .. }
        | Expr::SubroutineCall(_)
        | Expr::AstNode(..) => {}
    }

    byte_set
}

/// See [`crate::RegexOptionsBuilder::required_bytes`]
pub(crate) fn required_bytes(pattern: &str, options: &RegexOptions) -> Result<ByteSet> {
    let mut tree = Expr::parse_tree_with_flags(pattern, options.compute_flags())?;
    optimize(&mut tree);
    Ok(required_byte_set_from_expr(&tree.expr))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{RegexInput, RegexOptionsBuilder};
    use alloc::borrow::ToOwned;
    use alloc::vec;
    use alloc::vec::Vec;

    fn get_byte_set_from_pattern(pattern: &str, options: &RegexOptions) -> Option<ByteSet> {
        start_bytes(pattern, options).ok().flatten()
    }

    fn get_required_byte_set_from_pattern(pattern: &str) -> ByteSet {
        let mut byte_set = ByteSet::default();
        for b in pattern.bytes() {
            byte_set.insert(b);
        }
        byte_set
    }

    fn first_bytes_of(pattern: &str) -> Option<Vec<u8>> {
        let set = get_byte_set_from_pattern(pattern, &RegexOptions::default())?;
        Some(set.iter().collect())
    }

    #[test]
    fn can_extract_ascii_byteset_from_pattern() {
        let inputs = vec![
            (r"fn", Some(vec![b'f'])),
            (r"::", Some(vec![b':'])),
            (r"=>", Some(vec![b'='])),
            (r"\{", Some(vec![b'{'])),
            (r"fn|let|impl", Some(vec![b'f', b'i', b'l'])),
            (r"(fn|let|impl)", Some(vec![b'f', b'i', b'l'])),
            (r"(?:->|<-)", Some(vec![b'-', b'<'])),
            (r"\bfn\b", Some(vec![b'f'])),
            (r"^let", Some(vec![b'l'])),
            (r"\Gx", Some(vec![b'x'])),
            (r"(?<!\.)let", Some(vec![b'l'])),
            (r"[abc]x", Some(vec![b'a', b'b', b'c'])),
            (r"[a-d]", Some(vec![b'a', b'b', b'c', b'd'])),
            (r"(?i)fn", Some(vec![b'F', b'f'])),
            (r"(?i)[a-b]", Some(vec![b'A', b'B', b'a', b'b'])),
            (r"&(?![&=])", Some(vec![b'&'])),
            (r"(?x) fn # comment", Some(vec![b'f'])),
            (r"(?=\[)", Some(vec![b'['])),
            (r"(?=.*)x", Some(vec![b'x'])),
            (r"(?=a?)x", Some(vec![b'x'])),
            (r"(?!x)a", Some(vec![b'a'])),
            (r#"(?=["'`])""#, Some(vec![b'"', b'\'', b'`'])),
            (r#"(?=ab|xy)"#, Some(vec![b'a', b'x'])),
            (r#"r?""#, Some(vec![b'"', b'r'])),
            (r"a+", Some(vec![b'a'])),
            (r"(?>fn)", Some(vec![b'f'])),
            (r"\Kfn", Some(vec![b'f'])),
            (r"über", Some(vec![0xC3])),
            // lead bytes are exact for the class range: U+03B1..U+03C9 are 0xCE.. and 0xCF..
            (r"[α-ω]x", Some(vec![0xCE, 0xCF])),
            (r"a?", None),
            (r"(?!x)", None),
            (r".", None),
            (r".*x", None),
            (r"\R", None),
            (r"(", None),
            // Cases generated by Claude for (?i)
            // Unicode simple folding: (?i)s also matches ſ (U+017F, lead 0xC5)
            (r"(?i)sort", Some(vec![b'S', b's', 0xC5])),
            // and (?i)k also matches K (U+212A KELVIN SIGN, lead 0xE2)
            (r"(?i)kind", Some(vec![b'K', b'k', 0xE2])),
            // the closure works from the non-ASCII side too
            (r"(?i)ſ", Some(vec![b'S', b's', 0xC5])),
            // é folds to {é, É}, both with starting with 0xC3
            (r"(?i)é", Some(vec![0xC3])),
            // Same folds through a class
            (r"(?i)[k]", Some(vec![b'K', b'k', 0xE2])),
            (r"(?i)[s-t]x", Some(vec![b'S', b'T', b's', b't', 0xC5])),
            // Non-ASCII ranges fold to chars with other lead bytes, e.g. `ÿ` to `Ÿ` (0xC5)
            // and `å` to `Å` (U+212B ANGSTROM SIGN, 0xE2)
            (r"(?i)[à-ÿ]", Some(vec![0xC3, 0xC5, 0xE2])),
            (r"(?i)[α-ω]", Some(vec![0xC2, 0xCD, 0xCE, 0xCF, 0xE1, 0xE2])),
            // Non-letters don't fold
            (r"(?i)[0-9_]", Some(b"0123456789_".to_vec())),
        ];

        for (input, expected) in inputs {
            assert_eq!(first_bytes_of(input), expected, "pattern {input:?}");
        }
    }

    #[test]
    fn can_extract_byteset_from_unicode() {
        let upper = first_bytes_of(r"\p{upper}!").unwrap();
        assert!(upper.contains(&b'A') && upper.contains(&b'Z'));
        assert!(!upper.contains(&b'a') && !upper.contains(&b'!'));
        // unicode lead byte is present
        assert!(upper.contains(&0xC3));

        let not_upper = first_bytes_of(r"\P{upper}!").unwrap();
        assert!(not_upper.contains(&b'a') && not_upper.contains(&b'z'));
        // ! is not uppercase so it can be the first match
        assert!(!not_upper.contains(&b'A') && not_upper.contains(&b'!'));
        // something not recognised is just nothing for the prefilter
        assert!(first_bytes_of(r"\P{typo}!").is_none());
    }

    #[test]
    fn can_extract_byteset_from_negation() {
        let b1 = first_bytes_of(r"[^a-c]x").unwrap();
        assert!(!b1.contains(&b'a') && !b1.contains(&b'c'));
        assert!(b1.contains(&b'd') && b1.contains(&b' ') && b1.contains(&0xC3));

        let b2 = first_bytes_of(r"(?i)[^a]").unwrap();
        assert!(b2.contains(&b'b') && b2.contains(&b'B') && b2.contains(&0xC3));

        let b3 = first_bytes_of(r"[^[:alpha:]]").unwrap();
        assert!(!b3.contains(&b'a') && !b3.contains(&b'Z'));
        assert!(b3.contains(&b'0') && b3.contains(&b' ') && b3.contains(&0xC3));
    }

    #[test]
    fn non_ascii_only_folds_to_k_and_s() {
        let mut class = ClassUnicode::new([ClassUnicodeRange::new('\u{80}', '\u{10FFFF}')]);
        class.try_case_fold_simple().unwrap();
        let ascii: Vec<char> = class
            .ranges()
            .iter()
            .flat_map(|r| r.start()..=r.end())
            .filter(char::is_ascii)
            .collect();
        assert_eq!(ascii, vec!['K', 'S', 'k', 's']);
    }

    #[test]
    fn can_extract_required_bytes() {
        let inputs = vec![
            ("a?b", "b"),
            ("ab|ac", "a"),
            ("a|b", ""),
            ("(?=a)b", "ab"),
            ("(?<=a)b", "ab"),
            ("(?!a)b", "b"),
            ("(?i)k!", "!"),
            ("(?(DEFINE)(a))b", "b"),
            ("(a){0}b", "b"),
            ("é!", "é!"),
            ("(?i)é!", "!"),
            ("(?i:É)!", "!"),
            ("(x)?(?(1)b!|c!)", "!"),
            ("(x)(?(1)ab|ac)", "ax"),
            ("(x)(?(1)ab)", "x"),
            ("(?(a)b|ac)", "a"),
            ("(?(a)b|zc)", ""),
            ("(?(a)b|b)", "b"),
        ];

        for (pattern, bytes) in inputs {
            assert_eq!(
                required_bytes(pattern, &RegexOptions::default()).unwrap(),
                get_required_byte_set_from_pattern(bytes),
                "expected byte set for pattern {} doesn't match",
                pattern
            );
        }
    }

    #[test]
    fn required_bytes_match_match_result() {
        let inputs = vec![
            "a?b",
            "ab|ac",
            "a|b",
            "(?=a)a",
            "(?<=a)b",
            "(?!a)b",
            "(?i)k!",
            "(?i)s",
            "(?i:ABC)!",
            r"a\Kb",
            r"(a)\1",
            "(a){0}b",
            "(?x)a # ignored\n b",
            "a*",
            "^a$",
            r"\Ga",
            r"\Aa",
            r"(?<x>a)(?(x)b|c)",
            r"(x)?(?(1)b!|c!)",
            r"(x)(?(1)ab|ac)",
            r"(x)(?(1)ab)",
            "(?i)é!",
            "(?i:É)!",
            "日!",
            r"(?<x>a)\g<x>",
            "é!",
        ];

        for pattern in inputs {
            let re = RegexOptionsBuilder::default()
                .build(pattern.to_owned())
                .unwrap();

            // We're testing that for a given haystack, the haystack contains all the bytes
            // in the byteset
            for text in [
                "", "a", "b", "ab", "ac", "aa", "abc!", "ABC!", "K!", "ſ", "é!", "a\nb", "É!",
                "日!", "x", "xb!", "xc!", "xab", "xac", "b!", "c!",
            ] {
                for pos in (0..=text.len()).filter(|&p| text.is_char_boundary(p)) {
                    let input = RegexInput::new(text).from_pos(pos).anchored(true);
                    if re.find_input(input).unwrap().is_some() {
                        assert!(required_bytes(pattern, &RegexOptions::default())
                            .unwrap()
                            .iter()
                            .all(|byte| get_required_byte_set_from_pattern(text).contains(byte)));
                    }
                }
            }
        }
    }

    #[test]
    fn can_do_set_operations() {
        fn set(bytes: &[u8]) -> ByteSet {
            let mut set = ByteSet::default();
            for &b in bytes {
                set.insert(b);
            }
            set
        }

        let empty = ByteSet::default();
        let small = set(&[0, 70, 140]);
        let big = set(&[0, 70, 140, 200, 255]);
        let other = set(&[70, 140, 210]);

        assert!(empty.is_subset(&small));
        assert!(small.is_subset(&small));
        assert!(small.is_subset(&big));
        assert!(!big.is_subset(&small));
        assert!(!other.is_subset(&big));

        let mut intersection = big;
        intersection.intersection(&other);
        assert_eq!(intersection, set(&[70, 140]));

        let mut union = small;
        union.union(&other);
        assert_eq!(union, set(&[0, 70, 140, 210]));
    }
}
