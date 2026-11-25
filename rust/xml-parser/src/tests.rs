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

use crate::{parse, Event, ParseError, ParseMode, ParseOptions};

#[derive(Debug, Eq, PartialEq)]
enum OwnedEvent {
    StartDocument,
    EndDocument,
    Declaration(Vec<u8>),
    Start(String, Vec<(String, Vec<u8>)>, bool),
    End(bool),
    Text(Vec<u8>),
    CData(Vec<u8>),
    Comment(Vec<u8>),
    Pi(Vec<u8>, Vec<u8>),
}

#[derive(Default)]
struct Collector(Vec<OwnedEvent>);

impl crate::EventHandler for Collector {
    type Error = ();

    fn on_event(&mut self, event: Event<'_>) -> Result<(), Self::Error> {
        self.0.push(match event {
            Event::StartDocument => OwnedEvent::StartDocument,
            Event::EndDocument => OwnedEvent::EndDocument,
            Event::Declaration(value) => OwnedEvent::Declaration(value.into_owned()),
            Event::StartElement {
                name,
                attributes,
                empty,
            } => OwnedEvent::Start(
                String::from_utf8(name.to_vec()).unwrap(),
                attributes
                    .into_iter()
                    .map(|attribute| {
                        (
                            String::from_utf8(attribute.name.to_vec()).unwrap(),
                            attribute.value.into_owned(),
                        )
                    })
                    .collect(),
                empty,
            ),
            Event::EndElement { empty, .. } => OwnedEvent::End(empty),
            Event::Text(value) => OwnedEvent::Text(value.into_owned()),
            Event::CData(value) => OwnedEvent::CData(value.to_vec()),
            Event::Comment(value) => OwnedEvent::Comment(value.to_vec()),
            Event::ProcessingInstruction { target, value } => {
                OwnedEvent::Pi(target.to_vec(), value.to_vec())
            }
        });
        Ok(())
    }
}

fn events(
    input: impl AsRef<[u8]>,
    mode: ParseMode,
    options: ParseOptions,
) -> Result<Vec<OwnedEvent>, ParseError<()>> {
    let mut collector = Collector::default();
    parse(input.as_ref(), mode, options, &mut collector)?;
    Ok(collector.0)
}

