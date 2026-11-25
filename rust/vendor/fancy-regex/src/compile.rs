// Copyright 2016 The Fancy Regex Authors.
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

//! Compilation of regexes to VM.

use alloc::boxed::Box;
use alloc::format;
use alloc::string::{String, ToString};
#[cfg(feature = "variable-lookbehinds")]
use alloc::sync::Arc;
#[cfg(feature = "variable-lookbehinds")]
use alloc::vec;
use alloc::vec::Vec;
use regex_automata::meta::Regex as RaRegex;
use regex_automata::meta::{Builder as RaBuilder, Config as RaConfig};
use regex_automata::nfa::thompson::WhichCaptures;
#[cfg(feature = "variable-lookbehinds")]
use regex_automata::util::pool::Pool;

#[cfg(not(feature = "std"))]
use alloc::collections::BTreeMap as Map;
#[cfg(feature = "std")]
use std::collections::HashMap as Map;

use crate::analyze::Info;
use crate::seek::build_seek_pattern;
use crate::to_hir::{expr_to_hir, HirCtx};
#[cfg(feature = "variable-lookbehinds")]
use crate::vm::{CachePoolFn, HardVariableLookbehind, ReverseBackwardsDelegate};
use crate::vm::{CaptureGroupRange, CaseiLiteral, CharClassMatcher, Delegate, Insn, Prog, Seek};
use crate::LookAround::*;
use crate::{
    Absent, BacktrackingControlVerb, BytesMode, CompileError, Error, Expr, LookAround, Result,
};
use regex_syntax::hir::Hir;

/// Maximum recursion depth for subroutine calls (matches Oniguruma's limit)
pub(crate) const MAX_SUBROUTINE_RECURSION_DEPTH: usize = 19;

// I'm thinking it probably doesn't make a lot of sense having this split
// out from Compiler.
struct VMBuilder {
    prog: Vec<Insn>,
    n_saves: usize,
    /// Optional cap on the number of emitted VM instructions. Subroutine calls
    /// are inlined at compile time, so a self- or mutually-recursive pattern can
    /// expand without bound even while the recursion-depth cap is respected;
    /// this bounds the emitted instruction vector instead. `None` disables the
    /// check, preserving the previous behavior.
    max_prog_size: Option<usize>,
}

impl VMBuilder {
    fn new(max_group: usize, max_prog_size: Option<usize>) -> VMBuilder {
        VMBuilder {
            prog: Vec::new(),
            n_saves: max_group * 2,
            max_prog_size,
        }
    }

    fn build(
        self,
        bytes_mode: BytesMode,
        unicode: bool,
        seek_pattern: String,
        max_size: usize,
    ) -> Prog {
        Prog::new(
            self.prog,
            self.n_saves,
            bytes_mode,
            unicode,
            seek_pattern,
            max_size,
        )
    }

    fn newsave(&mut self) -> usize {
        let result = self.n_saves;
        self.n_saves += 1;
        result
    }

    fn pc(&self) -> usize {
        self.prog.len()
    }

    // would "emit" be a better name?
    /// Push an instruction, returning an error if doing so would exceed the
    /// configured `max_prog_size` cap.
    fn add(&mut self, insn: Insn) -> Result<()> {
        if let Some(limit) = self.max_prog_size {
            if self.prog.len() >= limit {
                return Err(Error::CompileError(Box::new(
                    CompileError::PatternTooComplex,
                )));
            }
        }
        self.prog.push(insn);
        Ok(())
    }

    fn set_jmp_target(&mut self, jmp_pc: usize, target: usize) {
        match self.prog[jmp_pc] {
            Insn::Jmp(ref mut next) => *next = target,
            _ => panic!("mutating instruction other than Jmp"),
        }
    }

    fn set_split_target(&mut self, split_pc: usize, target: usize, second: bool) {
        match self.prog[split_pc] {
            Insn::Split(_, ref mut y) if second => *y = target,
            Insn::Split(ref mut x, _) => *x = target,
            _ => panic!("mutating instruction other than Split"),
        }
    }

    fn set_repeat_target(&mut self, repeat_pc: usize, target: usize) {
        match self.prog[repeat_pc] {
            Insn::RepeatGr { ref mut next, .. }
            | Insn::RepeatNg { ref mut next, .. }
            | Insn::RepeatEpsilonGr { ref mut next, .. }
            | Insn::RepeatEpsilonNg { ref mut next, .. } => *next = target,
            _ => panic!("mutating instruction other than Repeat"),
        }
    }
}

struct Compiler<'a> {
    b: VMBuilder,
    options: CompileOptions,
    inside_alternation: bool,
    /// Map from group number to its Info node for subroutine expansion
    group_info_map: Map<usize, &'a Info<'a>>,
    /// Stack tracking currently expanding subroutine calls to detect recursion depth
    subroutine_recursion_stack: Vec<usize>,
    /// Root Info node for handling group 0 subroutine calls
    root_info: &'a Info<'a>,
    /// Delegated engines already built during this compile, keyed by the
    /// delegate pattern string and whether capture slots were compiled in.
    /// Alternations (lookbehind branches especially) often repeat the same
    /// fragment, and building a `meta::Regex` dominates compile time, so
    /// identical fragments share one engine (cloning it is a cheap Arc copy).
    delegate_memo: Map<(String, bool), RaRegex>,
}

#[cfg(feature = "variable-lookbehinds")]
const LOOKBEHIND_ALT_DELEGATE_MIN_BRANCHES: usize = 4;

#[cfg(feature = "variable-lookbehinds")]
fn lookbehind_alt_as_delegate(inner: &Info<'_>) -> bool {
    // Captures inside the alternation are extracted by a forward search that is
    // anchored only at the reverse match's start, so delegate only expressions
    // without captures. Small literal alternatives are cheaper as VM probes.
    !inner.hard
        && inner.start_group() == inner.end_group()
        && (inner.children.len() >= LOOKBEHIND_ALT_DELEGATE_MIN_BRANCHES
            || inner
                .children
                .iter()
                .any(|branch| branch.is_literal_get_casei().is_none()))
}

impl<'a> Compiler<'a> {
    #[inline]
    fn emit_literal_bytes(&mut self, bytes: &[u8]) -> Result<()> {
        if matches!(self.options.bytes_mode, BytesMode::Unicode) {
            let s: String = bytes
                .iter()
                .map(|&b| char::from_u32(b as u32).unwrap())
                .collect();
            self.b.add(Insn::Lit(s))?;
        } else {
            self.b.add(Insn::LitBytes(bytes.to_vec()))?;
        }
        Ok(())
    }

    fn visit(&mut self, info: &Info<'_>, hard: bool) -> Result<()> {
        if !hard && !info.hard {
            // easy case, delegate entire subexpr
            return self.compile_delegate(info);
        }
        match *info.expr {
            Expr::Empty => (),
            Expr::Literal { ref val, casei } => {
                if !casei {
                    self.b.add(Insn::Lit(val.clone()))?;
                } else {
                    self.compile_delegate(info)?;
                }
            }
            Expr::LiteralBytes { ref bytes, .. } => {
                self.emit_literal_bytes(bytes)?;
            }
            Expr::Any { newline: true, .. } => {
                self.b.add(Insn::Any)?;
            }
            Expr::Any {
                newline: false,
                crlf: true,
            } => {
                self.b.add(Insn::AnyNoCRLF)?;
            }
            Expr::Any {
                newline: false,
                crlf: false,
            } => {
                self.b.add(Insn::AnyNoNL)?;
            }
            Expr::GeneralNewline { unicode } => {
                self.compile_general_newline(unicode)?;
            }
            Expr::Concat(_) => {
                self.compile_concat(info, hard)?;
            }
            Expr::Alt(_) => {
                let count = info.children.len();
                self.compile_alt(count, |compiler, i| compiler.visit(&info.children[i], hard))?;
            }
            Expr::Group(_) => {
                let group = info.start_group();
                self.b.add(Insn::SaveCaptureGroupStart(group))?;
                self.visit(&info.children[0], hard)?;
                self.b.add(Insn::Save(group * 2 + 1))?;
            }
            Expr::Repeat { lo, hi, greedy, .. } => {
                self.compile_repeat(info, lo, hi, greedy, hard)?;
            }
            Expr::LookAround(_, la) => {
                self.compile_lookaround(info, la)?;
            }
            Expr::Backref { group, casei } => {
                self.b.add(Insn::Backref {
                    slot: group * 2,
                    casei,
                    // use the pre-computed effective unicode flag (unicode && !Ascii bytes mode)
                    unicode: self.options.unicode,
                })?;
            }
            Expr::BackrefExistsCondition {
                group,
                relative_recursion_level: None,
            } => {
                self.b.add(Insn::BackrefExistsCondition(group))?;
            }
            Expr::BackrefExistsCondition {
                relative_recursion_level: Some(_),
                ..
            } => {
                return Err(Error::CompileError(Box::new(
                    CompileError::FeatureNotYetSupported(
                        "Backref exists condition with relative recursion level".to_string(),
                    ),
                )));
            }
            Expr::BacktrackingControlVerb(BacktrackingControlVerb::Fail) => {
                self.b.add(Insn::Fail)?;
            }
            Expr::BacktrackingControlVerb(_) => {
                return Err(Error::CompileError(Box::new(
                    CompileError::FeatureNotYetSupported(
                        "Backtracking control verbs other than 'fail'".to_string(),
                    ),
                )));
            }
            Expr::AtomicGroup(_) => {
                // TODO optimization: atomic insns are not needed if the
                // child doesn't do any backtracking.
                self.b.add(Insn::BeginAtomic)?;
                self.visit(&info.children[0], false)?;
                self.b.add(Insn::EndAtomic)?;
            }
            Expr::Delegate { .. } => {
                // TODO: might want to have more specialized impls
                self.compile_delegate(info)?;
            }
            Expr::Assertion(assertion) => {
                self.b.add(Insn::Assertion(assertion))?;
            }
            Expr::KeepOut => {
                self.b.add(Insn::Save(0))?;
            }
            Expr::ContinueFromPreviousMatchEnd => {
                self.b.add(Insn::ContinueFromPreviousMatchEnd {
                    at_start: info.start_group() <= 1
                        && info.min_pos_in_group == 0
                        && !self.inside_alternation,
                })?;
            }
            Expr::Conditional { .. } => {
                self.compile_conditional(
                    |compiler| compiler.visit(&info.children[0], hard),
                    |compiler| compiler.visit(&info.children[1], hard),
                    |compiler| compiler.visit(&info.children[2], hard),
                )?;
            }
            Expr::SubroutineCall(target_group) => {
                // Check if we're already expanding this specific group (direct/indirect recursion)
                let recursion_count = self
                    .subroutine_recursion_stack
                    .iter()
                    .filter(|&&g| g == target_group)
                    .count();
                if recursion_count >= MAX_SUBROUTINE_RECURSION_DEPTH {
                    // Hit recursion limit - don't expand further, effectively making this match fail
                    // This matches Oniguruma's behavior of limiting recursion depth
                    self.b.add(Insn::Fail)?;
                    return Ok(());
                }

                // Handle group 0 (whole pattern) specially
                let target_info = if target_group == 0 {
                    Some(self.root_info)
                } else {
                    self.group_info_map.get(&target_group).map(|v| &**v)
                };

                if let Some(target_info) = target_info {
                    // Track that we're expanding this subroutine
                    self.subroutine_recursion_stack.push(target_group);

                    // For group 0, visit the entire root info
                    // For other groups, visit the child of the Group expression
                    if target_group == 0 {
                        self.visit(target_info, hard)?;
                    } else {
                        // Groups should always have at least one child (the group content)
                        // If empty, this is an error in the analysis phase
                        if target_info.children.is_empty() {
                            return Err(Error::CompileError(Box::new(
                                CompileError::UnexpectedGeneralError(format!(
                                    "Subroutine call to empty group {}",
                                    target_group
                                )),
                            )));
                        }
                        self.b.add(Insn::SaveCaptureGroupStart(target_group))?;
                        self.visit(&target_info.children[0], hard)?;
                        self.b.add(Insn::Save(target_group * 2 + 1))?;
                    }

                    // Pop the recursion stack
                    self.subroutine_recursion_stack.pop();
                } else {
                    // The target group doesn't exist (invalid group reference)
                    // This should have been caught by analysis, but be defensive
                    return Err(Error::CompileError(Box::new(
                        CompileError::SubroutineCallTargetNotFound(
                            format!(
                                "Invalid subroutine call to non-existent group {}",
                                target_group
                            ),
                            0,
                        ),
                    )));
                }
            }
            Expr::Absent(Absent::Repeater(_)) => {
                let child_info = &info.children[0];
                if child_info.hard {
                    // Nested absent operators are not yet supported
                    let is_absent = |e: &Expr| matches!(e, Expr::Absent(_));
                    if is_absent(child_info.expr) || child_info.expr.has_descendant(is_absent) {
                        return Err(Error::CompileError(Box::new(
                            CompileError::FeatureNotYetSupported(
                                "Nested absent operators".to_string(),
                            ),
                        )));
                    }
                    // Expand (?~hard) to (?((?!hard))\O|)* using the VM's conditional machinery
                    self.compile_hard_absent_repeater(child_info)?;
                } else {
                    // Compile the child expression as a delegate
                    let delegate = DelegateBuilder::new(&self.options)
                        .push(child_info)
                        .build_delegate(&self.options, &mut self.delegate_memo)?;

                    // Add the Absent instruction
                    self.b.add(Insn::AbsentRepeater(delegate))?;
                }
            }
            Expr::BackrefWithRelativeRecursionLevel { .. } => unreachable!(),
            Expr::Absent(ref absent) => {
                use crate::Absent::*;
                let error_msg = match absent {
                    Repeater(_) => "Absent repeater",
                    Expression { .. } => "Absent expression",
                    Stopper(_) => "Absent stopper",
                    Clear => "Range clear",
                };
                return Err(Error::CompileError(Box::new(
                    CompileError::FeatureNotYetSupported(error_msg.to_string()),
                )));
            }
            Expr::DefineGroup { .. } => {
                // DEFINE groups don't generate any VM instructions themselves.
                // The groups defined inside are available for subroutine calls,
                // but the DEFINE block itself doesn't match anything.
                // Group numbers were already assigned during analysis, and the
                // subroutine calls will inline the appropriate code when invoked.
            }
            Expr::AstNode { .. } => unreachable!("Should have been rejected during analysis"),
        }
        Ok(())
    }

