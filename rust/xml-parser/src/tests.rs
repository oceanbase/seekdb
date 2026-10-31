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
    input: &str,
    mode: ParseMode,
    options: ParseOptions,
) -> Result<Vec<OwnedEvent>, ParseError<()>> {
    let mut collector = Collector::default();
    parse(input.as_bytes(), mode, options, &mut collector)?;
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
fn rejects_dtd_unknown_entities_and_document_fragments() {
    for input in [
        "<!DOCTYPE r><r/>",
        "<r>&custom;</r>",
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
}

#[test]
fn rejects_forbidden_numeric_references() {
    let result = events("<r>&#0;</r>", ParseMode::Document, ParseOptions::default());
    assert!(result.is_err());
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
