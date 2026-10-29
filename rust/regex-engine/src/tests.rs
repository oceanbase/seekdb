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

fn engine(pattern: &str) -> Regex {
    compile(pattern.to_owned(), 0, 0, 0).unwrap().regex
}
fn encoded(text: &str) -> Vec<u8> {
    text.encode_utf16().flat_map(u16::to_be_bytes).collect()
}
fn replaced(regex: &Regex, text: &str, replacement: &str, start: i64, occurrence: i64) -> String {
    let bytes = replace(regex, text, replacement, start, occurrence).unwrap();
    // SAFETY: the Vec holds the complete readable buffer.
    unsafe { decode_utf16be(bytes.as_ptr(), bytes.len()) }.unwrap()
}
fn span(start: i64, end: i64) -> RegexMatch {
    RegexMatch {
        matched: 1,
        start,
        end,
    }
}
fn absent() -> RegexMatch {
    RegexMatch {
        matched: 0,
        start: -1,
        end: -1,
    }
}

#[test]
fn unicode_capture_offsets_are_utf16_units() {
    let regex = engine(r"(?<=😀)(é)(x)?");
    assert_eq!(find(&regex, "a😀é中", 3, 1, 0).unwrap(), span(3, 4));
    assert_eq!(find(&regex, "a😀é中", 3, 1, 1).unwrap(), span(3, 4));
    assert_eq!(find(&regex, "a😀é中", 3, 1, 2).unwrap(), absent());
    assert!(find(&regex, "a😀é中", 3, 1, 3).is_err());
    assert!(find(&regex, "a😀é中", 2, 1, 0).is_err());
}

#[test]
fn capture_bounds_are_checked_after_the_selected_match_exists() {
    let regex = engine("a");
    assert_eq!(find(&regex, "b", 0, 1, 999).unwrap(), absent());
    assert_eq!(find(&regex, "a", 0, 2, 999).unwrap(), absent());
    assert_eq!(find(&regex, "a", 0, 1, 999).err().unwrap().kind, ARGUMENT);
    assert_eq!(find(&regex, "b", 0, 1, -1).err().unwrap().kind, ARGUMENT);
}

#[test]
fn byte_values_mapped_to_utf16_round_trip() {
    let text: String = [0, 127, 128, 255].into_iter().map(char::from).collect();
    let bytes = encoded(&text);
    assert_eq!(bytes, [0, 0, 0, 127, 0, 128, 0, 255]);
    assert_eq!(
        unsafe { decode_utf16be(bytes.as_ptr(), bytes.len()) }.unwrap(),
        text
    );
    assert_eq!(
        find(&engine("\u{80}ÿ"), &text, 0, 1, 0).unwrap(),
        span(2, 4)
    );
    assert_eq!(
        replaced(&engine("ÿ"), &text, "\0", 0, 0),
        "\0\u{7f}\u{80}\0"
    );
}

#[test]
fn start_preserves_lookbehind_and_anchor_context() {
    assert_eq!(
        find(&engine("(?<=a)b"), "ab ab", 1, 2, 0).unwrap(),
        span(4, 5)
    );
    assert_eq!(find(&engine("^b"), "ab", 1, 1, 0).unwrap(), absent());
    assert_eq!(find(&engine(r"\bb"), "ab", 1, 1, 0).unwrap(), absent());
    assert_eq!(
        replaced(&engine("(?<=a)b"), "ab ab ab", "X", 1, 2),
        "ab aX ab"
    );
}

#[test]
fn zero_length_searches_advance_by_unicode_scalar() {
    let regex = engine("");
    assert_eq!(find(&regex, "😀x", 0, 1, 0).unwrap(), span(0, 0));
    assert_eq!(find(&regex, "😀x", 0, 2, 0).unwrap(), span(2, 2));
    assert_eq!(find(&regex, "😀x", 0, 3, 0).unwrap(), span(3, 3));
    assert_eq!(find(&regex, "😀x", 0, 4, 0).unwrap(), absent());
    assert_eq!(replaced(&regex, "😀x", "_", 0, 0), "_😀_x_");
    assert_eq!(find(&engine("a*"), "a", 0, 2, 0).unwrap(), span(1, 1));
    assert_eq!(replaced(&engine("a*"), "a", "X", 0, 0), "XX");
}