    fn compile_alt<F>(&mut self, count: usize, mut handle_alternative: F) -> Result<()>
    where
        F: FnMut(&mut Compiler, usize) -> Result<()>,
    {
        let was_inside_alternation = self.inside_alternation;
        self.inside_alternation = true;
        let mut jmps = Vec::new();
        let mut last_pc = usize::MAX;
        for i in 0..count {
            let has_next = i != count - 1;
            let pc = self.b.pc();
            if has_next {
                self.b.add(Insn::Split(pc + 1, usize::MAX))?;
            }
            if last_pc != usize::MAX {
                self.b.set_split_target(last_pc, pc, true);
            }
            last_pc = pc;

            handle_alternative(self, i)?;

            if has_next {
                // All except the last branch need to jump over instructions of
                // other branches. The last branch can just continue to the next
                // instruction.
                let pc = self.b.pc();
                jmps.push(pc);
                self.b.add(Insn::Jmp(0))?;
            }
        }
        let next_pc = self.b.pc();
        for jmp_pc in jmps {
            self.b.set_jmp_target(jmp_pc, next_pc);
        }
        self.inside_alternation = was_inside_alternation;
        Ok(())
    }

    fn compile_conditional<C, T, F>(
        &mut self,
        mut condition: C,
        mut truth: T,
        mut false_branch: F,
    ) -> Result<()>
    where
        C: FnMut(&mut Compiler) -> Result<()>,
        T: FnMut(&mut Compiler) -> Result<()>,
        F: FnMut(&mut Compiler) -> Result<()>,
    {
        // here we use atomic group functionality to be able to remove the program counter
        // relating to the split instruction's second position if the conditional succeeds
        // This is to ensure that if the condition succeeds, but the "true" branch from the
        // conditional fails, that it wouldn't jump to the "false" branch.
        self.b.add(Insn::BeginAtomic)?;

        let was_inside_alternation = self.inside_alternation;
        self.inside_alternation = true;

        let split_pc = self.b.pc();
        // add the split instruction - we will update it's second pc later
        self.b.add(Insn::Split(split_pc + 1, usize::MAX))?;

        // add the conditional expression
        condition(self)?;

        // mark it as successful to remove the state we added as a split earlier
        self.b.add(Insn::EndAtomic)?;

        // add the truth branch
        truth(self)?;
        // add an instruction to jump over the false branch - we will update the jump target later
        let jump_over_false_pc = self.b.pc();
        self.b.add(Insn::Jmp(0))?;

        // add the false branch, update the split target
        self.b.set_split_target(split_pc, self.b.pc(), true);
        false_branch(self)?;

        // update the jump target for jumping over the false branch
        self.b.set_jmp_target(jump_over_false_pc, self.b.pc());

        self.inside_alternation = was_inside_alternation;

        Ok(())
    }

    /// Compile a hard absent repeater `(?~inner)` by expanding it to its equivalent
    /// conditional form: `(?((?!inner))\O|)*`
    ///
    /// This is a greedy loop that:
    /// - When `inner` does not match at the current position: consumes one character (`\O`,
    ///   which matches any character including newlines)
    /// - When `inner` matches: consumes nothing and exits the loop
    fn compile_hard_absent_repeater(&mut self, inner: &Info<'_>) -> Result<()> {
        let repeat = self.b.newsave();
        let check = self.b.newsave();
        self.b.add(Insn::Save0(repeat))?;
        let loop_pc = self.b.pc();
        self.b.add(Insn::RepeatEpsilonGr {
            lo: 0,
            next: usize::MAX,
            repeat,
            check,
        })?;

        // Compile the body as: (?((?!inner))\O|)
        // Condition: negative lookahead - succeeds when inner does NOT match
        // Truth branch: consume one character (including newlines)
        // False branch: empty - when inner matches, consume nothing (triggers epsilon exit)
        self.compile_conditional(
            |compiler| compiler.compile_negative_lookaround(inner, LookAheadNeg),
            |compiler| {
                compiler.b.add(Insn::Any)?;
                Ok(())
            },
            |_| Ok(()),
        )?;

        self.b.add(Insn::Jmp(loop_pc))?;
        let next_pc = self.b.pc();
        self.b.set_repeat_target(loop_pc, next_pc);
        Ok(())
    }

    fn compile_concat(&mut self, info: &Info<'_>, hard: bool) -> Result<()> {
        // First: determine a prefix which is constant size and not hard.
        let prefix_end = info
            .children
            .iter()
            .take_while(|c| c.const_size && !c.hard)
            .count();

        // If incoming difficulty is not hard, the suffix after the last
        // hard child can be done with NFA.
        let suffix_len = if !hard {
            info.children[prefix_end..]
                .iter()
                .rev()
                .take_while(|c| !c.hard)
                .count()
        } else {
            // Even for hard, we can delegate a const-sized suffix
            info.children[prefix_end..]
                .iter()
                .rev()
                .take_while(|c| c.const_size && !c.hard)
                .count()
        };
        let suffix_begin = info.children.len() - suffix_len;

        self.compile_delegates(&info.children[..prefix_end])?;

        for child in info.children[prefix_end..suffix_begin].iter() {
            self.visit(child, true)?;
        }

        self.compile_delegates(&info.children[suffix_begin..])
    }

    fn compile_repeat(
        &mut self,
        info: &Info<'_>,
        lo: usize,
        hi: usize,
        greedy: bool,
        hard: bool,
    ) -> Result<()> {
        let child = &info.children[0];
        if lo == 0 && hi == 0 {
            // zero repetition, matches empty string without executing child
            // This can happen with patterns like (abc){0}, which should match but never
            // execute the child expression or its capture groups
            return Ok(());
        }
        if lo == 0 && hi == 1 {
            // e?
            let pc = self.b.pc();
            self.b.add(Insn::Split(pc + 1, pc + 1))?;
            // TODO: do we want to do an epsilon check here? If we do
            // it here and in Alt, we might be able to make a good
            // bound on stack depth
            self.visit(child, hard)?;
            let next_pc = self.b.pc();
            self.b.set_split_target(pc, next_pc, greedy);
            return Ok(());
        }
        let hard = hard | info.hard;
        if hi == usize::MAX && child.min_size == 0 {
            // Use RepeatEpsilon instructions to prevent empty repeat
            let repeat = self.b.newsave();
            let check = self.b.newsave();
            self.b.add(Insn::Save0(repeat))?;
            let pc = self.b.pc();
            if greedy {
                self.b.add(Insn::RepeatEpsilonGr {
                    lo,
                    next: usize::MAX,
                    repeat,
                    check,
                })?;
            } else {
                self.b.add(Insn::RepeatEpsilonNg {
                    lo,
                    next: usize::MAX,
                    repeat,
                    check,
                })?;
            }
            self.visit(child, hard)?;
            self.b.add(Insn::Jmp(pc))?;
            let next_pc = self.b.pc();
            self.b.set_repeat_target(pc, next_pc);
        } else if lo == 0 && hi == usize::MAX {
            // e*
            let pc = self.b.pc();
            self.b.add(Insn::Split(pc + 1, pc + 1))?;
            self.visit(child, hard)?;
            self.b.add(Insn::Jmp(pc))?;
            let next_pc = self.b.pc();
            self.b.set_split_target(pc, next_pc, greedy);
        } else if lo == 1 && hi == usize::MAX {
            // e+
            let pc = self.b.pc();
            self.visit(child, hard)?;
            let next = self.b.pc() + 1;
            let (x, y) = if greedy { (pc, next) } else { (next, pc) };
            self.b.add(Insn::Split(x, y))?;
        } else {
            let repeat = self.b.newsave();
            self.b.add(Insn::Save0(repeat))?;
            let pc = self.b.pc();
            if greedy {
                self.b.add(Insn::RepeatGr {
                    lo,
                    hi,
                    next: usize::MAX,
                    repeat,
                })?;
            } else {
                self.b.add(Insn::RepeatNg {
                    lo,
                    hi,
                    next: usize::MAX,
                    repeat,
                })?;
            }
            self.visit(child, hard)?;
            self.b.add(Insn::Jmp(pc))?;
            let next_pc = self.b.pc();
            self.b.set_repeat_target(pc, next_pc);
        }
        Ok(())
    }

    fn compile_lookaround(&mut self, info: &Info<'_>, la: LookAround) -> Result<()> {
        let inner = &info.children[0];
        match la {
            LookBehind => {
                if let &Info {
                    const_size: false,
                    expr: &Expr::Alt(_),
                    ..
                } = inner
                {
                    // One reverse-DFA delegate over the whole alternation is much cheaper per
                    // attempt than trying every branch as its own const-size lookbehind
                    #[cfg(feature = "variable-lookbehinds")]
                    if lookbehind_alt_as_delegate(inner) {
                        return self.compile_positive_lookaround(inner, la);
                    }
                    // Make const size by transforming `(?<=a|bb)` to `(?<=a)|(?<=bb)`
                    let alternatives = &inner.children;
                    self.compile_alt(alternatives.len(), |compiler, i| {
                        let alternative = &alternatives[i];
                        compiler.compile_positive_lookaround(alternative, la)
                    })
                } else {
                    self.compile_positive_lookaround(inner, la)
                }
            }
            LookBehindNeg => {
                if let &Info {
                    const_size: false,
                    expr: &Expr::Alt(_),
                    ..
                } = inner
                {
                    #[cfg(feature = "variable-lookbehinds")]
                    if lookbehind_alt_as_delegate(inner) {
                        return self.compile_negative_lookaround(inner, la);
                    }
                    // Make const size by transforming `(?<!a|bb)` to `(?<!a)(?<!bb)`
                    let alternatives = &inner.children;
                    for alternative in alternatives {
                        self.compile_negative_lookaround(alternative, la)?;
                    }
                    Ok(())
                } else {
                    self.compile_negative_lookaround(inner, la)
                }
            }
            LookAhead => self.compile_positive_lookaround(inner, la),
            LookAheadNeg => self.compile_negative_lookaround(inner, la),
        }
    }

    fn compile_positive_lookaround(&mut self, inner: &Info<'_>, la: LookAround) -> Result<()> {
        let save = self.b.newsave();
        self.b.add(Insn::Save(save))?;
        self.compile_lookaround_inner(inner, la)?;
        self.b.add(Insn::Restore(save))?;
        Ok(())
    }

    fn compile_negative_lookaround(&mut self, inner: &Info<'_>, la: LookAround) -> Result<()> {
        let pc = self.b.pc();
        self.b.add(Insn::Split(pc + 1, usize::MAX))?;
        self.compile_lookaround_inner(inner, la)?;
        self.b.add(Insn::FailNegativeLookAround)?;
        let next_pc = self.b.pc();
        self.b.set_split_target(pc, next_pc, true);
        Ok(())
    }