#[test]
fn parses_document_events_and_empty_elements() {
    let actual = events(
        " \n<root a='1'>text<empty/></root>",
        ParseMode::Document,
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(
        actual,
        vec![
            OwnedEvent::StartDocument,
            OwnedEvent::Start("root".into(), vec![("a".into(), b"1".to_vec())], false),
            OwnedEvent::Text(b"text".to_vec()),
            OwnedEvent::Start("empty".into(), vec![], true),
            OwnedEvent::End(true),
            OwnedEvent::End(false),
            OwnedEvent::EndDocument,
        ]
    );
}

#[test]
fn handles_entities_and_preserves_references_when_requested() {
    let input = "<r a='&lt;&amp;&#x41;&#10;'>x&lt;&#65;&amp;</r>";
    let expanded = events(input, ParseMode::Document, ParseOptions::default()).unwrap();
    assert_eq!(
        expanded[1],
        OwnedEvent::Start("r".into(), vec![("a".into(), b"<&A\n".to_vec())], false)
    );
    assert_eq!(expanded[2], OwnedEvent::Text(b"x<A&".to_vec()));

    let preserved = events(
        input,
        ParseMode::Document,
        ParseOptions {
            preserve_entity_references: true,
            ..ParseOptions::default()
        },
    )
    .unwrap();
    assert_eq!(
        preserved[1],
        OwnedEvent::Start(
            "r".into(),
            vec![("a".into(), b"&lt;&amp;A\n".to_vec())],
            false
        )
    );
    assert_eq!(preserved[2], OwnedEvent::Text(b"x&lt;&#65;&amp;".to_vec()));
}

#[test]
fn coalesces_text_and_references_through_fragment_end() {
    for (input, expanded) in [
        ("&lt;&#65;&amp;", "<A&"),
        ("prefix&#65;中&#x1F600;tail", "prefixA中😀tail"),
        ("&#10;\n&#13;", "\n\n\r"),
    ] {
        for preserve in [false, true] {
            let actual = events(
                input,
                ParseMode::Content,
                ParseOptions {
                    preserve_entity_references: preserve,
                    ..ParseOptions::default()
                },
            )
            .unwrap();
            let text = if preserve { input } else { expanded };
            assert_eq!(
                actual,
                vec![
                    OwnedEvent::StartDocument,
                    OwnedEvent::Text(text.as_bytes().to_vec()),
                    OwnedEvent::EndDocument,
                ]
            );
        }
    }
}

#[test]
fn coalesces_text_after_reader_consumes_a_bom() {
    for (content, expected) in [
        ("abc", "abc"),
        ("中&amp;😀", "中&😀"),
        ("\u{feff}abc", "\u{feff}abc"),
    ] {
        let actual = events(
            format!("\u{feff}\u{feff}{content}"),
            ParseMode::Content,
            ParseOptions::default(),
        )
        .unwrap();
        assert_eq!(actual[1], OwnedEvent::Text(expected.as_bytes().to_vec()));
    }
    let expected = events(
        "<r>中&amp;😀</r>",
        ParseMode::Document,
        ParseOptions::default(),
    )
    .unwrap();
    let mut utf16 = vec![0xff, 0xfe];
    utf16.extend("\u{feff}<r>中&amp;😀</r>".encode_utf16().flat_map(u16::to_le_bytes));
    for input in [
        "\u{feff}\u{feff}<r>中&amp;😀</r>".as_bytes().to_vec(),
        utf16,
    ] {
        assert_eq!(
            events(input, ParseMode::Document, ParseOptions::default()).unwrap(),
            expected
        );
    }
}

#[test]
fn rejects_duplicate_names_after_many_distinct_attributes() {
    let attributes = (0..512)
        .map(|index| format!(" a{index}='{index}'"))
        .collect::<String>();
    let actual = events(
        format!("<r{attributes}/>"),
        ParseMode::Document,
        ParseOptions::default(),
    )
    .unwrap();
    assert!(matches!(&actual[1], OwnedEvent::Start(_, attrs, true) if attrs.len() == 512));
    assert!(events(
        format!("<r{attributes} a511='duplicate'/>"),
        ParseMode::Document,
        ParseOptions::default(),
    )
    .is_err());
}

#[test]
fn normalizes_xml_newlines_and_attribute_spaces() {
    let actual = events(
        "<r a='x\t\ny'>a\r\nb\rc</r>",
        ParseMode::Document,
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(
        actual[1],
        OwnedEvent::Start("r".into(), vec![("a".into(), b"x  y".to_vec())], false)
    );
    assert_eq!(actual[2], OwnedEvent::Text(b"a\nb\nc".to_vec()));
}

#[test]
fn rejects_attributes_without_xml_whitespace_separators() {
    for input in [
        "<r a='1'b='2'/>",
        "<r a=\"1\"b=\"2\"/>",
        "<r a=''b=\"2\"/>",
        "<r a=\"1\"b=''/>",
        "<r a='1'b='2'></r>",
        "<r a=\"1\"b=\"2\"></r>",
        "<r xmlns:p='urn:p'p:a='2'/>",
        "<r a='1'\u{a0}b='2'/>",
    ] {
        for mode in [ParseMode::Document, ParseMode::Content] {
            for syntax_check in [false, true] {
                for preserve_entity_references in [false, true] {
                    let options = ParseOptions {
                        syntax_check,
                        preserve_entity_references,
                        ..ParseOptions::default()
                    };
                    assert!(
                        matches!(events(input, mode, options), Err(ParseError::Xml(_))),
                        "input: {input}, mode: {mode:?}, options: {options:?}"
                    );
                }
            }
        }
    }
}

#[test]
fn accepts_xml_whitespace_between_attributes_and_before_tag_end() {
    for separator in [" ", "\t", "\n", "\r", "\r\n", " \t\r\n "] {
        for ending in ["/>", "></r>"] {
            let input = format!("<r a = 'a\"&amp;b'{separator}b = \"c'&amp;d\"{separator}{ending}");
            for mode in [ParseMode::Document, ParseMode::Content] {
                let actual = events(&input, mode, ParseOptions::default()).unwrap();
                assert_eq!(
                    actual[1],
                    OwnedEvent::Start(
                        "r".into(),
                        vec![
                            ("a".into(), b"a\"&b".to_vec()),
                            ("b".into(), b"c'&d".to_vec())
                        ],
                        ending == "/>"
                    ),
                    "input: {input}, mode: {mode:?}"
                );
                assert!(events(
                    &input,
                    mode,
                    ParseOptions {
                        syntax_check: true,
                        ..ParseOptions::default()
                    }
                )
                .unwrap()
                .is_empty());
            }
        }
    }
}

#[test]
fn recover_does_not_emit_an_element_with_unseparated_attributes() {
    for mode in [ParseMode::Document, ParseMode::Content] {
        let actual = events(
            "<r a='1'b='2'/>",
            mode,
            ParseOptions {
                recover: true,
                ..ParseOptions::default()
            },
        )
        .unwrap();
        assert_eq!(actual, vec![OwnedEvent::StartDocument]);
    }
}

#[test]
fn supports_fragments_cdata_comments_and_processing_instructions() {
    let actual = events(
        " before<a/><![CDATA[x&y]]><!--c--><?pi   data ?><b/>after ",
        ParseMode::Content,
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(actual[1], OwnedEvent::Text(b" before".to_vec()));
    assert!(actual.contains(&OwnedEvent::CData(b"x&y".to_vec())));
    assert!(actual.contains(&OwnedEvent::Comment(b"c".to_vec())));
    assert!(actual.contains(&OwnedEvent::Pi(b"pi".to_vec(), b"data ".to_vec())));
}

#[test]
fn processing_instruction_targets_are_namespace_names_without_colons() {
    for target in ["a:b", ":a", "a:", "a:b:c"] {
        let input = format!("<?{target} data?><r/>");
        for mode in [ParseMode::Document, ParseMode::Content] {
            for syntax_check in [false, true] {
                assert!(matches!(
                    events(
                        &input,
                        mode,
                        ParseOptions {
                            syntax_check,
                            ..ParseOptions::default()
                        }
                    ),
                    Err(ParseError::Xml(_))
                ));
            }
        }
    }
    for target in ["pi", "a-b.c_1", "\u{4e00}a"] {
        let input = format!("<?{target} data?><r/>");
        for mode in [ParseMode::Document, ParseMode::Content] {
            let actual = events(&input, mode, ParseOptions::default()).unwrap();
            assert!(actual.contains(&OwnedEvent::Pi(
                target.as_bytes().to_vec(),
                b"data".to_vec()
            )));
        }
    }
}

#[test]
fn retains_declaration_bytes_and_ignores_encoding_for_memory_input() {
    let actual = events(
        "<?xml version='1.0' encoding='unknown' standalone='no'?><r/>",
        ParseMode::Document,
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(
        actual[1],
        OwnedEvent::Declaration(
            b"<?xml version='1.0' encoding='unknown' standalone='no'?>".to_vec()
        )
    );
}

#[test]
fn rejects_malformed_xml_declaration_fields() {
    for declaration in [
        "<?xml version='1.0'encoding='UTF-8'?>",
        "<?xml version=\"1.0\"standalone=\"yes\"?>",
        "<?xml version='1.0' encoding='UTF-8'standalone='no'?>",
        "<?xml version='1.0'\u{a0}encoding='UTF-8'?>",
        "<?xml version='1.0' bogus='x'?>",
        "<?xml version='1.0' encoding='UTF-8' bogus='x'?>",
        "<?xml version='1.0' standalone='yes' bogus='x'?>",
        "<?xml version='1.0' version='1.1'?>",
        "<?xml version='1.0' encoding='UTF-8' encoding='unknown'?>",
        "<?xml version='1.0' standalone='yes' standalone='no'?>",
        "<?xml version='1.0' standalone='yes' encoding='UTF-8'?>",
        "<?xml encoding='UTF-8' version='1.0'?>",
        "<?xml standalone='yes' version='1.0'?>",
        "<?xml encoding='UTF-8'?>",
        "<?xml version='1.0' standalone='maybe'?>",
    ] {
        let input = format!("{declaration}<r/>");
        for mode in [ParseMode::Document, ParseMode::Content] {
            for content_allow_xml_decl in [false, true] {
                for syntax_check in [false, true] {
                    let options = ParseOptions {
                        content_allow_xml_decl,
                        syntax_check,
                        ..ParseOptions::default()
                    };
                    let mut collector = Collector::default();
                    let result = parse(input.as_bytes(), mode, options, &mut collector);
                    assert!(
                        matches!(result, Err(ParseError::Xml(_))),
                        "input: {input}, mode: {mode:?}, options: {options:?}, result: {result:?}"
                    );
                    assert!(
                        collector
                            .0
                            .iter()
                            .all(|event| !matches!(event, OwnedEvent::Declaration(_))),
                        "input: {input}, mode: {mode:?}, options: {options:?}, events: {:?}",
                        collector.0
                    );
                }
            }
        }
    }
}

#[test]
fn accepts_ordered_xml_declaration_fields_with_xml_whitespace() {
    for separator in [" ", "\t", "\n", "\r", "\r\n", " \t\r\n "] {
        for quote in ['\'', '\"'] {
            for optional_fields in [
                String::new(),
                format!("{separator}encoding={quote}unknown{quote}"),
                format!("{separator}standalone={quote}yes{quote}"),
                format!(
                    "{separator}encoding={quote}UTF-8{quote}{separator}standalone={quote}no{quote}"
                ),
            ] {
                let declaration = format!(
                    "<?xml{separator}version = {quote}1.0{quote}{optional_fields}{separator}?>"
                );
                let input = format!("{declaration}<r/>");
                for mode in [ParseMode::Document, ParseMode::Content] {
                    for syntax_check in [false, true] {
                        let options = ParseOptions {
                            content_allow_xml_decl: true,
                            syntax_check,
                            ..ParseOptions::default()
                        };
                        let actual = events(&input, mode, options).unwrap_or_else(|error| {
                            panic!("input: {input}, mode: {mode:?}, options: {options:?}, error: {error:?}")
                        });
                        let normalized_declaration =
                            declaration.replace("\r\n", "\n").replace('\r', "\n");
                        let expected = if syntax_check {
                            vec![]
                        } else {
                            vec![
                                OwnedEvent::StartDocument,
                                OwnedEvent::Declaration(normalized_declaration.into_bytes()),
                                OwnedEvent::Start("r".into(), vec![], true),
                                OwnedEvent::End(true),
                                OwnedEvent::EndDocument,
                            ]
                        };
                        assert_eq!(
                            actual, expected,
                            "input: {input}, mode: {mode:?}, options: {options:?}"
                        );
                    }
                }
            }
        }
    }
}

#[test]
fn supports_utf16_bom_input() {
    for (name, mut input) in [("little", vec![0xff, 0xfe]), ("big", vec![0xfe, 0xff])] {
        let encoded: Vec<u8> = if name == "little" {
            "<r>A</r>"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect()
        } else {
            "<r>A</r>"
                .encode_utf16()
                .flat_map(u16::to_be_bytes)
                .collect()
        };
        input.extend(encoded);
        let actual = events(&input, ParseMode::Document, ParseOptions::default()).unwrap();
        assert_eq!(actual[2], OwnedEvent::Text(b"A".to_vec()));
    }
    assert!(events(
        [0xff, 0xfe, b'<'],
        ParseMode::Document,
        ParseOptions::default()
    )
    .is_err());
    assert!(events(
        [0xff, 0xfe, 0x00, 0xd8],
        ParseMode::Document,
        ParseOptions::default()
    )
    .is_err());
}

#[test]
fn rejects_dtd_unknown_entities_and_document_fragments() {
    for input in [
        "<!DOCTYPE r><r/>",
        "<r>&custom;</r>",
        "&#32;<r/>",
        "<a/><b/>",
        "text<r/>",
        "<r>]]></r>",
    ] {
        assert!(
            events(input, ParseMode::Document, ParseOptions::default()).is_err(),
            "input: {input}"
        );
    }
    assert!(events("", ParseMode::Document, ParseOptions::default()).is_err());
    assert!(events("", ParseMode::Content, ParseOptions::default()).is_ok());
}

#[test]
fn recover_returns_partial_events_but_does_not_hide_handler_errors() {
    let actual = events(
        "<r>ok<broken>",
        ParseMode::Document,
        ParseOptions {
            recover: true,
            ..ParseOptions::default()
        },
    )
    .unwrap();
    assert!(actual.contains(&OwnedEvent::Text(b"ok".to_vec())));
    assert!(!actual.contains(&OwnedEvent::EndDocument));

    let actual = events(
        "<r>ok&amp;<broken",
        ParseMode::Document,
        ParseOptions {
            recover: true,
            ..ParseOptions::default()
        },
    )
    .unwrap();
    assert!(actual.contains(&OwnedEvent::Text(b"ok&".to_vec())));
    assert!(!actual.contains(&OwnedEvent::EndDocument));
}

#[test]
fn rejects_forbidden_numeric_references() {
    for input in [
        "<r>&#0;</r>",
        "<?xml version='1.0'?><r>&#1;</r>",
        "<?xml version='1.1'?><r>&#1;</r>",
    ] {
        assert!(
            events(input, ParseMode::Document, ParseOptions::default()).is_err(),
            "input: {input}"
        );
    }
}

#[test]
fn rejects_non_xml10_document_versions() {
    for version in ["1.1", "1.2", "2.0"] {
        let input = format!("<?xml version='{version}'?><r>A</r>");
        assert!(events(&input, ParseMode::Document, ParseOptions::default()).is_err());
        assert!(events(
            &input,
            ParseMode::Content,
            ParseOptions {
                content_allow_xml_decl: true,
                ..ParseOptions::default()
            }
        )
        .is_ok());
    }
}

#[test]
fn matches_legacy_xml10_name_character_rules() {
    for ch in [
        '\u{c0}', '\u{d8}', '\u{386}', '\u{3007}', '\u{3021}', '\u{4e00}', '\u{9fa5}', '\u{ac00}',
    ] {
        let input = format!("<{ch}>x</{ch}>");
        assert!(
            events(&input, ParseMode::Document, ParseOptions::default()).is_ok(),
            "input: {input}"
        );
    }
    for ch in [
        '\u{d7}',
        '\u{f7}',
        '\u{300}',
        '\u{370}',
        '\u{37d}',
        '\u{37f}',
        '\u{640}',
        '\u{e46}',
        '\u{ec6}',
        '\u{200c}',
        '\u{2070}',
        '\u{218f}',
        '\u{2c00}',
        '\u{2fef}',
        '\u{3001}',
        '\u{3005}',
        '\u{3031}',
        '\u{309d}',
        '\u{30fc}',
        '\u{f900}',
        '\u{1f600}',
    ] {
        let input = format!("<{ch}>x</{ch}>");
        assert!(events(&input, ParseMode::Document, ParseOptions::default()).is_err());
    }
    for ch in [
        '\u{b7}', '\u{300}', '\u{387}', '\u{640}', '\u{660}', '\u{3005}', '\u{309d}', '\u{30fc}',
    ] {
        let input = format!("<r{ch}>x</r{ch}>");
        assert!(
            events(&input, ParseMode::Document, ParseOptions::default()).is_ok(),
            "input: {input}"
        );
    }
    for ch in ['\u{b2}', '\u{370}', '\u{203f}', '\u{f900}', '\u{1f600}'] {
        let input = format!("<r{ch}>x</r{ch}>");
        assert!(events(&input, ParseMode::Document, ParseOptions::default()).is_err());
    }
}

#[test]
fn syntax_check_validates_without_emitting_tree_events() {
    let actual = events(
        "<r><x/></r>",
        ParseMode::Document,
        ParseOptions {
            syntax_check: true,
            ..ParseOptions::default()
        },
    )
    .unwrap();
    assert!(actual.is_empty());
    assert!(events(
        "<r><x></r>",
        ParseMode::Document,
        ParseOptions {
            syntax_check: true,
            ..ParseOptions::default()
        },
    )
    .is_err());
}

#[test]
fn enforces_document_name_and_depth_rules() {
    for input in ["<1r/>", "<r><1x/></r>", "<\u{f0000}/>"] {
        assert!(
            events(input, ParseMode::Document, ParseOptions::default()).is_err(),
            "input: {input}"
        );
    }
    let accepted = format!("{}text{}", "<r>".repeat(257), "</r>".repeat(257));
    assert!(events(&accepted, ParseMode::Document, ParseOptions::default()).is_ok());
    let rejected = format!("{}text{}", "<r>".repeat(258), "</r>".repeat(258));
    assert!(events(&rejected, ParseMode::Document, ParseOptions::default()).is_err());
}