#[test]
fn replacement_uses_icu_numeric_named_and_quoted_syntax() {
    let regex = engine("(?<word>a)(b)?");
    assert_eq!(
        replaced(&regex, "a ab", "<$0:$1:$2:${word}>", 0, 0),
        "<a:a::a> <ab:a:b:a>"
    );
    assert_eq!(replaced(&regex, "a", "$10", 0, 0), "a0");
    assert_eq!(replaced(&regex, "a", "$01", 0, 0), "a");
    assert_eq!(replaced(&regex, "a", r"\$1\\$1", 0, 0), "$1\\a");
    assert_eq!(replaced(&regex, "中a文", "😀", 0, 0), "中😀文");
}

#[test]
fn replacement_errors_and_nonmatching_input() {
    let regex = engine("(a)");
    for replacement in ["$2", "${missing}", "${name", "${1}", "$", "$a"] {
        assert!(
            replace(&regex, "a", replacement, 0, 0).is_err(),
            "{replacement}"
        );
    }
    assert_eq!(replaced(&regex, "b", "$bad", 0, 0), "b");
    assert_eq!(replaced(&regex, "a", "$bad", 0, 2), "a");
    assert_eq!(replaced(&regex, "a", "\\", 0, 0), "");
    assert_eq!(replaced(&regex, "a", "b\\", 0, 0), "b");
}

#[test]
fn replacement_unicode_escapes_retain_icu_behavior() {
    let regex = engine("a");
    for (replacement, expected) in [
        (r"\u0041", "A"),
        (r"\U0001F600", "😀"),
        (r"\uD83D\uDE00", "😀"),
        (r"\u00ZZ", "u00ZZ"),
        (r"\u004", "u004"),
        (r"\x41", "x41"),
        (r"\x{41}", "x{41}"),
        (r"\n", "n"),
    ] {
        assert_eq!(
            replaced(&regex, "a", replacement, 0, 0),
            expected,
            "{replacement}"
        );
    }
    assert_eq!(replace(&regex, "a", r"\uD800", 0, 0).unwrap(), [0xd8, 0]);
}

#[test]
fn backward_keepout_matches_return_errors_without_panicking() {
    for (pattern, text) in [
        (r"(?<=\K..)b", "aabb"),
        (r"(?<=\K.*)b", "aabbb"),
        (r"(?<=\Ka)", "a"),
    ] {
        let regex = engine(pattern);
        assert_eq!(find(&regex, text, 0, 2, 0).err().unwrap().kind, GENERIC);
        assert_eq!(
            replace(&regex, text, "X", 0, 0).err().unwrap().kind,
            GENERIC
        );
    }
    // Captures in a normal lookbehind may precede the whole match and remain
    // valid for extraction and replacement; do not reject them as overlapping.
    let regex = engine(r"(?<=(a))b");
    assert_eq!(find(&regex, "ab", 1, 1, 1).unwrap(), span(0, 1));
    assert_eq!(replaced(&regex, "ab", "$1", 1, 0), "aa");
}

#[test]
fn invalid_capture_slices_return_errors() {
    for (start, end) in [(2, 1), (0, 9), (1, 2), (0, usize::MAX)] {
        assert_eq!(checked_slice("😀", start, end).err().unwrap().kind, GENERIC);
    }
    assert_eq!(checked_slice("😀", 0, 4).unwrap(), "😀");
    assert!(byte_to_utf16("😀", 1).is_err());
}