    fn compile_lookaround_inner(&mut self, inner: &Info<'_>, la: LookAround) -> Result<()> {
        if la == LookBehind || la == LookBehindNeg {
            if inner.const_size {
                self.b.add(Insn::GoBack(inner.min_size))?;
                self.visit(inner, false)
            } else if !inner.hard {
                #[cfg(feature = "variable-lookbehinds")]
                {
                    let mut delegate_builder = DelegateBuilder::new(&self.options);
                    delegate_builder.push(inner);
                    self.compile_variable_lookbehind(delegate_builder)
                }
                #[cfg(not(feature = "variable-lookbehinds"))]
                {
                    Err(Error::CompileError(Box::new(
                        CompileError::VariableLookBehindRequiresFeature,
                    )))
                }
            } else {
                // If the variable lookbehind is a Concat expression where all children
                // are either easy or are guaranteed to consume 0 characters, then we can
                // compile it as variable lookbehind without additional goback instructions.
                if let Expr::Concat(_) = inner.expr {
                    let can_compile = inner
                        .children
                        .iter()
                        .all(|child| !child.hard || child.const_size);

                    if can_compile {
                        #[cfg(feature = "variable-lookbehinds")]
                        {
                            let mut delegate_nodes = vec![];
                            let mut go_back: usize = 0;
                            for child in inner.children.iter().rev() {
                                if child.hard {
                                    self.compile_variable_lookbehind_from_concat_nodes(
                                        &delegate_nodes,
                                    )?;
                                    delegate_nodes.clear();

                                    go_back += child.min_size;
                                    if go_back > 0 {
                                        self.b.add(Insn::GoBack(go_back))?;
                                    }
                                    self.visit(child, false)?;
                                    go_back = child.min_size;
                                } else {
                                    if go_back > 0 {
                                        self.b.add(Insn::GoBack(go_back))?;
                                        go_back = 0;
                                    }
                                    delegate_nodes.push(child);
                                }
                            }
                            self.compile_variable_lookbehind_from_concat_nodes(&delegate_nodes)?;
                            Ok(())
                        }
                        #[cfg(not(feature = "variable-lookbehinds"))]
                        {
                            Err(Error::CompileError(Box::new(
                                CompileError::VariableLookBehindRequiresFeature,
                            )))
                        }
                    } else {
                        #[cfg(feature = "variable-lookbehinds")]
                        {
                            self.compile_hard_variable_lookbehind(inner, la)
                        }
                        #[cfg(not(feature = "variable-lookbehinds"))]
                        {
                            Err(Error::CompileError(Box::new(
                                CompileError::VariableLookBehindRequiresFeature,
                            )))
                        }
                    }
                } else {
                    #[cfg(feature = "variable-lookbehinds")]
                    {
                        self.compile_hard_variable_lookbehind(inner, la)
                    }
                    #[cfg(not(feature = "variable-lookbehinds"))]
                    {
                        Err(Error::CompileError(Box::new(
                            CompileError::VariableLookBehindRequiresFeature,
                        )))
                    }
                }
            }
        } else {
            self.visit(inner, false)
        }
    }

    #[cfg(feature = "variable-lookbehinds")]
    fn compile_variable_lookbehind_from_concat_nodes(
        &mut self,
        infos: &Vec<&Info<'_>>,
    ) -> Result<()> {
        if infos.is_empty() {
            Ok(())
        } else {
            let mut delegate_builder = DelegateBuilder::new(&self.options);
            for info in infos.iter().rev() {
                delegate_builder.push(info);
            }
            self.compile_variable_lookbehind(delegate_builder)
        }
    }

    #[cfg(feature = "variable-lookbehinds")]
    fn compile_hard_variable_lookbehind(
        &mut self,
        inner: &Info<'_>,
        _la: LookAround,
    ) -> Result<()> {
        let end_save = self.b.newsave();
        self.b.add(Insn::Save(end_save))?;
        let candidate_save = self.b.newsave();
        self.b.add(Insn::Save(candidate_save))?;

        let mut seek_pattern = String::new();
        build_seek_pattern(inner, &self.group_info_map, 0, &mut seek_pattern, 0);

        use regex_automata::hybrid::dfa;
        use regex_automata::nfa::thompson;
        use regex_automata::util::syntax::Config as SyntaxConfig;
        use regex_automata::MatchKind;
        // See `compile_variable_lookbehind` for why the capacity check is skipped by default.
        let mut dfa_config = dfa::Config::new()
            .match_kind(MatchKind::All)
            .unicode_word_boundary(true)
            .skip_cache_capacity_check(self.options.delegate_dfa_skip_cache_capacity_check);
        if let Some(limit) = self.options.delegate_dfa_size_limit {
            dfa_config = dfa_config.cache_capacity(limit);
        }
        let mut thompson_config = thompson::Config::new().reverse(true);
        if let Some(limit) = self.options.delegate_size_limit {
            thompson_config = thompson_config.nfa_size_limit(Some(limit));
        }
        // This builder parses `seek_pattern` itself instead of going through
        // `options_to_rabuilder`, so the caller's Unicode and bytes-mode settings
        // have to be applied to its syntax config too. Without them the reverse
        // DFA would use regex-automata's defaults while the forward
        // capture-extraction engine below uses the configured ones.
        let syntax = SyntaxConfig::new()
            .utf8(matches!(self.options.bytes_mode, BytesMode::Unicode))
            .unicode(self.options.unicode);
        let dfa = match dfa::DFA::builder()
            .configure(dfa_config)
            .thompson(thompson_config)
            .syntax(syntax)
            .build(&seek_pattern)
        {
            Ok(dfa) => Arc::new(dfa),
            Err(e) => {
                return Err(Error::CompileError(Box::new(CompileError::DfaBuildError(
                    seek_pattern,
                    e.to_string(),
                ))));
            }
        };

        let create: CachePoolFn = alloc::boxed::Box::new({
            let dfa = Arc::clone(&dfa);
            move || dfa.create_cache()
        });
        let cache_pool = Pool::new(create);

        let hard_lb_pc = self.b.pc();
        self.b
            .add(Insn::HardVariableLookbehind(HardVariableLookbehind {
                dfa,
                cache_pool,
                pattern: seek_pattern,
                end_pos_slot: end_save,
                candidate_pos_slot: candidate_save,
            }))?;

        self.visit(inner, false)?;
        self.b.add(Insn::ReverseLookbehindPosCheck {
            slot: end_save,
            candidate_pos_slot: candidate_save,
            hard_lb_pc,
        })?;
        self.b.add(Insn::Restore(end_save))?;
        self.b.add(Insn::Restore(candidate_save))?;
        Ok(())
    }

    #[cfg(feature = "variable-lookbehinds")]
    fn compile_variable_lookbehind(&mut self, delegate_builder: DelegateBuilder) -> Result<()> {
        let pattern = &delegate_builder.re;
        let capture_groups = delegate_builder
            .capture_groups
            .expect("Expected at least one expression");

        // Use reverse matching for variable-sized lookbehinds without fancy features
        use regex_automata::hybrid::dfa;
        use regex_automata::nfa::thompson;
        use regex_automata::util::syntax::Config as SyntaxConfig;

        // A directly built lazy DFA cannot fall back to another engine the way
        // `meta::Regex` does, so a too-small cache capacity must not be an error.
        // Users cannot know the minimum in advance (it depends on the NFA size),
        // so by default round it up instead. `delegate_dfa_skip_cache_capacity_check(false)`
        // keeps the limit strict and turns a too-small capacity into a compile error.
        let mut dfa_config = dfa::Config::new()
            .unicode_word_boundary(true)
            .skip_cache_capacity_check(self.options.delegate_dfa_skip_cache_capacity_check);
        if let Some(limit) = self.options.delegate_dfa_size_limit {
            dfa_config = dfa_config.cache_capacity(limit);
        }
        let mut thompson_config = thompson::Config::new().reverse(true);
        if let Some(limit) = self.options.delegate_size_limit {
            thompson_config = thompson_config.nfa_size_limit(Some(limit));
        }
        // This builder parses `pattern` itself instead of going through
        // `options_to_rabuilder`, so the caller's Unicode and bytes-mode settings
        // have to be applied to its syntax config too. Without them the reverse
        // DFA would use regex-automata's defaults while the forward
        // capture-extraction engine below uses the configured ones.
        let syntax = SyntaxConfig::new()
            .utf8(matches!(self.options.bytes_mode, BytesMode::Unicode))
            .unicode(self.options.unicode);
        // Build a reverse DFA for the pattern
        let dfa = match dfa::DFA::builder()
            .configure(dfa_config)
            .thompson(thompson_config)
            .syntax(syntax)
            .build(pattern)
        {
            Ok(dfa) => Arc::new(dfa),
            Err(e) => {
                return Err(Error::CompileError(Box::new(CompileError::DfaBuildError(
                    pattern.to_string(),
                    e.to_string(),
                ))))
            }
        };

        let create: CachePoolFn = alloc::boxed::Box::new({
            let dfa = Arc::clone(&dfa);
            move || dfa.create_cache()
        });
        let cache_pool = Pool::new(create);

        // Build the forward regex for capture group extraction. It is only run
        // anchored (see vm.rs BackwardsDelegate) and exists precisely to report
        // captures, so disable the prefilter but keep all capture groups.
        let forward_regex = if capture_groups.start() != capture_groups.end() {
            Some(compile_inner(
                pattern,
                &self.options,
                DelegateUsage::anchored(true),
            )?)
        } else {
            None
        };

        self.b
            .add(Insn::BackwardsDelegate(ReverseBackwardsDelegate {
                dfa,
                cache_pool,
                pattern: pattern.to_string(),
                capture_group_extraction_inner: forward_regex,
                capture_groups: capture_groups.to_option_if_non_empty(),
            }))?;
        Ok(())
    }

    fn compile_delegates(&mut self, infos: &[Info<'_>]) -> Result<()> {
        if infos.is_empty() {
            return Ok(());
        }
        if infos.len() == 1 {
            if let Expr::LiteralBytes { ref bytes, .. } = infos[0].expr {
                self.emit_literal_bytes(bytes)?;
                return Ok(());
            }
        }
        // A batch that is entirely literal compiles to a native literal
        // instruction instead of a delegated engine. Case-sensitive literals
        // become a plain byte-compare `Lit`; batches containing
        // case-insensitive characters become a `LitCasei` (Unicode mode only —
        // see `try_casei_literal`). This keeps each literal branch of an
        // alternation off a per-branch delegated engine, so build cost does not
        // scale with the number of branches.
        if let Some(any_casei) = infos
            .iter()
            .try_fold(false, |any, e| e.is_literal_get_casei().map(|c| any || c))
        {
            if !any_casei {
                let mut val = String::new();
                for info in infos {
                    info.push_literal(&mut val);
                }
                self.b.add(Insn::Lit(val))?;
                return Ok(());
            }
            let mut chars = Vec::new();
            for info in infos {
                info.push_literal_chars(&mut chars);
            }
            if let Some(lit) = self.try_casei_literal(&chars) {
                self.b.add(Insn::LitCasei(lit))?;
                return Ok(());
            }
        }

        let mut delegate_builder = DelegateBuilder::new(&self.options);
        for info in infos {
            delegate_builder.push(info);
        }
        // Skip emitting a delegate for an empty regex (e.g. a batch of
        // only DefineGroups), as it would just match the empty string.
        if !delegate_builder.is_empty() {
            self.b
                .add(delegate_builder.build(&self.options, &mut self.delegate_memo)?)?;
        }
        Ok(())
    }

    fn compile_delegate(&mut self, info: &Info) -> Result<()> {
        self.compile_delegates(core::slice::from_ref(info))
    }

    /// Builds a [`CaseiLiteral`] from `(char, casei)` pairs: each
    /// case-insensitive character contributes its Unicode simple case-fold
    /// class (what a delegated `(?i)` literal matches), each case-sensitive
    /// one a singleton. `None` when the fold semantics wouldn't match a
    /// delegated engine's — non-Unicode syntax folds ASCII-only, and non-UTF-8
    /// haystacks can't be decoded per codepoint — so the caller falls back to
    /// a delegate.
    fn try_casei_literal(&self, chars: &[(char, bool)]) -> Option<CaseiLiteral> {
        use regex_syntax::hir::{ClassUnicode, ClassUnicodeRange};

        if !self.options.unicode || !matches!(self.options.bytes_mode, BytesMode::Unicode) {
            return None;
        }
        let mut out = Vec::with_capacity(chars.len());
        for &(c, casei) in chars {
            let ranges: Box<[(char, char)]> = if casei {
                let mut class = ClassUnicode::new([ClassUnicodeRange::new(c, c)]);
                // Errs when regex-syntax was built without its case-folding
                // tables; the delegate fallback handles it the old way.
                class.try_case_fold_simple().ok()?;
                class
                    .ranges()
                    .iter()
                    .map(|r| (r.start(), r.end()))
                    .collect()
            } else {
                Box::new([(c, c)])
            };
            out.push(ranges);
        }
        Some(CaseiLiteral::new(out.into()))
    }

    fn compile_general_newline(&mut self, unicode: bool) -> Result<()> {
        // Compile \R as: try \r\n first, then try single newline chars
        // The entire \R is atomic - once it matches, we don't backtrack
        // This prevents \r\n from backtracking to \r

        self.b.add(Insn::BeginAtomic)?;

        // Split: try \r\n first, then single chars
        let split_pc = self.b.pc();
        self.b.add(Insn::Split(split_pc + 1, usize::MAX))?; // Will fix second target later

        // First alternative: \r\n
        self.b.add(Insn::Lit("\r\n".to_string()))?;

        // Jump over other alternatives
        let jmp_pc = self.b.pc();
        self.b.add(Insn::Jmp(usize::MAX))?; // Will fix target later

        // Second alternative: single newline characters
        let single_newline_char_pc = self.b.pc();
        self.b
            .set_split_target(split_pc, single_newline_char_pc, true);

        // Compile a delegate for matching single newline characters.
        // In Ascii bytes mode, Unicode chars are not allowed in the delegate,
        // so we always use the non-Unicode pattern regardless of the `unicode` flag.
        let use_unicode = unicode && !matches!(self.options.bytes_mode, BytesMode::Ascii);
        let pattern = if use_unicode {
            // Unicode mode: \n, \v, \f, \r, U+0085, U+2028, U+2029
            "[\n\x0B\x0C\r\u{0085}\u{2028}\u{2029}]"
        } else {
            // Non-Unicode mode: \n, \v, \f, \r
            "[\n\x0B\x0C\r]"
        };

        // This delegate (a single newline character class) is run anchored and
        // has no capture groups fancy-regex reads. Match it natively when possible
        // (skipping engine construction), falling back to a delegate otherwise.
        if let Some(matcher) = try_char_class_matcher(pattern, &self.options) {
            self.b.add(Insn::CharClass(matcher))?;
        } else {
            let compiled = compile_inner(pattern, &self.options, DelegateUsage::anchored(false))?;
            self.b.add(Insn::Delegate(Delegate {
                inner: compiled,
                pattern: pattern.to_string(),
                capture_groups: None,
            }))?;
        }

        // Fix the jump target
        let end_atomic_pc = self.b.pc();
        self.b.add(Insn::EndAtomic)?;

        self.b.set_jmp_target(jmp_pc, end_atomic_pc);

        Ok(())
    }
}

/// Describes how a delegated regex-automata engine will be searched, so the
/// builder can skip machinery that wouldn't help for that usage.
///
/// VM `Delegate` instructions are always executed anchored at the current input
/// position (see `vm::run`), so prefilters — which only accelerate *unanchored*
/// scans — are pure build-time and memory overhead for them. Likewise, a delegate
/// whose explicit capture groups fancy-regex never reads doesn't need them
/// compiled into the NFA.
#[derive(Clone, Copy)]
pub(crate) struct DelegateUsage {
    /// True if the engine should spend build time constructing a prefilter.
    /// Only useful for engines that are actually searched unanchored.
    prefilter: bool,
    /// True if explicit capture-group spans must be reported by the engine.
    needs_captures: bool,
}

impl DelegateUsage {
    /// An engine searched unanchored and expected to report full captures: the
    /// top-level `Wrap` engine and the seek pre-filter. Matches the historical
    /// default (prefilter on, all captures).
    pub(crate) const fn unanchored() -> Self {
        DelegateUsage {
            prefilter: true,
            needs_captures: true,
        }
    }

    /// An engine that reports full captures but is only ever searched anchored
    /// in practice, so a prefilter would never be consulted: `Wrap` engines for
    /// regexes built as members of a `RegexSet`, which the set only runs
    /// anchored at candidate positions.
    pub(crate) const fn unanchored_no_prefilter() -> Self {
        DelegateUsage {
            prefilter: false,
            needs_captures: true,
        }
    }

    /// An anchored VM delegate. `needs_captures` says whether fancy-regex reads
    /// the delegate's explicit capture groups.
    pub(crate) const fn anchored(needs_captures: bool) -> Self {
        DelegateUsage {
            prefilter: false,
            needs_captures,
        }
    }
}

pub(crate) fn compile_inner(
    inner_re: &str,
    options: &CompileOptions,
    usage: DelegateUsage,
) -> Result<RaRegex> {
    let builder = options_to_rabuilder(options, usage);

    let re = builder
        .build(inner_re)
        .map_err(CompileError::InnerError)
        .map_err(|e| Error::CompileError(Box::new(e)))?;

    Ok(re)
}

/// Like [`compile_inner`], but from an already-built `Hir`, so the engine
/// doesn't parse the pattern a second time. The syntax config set by
/// `options_to_rabuilder` is ignored on this path — the `Hir` must already
/// encode it (see `to_hir`).
pub(crate) fn compile_inner_from_hir(
    hir: &regex_syntax::hir::Hir,
    options: &CompileOptions,
    usage: DelegateUsage,
) -> Result<RaRegex> {
    let builder = options_to_rabuilder(options, usage);

    let re = builder
        .build_from_hir(hir)
        .map_err(CompileError::InnerError)
        .map_err(|e| Error::CompileError(Box::new(e)))?;

    Ok(re)
}

pub(crate) fn options_to_raconfig(options: &CompileOptions, usage: DelegateUsage) -> RaConfig {
    let mut config = RaConfig::new();
    if let Some(limit) = options.delegate_size_limit {
        config = config.nfa_size_limit(Some(limit));
    }
    if let Some(limit) = options.delegate_dfa_size_limit {
        config = config.hybrid_cache_capacity(limit);
    }
    if !usage.prefilter {
        // Anchored searches never consult a prefilter, so don't spend build time
        // (literal extraction, Aho-Corasick/Teddy tables) or memory constructing one.
        //
        // NOTE: we deliberately do *not* disable the lazy-DFA (`hybrid`) / DFA
        // engines here, even though a VM delegate is only searched anchored.
        // Doing so lets regex-automata skip building the reverse NFA and roughly
        // halves per-delegate build time, but benchmarking showed it regresses
        // *match* performance — the lazy DFA has lower per-search overhead than
        // the PikeVM, and delegates (especially short char classes) are searched
        // repeatedly inside the backtracking VM. The reverse-NFA build cost is
        // therefore coupled to match speed; see the compile benches.
        config = config.auto_prefilter(false);
    }
    if !usage.needs_captures {
        // We only need to know whether (and where) the overall match is, via
        // search_half/search; skip compiling explicit capture-group slots.
        config = config.which_captures(WhichCaptures::Implicit);
    }
    // Disable regex-automata's "UTF-8 empty" optimization when the input is not
    // guaranteed to be valid UTF-8 (i.e. in `Ascii` or `UnicodeBytes` bytes
    // modes). That optimization skips zero-width matches whose offset falls
    // inside a UTF-8 codepoint, on the premise that the haystack is valid UTF-8
    // and such a match could never be part of a real match. With arbitrary
    // bytes that premise is false: `^` / `(?m:^)` legitimately match at the
    // start of a haystack whose first byte is a non-ASCII byte (e.g. `\x80`,
    // also a UTF-8 continuation/lead byte), so the offset is not a codepoint
    // boundary and the optimization wrongly rejects the match.
    if !matches!(options.bytes_mode, BytesMode::Unicode) {
        config = config.utf8_empty(false);
    }
    config
}

pub(crate) fn options_to_rabuilder(options: &CompileOptions, usage: DelegateUsage) -> RaBuilder {
    use regex_automata::util::syntax::Config as SyntaxConfig;

    let config = options_to_raconfig(options, usage);
    let mut builder = RaBuilder::new();
    builder.configure(config);
    // NOTE: our Expr to_str function handles creating the correct pattern string for regex-automata
    //       using the default SyntaxConfig. If we were to set i.e. case sensitivity directly on
    //       the SyntaxConfig here, and the pattern includes the (?-i) flag, then that flag would
    //       effectively be ignored unless to_str would encode the flag for every node, even when it
    //       is the default, which feels too verbose and hurts readability, is more to parse etc.
    let utf8 = matches!(options.bytes_mode, BytesMode::Unicode);

    let syntax = SyntaxConfig::new().utf8(utf8).unicode(options.unicode);
    builder.syntax(syntax);

    builder
}

/// Strip the `(?i:` prefix and `)` suffix that `Expr::to_str` wraps around
/// case-insensitive delegate expressions. This keeps the stored `CharClassMatcher`
/// name readable (e.g. `\w` instead of `(?i:\w)`).
fn strip_delegate_flags(s: &str) -> &str {
    s.strip_prefix("(?i:")
        .and_then(|s| s.strip_suffix(')'))
        .unwrap_or(s)
}

/// Try to compile a delegated fragment to a native [`CharClassMatcher`] instead
/// of a regex-automata engine.
///
/// Most delegates are a single character class (`\d`, `[a-z]`, ...). Building a
/// full `meta::Regex` for each one dominates compile time and memory, and the
/// engine is then searched once per character inside the backtracking loop. A
/// class matches exactly one codepoint/byte, so we can match it directly.
///
/// Returns `None` (so the caller builds an engine instead) when the fragment is
/// not exactly one character class, or when the class can't be matched natively
/// for the current bytes mode.
fn try_char_class_matcher(re: &str, options: &CompileOptions) -> Option<CharClassMatcher> {
    use regex_syntax::ParserBuilder;

    // Parse with the same flags `options_to_rabuilder` would hand the engine, so
    // the resulting class is exactly the set the delegated engine would accept
    // (same case folding, same Unicode handling).
    let utf8 = matches!(options.bytes_mode, BytesMode::Unicode);
    let hir = ParserBuilder::new()
        .utf8(utf8)
        .unicode(options.unicode)
        .build()
        .parse(re)
        .ok()?;

    char_class_matcher_from_hir(&hir, options, Some(strip_delegate_flags(re).to_string()))
}

/// Build a [`CharClassMatcher`] from an `Hir` that is a single character class,
/// or `None` if it isn't one (or can't be matched natively in this bytes mode).
fn char_class_matcher_from_hir(
    hir: &Hir,
    options: &CompileOptions,
    name: Option<String>,
) -> Option<CharClassMatcher> {
    use regex_syntax::hir::{Class, HirKind};

    match hir.kind() {
        HirKind::Class(Class::Unicode(cu)) => {
            // A Unicode class over a possibly-invalid-UTF-8 haystack (UnicodeBytes
            // mode) is matched by the engine via a UTF-8 automaton; defer to it to
            // keep those exact semantics. In Unicode mode the haystack is valid
            // UTF-8, so direct codepoint membership is equivalent.
            if !matches!(options.bytes_mode, BytesMode::Unicode) {
                return None;
            }
            let ranges = cu.ranges().iter().map(|r| (r.start(), r.end())).collect();
            Some(CharClassMatcher::Codepoint { ranges, name })
        }
        HirKind::Class(Class::Bytes(cb)) => {
            let ranges = cb.ranges().iter().map(|r| (r.start(), r.end())).collect();
            Some(CharClassMatcher::Byte { ranges, name })
        }
        _ => None,
    }
}

/// Recursively populate the group_info_map with all capture groups in the Info tree
pub(crate) fn populate_group_info_map<'a>(map: &mut Map<usize, &'a Info<'a>>, info: &'a Info<'a>) {
    match info.expr {
        Expr::Group(_) => {
            let group = info.start_group();
            map.insert(group, info);
            // Continue recursing into children
            for child in &info.children {
                populate_group_info_map(map, child);
            }
        }
        _ => {
            // Recurse into all children
            for child in &info.children {
                populate_group_info_map(map, child);
            }
        }
    }
}

/// Options for compiling analyzed expressions into a program.
#[derive(Clone)]
pub struct CompileOptions {
    /// Whether the regex is anchored (starts matching at the beginning of the input).
    /// When `false`, a `SplitUnanchored` preamble is emitted to allow matching at any position.
    pub anchored: bool,
    /// Whether the regex contains subroutine calls, requiring group info to be pre-populated.
    pub contains_subroutines: bool,
    /// Optional filter function for the Seek pre-filter optimization.
    /// When `Some(f)` and a seek pattern can be derived, `f` is called with the pattern string
    /// to decide whether it is useful enough to replace the `SplitUnanchored` preamble with a
    /// `Seek` instruction. When `None`, seek is disabled entirely.
    pub seek_filter: Option<fn(&str) -> bool>,
    /// To match Oniguruma behavior where only \z can match at EOF if preceeded by a newline character
    pub disallow_empty_match_at_eof_after_newline: bool,
    /// How the VM should advance positions: byte-level (Ascii) vs codepoint-level (Unicode/UnicodeBytes).
    pub bytes_mode: BytesMode,
    /// Whether Unicode mode is enabled for the regex. This is the effective value combining
    /// the unicode flag and bytes_mode: `syntaxc.get_unicode() && !matches!(bytes_mode, BytesMode::Ascii)`.
    /// It is always `false` in Ascii bytes mode, regardless of the unicode flag.
    pub unicode: bool,
    /// Optional size limit in bytes for the NFA of each delegated sub-expression.
    pub delegate_size_limit: Option<usize>,
    /// Optional size limit in bytes for the DFA of each delegated sub-expression.
    pub delegate_dfa_size_limit: Option<usize>,
    /// Whether the reverse lazy DFAs of variable-length lookbehinds skip the minimum cache
    /// capacity check, rounding the capacity up instead of failing to build.
    pub delegate_dfa_skip_cache_capacity_check: bool,
    /// Optional cap on the number of VM instructions emitted while compiling.
    /// Subroutine calls are inlined at compile time, so a self- or
    /// mutually-recursive pattern can expand without bound even while the
    /// recursion-depth cap is respected; this bounds the emitted instruction
    /// vector instead. `None` disables the check.
    pub max_prog_size: Option<usize>,
}