#[test]
fn occurrence_and_prefix_are_preserved() {
    let regex = engine("a");
    assert_eq!(replaced(&regex, "aaaa", "x", 1, 0), "axxx");
    assert_eq!(replaced(&regex, "aaaa", "x", 1, 2), "aaxa");
    assert_eq!(replaced(&regex, "aaaa", "x", 1, 9), "aaaa");
    assert_eq!(find(&regex, "a", 1, 1, 0).unwrap(), absent());
    for (start, occurrence, group) in [(-1, 1, 0), (2, 1, 0), (0, 0, 0), (0, 1, -1)] {
        assert!(find(&regex, "a", start, occurrence, group).is_err());
    }
    assert!(replace(&regex, "a", "x", 0, -1).is_err());
}

#[test]
fn dot_crlf_and_unix_modes_work_in_delegated_and_vm_engines() {
    for pattern in [".", "(?=.)[^x]"] {
        let default = compile(pattern.to_owned(), 0, 0, 0).unwrap();
        let unix = compile(pattern.to_owned(), UNIX_LINES, 0, 0).unwrap();
        let dotall = compile(pattern.to_owned(), DOTALL, 0, 0).unwrap();
        assert!(!default.regex.is_match("\r").unwrap(), "{pattern}");
        assert!(unix.regex.is_match("\r").unwrap(), "{pattern}");
        assert!(dotall.regex.is_match("\r").unwrap(), "{pattern}");
        assert!(!unix.regex.is_match("\n").unwrap(), "{pattern}");
        assert!(dotall.regex.is_match("\n").unwrap(), "{pattern}");
    }
}

#[test]
fn multiline_crlf_and_case_flags() {
    for pattern in ["^b$", "(?=b)^b$"] {
        let regex = compile(pattern.to_owned(), MULTILINE, 0, 0).unwrap();
        assert!(regex.regex.is_match("a\r\nb\r\nc").unwrap());
        assert!(regex.regex.is_match("a\rb\rc").unwrap());
        let unix = compile(pattern.to_owned(), MULTILINE | UNIX_LINES, 0, 0).unwrap();
        assert!(!unix.regex.is_match("a\r\nb\r\nc").unwrap());
        assert!(unix.regex.is_match("a\nb\nc").unwrap());
    }
    let regex = compile("é".to_owned(), CASE_INSENSITIVE, 0, 0).unwrap();
    assert!(regex.regex.is_match("É").unwrap());
}

#[test]
fn advanced_fancy_features_are_supported() {
    assert!(engine(r"^(\w+)\s+\1$").is_match("foo foo").unwrap());
    assert_eq!(
        find(&engine(r"\d+(?=元)"), "100元", 0, 1, 0).unwrap(),
        span(0, 3)
    );
    assert!(!engine("a++a").is_match("aa").unwrap());
    assert!(!engine("(?>a+)a").is_match("aa").unwrap());
}

#[test]
fn known_unicode_and_syntax_differences_are_explicit() {
    assert!(!engine("(?i)^ss$").is_match("ß").unwrap());
    assert!(!engine("[[:alpha:]]+").is_match("中文").unwrap());
    assert!(!engine(r"\h+").is_match("\t").unwrap());
    assert!(engine(r"\h+").is_match("0AF").unwrap());
    assert!(compile(r"\X".to_owned(), 0, 0, 0).is_err());
    assert!(engine(".").is_match("\u{2028}").unwrap());
}

#[test]
fn resource_mapping_is_bounded_and_checked() {
    assert_eq!(limits(0, 0).unwrap(), (8 * 1024 * 1024, 1_000_000));
    assert_eq!(limits(1, 1).unwrap(), (1, 10_000));
    assert_eq!(
        limits(i64::MAX, i64::MAX).unwrap(),
        (64 * 1024 * 1024, 100_000_000)
    );
    assert!(limits(-1, 0).is_err());
    assert!(limits(0, -1).is_err());
    let error = compile("a{1000000}".to_owned(), 0, 64, 0).err().unwrap();
    assert_eq!(error.kind, LIMIT);
    assert!(compile("a".to_owned(), 0x10, 0, 0).is_err());
}