impl core::fmt::Debug for CompileOptions {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let seek_filter_desc = match self.seek_filter {
            None => "None",
            Some(f_ptr)
                if (f_ptr as *const ()) == (crate::seek::seek_pattern_is_useful as *const ()) =>
            {
                "Some(seek_pattern_is_useful)"
            }
            Some(_) => "Some(<custom>)",
        };
        f.debug_struct("CompileOptions")
            .field("anchored", &self.anchored)
            .field("contains_subroutines", &self.contains_subroutines)
            .field("seek_filter", &seek_filter_desc)
            .field(
                "disallow_empty_match_at_eof_after_newline",
                &self.disallow_empty_match_at_eof_after_newline,
            )
            .field("bytes_mode", &self.bytes_mode)
            .field("unicode", &self.unicode)
            .field("delegate_size_limit", &self.delegate_size_limit)
            .field("delegate_dfa_size_limit", &self.delegate_dfa_size_limit)
            .field(
                "delegate_dfa_skip_cache_capacity_check",
                &self.delegate_dfa_skip_cache_capacity_check,
            )
            .field("max_prog_size", &self.max_prog_size)
            .finish()
    }
}

impl Default for CompileOptions {
    fn default() -> Self {
        CompileOptions {
            anchored: false,
            contains_subroutines: false,
            seek_filter: None,
            disallow_empty_match_at_eof_after_newline: false,
            bytes_mode: BytesMode::default(),
            unicode: true,
            delegate_size_limit: None,
            delegate_dfa_size_limit: None,
            delegate_dfa_skip_cache_capacity_check: true,
            max_prog_size: None,
        }
    }
}

/// Compile the analyzed expressions into a program.
pub fn compile(info: &Info<'_>, options: CompileOptions) -> Result<Prog> {
    let bytes_mode = options.bytes_mode;
    // Pre-populate the group_info_map to support forward references
    let mut group_info_map = Map::new();
    populate_group_info_map(&mut group_info_map, info);

    let mut c = Compiler {
        b: VMBuilder::new(info.end_group(), options.max_prog_size),
        options,
        inside_alternation: false,
        group_info_map,
        subroutine_recursion_stack: Vec::new(),
        root_info: info,
        delegate_memo: Map::new(),
    };

    let mut seek_pattern = String::new();
    build_seek_pattern(info, &c.group_info_map, 0, &mut seek_pattern, 0);

    if !c.options.anchored {
        let mut used_seek = false;
        if let Some(filter) = c.options.seek_filter {
            if filter(&seek_pattern) {
                // The seek engine is searched unanchored to find the next candidate
                // position, so it keeps its prefilter (the whole point of seeking).
                if let Ok(inner) =
                    compile_inner(&seek_pattern, &c.options, DelegateUsage::unanchored())
                {
                    c.b.add(Insn::Seek(Seek {
                        inner,
                        pattern: seek_pattern.clone(),
                    }))?;
                    used_seek = true;
                }
            }
        }

        // If compilation of the seek pattern fails for any reason, or seeking for this
        // pattern is disabled, fall back to the standard SplitUnanchored preamble.
        if !used_seek {
            // add instructions as if \O*? was used at the start of the expression
            // so that we bump the haystack index by one when failing to match at the current position
            let current_pc = c.b.pc();
            // we are adding 3 instructions, so the current program counter plus 3 gives us the first real instruction
            c.b.add(Insn::SplitUnanchored(current_pc + 3, current_pc + 1))?;
            c.b.add(Insn::Any)?;
            c.b.add(Insn::Jmp(current_pc))?;
        }
    }
    if info.start_group() == 1 {
        // add implicit capture group 0 begin
        c.b.add(Insn::Save(0))?;
    }
    c.visit(info, false)?;
    if info.start_group() == 1 {
        // add implicit capture group 0 end
        c.b.add(Insn::Save(1))?;
    }
    if c.options.disallow_empty_match_at_eof_after_newline {
        c.b.add(Insn::RejectEmptyMatchAtEOFFollowingNewline)?;
    }
    c.b.add(Insn::End)?;
    Ok(c.b.build(bytes_mode, c.options.unicode, seek_pattern, info.max_size))
}

struct DelegateBuilder {
    re: String,
    min_size: usize,
    const_size: bool,
    capture_groups: Option<CaptureGroupRange>,
    /// Hir translations of the pushed fragments, built alongside `re` so the
    /// engine doesn't have to re-parse the string. `None` once any fragment
    /// fails to translate; the string path is used instead.
    hirs: Option<Vec<Hir>>,
    hir_ctx: HirCtx,
}

impl DelegateBuilder {
    fn new(options: &CompileOptions) -> Self {
        let utf8 = matches!(options.bytes_mode, BytesMode::Unicode);
        Self {
            re: String::new(),
            min_size: 0,
            const_size: true,
            capture_groups: None,
            hirs: Some(Vec::new()),
            hir_ctx: HirCtx::new(options.unicode, utf8),
        }
    }

    /// Returns true if the regex string built so far is empty.
    fn is_empty(&self) -> bool {
        self.re.is_empty()
    }

    fn push(&mut self, info: &Info<'_>) -> &mut DelegateBuilder {
        // TODO: might want to detect case of a group with no captures
        //  inside, so we can run find() instead of captures()

        self.min_size += info.min_size;
        self.const_size &= info.const_size;
        if self.capture_groups.is_none() {
            self.capture_groups = Some(info.capture_groups);
        } else {
            // Update the end_group to the latest
            self.capture_groups = self
                .capture_groups
                .map(|range| CaptureGroupRange(range.start(), info.end_group()));
        }

        // Add expression. The precedence argument has to be 1 here to
        // ensure correct grouping in these cases:
        //
        // If we have multiple expressions, we are building a concat.
        // Without grouping, we'd turn ["a", "b|c"] into "^ab|c". But we
        // want "^a(?:b|c)".
        //
        // Even with a single expression, because we add `^` at the
        // beginning, we need a group. Otherwise `["a|b"]` would be turned
        // into `"^a|b"` instead of `"^(?:a|b)"`.
        info.expr.to_str(&mut self.re, 1);

        // Track the Hir form alongside the string. Group numbering continues
        // across fragments (the ctx counter), matching how the parser would
        // number the concatenated string.
        if let Some(hirs) = &mut self.hirs {
            match expr_to_hir(info.expr, &mut self.hir_ctx) {
                Some(hir) => hirs.push(hir),
                None => self.hirs = None,
            }
        }
        self
    }

    fn build(
        &mut self,
        options: &CompileOptions,
        memo: &mut Map<(String, bool), RaRegex>,
    ) -> Result<Insn> {
        // A single character class is matched directly, skipping engine
        // construction entirely (see CharClassMatcher). A class has no capture
        // groups, so this only applies when none are needed.
        if self.capture_groups.map_or(false, |r| r.start() == r.end()) {
            let matcher = match &self.hirs {
                // A single fragment whose Hir is a class; no parse needed.
                Some(hirs) if hirs.len() == 1 => char_class_matcher_from_hir(
                    &hirs[0],
                    options,
                    Some(strip_delegate_flags(&self.re).to_string()),
                ),
                // Multiple fragments can't be a single class.
                Some(_) => None,
                // Translation bailed; fall back to parsing the string.
                None => try_char_class_matcher(&self.re, options),
            };
            if let Some(matcher) = matcher {
                return Ok(Insn::CharClass(matcher));
            }
        }
        Ok(Insn::Delegate(self.build_delegate(options, memo)?))
    }

    fn build_delegate(
        &mut self,
        options: &CompileOptions,
        memo: &mut Map<(String, bool), RaRegex>,
    ) -> Result<Delegate> {
        let capture_groups = self
            .capture_groups
            .expect("Expected at least one expression");

        // VM delegates are always run anchored. Only compile capture-group slots
        // when fancy-regex actually reads them (vm.rs uses search_slots when
        // capture_groups is Some, and the faster search_half otherwise).
        let capture_groups = capture_groups.to_option_if_non_empty();
        let usage = DelegateUsage::anchored(capture_groups.is_some());
        // The engine is fully determined by the delegate pattern string plus
        // whether capture slots are compiled in (options are fixed per compile),
        // so an identical fragment seen earlier in this compile shares its
        // engine. Outer group numbering may differ between the two sites; that
        // lives in `capture_groups` below, not in the engine.
        let memo_key = (self.re.clone(), capture_groups.is_some());
        let compiled = match memo.get(&memo_key) {
            Some(engine) => engine.clone(),
            None => {
                let engine = match self.hirs.take() {
                    Some(hirs) => compile_inner_from_hir(&Hir::concat(hirs), options, usage)?,
                    None => compile_inner(&self.re, options, usage)?,
                };
                memo.insert(memo_key, engine.clone());
                engine
            }
        };

        Ok(Delegate {
            inner: compiled,
            pattern: self.re.clone(),
            capture_groups,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyze::{analyze, AnalyzeContext};
    use crate::parse::ExprTree;
    use crate::vm::Insn::*;
    use crate::{BytesMode, Regex, RegexBuilder};

    use matches::assert_matches;

    #[test]
    fn delegate_dfa_size_limit_sets_hybrid_cache_capacity() {
        let options = CompileOptions {
            delegate_dfa_size_limit: Some(123_456),
            ..Default::default()
        };
        let config = options_to_raconfig(&options, DelegateUsage::unanchored());
        assert_eq!(config.get_hybrid_cache_capacity(), 123_456);
        // the fully compiled DFA size limit must be left alone
        assert_eq!(
            config.get_dfa_size_limit(),
            RaConfig::new().get_dfa_size_limit()
        );
    }

    #[test]
    fn delegate_size_limit_sets_nfa_size_limit() {
        let options = CompileOptions {
            delegate_size_limit: Some(54_321),
            ..Default::default()
        };
        let config = options_to_raconfig(&options, DelegateUsage::unanchored());
        assert_eq!(config.get_nfa_size_limit(), Some(54_321));
    }

    #[cfg_attr(feature = "track_caller", track_caller)]
    fn assert_compile_error<T, F>(result: crate::Result<T>, check: F)
    where
        F: FnOnce(&CompileError) -> bool,
    {
        match result {
            Err(Error::CompileError(ref e)) if check(e) => {}
            other => panic!(
                "expected a matching CompileError, but got: {:?}",
                other.err()
            ),
        }
    }

    #[test]
    fn casei_literal_compiles_to_native_insn() {
        // A case-insensitive literal inside a hard pattern becomes a single
        // native LitCasei instruction, not a delegated engine.
        let prog = compile_prog_forced_hard("(?i)abc");
        assert_eq!(prog.len(), 2, "prog: {:?}", prog);
        assert_matches!(prog[0], LitCasei(_));
        assert_matches!(prog[1], End);
    }

    #[test]
    fn casei_alternation_branches_compile_to_native_insns() {
        // A hard element in one branch forces the alternation to compile
        // branch by branch on the VM (a fully-easy alternation would be
        // delegated whole). The literal branches must become native LitCasei
        // instructions instead of one delegated engine each.
        let prog = compile_prog_forced_hard("(?i)(abort|absent|z(?<=q)z)");
        let count = prog
            .iter()
            .filter(|insn| matches!(insn, LitCasei(_)))
            .count();
        assert!(
            count >= 2,
            "literal branches should compile to LitCasei: {:?}",
            prog
        );
        assert!(
            prog.iter().all(|insn| !matches!(insn, Insn::Delegate(_))),
            "no branch should need a forward delegate: {:?}",
            prog
        );
    }

    #[test]
    fn jumps_for_alternation() {
        let prog = compile_prog_forced_hard("a|b|c");

        assert_eq!(prog.len(), 8, "prog: {:?}", prog);
        assert_matches!(prog[0], Split(1, 3));
        assert_matches!(prog[1], Lit(ref l) if l == "a");
        assert_matches!(prog[2], Jmp(7));
        assert_matches!(prog[3], Split(4, 6));
        assert_matches!(prog[4], Lit(ref l) if l == "b");
        assert_matches!(prog[5], Jmp(7));
        assert_matches!(prog[6], Lit(ref l) if l == "c");
        assert_matches!(prog[7], End);
    }

    #[test]
    fn ascii_literal_bytes_compiled_as_distinct_instruction() {
        let prog = compile_prog_ascii(r"\xFF");
        assert_eq!(prog.len(), 4, "prog: {:?}", prog);
        assert_matches!(prog[0], Save(0));
        assert_matches!(&prog[1], LitBytes(bytes) if bytes.len() == 1 && bytes[0] == 255);
        assert_matches!(prog[2], Save(1));
        assert_matches!(prog[3], End);
    }

    #[test]
    fn look_around_pattern_can_be_delegated() {
        let prog = compile_prog("(?=ab*)c");

        assert_eq!(prog.len(), 5, "prog: {:?}", prog);
        assert_matches!(prog[0], Save(0));
        assert_delegate_insn(&prog[1], "ab*", None);
        assert_matches!(prog[2], Restore(0));
        assert_matches!(prog[3], Lit(ref l) if l == "c");
        assert_matches!(prog[4], End);
    }

    #[test]
    fn easy_concat_can_delegate_end() {
        let prog = compile_prog("(?!x)(?:a|ab)x*");

        assert_eq!(prog.len(), 5, "prog: {:?}", prog);
        assert_matches!(prog[0], Split(1, 3));
        assert_matches!(prog[1], Lit(ref l) if l == "x");
        assert_matches!(prog[2], FailNegativeLookAround);
        assert_delegate_insn(&prog[3], "(?:a|ab)x*", None);
        assert_matches!(prog[4], End);
    }

    #[test]
    fn hard_concat_can_delegate_const_size_end() {
        let prog = compile_prog("(?:(?!x)(?:a|b)c)x*");

        assert_eq!(prog.len(), 6, "prog: {:?}", prog);
        assert_matches!(prog[0], Split(1, 3));
        assert_matches!(prog[1], Lit(ref l) if l == "x");
        assert_matches!(prog[2], FailNegativeLookAround);
        assert_delegate_insn(&prog[3], "(?:a|b)c", None);
        assert_delegate_insn(&prog[4], "x*", None);
        assert_matches!(prog[5], End);
    }

    #[test]
    fn hard_concat_can_not_delegate_variable_end() {
        let prog = compile_prog("(?:(?!x)(?:a|ab))x*");

        assert_eq!(prog.len(), 9, "prog: {:?}", prog);
        assert_matches!(prog[0], Split(1, 3));
        assert_matches!(prog[1], Lit(ref l) if l == "x");
        assert_matches!(prog[2], FailNegativeLookAround);
        assert_matches!(prog[3], Split(4, 6));
        assert_matches!(prog[4], Lit(ref l) if l == "a");
        assert_matches!(prog[5], Jmp(7));
        assert_matches!(prog[6], Lit(ref l) if l == "ab");
        assert_delegate_insn(&prog[7], "x*", None);
        assert_matches!(prog[8], End);
    }

    #[test]
    fn conditional_expression_can_be_compiled() {
        let prog = compile_prog(r"(?(ab)c|d)");

        assert_eq!(prog.len(), 8, "prog: {:?}", prog);

        assert_matches!(prog[0], BeginAtomic);
        assert_matches!(prog[1], Split(2, 6));
        assert_matches!(prog[2], Lit(ref l) if l == "ab");
        assert_matches!(prog[3], EndAtomic);
        assert_matches!(prog[4], Lit(ref l) if l == "c");
        assert_matches!(prog[5], Jmp(7));
        assert_matches!(prog[6], Lit(ref l) if l == "d");
        assert_matches!(prog[7], End);
    }

    #[test]
    fn lazy_any_can_be_compiled_explicit_capture_group_zero() {
        let prog = compile_prog(r"\O*?((?!a))");

        assert_eq!(prog.len(), 9, "prog: {:?}", prog);

        assert_matches!(prog[0], Split(3, 1));
        assert_matches!(prog[1], Any);
        assert_matches!(prog[2], Jmp(0));
        assert_matches!(prog[3], SaveCaptureGroupStart(0));
        assert_matches!(prog[4], Split(5, 7));
        assert_matches!(prog[5], Lit(ref l) if l == "a");
        assert_matches!(prog[6], FailNegativeLookAround);
        assert_matches!(prog[7], Save(1));
        assert_matches!(prog[8], End);
    }

    #[test]
    fn backtracking_control_verb_fail_can_be_compiled() {
        let prog = compile_prog(r"(*FAIL)");

        assert_eq!(prog.len(), 2, "prog: {:?}", prog);

        assert_matches!(prog[0], Fail);
        assert_matches!(prog[1], End);
    }

    #[test]
    fn other_backtracking_control_verbs_error() {
        let tree = Expr::parse_tree(r"(*ACCEPT)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );

        let tree = Expr::parse_tree(r"(*COMMIT)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );

        let tree = Expr::parse_tree(r"(*SKIP)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );

        let tree = Expr::parse_tree(r"(*PRUNE)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );
    }

    #[test]
    fn backref_exists_condition_with_recursion_level_not_yet_supported() {
        for pattern in &[
            r"(a)(?(1+0)b|c)d",
            r"(?<n>a)(?(<n+0>)b|c)d",
            r"(?<n>a)(?('n+0')b|c)d",
        ] {
            let tree = Expr::parse_tree(pattern).unwrap();
            let info = analyze(
                &tree,
                AnalyzeContext {
                    explicit_capture_group_0: false,
                    ..Default::default()
                },
            )
            .unwrap();
            assert_compile_error(
                compile(
                    &info,
                    CompileOptions {
                        anchored: true,
                        contains_subroutines: tree.contains_subroutines,
                        ..CompileOptions::default()
                    },
                ),
                |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
            );
        }
    }

    #[test]
    #[cfg(not(feature = "variable-lookbehinds"))]
    fn variable_lookbehind_requires_feature() {
        // Without the feature flag, variable-length lookbehinds should error
        let tree = Expr::parse_tree(r"(?<=ab+)x").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::VariableLookBehindRequiresFeature),
        );

        let tree = Expr::parse_tree(r"(?<=\bab+)x").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::VariableLookBehindRequiresFeature),
        );
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_lookbehind_with_required_feature_no_captures_easy() {
        let prog = compile_prog(r"(?<=ab+)x");

        assert_eq!(prog.len(), 5, "prog: {:?}", prog);

        assert_matches!(prog[0], Save(0));
        assert_matches!(&prog[1], BackwardsDelegate(ReverseBackwardsDelegate { pattern, dfa: _, cache_pool: _, capture_group_extraction_inner: None, capture_groups: None }) if pattern == "ab+");
        assert_matches!(prog[2], Restore(0));
        assert_matches!(prog[3], Lit(ref l) if l == "x");
        assert_matches!(prog[4], End);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_lookbehind_with_required_feature_no_captures_hard_const_size_zero_length() {
        let prog = compile_prog(r"(?<=\bab+)x");

        assert_eq!(prog.len(), 6, "prog: {:?}", prog);

        assert_matches!(prog[0], Save(0));
        assert_matches!(&prog[1], BackwardsDelegate(ReverseBackwardsDelegate { pattern, dfa: _, cache_pool: _, capture_group_extraction_inner: None, capture_groups: None }) if pattern == "ab+");
        assert_matches!(prog[2], Insn::Assertion(crate::Assertion::WordBoundary));
        assert_matches!(prog[3], Restore(0));
        assert_matches!(prog[4], Lit(ref l) if l == "x");
        assert_matches!(prog[5], End);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_lookbehind_with_required_feature_no_captures_hard_const_size_non_zero_length() {
        let prog = compile_prog(r"((.)b+(?<=\1\1b+)x)");

        assert_eq!(prog.len(), 16, "prog: {:?}", prog);

        assert_matches!(prog[0], SaveCaptureGroupStart(0));
        assert_matches!(prog[1], SaveCaptureGroupStart(1));
        assert_matches!(prog[2], AnyNoNL);
        assert_matches!(prog[3], Save(3));
        assert_matches!(prog[4], Lit(ref l) if l == "b");
        assert_matches!(prog[5], Split(4, 6));
        assert_matches!(prog[6], Save(4));
        assert_matches!(&prog[7], BackwardsDelegate(ReverseBackwardsDelegate { pattern, dfa: _, cache_pool: _, capture_group_extraction_inner: None, capture_groups: None }) if pattern == "b+");
        assert_matches!(prog[8], GoBack(1));
        assert_matches!(
            prog[9],
            Backref {
                slot: 2,
                casei: false,
                unicode: true,
            }
        );
        assert_matches!(prog[10], GoBack(2));
        assert_matches!(
            prog[11],
            Backref {
                slot: 2,
                casei: false,
                unicode: true,
            }
        );
        assert_matches!(prog[12], Restore(4));
        assert_matches!(prog[13], Lit(ref l) if l == "x");
        assert_matches!(prog[14], Save(1));
        assert_matches!(prog[15], End);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_lookbehind_with_required_feature_captures() {
        let prog = compile_prog(r"(?<=a(b+))x");

        assert_eq!(prog.len(), 5, "prog: {:?}", prog);

        assert_matches!(prog[0], Save(2));
        assert_matches!(&prog[1], BackwardsDelegate(ReverseBackwardsDelegate { pattern, dfa: _, cache_pool: _, capture_group_extraction_inner: ref inner, capture_groups: Some(CaptureGroupRange(0, 1)) }) if pattern == "a(b+)" && inner.is_some());
        assert_matches!(prog[2], Restore(2));
        assert_matches!(prog[3], Lit(ref l) if l == "x");
        assert_matches!(prog[4], End);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_hard_lookbehind_patterns_compile() {
        let patterns = vec![
            r"(a+)(?<=\1)b",
            r"(a+)\1(?<=\1)b",
            r"(a+)(b)(?<=\1)c",
            r"(a+)(?<=\1\1)b",
            r"(?<=(\G|^\s*\*?)\s*)(?i:(todo\b:?)|(fixme\b:?)|(note\b:?))",
            r"(?<=([!\&*-/:-=?^|~]|\b(?i:and|contain|in|is|or|not))[\t ]*)\n",
            r"(?<=a(b+))\1",
        ];
        for p in patterns {
            let prog = compile_prog_no_explicit_group0(p);
            assert!(
                prog.iter()
                    .any(|i| matches!(i, crate::vm::Insn::HardVariableLookbehind(_))),
                "HardVariableLookbehind not found in {}: {:?}",
                p,
                prog
            );
        }
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_hard_lookbehind_applies_dfa_size_limit() {
        let prog = compile_prog_with(r"(?<=(a+)\1)x", AnalyzeContext::default(), |info, tree| {
            compile(
                info,
                CompileOptions {
                    anchored: false,
                    contains_subroutines: tree.contains_subroutines,
                    delegate_dfa_size_limit: Some(1),
                    ..CompileOptions::default()
                },
            )
            .unwrap()
            .body
        });
        let lookbehind = prog
            .iter()
            .find_map(|i| match i {
                Insn::HardVariableLookbehind(lb) => Some(lb),
                _ => None,
            })
            .expect("HardVariableLookbehind not found");
        let config = lookbehind.dfa.get_config();
        assert!(config.get_skip_cache_capacity_check());
        // Checks that the configured value was forwarded; there is no API to
        // read the effective capacity after regex-automata rounds it up.
        assert_eq!(config.get_cache_capacity(), 1);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_hard_lookbehind_can_disable_skip_cache_capacity_check() {
        let tree = Expr::parse_tree(r"(?<=(a+)\1)x").unwrap();
        let mut info = analyze(&tree, AnalyzeContext::default()).unwrap();
        let result = compile(
            &mut info,
            CompileOptions {
                anchored: false,
                contains_subroutines: tree.contains_subroutines,
                delegate_dfa_size_limit: Some(1),
                delegate_dfa_skip_cache_capacity_check: false,
                ..CompileOptions::default()
            },
        );
        assert_compile_error(result, |e| matches!(e, CompileError::DfaBuildError(..)));
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_hard_lookbehind_applies_nfa_size_limit() {
        let tree = Expr::parse_tree(r"(?<=(a+)\1)x").unwrap();
        let mut info = analyze(&tree, AnalyzeContext::default()).unwrap();
        let result = compile(
            &mut info,
            CompileOptions {
                anchored: false,
                contains_subroutines: tree.contains_subroutines,
                delegate_size_limit: Some(1),
                ..CompileOptions::default()
            },
        );
        assert_compile_error(result, |e| matches!(e, CompileError::DfaBuildError(..)));
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_hard_lookbehind_debug_output() {
        // Exercises the Debug impls of `HardVariableLookbehind` and
        // `ReverseLookbehindPosCheck`, which format the seek pattern and the
        // save slots but deliberately omit the DFA and cache pool.
        let prog = compile_prog_no_explicit_group0(r"(a+)(?<=\1)b");
        assert!(
            prog.iter().any(|i| matches!(
                i,
                Insn::HardVariableLookbehind(crate::vm::HardVariableLookbehind {
                    pattern,
                    end_pos_slot: 5,
                    candidate_pos_slot: 6,
                    ..
                }) if pattern == "a+"
            )),
            "HardVariableLookbehind not found or wrong fields: {:?}",
            prog
        );
        assert!(
            prog.iter().any(|i| matches!(
                i,
                Insn::ReverseLookbehindPosCheck {
                    slot: 5,
                    candidate_pos_slot: 6,
                    hard_lb_pc: 11,
                }
            )),
            "ReverseLookbehindPosCheck not found or wrong fields: {:?}",
            prog
        );
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn variable_hard_lookbehind_regex_clone() {
        // `Regex::clone` shares the compiled `Arc<Prog>` rather than cloning
        // the program, so the clone must still match the same inputs.  This
        // is the only Clone path exercised at runtime for patterns that emit
        // a `HardVariableLookbehind` instruction.
        let r = RegexBuilder::new(r"(a+)(?<=\1)b").build().expect("compile");
        let r2 = r.clone();
        let m1 = r.find("aaab").expect("find").unwrap();
        let m2 = r2.find("aaab").expect("find2").unwrap();
        assert_eq!(m1.as_str(), m2.as_str());
        assert_eq!(m1.as_str(), "aaab");
    }

    #[test]
    fn absent_repeater_with_easy_inner_compiles() {
        let prog = compile_prog(r"((?~abc))");

        assert_eq!(prog.len(), 4, "prog: {:?}", prog);
        assert_matches!(prog[0], SaveCaptureGroupStart(0));
        assert_absent_repeater_insn(&prog[1], "abc", None);
        assert_matches!(prog[2], Save(1));
        assert_matches!(prog[3], End);
    }

    #[test]
    fn absent_repeater_with_hard_inner_compiles() {
        // A hard absent repeater (?~\1) expands to (?((?!\1))\O|)*
        // Use explicit_capture_group_0=false so that (\w) = group 1 and \1 refers to it
        let prog = compile_prog_no_explicit_group0(r"(\w)(?~\1)");

        assert_eq!(prog.len(), 20, "prog: {:?}", prog);

        assert_matches!(prog[0], SplitUnanchored(3, 1));
        assert_matches!(prog[1], Any);
        assert_matches!(prog[2], Jmp(0));
        assert_matches!(prog[3], Save(0));
        assert_matches!(prog[4], SaveCaptureGroupStart(1));
        assert_char_class_insn(&prog[5], r"\w");
        assert_matches!(prog[6], Save(3));
        assert_matches!(prog[7], Save0(4));
        assert_matches!(
            prog[8],
            RepeatEpsilonGr {
                lo: 0,
                next: 18,
                repeat: 4,
                check: 5
            }
        );
        assert_matches!(prog[9], BeginAtomic);
        assert_matches!(prog[10], Split(11, 17));
        assert_matches!(prog[11], Split(12, 14));
        assert_matches!(
            prog[12],
            Backref {
                slot: 2,
                casei: false,
                unicode: true,
            }
        );
        assert_matches!(prog[13], FailNegativeLookAround);
        assert_matches!(prog[14], EndAtomic);
        assert_matches!(prog[15], Any);
        assert_matches!(prog[16], Jmp(17));
        assert_matches!(prog[17], Jmp(8));
        assert_matches!(prog[18], Save(1));
        assert_matches!(prog[19], End);
    }

    #[test]
    fn absent_repeater_nested_absent_error() {
        // Nested absent operators are not yet supported (direct child)
        let tree = Expr::parse_tree(r"(?~(?~abc))").unwrap();
        let info = analyze(&tree, AnalyzeContext::default()).unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );

        // Nested absent operators are not yet supported (indirect descendant)
        let tree = Expr::parse_tree(r"(?~a(?<=b(?~c)))").unwrap();
        let info = analyze(&tree, AnalyzeContext::default()).unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );
    }

    #[test]
    fn absent_operators_error() {
        // Test that absent expression returns feature not supported
        let tree = Expr::parse_tree(r"(?~|abc|\d*)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );

        // Test that absent stopper returns feature not supported
        let tree = Expr::parse_tree(r"(?~|abc)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );

        // Test that range clear returns feature not supported
        let tree = Expr::parse_tree(r"(?~|)").unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_compile_error(
            compile(
                &info,
                CompileOptions {
                    anchored: true,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            ),
            |e| matches!(e, CompileError::FeatureNotYetSupported(_)),
        );
    }

    #[test]
    fn single_delegate_with_capture_groups_can_be_compiled() {
        let prog = compile_prog(r"(.(b)([^a]+))c");

        assert_eq!(prog.len(), 2, "prog: {:?}", prog);
        assert_delegate_insn(&prog[0], "(.(b)([^a]+))c", Some(CaptureGroupRange(0, 3)));
        assert_matches!(prog[1], End);
    }

    #[test]
    fn delegated_capture_groups_can_be_compiled() {
        let prog = compile_prog(r"(.(b)([^a]+)(?!c)(\w))");

        assert_eq!(prog.len(), 12, "prog: {:?}", prog);

        assert_matches!(prog[0], SaveCaptureGroupStart(0));
        assert_delegate_insn(&prog[1], ".(b)", Some(CaptureGroupRange(1, 2)));
        assert_matches!(prog[2], SaveCaptureGroupStart(2));
        assert_char_class_insn(&prog[3], "[^a]");
        assert_matches!(prog[4], Split(3, 5));
        assert_matches!(prog[5], Save(5));
        assert_matches!(prog[6], Split(7, 9));
        assert_matches!(prog[7], Lit(ref l) if l == "c");
        assert_matches!(prog[8], FailNegativeLookAround);
        assert_delegate_insn(&prog[9], r"(\w)", Some(CaptureGroupRange(3, 4)));
        assert_matches!(prog[10], Save(1));
        assert_matches!(prog[11], End);
    }

    #[test]
    fn subroutine_call_can_be_compiled() {
        let prog = compile_prog(r"((.)\g<1>)");

        assert_eq!(prog.len(), 7, "prog: {:?}", prog);

        assert_matches!(prog[0], SaveCaptureGroupStart(0));
        assert_delegate_insn(&prog[1], "(.)", Some(CaptureGroupRange(1, 2)));
        assert_matches!(prog[2], SaveCaptureGroupStart(1));
        assert_matches!(prog[3], AnyNoNL);
        assert_matches!(prog[4], Save(3));
        assert_matches!(prog[5], Save(1));
        assert_matches!(prog[6], End);
    }

    #[test]
    fn forward_reference_subroutine_call_can_be_compiled() {
        let prog = compile_prog(r"(\g<1>(.))");

        assert_eq!(prog.len(), 7, "prog: {:?}", prog);

        assert_matches!(prog[0], SaveCaptureGroupStart(0));
        assert_matches!(prog[1], SaveCaptureGroupStart(1));
        assert_matches!(prog[2], AnyNoNL);
        assert_matches!(prog[3], Save(3));
        assert_delegate_insn(&prog[4], "(.)", Some(CaptureGroupRange(1, 2)));
        assert_matches!(prog[5], Save(1));
        assert_matches!(prog[6], End);
    }

    /// Compile a pattern with an explicit `max_prog_size` cap. Returns the
    /// resulting program, or the compile error if the cap was exceeded.
    fn compile_prog_with_cap(re: &str, cap: usize) -> Result<Vec<Insn>> {
        let tree = Expr::parse_tree(re).unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: false,
                ..Default::default()
            },
        )?;
        compile(
            &info,
            CompileOptions {
                anchored: true,
                contains_subroutines: tree.contains_subroutines,
                max_prog_size: Some(cap),
                ..CompileOptions::default()
            },
        )
        .map(|p| p.body)
    }

    #[test]
    fn max_prog_size_caps_recursive_subroutine_expansion() {
        // `(a|b\g<1>\g<1>)` is not provably non-terminating (the `a` branch
        // terminates), so analysis passes and the recursive expansion is
        // materialized at compile time. A tiny cap rejects it with a typed
        // error instead of letting it grow without bound.
        let err = compile_prog_with_cap(r"(a|b\g<1>\g<1>)", 4).unwrap_err();
        assert!(
            matches!(
                err,
                Error::CompileError(ref e) if matches!(e.as_ref(), CompileError::PatternTooComplex)
            ),
            "expected PatternTooComplex, got: {:?}",
            err
        );
    }

    #[test]
    fn max_prog_size_disabled_by_default_allows_recursive_pattern() {
        // With no cap (the default), the recursive pattern compiles as before.
        let prog = compile_prog(r"(a|b\g<0>\g<0>)");
        assert!(prog.iter().any(|insn| matches!(insn, Insn::End)));
    }

    #[test]
    fn max_prog_size_allows_smaller_programs() {
        let prog = compile_prog_with_cap(r"abc", 5).unwrap();
        assert_eq!(prog.len(), 4, "prog: {:?}", prog);
        assert_matches!(prog[0], Save(0));
        assert_matches!(prog[1], Insn::Lit(ref l) if l == "abc");
        assert_matches!(prog[2], Save(1));
        assert_matches!(prog[3], Insn::End);
    }

    #[test]
    fn max_prog_size_disallows_larger_programs() {
        let err = compile_prog_with_cap(r"abc", 3).unwrap_err();
        assert!(
            matches!(
                err,
                Error::CompileError(ref e) if matches!(e.as_ref(), CompileError::PatternTooComplex)
            ),
            "expected PatternTooComplex, got: {:?}",
            err
        );
    }

    #[test]
    fn define_group_is_a_no_op() {
        // A DEFINE block is not hard but produces an empty delegate regex,
        // so compilation skips it entirely — only the End instruction remains.
        let prog = compile_prog(r"(?(DEFINE)(?<word>\w+))");

        assert_eq!(prog.len(), 1, "prog: {:?}", prog);
        assert_matches!(prog[0], End);
    }

    #[test]
    fn continue_from_prev_match_inside_variable_lookbehind_alt_can_be_compiled() {
        let prog = compile_prog(r"(?<=\G|\s)\d");

        assert_eq!(prog.len(), 12, "prog: {:?}", prog);

        assert_matches!(prog[0], Split(1, 6));
        assert_matches!(prog[1], Save(0));
        assert_matches!(prog[2], GoBack(0));
        assert_matches!(prog[3], ContinueFromPreviousMatchEnd { at_start: false });
        assert_matches!(prog[4], Restore(0));
        assert_matches!(prog[5], Jmp(10));
        assert_matches!(prog[6], Save(1));
        assert_matches!(prog[7], GoBack(1));
        assert_char_class_insn(&prog[8], r"\s");
        assert_matches!(prog[9], Restore(1));
        assert_char_class_insn(&prog[10], r"\d");
        assert_matches!(prog[11], End);
    }

    #[test]
    fn continue_from_prev_match_inside_alt_can_be_compiled() {
        let prog = compile_prog(r"(?:\G|\s)\d");

        assert_eq!(prog.len(), 6, "prog: {:?}", prog);

        assert_matches!(prog[0], Split(1, 3));
        assert_matches!(prog[1], ContinueFromPreviousMatchEnd { at_start: false });
        assert_matches!(prog[2], Jmp(4));
        assert_char_class_insn(&prog[3], r"\s");
        assert_char_class_insn(&prog[4], r"\d");
        assert_matches!(prog[5], End);
    }

    #[test]
    fn continue_from_prev_match_first_instruction_can_be_compiled() {
        let prog = compile_prog(r"\G\s\d");

        assert_eq!(prog.len(), 3, "prog: {:?}", prog);

        assert_matches!(prog[0], ContinueFromPreviousMatchEnd { at_start: true });
        assert_delegate_insn(&prog[1], r"\s\d", None);
        assert_matches!(prog[2], End);

        let prog = compile_prog(r"^\G\s\d");

        assert_eq!(prog.len(), 4, "prog: {:?}", prog);

        assert_matches!(prog[1], ContinueFromPreviousMatchEnd { at_start: true });
        assert_delegate_insn(&prog[2], r"\s\d", None);
        assert_matches!(prog[3], End);
    }

    #[test]
    fn word_boundary_outside_lookbehind_delegates_with_alternation() {
        // Word boundaries are const-size and regex-automata supports them, so
        // `\b(?:alt)\b` compiles to a single delegated engine regardless of
        // how many alternatives it has. Previously the whole alternation was
        // compiled branch by branch on the VM, growing linearly with branch
        // count (e.g. MATLAB's 6741-instruction `begin` regex).
        let prog = compile_prog(r"\b(?:abs|accumarray|acos|abcdef|ghijklmnop)\b");
        assert_eq!(prog.len(), 2, "prog: {:?}", prog);
        assert_delegate_insn(
            &prog[0],
            r"\b(?:abs|accumarray|acos|abcdef|ghijklmnop)\b",
            None,
        );
        assert_matches!(prog[1], End);
    }

    #[test]
    fn word_boundary_inside_lookbehind_stays_on_vm() {
        // regex-automata's reverse DFA can't run a variable-length lookbehind,
        // so the alternation *inside* a lookbehind is compiled branch by
        // branch on the VM (GoBack + Split + per-branch literals), even though
        // the word boundaries around it would otherwise delegate.
        let prog = compile_prog(r"\b(?<=\b(?:foo|bar)\b)\b");
        assert!(
            prog.iter().any(|insn| matches!(insn, GoBack(_))),
            "lookbehind should be compiled on the VM: {:?}",
            prog
        );
        assert!(
            prog.iter().any(|insn| matches!(insn, Lit(s) if s == "foo")),
            "lookbehind alternation should be compiled branch by branch: {:?}",
            prog
        );
    }

    #[test]
    fn continue_from_prev_match_not_first_instruction_can_be_compiled() {
        let prog = compile_prog(r"\w\G\s\d");

        assert_eq!(prog.len(), 4, "prog: {:?}", prog);

        assert_char_class_insn(&prog[0], r"\w");
        assert_matches!(prog[1], ContinueFromPreviousMatchEnd { at_start: false });
        assert_delegate_insn(&prog[2], r"\s\d", None);
        assert_matches!(prog[3], End);
    }

    #[test]
    fn word_boundary_delegates_as_single_engine_with_backref() {
        // A backref makes the pattern hard, but the word boundaries are still
        // const-size and easy. The trailing `\b` is folded into a delegated
        // engine; the leading `\b` is hard context (the backref follows it),
        // so it is emitted as a native VM assertion.
        let prog = compile_prog_no_explicit_group0(r"(\w)\b\1\b");
        assert_eq!(prog.len(), 12, "prog: {:?}", prog);
        assert_matches!(prog[0], SplitUnanchored(3, 1));
        assert_matches!(prog[3], Save(0));
        assert_matches!(prog[4], SaveCaptureGroupStart(1));
        assert_matches!(prog[5], CharClass(_));
        assert_matches!(prog[6], Save(3));
        assert_matches!(prog[7], Insn::Assertion(crate::Assertion::WordBoundary));
        assert_matches!(
            prog[8],
            Backref {
                slot: 2,
                casei: false,
                unicode: true
            }
        );
        assert_delegate_insn(&prog[9], r"\b", None);
        assert_matches!(prog[10], Save(1));
        assert_matches!(prog[11], End);
    }

    #[test]
    fn word_boundary_delegates_inside_hard_concat_with_hard_middle() {
        // The hard context flows right-to-left, so the trailing `\b` is part
        // of the delegated suffix. The leading `\b` is easy context (it is
        // the leftmost element) and is also delegated; the hard backref in
        // the middle is compiled on the VM.
        let prog = compile_prog_no_explicit_group0(r"\b(\w)\1\b");
        assert_eq!(prog.len(), 12, "prog: {:?}", prog);
        assert_matches!(prog[0], SplitUnanchored(3, 1));
        assert_matches!(prog[1], Any);
        assert_matches!(prog[2], Jmp(0));
        assert_matches!(prog[3], Save(0));
        assert_delegate_insn(&prog[4], r"\b", None);
        assert_matches!(prog[5], SaveCaptureGroupStart(1));
        assert_matches!(prog[6], CharClass(_));
        assert_matches!(prog[7], Save(3));
        assert_matches!(
            prog[8],
            Backref {
                slot: 2,
                casei: false,
                unicode: true
            }
        );
        assert_delegate_insn(&prog[9], r"\b", None);
        assert_matches!(prog[10], Save(1));
        assert_matches!(prog[11], End);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn word_boundary_inside_lookbehind_compiled_on_vm_with_backref() {
        // A backref makes the lookbehind hard; the word boundary inside it
        // stays hard too, so the whole lookbehind is compiled on the VM with
        // the HardVariableLookbehind reverse-DFA seek path.
        let prog = compile_prog_no_explicit_group0(r"(?<=(a+)\1)b");
        assert!(
            prog.iter()
                .any(|i| matches!(i, Insn::HardVariableLookbehind(_))),
            "expected HardVariableLookbehind: {:?}",
            prog
        );
        assert!(
            prog.iter()
                .any(|i| matches!(i, Insn::ReverseLookbehindPosCheck { .. })),
            "lookbehind should use ReverseLookbehindPosCheck: {:?}",
            prog
        );
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn word_boundary_inside_easy_variable_lookbehind_stays_delegated() {
        // Without a backref, a variable-length lookbehind delegates to a
        // reverse DFA. Word boundaries inside it are still hard, but the
        // reverse DFA honours `unicode_word_boundary`, so the lookbehind
        // compiles as a single BackwardsDelegate carrying the `\b` assertions.
        let prog = compile_prog(r"(?<=\b\w+\b)x");
        assert_eq!(prog.len(), 7, "prog: {:?}", prog);
        assert_matches!(prog[0], Save(0));
        assert_matches!(prog[1], Insn::Assertion(crate::Assertion::WordBoundary));
        assert_matches!(
            &prog[2],
            BackwardsDelegate(ReverseBackwardsDelegate { pattern, .. }) if pattern == "\\w+"
        );
        assert_matches!(prog[3], Insn::Assertion(crate::Assertion::WordBoundary));
        assert_matches!(prog[4], Restore(0));
        assert_matches!(prog[5], Lit(ref l) if l == "x");
        assert_matches!(prog[6], End);
    }

    #[test]
    fn word_boundary_in_negated_lookahead_delegates() {
        // A negative lookahead makes the pattern hard, but the word boundary
        // inside it is easy and is folded into the delegated negative
        // lookahead rather than compiled as a separate VM assertion. The
        // trailing `bar` is a single literal, so it stays a native Lit.
        let prog = compile_prog(r"\b(?!\bfoo\b)bar");
        assert_eq!(prog.len(), 6, "prog: {:?}", prog);
        assert_delegate_insn(&prog[0], r"\b", None);
        assert_matches!(prog[1], Split(2, 4));
        assert_delegate_insn(&prog[2], r"\bfoo\b", None);
        assert_matches!(prog[3], FailNegativeLookAround);
        assert_matches!(prog[4], Lit(ref l) if l == "bar");
        assert_matches!(prog[5], End);
    }

    #[test]
    fn word_boundary_unicode_and_ascii_modes_match() {
        // `\b` delegates in both Unicode and ASCII bytes mode, and both must
        // agree on where word boundaries fall. The ASCII variant is the
        // regex-automata `WordAscii` look; the Unicode variant is `WordUnicode`.
        for (re, text, expect) in [
            (r"\bat\b", "at", true),
            (r"\bat\b", "cat", false),
            (r"\bat\b", "at ", true),
            (r"\bat\b", " at", true),
            (r"\bat\b", "x at y", true),
            (r"\bat\b", " bat ", false),
            (r"\b\d+\b", "42", true),
            (r"\b\d+\b", "x42y", false),
            (r"\b\d+\b", "x 42 y", true),
        ] {
            let str_re = regex::Regex::new(re).unwrap();
            assert_eq!(str_re.is_match(text), expect, "str {re} on {text:?}");
            let ascii_re = RegexBuilder::new(re)
                .bytes_mode(BytesMode::Ascii)
                .build()
                .unwrap();
            assert_eq!(
                ascii_re.is_match(text.as_bytes()).unwrap(),
                expect,
                "ascii {re} on {text:?}"
            );
        }
    }

    #[test]
    fn word_boundary_at_start_and_end_of_haystack() {
        // Word boundaries at the very start and end of the haystack must match
        // when the adjacent character is a word char and fail otherwise.
        let re = Regex::new(r"\bat\b").unwrap();
        assert!(re.is_match("at").unwrap());
        assert!(re.is_match("at ").unwrap());
        assert!(re.is_match(" at").unwrap());
        assert!(re.find("x at y").unwrap().is_some());
        assert!(!re.find("cat").unwrap().is_some());
    }

    #[test]
    fn word_boundary_not_boundary_delegates() {
        // `\B` is the negated word boundary; it is also const-size and easy
        // outside a lookbehind, so it delegates.
        let prog = compile_prog(r"\Bfoo\B");
        assert_eq!(prog.len(), 2, "prog: {:?}", prog);
        assert_delegate_insn(&prog[0], r"\Bfoo\B", None);
        assert_matches!(prog[1], End);
    }

    #[test]
    fn continue_from_prev_match_inside_conditional_can_be_compiled() {
        let prog = compile_prog(r"(?(\G)\s\d|$)");

        assert_eq!(prog.len(), 8, "prog: {:?}", prog);

        assert_matches!(prog[0], BeginAtomic);
        assert_matches!(prog[1], Split(2, 6));
        assert_matches!(prog[2], ContinueFromPreviousMatchEnd { at_start: false });
        assert_matches!(prog[3], EndAtomic);
        assert_delegate_insn(&prog[4], r"\s\d", None);
        assert_matches!(prog[5], Jmp(7));
        assert_delegate_insn(&prog[6], r"$", None);
        assert_matches!(prog[7], End);
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn compile_many_alts_inside_lookbehind_as_variable_lookbehind() {
        let prog = compile_prog("(?<=hello|world|foo|bar)");
        assert_eq!(prog.len(), 4, "prog: {:?}", prog);
        assert_matches!(prog[0], Save(0));
        assert_matches!(&prog[1], BackwardsDelegate(ReverseBackwardsDelegate { pattern, capture_groups: None, .. }) if pattern == "(?:hello|world|foo|bar)");
        assert_matches!(prog[2], Restore(0));
        assert_matches!(prog[3], End);
    }

    #[test]
    fn compile_non_const_size_alts_inside_lookbehind() {
        let prog = compile_prog("(?<=hello|world|foo)");
        assert_eq!(prog.len(), 17, "prog: {:?}", prog);
        assert_matches!(prog[0], Split(1, 6));
        assert_matches!(prog[1], Save(0));
        assert_matches!(prog[2], GoBack(5));
        assert_matches!(prog[3], Lit(ref l) if l == "hello");
        assert_matches!(prog[4], Restore(0));
        assert_matches!(prog[5], Jmp(16));
        assert_matches!(prog[6], Split(7, 12));
        assert_matches!(prog[7], Save(1));
        assert_matches!(prog[8], GoBack(5));
        assert_matches!(prog[9], Lit(ref l) if l == "world");
        assert_matches!(prog[10], Restore(1));
        assert_matches!(prog[11], Jmp(16));
        assert_matches!(prog[12], Save(2));
        assert_matches!(prog[13], GoBack(3));
        assert_matches!(prog[14], Lit(ref l) if l == "foo");
        assert_matches!(prog[15], Restore(2));
        assert_matches!(prog[16], End);
    }

    #[track_caller]
    fn compile_prog(re: &str) -> Vec<Insn> {
        compile_prog_with(
            re,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
            |info, tree| {
                compile(
                    info,
                    CompileOptions {
                        anchored: true,
                        contains_subroutines: tree.contains_subroutines,
                        ..CompileOptions::default()
                    },
                )
                .unwrap()
                .body
            },
        )
    }

    #[track_caller]
    fn compile_prog_no_explicit_group0(re: &str) -> Vec<Insn> {
        compile_prog_with(re, AnalyzeContext::default(), |info, tree| {
            compile(
                info,
                CompileOptions {
                    anchored: false,
                    contains_subroutines: tree.contains_subroutines,
                    ..CompileOptions::default()
                },
            )
            .unwrap()
            .body
        })
    }

    #[track_caller]
    fn compile_prog_forced_hard(re: &str) -> Vec<Insn> {
        compile_prog_with(
            re,
            AnalyzeContext {
                explicit_capture_group_0: true,
                ..Default::default()
            },
            |info, tree| {
                info.hard = true;
                compile(
                    info,
                    CompileOptions {
                        anchored: true,
                        contains_subroutines: tree.contains_subroutines,
                        ..CompileOptions::default()
                    },
                )
                .unwrap()
                .body
            },
        )
    }

    #[track_caller]
    fn compile_prog_ascii(re: &str) -> Vec<Insn> {
        let tree = Expr::parse_tree_with_flags(re, 0).unwrap();
        let info = analyze(
            &tree,
            AnalyzeContext {
                ..Default::default()
            },
        )
        .unwrap();
        compile(
            &info,
            CompileOptions {
                anchored: true,
                contains_subroutines: tree.contains_subroutines,
                bytes_mode: BytesMode::Ascii,
                ..CompileOptions::default()
            },
        )
        .unwrap()
        .body
    }

    #[track_caller]
    fn compile_prog_with<F>(re: &str, analyze_context: AnalyzeContext, compile_fn: F) -> Vec<Insn>
    where
        F: FnOnce(&mut Info<'_>, &ExprTree) -> Vec<Insn>,
    {
        let tree = Expr::parse_tree(re).unwrap();
        let mut info = analyze(&tree, analyze_context).unwrap();
        compile_fn(&mut info, &tree)
    }

    fn assert_delegate_insn(insn: &Insn, re: &str, captures: Option<CaptureGroupRange>) {
        match insn {
            Insn::Delegate(delegate) => assert_delegate(delegate, re, captures),
            _ => {
                panic!("Expected Insn::Delegate but was {:#?}", insn);
            }
        }
    }

    /// Assert that `insn` is a native `CharClass` matching the single character
    /// class `re` (compiled with default options, matching `compile_prog`).
    fn assert_char_class_insn(insn: &Insn, re: &str) {
        let expected = try_char_class_matcher(re, &CompileOptions::default())
            .unwrap_or_else(|| panic!("test re {:?} is not a single char class", re));
        match insn {
            Insn::CharClass(matcher) => assert_eq!(
                matcher, &expected,
                "char class mismatch for {:?}: {:#?}",
                re, matcher
            ),
            _ => panic!("Expected Insn::CharClass but was {:#?}", insn),
        }
    }

    fn assert_absent_repeater_insn(insn: &Insn, re: &str, captures: Option<CaptureGroupRange>) {
        match insn {
            Insn::AbsentRepeater(delegate) => assert_delegate(delegate, re, captures),
            _ => {
                panic!("Expected Insn::AbsentRepeater but was {:#?}", insn);
            }
        }
    }

    fn assert_delegate(
        delegate: &crate::vm::Delegate,
        re: &str,
        captures: Option<CaptureGroupRange>,
    ) {
        assert_eq!(delegate.pattern, re);
        assert_eq!(captures, delegate.capture_groups);
    }
}