#[test]
fn backtracking_limit_errors_propagate_from_find_and_replace() {
    let regex = compile(r"^(a|aa)+\1$".to_owned(), 0, 0, 1).unwrap();
    let text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaX";
    assert_eq!(find(&regex.regex, text, 0, 1, 0).err().unwrap().kind, LIMIT);
    assert_eq!(
        replace(&regex.regex, text, "x", 0, 0).err().unwrap().kind,
        LIMIT
    );
}

#[test]
fn invalid_utf16_and_null_buffers_are_rejected() {
    unsafe {
        assert_eq!(
            decode_utf16be([0].as_ptr(), 1).err().unwrap().kind,
            INVALID_ENCODING
        );
        assert_eq!(
            decode_utf16be([0xd8, 0].as_ptr(), 2).err().unwrap().kind,
            INVALID_ENCODING
        );
        assert_eq!(decode_utf16be(ptr::null(), 2).err().unwrap().kind, ARGUMENT);
        assert_eq!(decode_utf16be(ptr::null(), 0).unwrap(), "");
        assert_eq!(
            decode_utf16be(ptr::null(), usize::MAX).err().unwrap().kind,
            LIMIT
        );
    }
}

#[test]
fn ffi_round_trip_owns_and_frees_outputs() {
    unsafe {
        let pattern = encoded("(中)");
        let mut regex = ptr::null_mut();
        let mut error = RegexError {
            kind: 99,
            message: [1; 256],
        };
        assert_eq!(
            regex_compile(
                pattern.as_ptr(),
                pattern.len(),
                0,
                0,
                0,
                &mut regex,
                &mut error
            ),
            0
        );
        assert_eq!(error.kind, 0);
        assert_eq!(error.message[0], 0);
        let text = encoded("😀中文");
        let mut found = absent();
        assert_eq!(
            regex_find(
                regex,
                text.as_ptr(),
                text.len(),
                0,
                1,
                1,
                &mut found,
                &mut error
            ),
            0
        );
        assert_eq!(found, span(2, 3));
        let replacement = encoded("<$1>");
        let mut buffer = RegexBuffer {
            data: ptr::null_mut(),
            len: 0,
        };
        assert_eq!(
            regex_replace(
                regex,
                text.as_ptr(),
                text.len(),
                replacement.as_ptr(),
                replacement.len(),
                0,
                0,
                &mut buffer,
                &mut error
            ),
            0
        );
        assert_eq!(decode_utf16be(buffer.data, buffer.len).unwrap(), "😀<中>文");
        regex_buffer_free(&mut buffer);
        assert!(buffer.data.is_null());
        assert_eq!(buffer.len, 0);
        regex_buffer_free(&mut buffer);
        regex_free(regex);
        regex_free(ptr::null_mut());
        regex_buffer_free(ptr::null_mut());
    }
}

#[test]
fn ffi_error_outputs_are_reset_and_never_unwind() {
    unsafe {
        let mut regex = ptr::null_mut();
        let mut error = RegexError {
            kind: 99,
            message: [1; 256],
        };
        assert_eq!(
            regex_compile(ptr::null(), 2, 0, 0, 0, &mut regex, &mut error),
            1
        );
        assert!(regex.is_null());
        assert_eq!(error.kind, ARGUMENT);
        assert!(error.message.contains(&0));
        assert_eq!(
            regex_compile(
                ptr::null(),
                MAX_PATTERN_BYTES + 1,
                0,
                0,
                0,
                &mut regex,
                &mut error
            ),
            1
        );
        assert_eq!(error.kind, LIMIT);
        assert_eq!(
            regex_compile(ptr::null(), 0, 0, 0, 0, ptr::null_mut(), &mut error),
            1
        );
        assert_eq!(error.kind, ARGUMENT);
        assert_eq!(ffi_call(&mut error, || panic!("test unwind")), 1);
        assert_eq!(error.kind, GENERIC);
    }
}
