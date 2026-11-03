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

use crate::{access, ddl, load_data, select_into, lex::decode_string as decode, node::*, SqlMode};
use std::borrow::Cow;
use crate::budget::Budget;
use crate::diagnostic::Diagnostics;

pub(crate) fn executable_sql<'sql>(
    sql: &'sql [u8],
    mode: SqlMode,
    execute: bool,
) -> Result<Cow<'sql, [u8]>, peg::error::ParseError<usize>> {
    if !sql.windows(3).any(|bytes| bytes == b"/*!") {
        return Ok(Cow::Borrowed(sql));
    }
    let spans = sql::executable_comments(sql, mode, false, &Budget::lexical_scan(), &Diagnostics::default())?;
    if spans.is_empty() {
        return Ok(Cow::Borrowed(sql));
    }
    let mut bytes = sql.to_vec();
    for (start, content, close, end) in spans {
        if execute {
            bytes[start..content].fill(b' ');
            bytes[close..end].fill(b' ');
        } else {
            bytes[start + 2..close].fill(b' ');
        }
    }
    Ok(Cow::Owned(bytes))
}

fn reserved(word: &str) -> bool {
    [
        "select",
        "distinct",
        "distinctrow",
        "all",
        "as",
        "from",
        "to",
        "where",
        "group",
        "by",
        "having",
        "order",
        "limit",
        "asc",
        "desc",
        "join",
        "natural",
        "straight_join",
        "inner",
        "left",
        "right",
        "outer",
        "cross",
        "on",
        "using",
        "force",
        "ignore",
        "use",
        "index",
        "or",
        "xor",
        "and",
        "not",
        "between",
        "in",
        "interval",
        "like",
        "regexp",
        "rlike",
        "is",
        "null",
        "true",
        "false",
        "exists",
        "div",
        "mod",
        "union",
        "insert",
        "replace",
        "update",
        "delete",
        "set",
        "into",
        "values",
        "default",
        "create",
        "drop",
        "table",
        "database",
        "schema",
        "primary",
        "key",
        "unique",
        "constraint",
        "foreign",
        "references",
        "case",
        "when",
        "then",
        "else",
        "for",
        "lock",
        "convert",
        "partition",
        "range",
        "with",
        "check",
        "accessible", "add", "alter", "analyze", "asensitive", "before",
        "bigint", "binary", "blob", "both", "call", "cascade",
        "change", "char", "character", "collate", "column", "condition",
        "continue", "current_date", "current_time", "current_timestamp", "current_user", "cursor",
        "databases", "day_hour", "day_microsecond", "day_minute", "day_second", "decimal",
        "declare", "describe", "deterministic", "double", "dual", "each",
        "elseif", "enclosed", "escaped", "exit", "explain", "fetch",
        "float", "float4", "float8", "fulltext", "generated", "get",
        "grant", "high_priority", "hour_microsecond", "hour_minute", "hour_second", "if",
        "infile", "inout", "insensitive", "int", "int1", "int2",
        "int3", "int4", "int8", "integer", "io_after_gtids", "io_before_gtids",
        "iterate", "keys", "kill", "leading", "leave", "linear",
        "lines", "load", "localtime", "localtimestamp", "long", "longblob",
        "longtext", "loop", "low_priority", "master_bind", "master_ssl_verify_server_cert", "match",
        "maxvalue", "mediumblob", "mediumint", "mediumtext", "middleint", "minute_microsecond",
        "minute_second", "modifies", "no_write_to_binlog", "numeric", "optimize", "option",
        "optionally", "out", "outfile", "precision", "procedure", "purge",
        "read", "read_write", "reads", "real", "release", "rename",
        "repeat", "require", "resignal", "restrict", "return", "revoke",
        "schemas", "second_microsecond", "sensitive", "separator", "show", "signal",
        "skip", "smallint", "spatial", "specific", "sql", "sql_big_result",
        "sql_calc_found_rows", "sql_small_result", "sqlexception", "sqlstate", "sqlwarning", "ssl",
        "starting", "stored", "terminated", "tinyblob", "tinyint", "tinytext",
        "trailing", "trigger", "undo", "unlock", "unsigned", "usage",
        "utc_date", "utc_time", "utc_timestamp", "varbinary", "varchar", "varcharacter",
        "varying", "virtual", "while", "write", "year_month", "zerofill",
    ]
    .iter()
    .any(|keyword| word.eq_ignore_ascii_case(keyword))
}

fn reserved_in_routine(word: &str) -> bool {
    [
        "select", "from", "by", "limit", "desc", "on", "using", "index",
        "or", "not", "in", "is", "null", "exists", "insert", "replace",
        "update", "delete", "set", "into", "default", "create", "drop", "table",
        "database", "unique", "commit", "rollback", "case", "when", "then", "else",
        "for", "lock", "with",
        "alter", "analyze", "before", "bigint", "binary", "binlog", "blob", "cache", "call",
        "change", "char", "character", "charset", "checksum", "collate", "condition", "continue",
        "current_user", "cursor", "deallocate", "dec", "decimal", "declare", "describe",
        "deterministic", "do", "double", "each", "elseif", "errors", "execute", "exit", "explain",
        "fetch", "float", "float4", "float8", "flush", "get", "grant", "if", "inout", "int",
        "int1", "int2", "int3", "int4", "int8", "integer", "iterate", "kill", "leave", "load",
        "logs", "long", "longblob", "longtext", "loop", "master", "mediumblob", "mediumint",
        "mediumtext", "middleint", "modifies", "numeric", "optimize", "out", "precision",
        "prepare", "privileges", "procedure", "query", "read", "reads", "real", "release",
        "rename", "repeat", "reset", "resignal", "return", "revoke", "savepoint", "show", "signal",
        "slave", "smallint", "sql", "sqlexception", "sqlstate", "sqlwarning", "start", "stop",
        "tables", "temporary", "tinyblob", "tinyint", "tinytext", "trigger", "truncate",
        "unsigned", "until", "varbinary", "varchar", "varcharacter", "varying", "view", "warnings",
        "while", "zerofill",
    ]
    .iter()
    .any(|keyword| word.eq_ignore_ascii_case(keyword))
}

fn keyword_function(word: &str) -> bool {
    ["current_timestamp", "localtime", "localtimestamp", "current_date", "current_time",
        "current_user", "utc_timestamp", "utc_date", "utc_time"]
        .iter().any(|name| word.eq_ignore_ascii_case(name))
}

fn chain(first: Node, rest: Vec<(String, Node)>) -> Result<Node, &'static str> {
    rest.into_iter()
        .try_fold(first, |left, (op, right)| Node::binary(op, left, right))
}

fn boolean_chain(op: &str, first: Node, mut rest: Vec<Node>) -> Result<Node, &'static str> {
    if rest.is_empty() {
        return Ok(first);
    }
    fn build(op: &str, operands: &mut std::vec::IntoIter<Node>, count: usize) -> Result<Node, &'static str> {
        if count == 1 {
            Ok(operands.next().unwrap())
        } else {
            let left = build(op, operands, count / 2)?;
            let right = build(op, operands, count - count / 2)?;
            Node::binary(op.into(), left, right)
        }
    }
    rest.insert(0, first);
    let count = rest.len();
    build(op, &mut rest.into_iter(), count)
}

enum Tail {
    Compare(String, Node),
    Like(bool, Node, Option<Node>),
    SoundsLike(Node),
    Between(bool, Node, Node),
    In(bool, Node, usize),
    Member(Node, usize),
    ArrayMember(Node, usize),
    Is(bool, Node),
}

impl Tail {
    fn apply(self, value: Node) -> Result<Node, &'static str> {
        let start=value.start;
        match self {
            Tail::Compare(op, right) => Node::binary(op, value, right),
            Tail::SoundsLike(right) => {
                let soundex=|value:Node| {
                    let (start,end)=(value.start,value.end);
                    Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("soundex".into()),vec![value]).expression(start,end)
                };
                Node::binary("=".into(),soundex(value)?,soundex(right)?)
            },
            Tail::Like(negated,right,escape) => {
                let end=escape.as_ref().map_or(right.end,|value| value.end);
                let mut children=vec![value,right];
                if let Some(mut escape)=escape {
                    if escape.kind==Kind::String && escape.text.is_empty() { escape.text="\\".into(); }
                    children.push(escape);
                }
                Node::binary_operands(if negated { "not like" } else { "like" }.into(),children).expression(start,end)
            },
            Tail::Between(negated, low, high) => { let end=high.end; Node::new(Kind::Between, vec![value, low, high]).value(u64::from(negated)).expression(start, end) },
            Tail::In(negated, right, end) => Node::new(Kind::In, vec![value, right]).value(u64::from(negated)).expression(start, end),
            Tail::Member(right,end) => Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("JSON_MEMBER_OF".into()),vec![value,right]).expression(start,end),
            Tail::ArrayMember(right,end) => Node::callable(Kind::ArrayMembership,vec![value,right]).expression(start,end),
            Tail::Is(negated, test) => { let end=test.end; Node::new(Kind::Is, vec![value, test]).value(u64::from(negated)).expression(start, end) },
        }
    }
}

peg::parser! {
    pub grammar sql(mode: SqlMode, pl: bool, budget: &Budget, diagnostics: &Diagnostics) for [u8] {
        rule opaque_comment_token()
            = "'" ("''" / enabled(!mode.no_backslash_escapes) "\\" [_] / !"'" [_])* "'"
            / "\"" ("\"\"" / enabled(!mode.ansi_quotes && !mode.no_backslash_escapes) "\\" [_] / !"\"" [_])* "\""
            / "`" ("``" / !"`" [_])* "`"
            / "/*" !"!" (!"*/" [_])* "*/"
            / ("--" &(space() / ![_]) / "#") (!"\n" [_])*
        rule executable_comment_start() -> (usize,usize)
            = start:position!() "/*!" ([b'0'..=b'9']*<5>)? content:position!() { (start,content) }
        pub rule executable_comments() -> Vec<(usize,usize,usize,usize)> = spans:(
                outer:executable_comment_start()
                nested:(opaque_comment_token() { None }
                    / marker:executable_comment_start() { Some((marker.0,marker.1,marker.1,marker.1)) }
                    / !"*/" [_] { None })* close:position!() "*/" end:position!() {
                    let mut spans=vec![(outer.0,outer.1,close,end)];
                    spans.extend(nested.into_iter().flatten());
                    spans
                }
                / opaque_comment_token() { vec![] }
                / ((![b'\''|b'"'|b'`'|b'/'|b'-'|b'#'] [_])+ / !"/*!" [_]) { vec![] }
            )* { spans.into_iter().flatten().collect() }
        rule syntax_text(value: rule<&'input [u8]>) -> &'input str
            = bytes:value() {? std::str::from_utf8(bytes).map_err(|_| "UTF-8 syntax text") }
        rule space() = [b' ' | b'\t' | b'\r' | b'\n' | b'\x0b' | b'\x0c']
        rule block_comment() = "/*" !"!" (!"*/" [_])* "*/"
        rule _() = quiet!{(space() / block_comment()
            / ("--" &(space() / ![_]) / "#") (!"\n" [_])*)*}
        rule hint_gap() = quiet!{(space() / !"/*+" block_comment())*}
        rule cont() = [b'a'..=b'z' | b'A'..=b'Z' | b'0'..=b'9' | b'_' | b'$' | b'\x80'..=b'\xff']
        rule word() = [b'a'..=b'z' | b'A'..=b'Z' | b'_' | b'$' | b'\x80'..=b'\xff'] cont()*
        rule keyword(word: &'static str) -> ()
            = quiet!{#{|input: &[u8], pos| {
                if input.get(pos..pos+word.len()).is_some_and(|text| text.eq_ignore_ascii_case(word.as_bytes())) {
                    peg::RuleResult::Matched(pos+word.len(), ())
                } else { peg::RuleResult::Failed }
            }} !cont() !("." cont())} / expected!(word)
        rule kw(word: &'static str) -> () = keyword(word) _
        rule distinct_keyword() = kw("distinct") / kw("distinctrow") / kw("unique")
        rule hint_slot() -> Option<Vec<Node>> = hint_gap() value:hints()? !"/*+" _ { value }
        rule unterminated_token() -> &'static str
            = "/*" (!"*/" [_])* ![_] { "unterminated block comment" }
            / "'" ("''" / enabled(!mode.no_backslash_escapes) "\\" [_] / !"'" [_])* ![_] { "unterminated string literal" }
            / "\"" ("\"\"" / enabled(!mode.ansi_quotes && !mode.no_backslash_escapes) "\\" [_] / !"\"" [_])* ![_] {
                if mode.ansi_quotes { "unterminated quoted identifier" } else { "unterminated string literal" }
            }
            / "`" ("``" / !"`" [_])* ![_] { "unterminated quoted identifier" }
        rule quoted_binary_diagnostic() -> &'static str
            = [b'x'|b'X'] "'" text:$((!"'" [_])*) "'" {
                if text.len()%2==0 && text.iter().all(u8::is_ascii_hexdigit) { "" }
                else { "invalid hexadecimal literal" }
            }
            / [b'b'|b'B'] "'" text:$((!"'" [_])*) "'" {
                if text.iter().all(|byte| matches!(byte,b'0'|b'1')) { "" }
                else { "invalid binary literal" }
            }
        pub rule diagnostic_token(offset: usize) -> (crate::Span, &'static str) = tokens:(space()* span:(
            value:(temporal_literal() / number()) { (crate::Span { start:value.start,end:value.end },"") }
            / start:position!() detail:(quoted_binary_diagnostic() / unterminated_token()) end:position!() { (crate::Span { start,end },detail) }
            / start:position!() (opaque_comment_token() / cont()+ ("." (cont()+ / opaque_comment_token()))*
                / "<=>" / "<=" / ">=" / "<>" / "!=" / ":=" / "<<" / ">>" / "||" / "&&" / [_]) end:position!() {
                    (crate::Span { start,end },"")
                }) { span })* space()* end:position!() {
                tokens.into_iter().find(|(span,detail)| offset<span.end || !detail.is_empty() && span.start<=offset)
                    .unwrap_or((crate::Span { start:end,end },""))
            }
        rule depth(level: usize)
            = offset:position!() {?
                budget.checkpoint(offset)?;
                if level < MAX_DEPTH { Ok(()) } else { Err("SQL nesting below 1024 levels") }
            }
        rule enabled(value: bool) = quiet!{ {? if value { Ok(()) } else { Err("SQL mode") } } }
        rule reserved_word() = text:syntax_text(<$(word())>) {? if reserved(text) { Ok(()) } else { Err("reserved keyword") } }
        rule ident() -> Name
            = quiet!{ start:position!() text:syntax_text(<$("`" ("``" / !"`" [_])* "`")>) end:position!() _
                { Name { text: text[1..text.len()-1].replace("``", "`"), quoted: true, start, end } }
            / enabled(mode.ansi_quotes) start:position!() text:syntax_text(<$("\"" ("\"\"" / !"\"" [_])* "\"")>) end:position!() _
                { Name { text: text[1..text.len()-1].replace("\"\"", "\""), quoted: true, start, end } }
            / !reserved_word() start:position!() text:syntax_text(<$(word())>) end:position!() _
                { Name { text: text.to_owned(), quoted: false, start, end } }
            / !(number() / binary_literal() / ("0X" hex_digit()+ / "0B" [b'0'|b'1']+) !cont())
                start:position!() text:syntax_text(<$([b'0'..=b'9']+ word())>) end:position!() _
                { Name { text: text.to_owned(), quoted: false, start, end } }
            } / expected!("identifier")
        rule name_keyword() -> Name
            = &reserved_word() !(kw("all") / kw("distinct") / kw("from") / kw("select")
                / kw("straight_join") / kw("unique") / kw("when") / kw("both") / kw("leading") / kw("trailing")
                / kw("high_priority") / kw("sql_big_result") / kw("sql_small_result") / kw("sql_calc_found_rows"))
                start:position!() text:syntax_text(<$(word())>) end:position!() _
                { Name { text: text.to_owned(), quoted: false, start, end } }
        rule dotted_ident() -> Name = ident() / name_keyword()
        rule qualified_ident() -> Name = ident()
            / start:position!() text:syntax_text(<$(cont()+)>) end:position!() _
            { Name { text: text.to_owned(), quoted: false, start, end } }
        rule qualified_name() -> Vec<Name>
            = &quiet!{(cont()+ / ident()) _ "."} !([b'0'..=b'9']+ _ ".")
                first:qualified_ident() "." _ rest:(qualified_ident() ++ ("." _)) {
            let mut parts=vec![first];parts.extend(rest);parts
        }
        rule name() -> Vec<Name>
            = qualified_name() / first:ident() { vec![first] }
        rule column_name() -> Vec<Name>
            = "." _ first:qualified_ident() "." _ last:qualified_ident() { vec![first,last] }
            / name()
        rule relation_name() -> Vec<Name>
            = "." ![b'0'..=b'9'] _ !(kw("true") / kw("false") / kw("null")) name:dotted_ident() { vec![name] }
            / name()
        rule expression_name() -> Vec<Name>
            = column_name()
            / quiet!{ name:name_keyword() enabled(keyword_function(&name.text)) { vec![name] } }
            / start:position!() name:(kw("database") { "database" } / kw("schema") { "schema" }
                / kw("replace") { "replace" } / kw("insert") { "insert" }
                / kw("left") { "left" } / kw("right") { "right" } / kw("mod") { "mod" } / kw("if") { "if" } / kw("repeat") { "repeat" }) &"("
                { vec![Name { text: name.into(), quoted: false, start, end: start + name.len() }] }
        rule implicit_alias_keyword() = kw("except") / kw("intersect") / kw("minus") / kw("window")
        rule alias() -> Name = (kw("as") / !implicit_alias_keyword()) value:ident() { value }
        rule table_alias() -> Name = !(kw("full") kw("outer")? kw("join") / (kw("use") / kw("force") / kw("ignore"))) value:alias() { value }
        rule number() -> Node
            = start:position!() text:syntax_text(<$(
                [b'0'..=b'9']+ (
                    "." [b'0'..=b'9']* ([b'e' | b'E'] [b'+' | b'-']? [b'0'..=b'9']+)?
                    / [b'e' | b'E'] ([b'+' | b'-'] [b'0'..=b'9']+ / [b'0'..=b'9']+ !cont())
                    / !cont()
                )
                / "." [b'0'..=b'9']+ ([b'e' | b'E'] [b'+' | b'-']? [b'0'..=b'9']+)?
            )>) end:position!() _
                {? Node::number(text, start, end) }
        rule string_segment(quote: rule<()>) -> &'input [u8]
            = text:$(quote() (quote() quote() / enabled(!mode.no_backslash_escapes) "\\" [_]
                / !(quote() / enabled(!mode.no_backslash_escapes) "\\") [_])* quote()) { text }
        rule string_gap() = [b' '|b'\t'|b'\n'|b'\r'|b'\x0c']+ / "--" [b'\n'|b'\r']
        rule string_token(quote: rule<()>) -> Node
            = start:position!() parts:(string_segment(&quote) ++ string_gap()) end:position!() _ {?
                let mut text=decode(parts[0],mode.no_backslash_escapes);
                let children=if parts.len()>1 { vec![Node::new(Kind::StringColumnName,vec![]).bytes(text.clone())] } else { vec![] };
                for part in &parts[1..] { text.extend_from_slice(&decode(part,mode.no_backslash_escapes)); }
                Node::new(Kind::String,children).bytes(text).expression(start,end)
            }
        rule string() -> Node = quiet!{string_token(<"'">)
            / enabled(!mode.ansi_quotes) value:string_token(<"\"">) { value }} / expected!("string literal")
        rule string_expression() -> Node
            = charset_literal()
            / &("n'" / "N'") start:position!() [b'n'|b'N'] value:string_token(<"'">) {
                let end=value.end;value.with_kind(Kind::NationalString).at(start,end)
            }
            / first:string() rest:string()* {?
            if rest.is_empty() { Ok(first) }
            else {
                let (start,end)=(first.start,rest.last().unwrap().end);
                let mut args=vec![first];args.extend(rest);
                Node::named_call(Kind::StringConcat,Node::new(Kind::Ident,vec![]).text("concat".into()),args).expression(start,end)
            }
        }
        rule null_literal() -> Node
            = start:position!() kw("null") {? Node::new(Kind::Null, vec![]).expression(start, start+4) }
            / start:position!() "\\N" end:position!() _ {? Node::new(Kind::Null, vec![]).expression(start,end) }
        rule boolean_literal() -> Node
            = start:position!() kw("true") {? Node::new(Kind::Bool, vec![]).value(1).expression(start, start+4) }
            / start:position!() kw("false") {? Node::new(Kind::Bool, vec![]).value(0).expression(start, start+5) }
        rule bool_null() -> Node = null_literal() / boolean_literal()
        rule signed_literal() -> Node
            = start:position!() op:syntax_text(<$([b'+' | b'-'])>) _ value:number() {? Node::unary(op,value,start) }
            / temporal_literal() / binary_literal() / number() / string_expression() / bool_null()
        rule temporal_kind() -> Kind = keyword("date") { Kind::DateLiteral }
            / keyword("time") { Kind::TimeLiteral } / keyword("timestamp") { Kind::TimestampLiteral }
        rule temporal_literal() -> Node = start:position!() kind:temporal_kind() _
            text:("'" text:$([^b'\'']*) "'" { text } / "\"" text:$([^b'"']*) "\"" { text }) end:position!() _ {?
                Node::new(kind,vec![]).bytes(text.to_vec()).expression(start,end)
            }
        rule parameter() -> Node
            = start:position!() "?" end:position!() _ {? Node::new(Kind::Parameter, vec![]).expression(start, end) }
            / start:position!() text:syntax_text(<$(":" ([b'0'..=b'9']+ !cont()
                / word() ("." word())?))>) end:position!() _ {?
                if let Some((name,_))=text[1..].split_once('.') {
                    if !name.eq_ignore_ascii_case("new") && !name.eq_ignore_ascii_case("old") { return Err("NEW or OLD row binding"); }
                }
                Node::new(Kind::Parameter, vec![]).text(text.into()).expression(start, end)
            }
        rule hex_digit() = [b'0'..=b'9' | b'a'..=b'f' | b'A'..=b'F']
        rule binary_literal() -> Node
            = start:position!() [b'x'|b'X'] "'" text:syntax_text(<$(hex_digit()*)>) "'" end:position!() _ failure:position!() {?
                if text.len()%2!=0 { Err(diagnostics.record("even number of hexadecimal digits",crate::Span { start,end },failure)) }
                else { Node::new(Kind::HexString,vec![]).text(text.into()).expression(start,end) }
            }
            / start:position!() "0x" text:syntax_text(<$(hex_digit()+)>) !cont() end:position!() _ {?
                Node::new(Kind::HexString,vec![]).text(text.into()).expression(start,end)
            }
            / start:position!() text:([b'b'|b'B'] "'" text:syntax_text(<$([b'0'|b'1']*)>) "'" { text }
                / "0b" text:syntax_text(<$([b'0'|b'1']+)>) !cont() { text }) end:position!() _ {?
                Node::new(Kind::BitString,vec![]).text(text.into()).expression(start,end)
            }
        rule scope() -> u64 = quiet!{ value:syntax_text(<$(word())>) _ {?
            if value.eq_ignore_ascii_case("global") { Ok(1) }
            else if value.eq_ignore_ascii_case("session") || value.eq_ignore_ascii_case("local") { Ok(2) }
            else { Err("variable scope") }
        }} / expected!("GLOBAL, SESSION or LOCAL")
        rule system_variable() -> Node = start:position!() "@@" scope:(value:scope() "." _ { value })? name:ident() {
            let end=name.end;
            Node::new(Kind::SystemVariable, vec![]).text(name.text).value(scope.unwrap_or(0)).at(start,end)
        }
        rule quoted_variable(quote: rule<()>) -> (String,usize)
            = text:syntax_text(<$(quote() (quote() quote() / !quote() (cont() / "." / "/" / "%" / "'" / "\"" / "`"))* quote())>)
                end:position!() _ {? String::from_utf8(decode(text.as_bytes(),true)).map(|text| (text,end)).map_err(|_| "UTF-8 variable name") }
        rule user_variable() -> Node = quiet!{start:position!() "@" !"@" name:(
                quoted_variable(<"'">) / quoted_variable(<"\"">) / quoted_variable(<"`">)
                / text:syntax_text(<$((cont() / ".")+)>) end:position!() { (text.into(),end) }
                / end:position!() ![b'\''|b'"'|b'`'] { (String::new(),end) }
            ) _ {
                Node::new(Kind::UserVariable,vec![]).text(name.0).at(start,name.1)
        }} / expected!("user variable")
        rule atom(level: usize) -> Node
            = temporal_literal() / binary_literal() / number() / string_expression() / bool_null() / parameter()
            / depth(level) start:position!() "{" _
                !(temporal_kind() (space()+ / "--" [b'\n'|b'\r'])? [b'\''|b'"'])
                action:ident() operand:expr(level+1) "}" end:position!() _ {?
                Node::odbc_escape(&action.text,operand,start,end)
            }
            / depth(level) value:collection_expression(level+1) { value }
            / depth(level) value:date_function(level+1) { value }
            / depth(level) value:string_function(level+1) { value }
            / depth(level) value:json_function(level+1) { value }
            / depth(level) start:position!() value:(kw("cast") "(" _ value:expr(level+1) kw("as") target:cast_type() array:kw("array")? ignore:kw("ignore")? { (value,target,ignore.is_some(),array.is_some()) }
                / kw("convert") "(" _ value:expr(level+1) "," _ target:cast_type() { (value,target,false,false) }) ")" end:position!() _ failure:position!() {?
                let result=Node::named_call(Kind::Cast,Node::new(Kind::Ident,vec![]).text("cast".into()).at(start,start+4),vec![value.0,value.1])
                    .value(u64::from(value.2) | (u64::from(value.3) << 1)).expression(start,end);
                result.map_err(|message| diagnostics.record(message,crate::Span { start,end },failure))
            }
            / depth(level) start:position!() kw("case") operand:expr(level+1)?
                branches:(kw("when") condition:expr(level+1) kw("then") value:expr(level+1)
                    { Node::new(Kind::When, vec![condition,value]) })+
                default:(kw("else") value:expr(level+1) { value })? end:position!() kw("end") {?
                    Node::new(Kind::Case, vec![Node::optional(operand),Node::new(Kind::WhenList,branches),
                        default.unwrap_or_else(|| Node::new(Kind::DefaultNull,vec![]))]).expression(start,end+3)
                }
            / depth(level) start:position!() function:(kw("default") { (Kind::DefaultValue,"default") }
                / kw("values") { (Kind::Function,"values") }) "(" _ value:column() ")" end:position!() _ {?
                Node::named_call(function.0,Node::new(Kind::Ident,vec![]).text(function.1.into()),vec![value]).expression(start,end)
            }
            / system_variable()
            / depth(level) value:user_variable() assigned:(":=" _ value:expr(level+1) { value })? {?
                let (start,end)=(value.start,assigned.as_ref().map_or(value.end,|value| value.end));
                match assigned {
                    Some(assigned) => Node::new(Kind::UserAssignment,vec![value.with_kind(Kind::UserVariableTarget),assigned]),
                    None => Node::new(Kind::GetUserVariable,vec![value]),
                }.expression(start,end)
            }
            / depth(level) start:position!() "(" _ query:select(level+1) ")" end:position!() _
                {? query.expression(start, end) }
            / depth(level) start:position!() "(" _ first:expr(level+1) rest:("," _ values:expr_list(level+1) { values })? ")" end:position!() _ {?
                match rest {
                    None => Node::new(Kind::Parenthesized,vec![first]).expression(start,end),
                    Some(rest) => { let mut values=vec![first]; values.extend(rest); Node::new(Kind::ExprList,values).expression(start,end) }
                }
            }
            / depth(level) value:fulltext_search(level+1) { value }
            / depth(level) start:position!() kw("row") "(" _ first:expr(level+1) "," _ rest:expr_list(level+1) ")" end:position!() _ {?
                let mut values=vec![first]; values.extend(rest); Node::new(Kind::ExprList,values).expression(start,end)
            }
            / depth(level) start:position!() kw("exists") "(" _ query:select(level+1) ")" end:position!() _
                {? Node::new(Kind::Exists, vec![query]).expression(start, end) }
            / depth(level) start:position!() function:window_value(level+1) kw("over") window:window(level+1) {?
                let end=window.end;
                Node::new(Kind::WindowFunction,vec![function,window]).expression(start,end)
            }
            / depth(level) start:position!() listagg:(kw("group_concat") { false } / kw("listagg") { true }) "(" _
                modifier:(distinct_keyword() { Node::new(Kind::Distinct,vec![]) })?
                arguments:expr_list(level+1) order:(kw("order") kw("by") values:(order_item(level+1) ++ ("," _)) { values })?
                separator:(kw("separator") value:string() { Node::new(Kind::Separator,vec![value]) })? ")" close:position!() _
                window:(kw("over") value:window(level+1) { value })? !(enabled(window.is_none()) kw("over")) {?
                    let end=window.as_ref().map_or(close,|value| value.end);
                    if listagg && window.is_none() { return Err("OVER clause for LISTAGG"); }
                    let mut value=Node::callable(Kind::GroupConcat,arguments);
                    value.children.extend(modifier);
                    if let Some(order)=order { value.children.push(Node::new(Kind::Order,order)); }
                    value.children.extend(separator);
                    match window { Some(window) => Node::new(Kind::WindowFunction,vec![value,window]),None => value }.expression(start,end)
                }
            / depth(level) start:position!() names:name() column_end:position!() op:syntax_text(<$("->>" / "->")>) _ path:string_expression() {?
                let end=path.end;
                let value=Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("JSON_EXTRACT".into()),vec![Node::column(names,false,start,column_end)?,path]);
                let value=if op=="->>" { Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("JSON_UNQUOTE".into()),vec![value]) } else { value };
                value.expression(start,end)
            }
            / depth(level) start:position!() "." _ names:expression_name() {?
                if names.len()!=2 { return Err("table and column name"); }
                let end=names[1].end;Node::column(names,false,start,end)
            }
            / depth(level) start:position!() !([b'x'|b'X'] "'" hex_digit()* "'") parts:expression_name() tail:("." _ "*" end:position!() _ { (None, end) }
                / enabled(parts.len()!=1 || parts[0].quoted || !["row","match","score","point","json_query","json_value","trim","position","weight_string","char","character","array","map","array_agg",
                    "array_map","array_filter","array_first","array_sortby","vector_distance","vector_similarity","group_concat","listagg","get_format","cast","convert"]
                    .iter().any(|name| parts[0].text.eq_ignore_ascii_case(name)))
                    &"(" valid_function(parts.as_slice(),start)
                    "(" _ modifier:(distinct_keyword() { Kind::Distinct } / kw("all") { Kind::All })?
                    args:(enabled(parts.len()==1 && !parts[0].quoted && parts[0].text.eq_ignore_ascii_case("count")
                            && modifier!=Some(Kind::Distinct)) value:star() { vec![value] }
                        / values:(function_argument(level+1) ++ ("," _))? { values.unwrap_or_default() }) ")" close:position!() _
                    window:(kw("over") value:window(level+1) { value })? followed_by_over:(&kw("over") { true } / { false })
                    { let end=window.as_ref().map_or(close,|value| value.end); (Some((modifier, args,window,followed_by_over)), end) })? failure:position!()
                {? let name_end=parts.last().unwrap().end; match tail {
                    None => {
                        let end=parts.last().unwrap().end;
                        let keyword_function=parts.len()==1 && !parts[0].quoted && keyword_function(&parts[0].text);
                        if keyword_function { Node::call(parts,None,vec![],None,false,start,end) }
                        else { Node::column(parts,false,start,end) }
                    },
                    Some((None, end)) => Node::column(parts, true, start, end),
                    Some((Some((modifier, arguments, window, followed_by_over)), end)) =>
                        Node::call(parts, modifier, arguments, window, followed_by_over, start, end)
                            .map_err(|message| diagnostics.record(message,crate::Span { start,end:name_end },failure)),
                } }
            / enabled(pl) depth(level) start:position!() text:syntax_text(<$(keyword("force") / keyword("cascade"))>) end:position!() _ {?
                Node::column(vec![Name { text:text.to_owned(),quoted:false,start,end }],false,start,end)
            }
        rule valid_function(parts: &[Name], start: usize) = failure:position!() {?
            if parts.len()!=1 || parts[0].quoted || valid_function_name(&parts[0].text) { Ok(()) }
            else { Err(diagnostics.record("invalid function name",crate::Span { start,end:parts[0].end },failure)) }
        }
        rule star() -> Node = start:position!() "*" end:position!() _ {? Node::new(Kind::Star, vec![]).expression(start, end) }
        rule function_argument(level: usize) -> Node = value:expr(level)
            alias:(enabled(!value.is_wildcard()) kw("as")? label:(name:ident() { Node::name(name) } / string()) { label })? {
                match alias {
                    Some(alias) => { let (start,end)=(value.start,alias.end); Node::new(Kind::AliasedArgument,vec![value,alias]).at(start,end) },
                    None => value,
                }
            }
        rule lambda(level: usize) -> Node = parameters:(name:ident() { vec![name] }
                / "(" _ names:(ident() ++ ("," _)) ")" _ { names }) "->" _ value:expr(level) {
                Node::new(Kind::Lambda,vec![Node::new(Kind::ExprList,parameters.into_iter().map(Node::name).collect()),value])
            }
        rule vector_metric() -> Node = value:(kw("cosine") { 0 } / kw("dot") { 1 } / kw("euclidean") { 2 } / kw("manhattan") { 3 }) {
            Node::new(Kind::HiddenInt,vec![]).value(value)
        }
        rule collection_expression(level: usize) -> Node = start:position!() value:(
                "[" _ args:expr_list(level) "]" { Node::callable(Kind::ArrayLiteral,args) }
                / kind:(kw("array") { Kind::ArrayLiteral } / kw("map") { Kind::MapLiteral })
                    "(" _ args:expr_list(level) ")" { Node::callable(kind,args) }
                / kw("array_agg") "(" _ distinct:(distinct_keyword() { true })? value:expr(level)
                    order:(kw("order") kw("by") keys:(array_order_item(level) ++ ("," _)) { keys })? ")" {
                    let mut result=Node::callable(Kind::ArrayAgg,vec![value]);
                    if distinct.is_some() { result.children.push(Node::new(Kind::Distinct,vec![])); }
                    if let Some(keys)=order { result.children.push(Node::new(Kind::Order,keys)); }
                    result
                }
                / kind:(kw("array_map") { Kind::ArrayMap } / kw("array_filter") { Kind::ArrayFilter }
                    / kw("array_first") { Kind::ArrayFirst } / kw("array_sortby") { Kind::ArraySortBy })
                    "(" _ function:lambda(level) "," _ args:expr_list(level) ")" {
                    let mut result=Node::callable(kind,args);result.children.push(function);result
                }
                / kind:(kw("vector_distance") { Kind::VectorDistance } / kw("vector_similarity") { Kind::VectorSimilarity })
                    "(" _ left:expr(level) "," _ right:expr(level) metric:("," _ value:vector_metric() { value })? ")" {
                    let mut result=Node::callable(kind,vec![left,right]);result.children.extend(metric);result
                }
                / kw("point") "(" _ x:expr(level) "," _ y:expr(level) ")" { Node::callable(Kind::Point,vec![x,y]) }
            ) end:position!() _ {? value.expression(start,end) }
        rule charset_literal() -> Node = start:position!() charset:((kw("_utf8") / kw("_utf8mb4") / kw("_utf8mb3")) { "utf8mb4" } / kw("_binary") { "binary" })
            value:(string() / binary_literal()) {
                let end=value.end;
                Node::new(Kind::CharsetLiteral,vec![Node::new(Kind::Charset,vec![]).text(charset.into()),value]).at(start,end)
            }
        rule trim_arguments(level: usize) -> Vec<Node>
            = direction:(kw("both") { 0 } / kw("leading") { 1 } / kw("trailing") { 2 })
                remove:expr(level)? kw("from") value:expr(level) {
                    let mut args=vec![Node::new(Kind::DefaultInt,vec![]).value(direction)];
                    args.extend(remove);args.push(value);args
                }
            / first:expr(level) rest:(kw("from") value:expr(level) { value })? {
                match rest { Some(value) => vec![Node::new(Kind::DefaultInt,vec![]),first,value],None => vec![first] }
            }
        rule json_query_response() -> Node
            = kw("error") { Node::new(Kind::HiddenInt,vec![]) }
            / start:position!() kw("null") { Node::new(Kind::NullClause,vec![]).value(1).at(start,start+4) }
        rule json_query_responses() -> Vec<Node> = clauses:((value:(json_query_response()
                    / kw("empty") kind:(kw("array") { 3 } / kw("object") { 4 })? { Node::new(Kind::HiddenInt,vec![]).value(kind.unwrap_or(2)) })
                kw("on") slot:(kw("error") { 0 } / kw("empty") { 1 }) { (slot,value) })*)
            mismatch:(value:(json_query_response() / kw("dot") { Node::new(Kind::HiddenInt,vec![]).value(3) }) kw("on") kw("mismatch") { value })? {?
                let mut responses=[None,None];
                for (slot,value) in clauses { if responses[slot].replace(value).is_some() { return Err("duplicate JSON response clause"); } }
                let mut values:Vec<_>=responses.into_iter().map(|value| value.unwrap_or_else(|| Node::new(Kind::HiddenInt,vec![]).value(5))).collect();
                values.push(mismatch.unwrap_or_else(|| Node::new(Kind::HiddenInt,vec![]).value(2)));Ok(values)
            }
        rule json_wrapper() -> u64
            = kw("without") array:kw("array")? kw("wrapper") { u64::from(array.is_some()) }
            / kw("with") conditional:(kw("unconditional") { false } / kw("conditional") { true })? array:kw("array")? kw("wrapper") {
                match conditional { None => 2+u64::from(array.is_some()),Some(value) => 4+u64::from(value)+2*u64::from(array.is_some()) }
            }
        rule json_returning_type() -> Node = target:(kw("returning") target:cast_type() { target })? {
            target.unwrap_or_else(|| Node::new(Kind::CastType,vec![Node::new(Kind::HiddenInt,vec![]).value(512)]).text("char".into()))
        }
        rule json_function(level: usize) -> Node = start:position!() kw("json_query") "(" _ data:expr(level) "," _ path:string_expression()
            target:json_returning_type() truncate:kw("truncate")?
            scalars:(kw("allow") kw("scalars") { 0 } / kw("disallow") kw("scalars") { 1 })? pretty:kw("pretty")? ascii:kw("ascii")?
            wrapper:json_wrapper()? asis:kw("asis")? responses:json_query_responses() multivalue:kw("multivalue")? ")" end:position!() _ {?
                let mut children=vec![data,path,target];
                children.extend([u64::from(truncate.is_some()),scalars.unwrap_or(2),u64::from(pretty.is_some()),u64::from(ascii.is_some()),wrapper.unwrap_or(8),u64::from(asis.is_some())]
                    .into_iter().map(|value| Node::new(Kind::HiddenInt,vec![]).value(value)));
                children.extend(responses);children.push(Node::new(Kind::HiddenInt,vec![]).value(u64::from(multivalue.is_some())));
                Node::new(Kind::JsonQuery,children).expression(start,end)
            }
            / start:position!() kw("json_value") "(" _ data:expr(level) "," _ path:string_expression()
                target:json_returning_type() truncate:kw("truncate")? ascii:kw("ascii")? options:json_options() ")" end:position!() _ {?
                    let mut children=vec![data,path,target,Node::new(Kind::HiddenInt,vec![]).value(u64::from(truncate.is_some())),Node::new(Kind::HiddenInt,vec![]).value(u64::from(ascii.is_some()))];
                    children.extend(options.into_children());Node::new(Kind::JsonValue,children).expression(start,end)
                }
        rule fulltext_argument(level: usize) -> Node
            = ![b'+'|b'-'] value:signed_literal() { value } / system_variable() / parameter()
            / value:atom(level) {?
                if crate::expression::is_call_expression(&value) || matches!(value.kind,Kind::Binary|Kind::JsonValue|Kind::JsonQuery) { Ok(value) }
                else { Err("literal or function search argument") }
            }
        rule fulltext_boosted_column() -> Node = column:column() "^" _ boost:signed_literal() {
            Node::new(Kind::MatchColumn,vec![column,boost])
        }
        rule fulltext_search(level: usize) -> Node = start:position!() kw("match") "(" _ columns:(column() ++ ("," _)) ")" _ kw("against") "(" _ search:fulltext_argument(level)
            search_mode:(kw("in") kw("natural") kw("language") kw("mode") expansion:(kw("with") kw("query") kw("expansion"))? { u64::from(expansion.is_some()) }
                / kw("in") kw("boolean") kw("mode") { 2 } / kw("with") kw("query") kw("expansion") { 3 }
                / kw("in") kw("match") kw("phrase") kw("mode") { 4 })? ")" end:position!() _ {?
                Node::new(Kind::MatchAgainst,vec![Node::new(Kind::MatchColumns,columns),search]).value(search_mode.unwrap_or(0)).expression(start,end)
            }
            / start:position!() kw("match") "(" _ columns:(fulltext_boosted_column() ++ ("," _))
                "," _ search:fulltext_argument(level) "," _ options:fulltext_argument(level) ")" end:position!() _ {?
                Node::new(Kind::ElasticMatch,vec![Node::new(Kind::MatchColumns,columns),search,options]).expression(start,end)
            }
            / start:position!() kw("score") "(" _ ")" end:position!() _ {?
                Node::new(Kind::MatchScore,vec![]).expression(start,end)
            }
        rule weight_level() -> Node = start:position!() value:type_size()
            descending:(kw("asc") { false } / kw("desc") { true })? reverse:kw("reverse")? end:position!() {
                Node::new(Kind::WeightStringLevel,vec![value])
                    .value(u64::from(descending==Some(true)) | (u64::from(reverse.is_some()) << 1)).at(start,end)
            }
        rule weight_levels() -> Vec<Node>
            = start:position!() first:type_size() "-" _ last:type_size() end:position!() {
                vec![Node::new(Kind::WeightStringLevel,vec![first,last]).at(start,end)]
            }
            / levels:(weight_level() ++ ("," _)) { levels }
        rule weight_string(level: usize) -> Node = start:position!() kw("weight_string") "(" _ value:expr(level)
            tail:("," _ length:type_size() "," _ weights:type_size() "," _ flags:type_size() "," _ binary:type_size() {
                    (vec![length,weights,flags,binary],None,None)
                }
                / as_type:(start:position!() kw("as") binary:(kw("binary") { true } / (kw("char") / kw("character")) { false })
                    "(" _ weights:type_size() ")" end:position!() _ {
                        Node::new(Kind::WeightStringType,vec![weights]).value(u64::from(binary)).at(start,end)
                    })?
                  levels:(start:position!() kw("level") levels:weight_levels() end:position!() {
                      Node::new(Kind::WeightStringLevels,levels).at(start,end)
                  })? { (vec![],as_type,levels) }) ")" end:position!() _ {?
                let mut arguments=vec![value];arguments.extend(tail.0);
                let mut call=Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("weight_string".into()),arguments);
                call.children.extend(tail.1);call.children.extend(tail.2);
                call.expression(start,end)
            }
        rule string_function(level: usize) -> Node = weight_string(level) / start:position!() function:(
                kw("position") "(" _ needle:bit_or(level) kw("in") haystack:expr(level) { ("position",vec![needle,haystack]) }
                / (kw("substr") / kw("substring")) "(" _ value:expr(level)
                    rest:("," _ position:expr(level) length:("," _ value:expr(level) { value })? { (position,length) }
                        / kw("from") position:expr(level) length:(kw("for") value:expr(level) { value })? { (position,length) }) {
                    let mut args=vec![value,rest.0];args.extend(rest.1);("substr",args)
                }
                / kw("convert") "(" _ value:expr(level) kw("using") !kw("default") charset:charset_name() { ("convert",vec![value,charset]) }
                / (kw("char") / kw("character")) "(" _ args:expr_list(level) charset:(kw("using") value:charset_name() { value })? {
                    let mut args=args;args.push(charset.unwrap_or_else(|| Node::new(Kind::CharsetName,vec![]).text("binary".into())));("char",args)
                }
                / kw("trim") "(" _ args:trim_arguments(level) { ("trim",args) }
            ) ")" end:position!() _ {?
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text(function.0.into()),function.1).expression(start,end)
            }
        rule date_unit() -> Node = start:position!() name:syntax_text(<$(word())>) end:position!() _ failure:position!() {?
            let name=name.to_ascii_lowercase();
            if ["microsecond","second","minute","hour","day","week","month","quarter","year",
                "second_microsecond","minute_microsecond","minute_second","hour_microsecond","hour_second","hour_minute",
                "day_microsecond","day_second","day_minute","day_hour","year_month"].contains(&name.as_str()) {
                Ok(Node::new(Kind::DateUnit,vec![]).text(name).at(start,end))
            } else { Err(diagnostics.record("invalid date unit",crate::Span { start,end },failure)) }
        }
        rule date_function(level: usize) -> Node
            = start:position!() function:(kw("date_add") { ("date_add",false) } / kw("date_sub") { ("date_sub",false) }
                / kw("adddate") { ("date_add",true) } / kw("subdate") { ("date_sub",true) }) "(" _ date:expr(level) "," _
                interval:(kw("interval") value:expr(level) unit:date_unit() { (value,unit) }
                    / enabled(function.1) value:expr(level) { (value,Node::new(Kind::DateUnit,vec![]).text("day".into())) }) ")" end:position!() _ {?
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text(function.0.into()),vec![date,interval.0,interval.1]).expression(start,end)
            }
            / start:position!() function:(kw("timestampadd") { "timestampadd" } / kw("timestampdiff") { "timestampdiff" })
                "(" _ unit:date_unit() "," _ left:expr(level) "," _ right:expr(level) ")" end:position!() _ {?
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text(function.into()),vec![unit,left,right]).expression(start,end)
            }
            / start:position!() kw("extract") "(" _ unit:date_unit() kw("from") value:expr(level) ")" end:position!() _ {?
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("extract".into()),vec![unit,value]).expression(start,end)
            }
            / start:position!() kw("get_format") "(" _
                unit:(kw("date") { 0 } / kw("time") { 1 } / (kw("datetime") / kw("timestamp")) { 2 })
                "," _ format:expr(level) ")" end:position!() _ {?
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("get_format".into()),
                    vec![Node::new(Kind::HiddenInt,vec![]).value(unit),format]).expression(start,end)
            }
            / start:position!() kw("interval") "(" _ first:expr(level) "," _ rest:expr_list(level) ")" end:position!() _ {?
                let mut arguments=vec![first];arguments.extend(rest);
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("interval".into()),arguments).expression(start,end)
            }
        rule collated(level: usize) -> Node = value:atom(level) collations:(kw("collate") value:collation_name() { value.with_kind(Kind::Collation) })* {?
            collations.into_iter().try_fold(value,|value,collation| {
                let (start,end)=(value.start,collation.end);
                Node::named_call(Kind::SetCollation,Node::new(Kind::Ident,vec![]).text("set_collation".into()),vec![value,collation]).expression(start,end)
            })
        }
        rule unary_parts(level: usize) -> (Vec<(usize, &'input str)>, Node)
            = ops:(start:position!() op:syntax_text(<$([b'+' | b'-' | b'!' | b'~'])>) _ { (start, op) }
                / enabled(mode.high_not_precedence) start:position!() kw("not") !"." { (start, "not") }
                / start:position!() kw("binary") !"." { (start, "binary") })* value:collated(level) { (ops,value) }
        rule unary(level: usize) -> Node = parts:unary_parts(level) {?
            parts.0.into_iter().rev().try_fold(parts.1, |value, (start, op)| Node::unary(op,value,start))
        }
        rule bit_or(level: usize) -> Node = value:precedence!{
            left:(@) "|" !"|" _ right:@ { Node::binary("|".into(),left?,right?) }
            --
            left:(@) "&" !"&" _ right:@ { Node::binary("&".into(),left?,right?) }
            --
            left:(@) op:syntax_text(<$("<<" / ">>")>) _ right:@ { Node::binary(op.into(),left?,right?) }
            --
            left:(@) op:syntax_text(<$("+" / "-")>) _ kw("interval") depth(level) quantity:expr(level+1) unit:date_unit() {
                let date=left?;let (start,end)=(date.start,unit.end);
                let function=if op=="+" { "date_add" } else { "date_sub" };
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text(function.into()),vec![date,quantity,unit]).expression(start,end)
            }
            start:position!() kw("interval") depth(level) quantity:expr(level+1) unit:date_unit() "+" _ right:@ {
                let date:Node=right?;let end=date.end;
                Node::named_call(Kind::Function,Node::new(Kind::Ident,vec![]).text("date_add".into()),vec![date,quantity,unit]).expression(start,end)
            }
            left:(@) op:syntax_text(<$("+" / "-")>) _ right:@ { Node::binary(op.into(),left?,right?) }
            --
            left:(@) op:(s:syntax_text(<$("*" / "/" / "%")>) _ { s } / kw("div") { "div" } / kw("mod") { "mod" }) right:@ { Node::binary(op.into(),left?,right?) }
            --
            left:(@) "^" _ right:@ { Node::binary("^".into(),left?,right?) }
            --
            value:simple_expr(level) { Ok(value) }
        } {? value }
        rule simple_expr(level: usize) -> Node = left:unary(level)
            rest:(enabled(mode.pipes_as_concat) "||" _ right:unary(level) { ("concat".into(),right) })* {? chain(left,rest) }
        rule comparison_tail(level: usize) -> Tail
            = op:(enabled(pl) "==" { "=" } / value:syntax_text(<$("<=>" / "<=" / ">=" / "<>" / "!=" / "=" / "<" / ">")>) { value }) _
                tail:(enabled(op!="<=>") start:position!() kind:(kw("all") { Kind::All } / (kw("any") / kw("some")) { Kind::Any })
                    "(" _ query:select(level+1) ")" end:position!() _ {?
                        Node::new(kind,vec![query]).expression(start,end).map(|right| Tail::Compare(op.into(),right))
                    }
                    / enabled(op=="=") start:position!() kw("all") "(" _ value:expr(level+1) ")" end:position!() _ {?
                        Node::new(Kind::All,vec![value]).expression(start,end).map(|right| Tail::Compare(op.into(),right))
                    }
                    / enabled(op=="=") (kw("any") / kw("some")) "(" _ value:expr(level+1) ")" end:position!() _ { Tail::ArrayMember(value,end) }
                    / right:predicate(level) { Tail::Compare(op.into(),right) }) { tail }
            / kw("is") negated:kw("not")? test:null_literal() { Tail::Is(negated.is_some(), test) }
        rule predicate_tail(level: usize) -> Tail
            = kw("sounds") kw("like") right:simple_expr(level) { Tail::SoundsLike(right) }
            / kw("member") kw("of")? "(" _ value:simple_expr(level+1) ")" end:position!() _ { Tail::Member(value,end) }
            / negated:kw("not")? tail:(kw("between") low:bit_or(level) kw("and") high:predicate(level+1) { Tail::Between(false, low, high) }
                / kw("like") right:simple_expr(level) escape:(kw("escape") value:simple_expr(level) { value })? { Tail::Like(false,right,escape) }
                / (kw("regexp") / kw("rlike")) right:bit_or(level) { Tail::Compare("regexp".into(),right) }
                / kw("in") "(" _ values:(query:select(level+1) &")" { query } / values:expr_list(level+1) { Node::new(Kind::ExprList, values) }) ")" end:position!() _ { Tail::In(false, values, end) })
                { match tail {
                    Tail::Between(_, low, high) => Tail::Between(negated.is_some(), low, high),
                    Tail::In(_, values, end) => Tail::In(negated.is_some(), values, end),
                    Tail::Like(_, right, escape) => Tail::Like(negated.is_some(), right, escape),
                    Tail::Compare(op, right) => Tail::Compare(if negated.is_some() { "not regexp".into() } else { op },right),
                    _ => unreachable!(),
                } }
        rule predicate(level: usize) -> Node = depth(level) left:bit_or(level) tail:predicate_tail(level)? {?
            match tail { Some(tail) => tail.apply(left), None => Ok(left) }
        }
        rule bool_primary(level: usize) -> Node = left:predicate(level) tails:comparison_tail(level)*
            truth:(kw("is") negated:kw("not")? test:(boolean_literal()
                / start:position!() kw("unknown") { Node::new(Kind::DefaultNull,vec![]).at(start,start+7) }) { Tail::Is(negated.is_some(),test) })? {?
            tails.into_iter().chain(truth).try_fold(left, |value, tail| tail.apply(value))
        }
        rule not_expr(level: usize) -> Node = ops:(enabled(!mode.high_not_precedence) start:position!() kw("not") !"." { start })* value:bool_primary(level) {?
            ops.into_iter().rev().try_fold(value, |value, start| { Node::unary("not", value, start) })
        }
        rule and_expr(level: usize) -> Node = left:not_expr(level) rest:((kw("and") / "&&" _) right:not_expr(level) { right })* {? boolean_chain("and", left, rest) }
        rule xor_expr(level: usize) -> Node = left:and_expr(level) rest:(kw("xor") right:and_expr(level) { ("xor".into(), right) })* {? chain(left, rest) }
        rule expr(level: usize) -> Node = left:xor_expr(level) rest:((kw("or") / enabled(!mode.pipes_as_concat) "||" _) right:xor_expr(level) { right })* {? boolean_chain("or", left, rest) }
        rule expr_list(level: usize) -> Vec<Node> = values:(expr(level) ++ ("," _)) { values }
        rule projection_label() -> Node
            = start:position!() text:$("`" ("``" / !"`" [_])* "`") end:position!() _ {
                Node::new(Kind::Ident,vec![]).bytes(decode(text,true)).value(1).at(start,end)
            }
            / enabled(mode.ansi_quotes) start:position!() text:$("\"" ("\"\"" / !"\"" [_])* "\"") end:position!() _ {
                Node::new(Kind::Ident,vec![]).bytes(decode(text,true)).value(1).at(start,end)
            }
            / name:ident() { Node::name(name) }
            / string()
        rule item(level: usize) -> Node = expression:(star() / expr(level)) alias:(enabled(!expression.is_wildcard()) (kw("as") / !implicit_alias_keyword()) value:projection_label() { value })? { Node::projection(expression, alias) }
        rule json_response() -> (Node,Node) = kw("error") { (Node::new(Kind::HiddenInt,vec![]),Node::new(Kind::HiddenNull,vec![])) }
            / start:position!() kw("null") { (Node::new(Kind::NullClause,vec![]).value(1).at(start,start+4),Node::new(Kind::HiddenNull,vec![])) }
            / kw("default") value:signed_literal() { (Node::new(Kind::HiddenInt,vec![]).value(2),value) }
        rule json_options() -> Node
            = empty:(response:json_response() kw("on") kw("empty") { response })?
                error:(response:json_response() kw("on") kw("error") { response })? {
            let default=|| (Node::new(Kind::HiddenInt,vec![]).value(3),Node::new(Kind::HiddenNull,vec![]));
            let (empty_kind,empty_value)=empty.unwrap_or_else(default);let (error_kind,error_value)=error.unwrap_or_else(default);
            Node::new(Kind::Link,vec![empty_kind,empty_value,error_kind,error_value,Node::new(Kind::Link,vec![Node::new(Kind::HiddenInt,vec![]).value(3),Node::new(Kind::HiddenInt,vec![]).value(3)])])
        }
        rule json_column(level: usize) -> Node
            = depth(level) kw("nested") kw("path")? path:string() kw("columns") "(" _ columns:(json_column(level+1) ++ ("," _)) ")" _ {
                Node::new(Kind::JsonColumn,vec![path,Node::new(Kind::ExprList,columns)]).value(5)
            }
            / name:ident() kw("for") kw("ordinality") { Node::new(Kind::JsonColumn,vec![Node::name(name),Node::new(Kind::HiddenInt,vec![])]).value(1) }
            / name:ident() data_type:data_type() collation:(kw("collate") value:collation_name() { value.with_kind(Kind::Collation) })? end:position!()
                exists:kw("exists")? kw("path") path:string()
                options:(enabled(exists.is_none()) value:json_options() { value }
                    / { Node::new(Kind::Link,vec![Node::new(Kind::HiddenInt,vec![]).value(3),Node::new(Kind::HiddenInt,vec![]).value(3)]) }) {
                let mut data_type=data_type;
                data_type.children.extend(collation);
                data_type.end=end;
                Node::new(Kind::JsonColumn,vec![Node::name(name),data_type,Node::new(Kind::HiddenInt,vec![]),path,options]).value(if exists.is_some() { 2 } else { 4 })
            }
        rule json_table(level: usize) -> Node = kw("json_table") "(" _ document:expr(level+1) "," _ path:string() kw("columns") "(" _ columns:(json_column(level+1) ++ ("," _)) ")" _ ")" _ alias:table_alias()? {
            let mut children=vec![document,path,Node::new(Kind::Link,vec![Node::new(Kind::HiddenInt,vec![]).value(3),Node::new(Kind::HiddenInt,vec![]).value(3)]),Node::new(Kind::ExprList,columns)];
            if let Some(alias)=alias { children.push(Node::new(Kind::TableAlias,vec![Node::name(alias)])); }
            Node::new(Kind::JsonTable,children)
        }
        rule table_collection(level: usize) -> Node = kw("table") "(" _ value:expr(level+1) ")" _ alias:table_alias()? {
            let mut children=vec![value];
            if let Some(alias)=alias { children.push(Node::new(Kind::TableAlias,vec![Node::name(alias)])); }
            Node::new(Kind::TableCollection,children)
        }
        rule unnest_unary(level: usize) -> Node = parts:unary_parts(level) {?
            let mut operand=&parts.1;
            while operand.kind==Kind::SetCollation {
                operand=&crate::expression::call_arguments(operand)[0];
            }
            if operand.kind==Kind::UserAssignment { return Err("parenthesized assignment"); }
            parts.0.into_iter().rev().try_fold(parts.1, |value, (start, op)| Node::unary(op,value,start))
        }
        rule unnest_argument(level: usize) -> Node = first:unnest_unary(level)
            rest:(enabled(mode.pipes_as_concat) "||" _ value:unnest_unary(level) { ("concat".into(),value) })* {? chain(first,rest) }
        rule unnest_table(level: usize) -> Node = depth(level) start:position!() kw("unnest") "(" _
            arguments:(unnest_argument(level+1) ++ ("," _)) ")" _
            alias:(name:table_alias() columns:("(" _ names:(ident() ++ ("," _)) ")" _ { names })? { (name,columns) })?
            end:position!() {?
                let mut children=vec![Node::new(Kind::ExprList,arguments)];
                if let Some((alias,columns))=alias {
                    children.push(Node::new(Kind::TableAlias,vec![Node::name(alias)]));
                    if let Some(columns)=columns {
                        children.push(Node::new(Kind::DerivedColumns,columns.into_iter().map(Node::name).collect()));
                    }
                }
                Node::new(Kind::UnnestTable,children).expression(start,end)
            }
        rule hybrid_search_literal() -> Node = value:signed_literal() {?
            if value.kind==Kind::Unary { Err("literal") } else { Ok(value) }
        }
        rule hybrid_search_table() -> Node = kw("hybrid_search") "(" _ table:hybrid_search_literal() "," _
            parameters:(hybrid_search_literal() / user_variable()) ")" _ alias:table_alias()? {?
                Node::table(Node::new(Kind::HybridSearchTable,vec![table,parameters]),alias)
            }
        rule relation(level: usize) -> Node = json_table(level) / table_collection(level) / unnest_table(level) / hybrid_search_table() / lateral:(kw("lateral") &"(")? source:("(" _ query:select(level+1) ")" _ { query }
            / name:relation_name() {? Node::relation(name) }) partition:use_partition()? sample:sample()? snapshot:snapshot(level)?
            alias:table_alias()?
            columns:(enabled(source.kind==Kind::Select) "(" _ names:(ident() ++ ("," _)) ")" _ { names })? indexes:table_index_hint()* {?
                if lateral.is_some() && source.kind!=Kind::Select { return Err("derived table"); }
                if (partition.is_some() || sample.is_some() || !indexes.is_empty()) && source.kind==Kind::Select { return Err("base table"); }
                let mut table=Node::table(source,alias)?;
                if !indexes.is_empty() { table.children.push(Node::new(Kind::TableIndexHints,indexes)); }
                table.children.extend(partition);
                table.children.extend(sample);
                table.children.extend(snapshot);
                if let Some(columns)=columns {
                    table.children.push(Node::new(Kind::DerivedColumns,columns.into_iter().map(Node::name).collect()));
                }
                if lateral.is_some() { table.children.push(Node::new(Kind::Lateral,vec![])); }
                Ok(table)
            }
            / depth(level) "(" _ first:table(level+1) rest:("," _ value:table(level+1) { value })* ")" _ {?
                let mut value=rest.into_iter().try_fold(first, |left,right| {
                    crate::query::joined_table(Kind::InnerJoin,left,right,Node::missing())
                })?;
                value.children.push(Node::new(Kind::ParenthesizedTable,vec![]));
                Ok(value)
            }
        rule snapshot(level: usize) -> Node = kw("as") kw("of") kw("snapshot") value:expr(level) {
            Node::new(Kind::Snapshot,vec![value,Node::new(Kind::Ident,vec![])])
        }
        rule sample_number(integer: bool) -> Node
            = value:(enabled(integer) value:type_size() { value } / enabled(!integer) value:number() { value }) {
                let kind=match value.kind {
                    Kind::Int => Kind::SampleInt,
                    Kind::Number | Kind::Double if !integer => Kind::SampleDecimal,
                    kind => kind,
                };
                value.with_kind(kind)
            }
        rule sample() -> Node = kw("sample") method:(kw("block") { 2 } / kw("hybrid") { 3 })?
            scope:(kw("all") { Kind::All } / kw("base") { Kind::SampleBase } / kw("incr") { Kind::SampleIncremental })?
            "(" _ percent:sample_number(false) ")" _ seed:(kw("seed") "(" _ value:sample_number(true) ")" _ { value })? {
                Node::new(Kind::Sample,vec![Node::new(Kind::DefaultInt,vec![]).value(method.unwrap_or(1)),percent,Node::optional(seed),Node::new(scope.unwrap_or(Kind::All),vec![])])
            }
        rule use_partition() -> Node = kw("partition") "(" _ names:partition_names(true) ")" _ {
            Node::new(Kind::UsePartition,vec![names])
        }
        rule table_index_hint() -> Node = kind:(kw("use") { Kind::Use } / kw("force") { Kind::Force } / kw("ignore") { Kind::Ignore })
            (kw("index") / kw("key")) (kw("for") (kw("join") / ((kw("order") / kw("group")) kw("by"))))?
            "(" _ names:((name:ident() { Node::name(name) } / start:position!() kw("primary") { Node::new(Kind::Ident,vec![]).text("PRIMARY".into()).at(start,start+7) }) ** ("," _)) ")" _ {?
                if names.is_empty() && kind!=Kind::Use { Err("index name") }
                else { Ok(Node::new(Kind::TableIndexHint,vec![Node::new(kind,vec![]),Node::list(Kind::Names,names)])) }
            }
        rule join(level: usize) -> (Kind, Node, Node)
            = kw("natural") kind:((kw("left") kw("outer")?) { Kind::LeftJoin }
                / (kw("right") kw("outer")?) { Kind::RightJoin } / (kw("full") kw("outer")?) { Kind::FullJoin }
                / kw("inner") { Kind::InnerJoin })? kw("join") relation:relation(level) {
                (kind.unwrap_or(Kind::InnerJoin),relation,Node::new(Kind::NaturalJoin,vec![]))
            }
            / kind:(kw("straight_join") { Kind::StraightJoin }
                / kind:((kw("left") kw("outer")?) { Kind::LeftJoin }
                    / (kw("right") kw("outer")?) { Kind::RightJoin } / (kw("full") kw("outer")?) { Kind::FullJoin }
                    / kw("inner") { Kind::InnerJoin } / kw("cross") { Kind::InnerJoin })?
                    kw("join") { kind.unwrap_or(Kind::InnerJoin) })
            relation:table(level+1)
            condition:(kw("on") value:expr(level) { Node::new(Kind::JoinOn,vec![value]) }
                / kw("using") "(" _ names:(ident() ++ ("," _)) ")" _ { Node::new(Kind::ColumnList, names.into_iter().map(Node::name).collect()) })? {?
                if matches!(kind, Kind::LeftJoin | Kind::RightJoin | Kind::FullJoin) && condition.is_none() { Err("join condition") }
                else { Ok((kind, relation, condition.unwrap_or_else(Node::missing))) }
            }
        rule table(level: usize) -> Node = value:table_reference(level,false) { value }
        rule table_reference(level: usize, escaped: bool) -> Node = depth(level)
            head:(value:relation(level) { (value,false) }
                / "{" _ kw("oj") value:table_reference(level+1,true) "}" _
                    alias:(enabled(value.kind==Kind::Table && !crate::query::table_has_alias(&value)) name:table_alias() { name })? {
                    let mut value=value;
                    if let Some(alias)=alias { value.children.push(Node::new(Kind::TableAlias,vec![Node::name(alias)])); }
                    (value,true)
                })
            joins:join(level)* {?
                if escaped && head.1 && joins.is_empty() { return Err("table factor or joined table"); }
                joins.into_iter().try_fold(head.0, |left, (kind, right, condition)| {
                    crate::query::associate_join(kind,left,right,condition)
                })
            }
        rule array_order_item(level: usize) -> Node = key:order_item(level)
            nulls:(kw("nulls") value:(kw("first") { 1 } / kw("last") { 2 }) { value })? {
                key.value(nulls.unwrap_or(0))
            }
        rule order_item(level: usize) -> Node = expression:expr(level) direction:(kw("asc") { Kind::Asc } / kw("desc") { Kind::Desc })? {
            Node::new(Kind::SortKey, vec![expression, Node::new(direction.unwrap_or(Kind::Asc), vec![])])
        }
        rule null_treatment() -> Node = kind:(kw("respect") { Kind::Respect } / kw("ignore") { Kind::Ignore }) kw("nulls") { Node::new(kind,vec![]) }
        rule window_value(level: usize) -> Node
            = kind:(kw("first_value") { Kind::FirstValue } / kw("last_value") { Kind::LastValue }) "(" _ values:expr_list(level)?
                nulls:(value:null_treatment() ")" _ { Some(value) } / ")" _ value:null_treatment()? { value }) {
                let mut result=Node::callable(kind,values.unwrap_or_default());result.children.extend(nulls);result
            }
            / kind:(kw("lead") { Kind::Lead } / kw("lag") { Kind::Lag }) "(" _ first:expr(level)?
                rest:(enabled(first.is_some()) nulls:null_treatment() rest:("," _ values:expr_list(level) { values })? ")" _ { (rest.unwrap_or_default(),Some(nulls)) }
                    / rest:(enabled(first.is_some()) "," _ values:expr_list(level) { values })? ")" _ nulls:null_treatment()? { (rest.unwrap_or_default(),nulls) }) {
                let mut arguments:Vec<_>=first.into_iter().collect(); arguments.extend(rest.0);
                let mut result=Node::callable(kind,arguments);result.children.extend(rest.1);result
            }
            / kw("nth_value") "(" _ values:expr_list(level)? ")" _
                from:(kw("from") kind:(kw("first") { Kind::First } / kw("last") { Kind::Last }) { Node::new(kind,vec![]) })?
                nulls:null_treatment()? {
                let mut result=Node::callable(Kind::NthValue,values.unwrap_or_default());result.children.extend(from);result.children.extend(nulls);result
            }
        rule window_bound(level: usize) -> Node
            = kw("current") kw("row") { Node::new(Kind::WindowBound,vec![]).value(1) }
            / value:(start:position!() kw("unbounded") {
                    Node::new(Kind::WindowInterval,vec![Node::new(Kind::Ident,vec![]).text("unbounded".into())]).text("unbounded".into()).value(1).at(start,start+9)
                }
                / value:(number() / parameter()) {
                    let (start,end)=(value.start,value.end);
                    Node::new(Kind::WindowInterval,vec![value]).value(1).at(start,end)
                }
                / start:position!() kw("interval") value:expr(level+1) unit:date_unit() {
                    let end=unit.end;
                    Node::new(Kind::WindowInterval,vec![value,unit]).at(start,end)
                }) direction:(kw("preceding") { 1 } / kw("following") { 2 }) {
                    Node::new(Kind::WindowBound,vec![value,Node::new(Kind::HiddenInt,vec![]).value(direction)]).value(2)
                }
        rule window_frame(level: usize) -> Node = kind:(kw("rows") { 1 } / kw("range") { 2 })
            bounds:(kw("between") first:window_bound(level) kw("and") last:window_bound(level) { (1,vec![first,last]) }
                / first:window_bound(level) { (2,vec![first]) }) {
                let mut children=vec![Node::new(Kind::HiddenInt,vec![]).value(kind)];
                children.extend(bounds.1);
                Node::new(Kind::WindowFrame,children).value(bounds.0)
            }
        rule window_name() -> Name = !(kw("rows") / kw("commit") / kw("end") / kw("except")
            / kw("intersect") / kw("lateral") / kw("minus") / kw("over") / kw("rollback") / kw("window")) name:ident() { name }
        rule window_spec(level: usize) -> Node = start:position!() "(" _ name:window_name()?
            partition:(kw("partition") kw("by") values:expr_list(level) { values })?
            order:(kw("order") kw("by") values:(order_item(level) ++ ("," _)) { values })?
            frame:window_frame(level)? ")" end:position!() _ {
                let mut spec=vec![];
                if let Some(partition)=partition { spec.push(Node::new(Kind::ExprList,partition)); }
                if let Some(order)=order { spec.push(Node::new(Kind::Order,order)); }
                spec.extend(frame);
                let mut children=vec![Node::new(Kind::WindowSpec,spec)];children.extend(name.map(Node::name));
                Node::new(Kind::Window,children).at(start,end)
            }
        rule window(level: usize) -> Node = window_spec(level) / name:window_name() {
            let (start,end)=(name.start,name.end);
            Node::new(Kind::Window,vec![Node::name(name)]).at(start,end)
        }
        rule named_window(level: usize) -> Node = name:window_name() kw("as") window:window_spec(level) {
            Node::new(Kind::NamedWindow,vec![Node::name(name),window])
        }
        rule limit_value() -> Node = parameter() / type_size()
        rule limit_expr() -> Node = limit_value() / enabled(pl) value:column() { value }
        rule limit() -> Node = kw("limit") first:limit_expr() tail:(kw("offset") value:limit_expr() { (false, value) } / "," _ value:limit_expr() { (true, value) })? {
            match tail {
                Some((true, count)) => Node::new(Kind::CommaLimit, vec![first, count]),
                Some((false, offset)) => Node::new(Kind::Limit, vec![first, offset]),
                None => Node::new(Kind::Limit, vec![first]),
            }
        }
        rule lock_wait() -> Node
            = kw("skip") kw("locked") { Node::new(Kind::SkipLocked,vec![]) }
            / start:position!() (kw("nowait") / kw("no_wait")) { Node::new(Kind::LockWait,vec![]).at(start,start) }
            / kw("wait") start:position!() text:syntax_text(<$([b'0'..=b'9']+ ("." [b'0'..=b'9']+)?)>) end:position!() _ {?
                let mut value=Node::number(text,start,end)?;
                value.kind=if matches!(value.kind,Kind::Int|Kind::Uint) { Kind::LockWait } else { Kind::LockWaitDecimal };
                Ok(value)
            }
        rule lock() -> Node
            = kw("for") kw("update") wait:lock_wait()? { wait.unwrap_or_else(|| Node::new(Kind::LockWait,vec![]).value(u64::MAX)) }
            / kw("lock") kw("in") kw("share") kw("mode") { Node::new(Kind::LockWait,vec![]).value(u64::MAX) }
        rule hint_string() -> Node
            = start:position!() text:$("'" ("''" / "\\" [_] / !"'" [_])* "'") end:position!() _ {
                Node::new(Kind::String,vec![]).bytes(decode(text,false)).at(start,end)
            }
        rule hint_name() -> Name
            = start:position!() text:syntax_text(<$("\"" ("\"\"" / !"\"" [_])+ "\"")>) end:position!() _ {
                Name { text:text[1..text.len()-1].replace("\"\"","\""),quoted:true,start,end }
            }
            / start:position!() text:syntax_text(<$(word())>) end:position!() _ { Name { text:text.into(),quoted:false,start,end } }
            / ident()
        rule hint_qb_name() -> Node = name:hint_name() {
            let mut name=name;name.text.make_ascii_uppercase();Node::name(name)
        }
        rule hint_qb() -> Node = "@" _ name:hint_qb_name() { name }
        rule hint_qb_names() -> Node = "(" _ names:(hint_qb_name() ++ (("," _)?)) ")" _ {
            Node::new(Kind::HintQbNames,names)
        }
        rule hint_relation() -> Node = names:(hint_name() ++ ("." _)) qb:hint_qb()? {?
            Ok(Node::new(Kind::HintRelation,vec![Node::relation(names)?,Node::optional(qb)]))
        }
        rule hint_leading_item(level: usize) -> Node = depth(level)
            value:("(" _ value:hint_leading(level+1) ")" _ { value } / hint_relation()) { value }
        rule hint_leading(level: usize) -> Node = first:hint_leading_item(level) rest:(("," _)? value:hint_leading_item(level) { value })* {
            rest.into_iter().fold(first,|left,right| Node::new(Kind::Link,vec![left,right]))
        }
        rule hint_join_item() -> Node
            = "(" _ tables:(hint_relation() ++ (("," _)?)) ")" _ { Node::new(Kind::HintTables,tables) }
            / hint_relation()
        rule distribute_method() -> Node = kind:(kw("all") { Kind::DistributeAll }
                / kw("none") { Kind::DistributeNone }
                / kw("partition") { Kind::DistributePartition }
                / kw("random") { Kind::DistributeRandom }
                / kw("hash") { Kind::DistributeHash }
                / kw("broadcast") { Kind::DistributeBroadcast }
                / kw("local") { Kind::DistributeLocal }
                / kw("bc2host") { Kind::DistributeBc2Host }
                / kw("range") { Kind::DistributeRange }
                / kw("list") { Kind::DistributeList }
                / kw("basic") { Kind::DistributeBasic }
                / kw("hash_local") { Kind::DistributeHashLocal }) { Node::new(kind,vec![]) }
        rule window_distribution() -> Node =
            indexes:("(" _ ids:((("+" _)? value:hint_integer() { value }) ++ (("," _)?)) ")" _ { ids })?
            method:distribute_method()
            flags:(kw("partition_sort") { 1 } / kw("pushdown") { 2 } / kw("wf_topn") { 4 })* {
                let mut children=vec![method];children.extend(indexes.unwrap_or_default());
                Node::new(Kind::WindowDistribution,children).value(flags.into_iter().fold(0,|flags,value| flags|value))
            }
        rule hint_option() -> Node
            = kw("leading") "(" _ qb:hint_qb()? tables:hint_leading(0) ")" _ { Node::new(Kind::Leading,vec![Node::optional(qb),tables]) }
            / kw("ordered") qb:("(" _ value:hint_qb()? ")" _ { value })? {
                Node::new(Kind::Ordered,vec![Node::optional(qb.flatten())])
            }
            / kind:(kw("begin_outline_data") { Kind::BeginOutline } / kw("end_outline_data") { Kind::EndOutline }) { Node::new(kind,vec![]) }
            / kw("pq_distribute") "(" _ qb:hint_qb()? tables:hint_join_item()
                methods:(("," _)? outer:distribute_method() ("," _)? inner:distribute_method() { (outer,inner) })? ")" _ {
                    let (outer,inner)=methods.unwrap_or_else(|| (Node::missing(),Node::missing()));
                    Node::new(Kind::PqDistribute,vec![Node::optional(qb),tables,outer,inner])
                }
            / kind:(kw("px_join_filter") { Kind::JoinFilter } / kw("no_px_join_filter") { Kind::NoJoinFilter }
                / kw("px_part_join_filter") { Kind::PartJoinFilter } / kw("no_px_part_join_filter") { Kind::NoPartJoinFilter })
                "(" _ qb:hint_qb()? table:hint_relation()
                build:(tables:hint_join_item() target:hint_relation()? { (tables,target) })? ")" _ {
                    let (tables,target)=build.map_or((None,None),|(tables,target)| (Some(tables),target));
                    Node::new(kind,vec![Node::optional(qb),table,Node::optional(tables),Node::optional(target)])
                }
            / kw("pq_distribute_window") "(" _ qb:hint_qb()? ("," _)?
                options:(window_distribution() ++ (("," _)?)) ")" _ {
                    let mut children=vec![Node::optional(qb)];children.extend(options);
                    Node::new(Kind::PqDistributeWindow,children)
                }
            / kw("pq_map") "(" _ qb:hint_qb()? tables:hint_join_item() ")" _ { Node::new(Kind::PqMap,vec![Node::optional(qb),tables]) }
            / kw("pq_set") "(" _ qb:hint_qb() target:(!distribute_method() value:hint_qb_name() { value })?
                methods:(distribute_method() ** (("," _)?)) ")" _ { Node::new(Kind::PqSet,vec![qb,Node::optional(target),Node::list(Kind::DistributeMethods,methods)]) }
            / kw("pq_subquery") "(" _ qb:hint_qb()? ("," _)? names:hint_qb_names()?
                methods:(distribute_method() ** (("," _)?)) ")" _ {
                    Node::new(Kind::PqSubquery,vec![Node::optional(qb),Node::optional(names),
                        Node::list(Kind::DistributeMethods,methods)])
                }
            / kind:(kw("pq_gby") { Kind::PqGroupBy } / kw("pq_distinct") { Kind::PqDistinct }) "(" _ qb:hint_qb()?
                method:distribute_method() sort:(enabled(kind==Kind::PqGroupBy) ("," _)? value:type_size() { value })? ")" _ {
                    Node::new(kind,vec![Node::optional(qb),method,Node::optional(sort)])
                }
            / kind:(kw("gby_pushdown") { Kind::GroupByPushdown } / kw("no_gby_pushdown") { Kind::NoGroupByPushdown }
                / kw("use_hash_distinct") { Kind::HashDistinct } / kw("no_use_hash_distinct") { Kind::NoHashDistinct }
                / kw("distinct_pushdown") { Kind::DistinctPushdown } / kw("no_distinct_pushdown") { Kind::NoDistinctPushdown }
                / kw("use_hash_set") { Kind::HashSet } / kw("no_use_hash_set") { Kind::NoHashSet })
                qb:("(" _ qb:hint_qb()? ")" _ { qb })? { Node::new(kind,vec![Node::optional(qb.flatten())]) }
            / kw("dbms_stats") { Node::new(Kind::DbmsStats,vec![]) }
            / kind:(kw("enable_parallel_dml") { Kind::EnableParallelDml } / kw("disable_parallel_dml") { Kind::DisableParallelDml }
                / kw("enable_parallel_das_dml") { Kind::EnableParallelDasDml } / kw("disable_parallel_das_dml") { Kind::DisableParallelDasDml }
                / kw("gather_optimizer_statistics") { Kind::GatherStatistics } / kw("no_gather_optimizer_statistics") { Kind::NoGatherStatistics }
                / kw("trace_log") { Kind::TraceLog }
                / kw("no_query_transformation") { Kind::NoQueryTransformation }
                / kw("no_cost_based_query_transformation") { Kind::NoCostBasedQueryTransformation }
                / kw("cursor_sharing_exact") { Kind::CursorSharingExact }
                / kw("no_parallel") { Kind::NoParallel }) { Node::new(kind,vec![]) }
            / kind:(kw("tracing") { Kind::Tracing } / kw("stat") { Kind::Stat } / kw("blocking") { Kind::Blocking })
                "(" _ targets:(enabled(kind==Kind::Blocking) value:hint_string() { vec![value] }
                    / ids:((("+" _)? value:hint_integer() { value }) ++ (("," _)?)) { ids }) ")" _ {
                    Node::new(kind,targets)
                }
            / kw("dop") "(" _ ("+" _)? fragment:hint_integer() "," _ ("+" _)? degree:hint_integer() ")" _ {
                Node::new(Kind::Dop,vec![fragment,degree])
            }
            / kw("dml_parallel") "(" _ value:type_size() ")" _ { Node::new(Kind::DmlParallel,vec![value]) }
            / kw("query_timeout") "(" _ value:type_size() ")" _ { Node::new(Kind::QueryTimeout,vec![value]) }
            / kw("log_level") "(" _ value:(&"'" value:load_string() { value } / name:hint_name() { Node::name(name) }) ")" _ {
                Node::new(Kind::LogLevel,vec![value])
            }
            / kw("load_batch_size") "(" _ size:hint_integer() buffer:("," _ &"'" value:load_string() { value })? ")" _ {
                let mut children=vec![size];children.extend(buffer);Node::new(Kind::LoadBatchSize,children)
            }
            / kw("topk") "(" _ minimum:type_size() precision:type_size() ")" _ { Node::new(Kind::TopK,vec![minimum,precision]) }
            / kw("read_consistency") "(" _ level:(kw("weak") { 3 } / kw("strong") { 4 } / kw("frozen") { 2 }) ")" _ { Node::new(Kind::ReadConsistency,vec![]).value(level) }
            / kw("dynamic_sampling") "(" _ value:(value:type_size() { Node::new(Kind::DynamicSampling,vec![value]) }
                / qb:hint_qb()? table:hint_relation() ("," _)? level:type_size() blocks:(("," _)? value:type_size() { value })? {
                    Node::new(Kind::TableDynamicSampling,vec![Node::optional(qb),table,level,Node::optional(blocks)])
                }) ")" _ { value }
            / kw("parallel") "(" _ value:(value:type_size() { Node::new(Kind::Parallel,vec![value]) }
                / kw("manual") { Node::new(Kind::Parallel,vec![]).value(1) }
                / kw("auto") { Node::new(Kind::Parallel,vec![]).value(2) }
                / qb:hint_qb()? table:hint_relation() ("," _)? value:type_size() { Node::new(Kind::TableParallel,vec![Node::optional(qb),table,value]) }) ")" _ { value }
            / kind:(kw("full") { Kind::FullScan } / kw("use_das") { Kind::UseDas } / kw("no_use_das") { Kind::NoUseDas })
                "(" _ qb:hint_qb()? table:hint_relation() ")" _ { Node::new(kind,vec![Node::optional(qb),table]) }
            / kw("union_merge") "(" _ qb:hint_qb()? table:hint_relation()
                names:(hint_name() ++ (("," _)?)) ")" _ {
                    Node::new(Kind::UnionMerge,vec![Node::optional(qb),table,
                        Node::list(Kind::Names,names.into_iter().map(Node::name).collect())])
                }
            / kind:(kw("index") { Kind::IndexScan } / kw("no_index") { Kind::NoIndex }
                / kw("index_asc") { Kind::IndexAsc } / kw("index_desc") { Kind::IndexDesc })
                "(" _ qb:hint_qb()? table:hint_relation() ("," _)? index:hint_name()
                prefix:(enabled(kind!=Kind::NoIndex) negative:("-" _)? value:type_size() {
                    let mut value=value;if negative.is_some() { value.value=value.value.wrapping_neg(); } value
                })? ")" _ { Node::new(kind,vec![Node::optional(qb),table,Node::name(index),Node::optional(prefix)]) }
            / kind:(kw("use_nl") { Kind::UseNl } / kw("no_use_nl") { Kind::NoUseNl }
                / kw("use_nl_materialization") { Kind::UseNlMaterialization } / kw("no_use_nl_materialization") { Kind::NoUseNlMaterialization }
                / kw("use_hash") { Kind::UseHash } / kw("no_use_hash") { Kind::NoUseHash }
                / kw("use_merge") { Kind::UseMerge } / kw("no_use_merge") { Kind::NoUseMerge })
                "(" _ qb:hint_qb()? tables:(hint_join_item() ++ (("," _)?)) ")" _ {
                    Node::new(kind,vec![Node::optional(qb),Node::new(Kind::JoinHintTables,tables)])
                }
            / kw("use_plan_cache") "(" _ policy:(kw("none") { 1 } / kw("default") { 2 }) ")" _ { Node::new(Kind::UsePlanCache,vec![]).value(policy) }
            / kind:(kw("use_concat") { Kind::UseConcat } / kw("no_expand") { Kind::NoExpand })
                args:("(" _ qb:hint_qb()? condition:(enabled(kind==Kind::UseConcat) value:string() { value })? ")" _ { (qb,condition) })? {
                    let (qb,condition)=args.unwrap_or_default();Node::new(kind,vec![Node::optional(qb),Node::optional(condition)])
                }
            / kind:(kw("merge") { Kind::MergeView } / kw("no_merge") { Kind::NoMergeView })
                args:("(" _ qb:hint_qb()? target:(enabled(kind==Kind::MergeView) direction:("<" _ { 1 } / ">" _ { 0 }) name:hint_qb_name() { (direction,name) })? ")" _ { (qb,target) })? {
                    let (qb,target)=args.unwrap_or_default();
                    let (direction,target)=target.unwrap_or_else(|| (0,Node::missing()));
                    Node::new(kind,vec![Node::optional(qb),target]).value(direction)
                }
            / kw("semi_to_inner") args:("(" _ qb:hint_qb()? tables:(hint_join_item() ++ (("," _)?))? ")" _ { (qb,tables) })? {
                    let (qb,tables)=args.unwrap_or_default();
                    let mut children=vec![Node::optional(qb)];
                    if let Some(tables)=tables { children.push(Node::new(Kind::JoinHintTables,tables)); }
                    Node::new(Kind::SemiToInner,children)
                }
            / kind:(kw("eliminate_join") { Kind::EliminateJoin } / kw("no_eliminate_join") { Kind::NoEliminateJoin }
                / kw("left_to_anti") { Kind::LeftToAnti } / kw("no_left_to_anti") { Kind::NoLeftToAnti }
                / kw("place_group_by") { Kind::PlaceGroupBy } / kw("no_place_group_by") { Kind::NoPlaceGroupBy }
                / kw("win_magic") { Kind::WindowMagic } / kw("no_win_magic") { Kind::NoWindowMagic })
                args:("(" _ qb:hint_qb()? tables:(enabled(matches!(kind,Kind::EliminateJoin|Kind::LeftToAnti|Kind::PlaceGroupBy|Kind::WindowMagic))
                    tables:(hint_join_item() ++ (("," _)?)) { tables })? ")" _ { (qb,tables) })? {
                    let (qb,tables)=args.unwrap_or_default();
                    Node::new(kind,vec![Node::optional(qb),Node::list(Kind::JoinHintTables,tables.unwrap_or_default())])
                }
            / kind:(kw("count_to_exists") { Kind::CountToExists } / kw("no_count_to_exists") { Kind::NoCountToExists })
                args:("(" _ qb:hint_qb()? names:(enabled(kind==Kind::CountToExists)
                    names:(hint_qb_name() ++ (("," _)?)) { names })? ")" _ { (qb,names) })? !"(" {
                    let (qb,names)=args.unwrap_or_default();
                    Node::new(kind,vec![Node::optional(qb),Node::list(Kind::HintQbNames,names.unwrap_or_default())])
                }
            / kind:(kw("materialize") { Kind::Materialize } / kw("coalesce_sq") { Kind::CoalesceSubqueries })
                args:("(" _ qb:hint_qb()? groups:hint_qb_names()* ")" _ { (qb,groups) })? {
                    let (qb,groups)=args.unwrap_or_default();
                    let mut children=vec![Node::optional(qb)];
                    if !groups.is_empty() { children.push(Node::new(Kind::HintQbGroups,groups)); }
                    Node::new(kind,children)
                }
            / kind:(kw("use_distributed_dml") { Kind::UseDistributedDml }
                / kw("no_use_distributed_dml") { Kind::NoUseDistributedDml })
                qb:("(" _ qb:hint_qb()? ")" _ { qb })? !"(" {
                    Node::new(kind,vec![Node::optional(qb.flatten())])
                }
            / kind:(kw("no_rewrite") { Kind::NoRewrite }
                / kw("outer_to_inner") { Kind::OuterToInner } / kw("no_outer_to_inner") { Kind::NoOuterToInner }
                / kw("unnest") { Kind::Unnest } / kw("no_unnest") { Kind::NoUnnest }
                / kw("simplify_group_by") { Kind::SimplifyGroupBy } / kw("no_simplify_group_by") { Kind::NoSimplifyGroupBy }
                / kw("pred_deduce") { Kind::PredicateDeduce } / kw("no_pred_deduce") { Kind::NoPredicateDeduce }
                / kw("project_prune") { Kind::ProjectPrune } / kw("no_project_prune") { Kind::NoProjectPrune }
                / kw("no_semi_to_inner") { Kind::NoSemiToInner }
                / kw("no_coalesce_sq") { Kind::NoCoalesceSubqueries }
                / kw("push_pred_cte") { Kind::PushPredCte }
                / kw("no_push_pred_cte") { Kind::NoPushPredCte }
                / kw("replace_const") { Kind::ReplaceConst }
                / kw("no_replace_const") { Kind::NoReplaceConst }
                / kw("simplify_order_by") { Kind::SimplifyOrderBy }
                / kw("no_simplify_order_by") { Kind::NoSimplifyOrderBy }
                / kw("simplify_distinct") { Kind::SimplifyDistinct }
                / kw("no_simplify_distinct") { Kind::NoSimplifyDistinct }
                / kw("simplify_winfunc") { Kind::SimplifyWinfunc }
                / kw("no_simplify_winfunc") { Kind::NoSimplifyWinfunc }
                / kw("simplify_expr") { Kind::SimplifyExpr }
                / kw("no_simplify_expr") { Kind::NoSimplifyExpr }
                / kw("simplify_limit") { Kind::SimplifyLimit }
                / kw("no_simplify_limit") { Kind::NoSimplifyLimit }
                / kw("simplify_subquery") { Kind::SimplifySubquery }
                / kw("no_simplify_subquery") { Kind::NoSimplifySubquery }
                / kw("fast_minmax") { Kind::FastMinmax }
                / kw("no_fast_minmax") { Kind::NoFastMinmax }
                / kw("simplify_set") { Kind::SimplifySet }
                / kw("no_simplify_set") { Kind::NoSimplifySet }
                / kw("push_limit") { Kind::PushLimit }
                / kw("no_push_limit") { Kind::NoPushLimit }
                / kw("pullup_expr") { Kind::PullupExpr }
                / kw("no_pullup_expr") { Kind::NoPullupExpr }
                / kw("aggr_first_unnest") { Kind::AggrFirstUnnest }
                / kw("no_aggr_first_unnest") { Kind::NoAggrFirstUnnest }
                / kw("join_first_unnest") { Kind::JoinFirstUnnest }
                / kw("no_join_first_unnest") { Kind::NoJoinFirstUnnest }
                / kw("decorrelate") { Kind::Decorrelate }
                / kw("no_decorrelate") { Kind::NoDecorrelate }
                / kw("use_late_materialization") { Kind::UseLateMaterialization }
                / kw("no_use_late_materialization") { Kind::NoUseLateMaterialization }
                / kw("transform_distinct_agg") { Kind::TransformDistinctAgg }
                / kw("no_transform_distinct_agg") { Kind::NoTransformDistinctAgg }
                / kw("push_subq") { Kind::PushSubq }
                / kw("no_push_subq") { Kind::NoPushSubq }
                / kw("inline") { Kind::Inline })
                qb:("(" _ qb:hint_qb()? ")" _ { qb })? { Node::new(kind,vec![Node::optional(qb.flatten())]) }
            / kind:(kw("use_hash_aggregation") { Kind::HashAggregate } / kw("no_use_hash_aggregation") { Kind::NoHashAggregate })
                args:("(" _ qb:hint_qb()?
                    sort:(enabled(kind==Kind::NoHashAggregate)
                        value:(kw("no_partition_sort") { 0 } / kw("partition_sort") { 1 }) { value })?
                    ")" _ { (qb,sort.unwrap_or(u64::MAX)) })? {
                    let (qb,sort)=args.unwrap_or((None,u64::MAX));
                    Node::new(kind,vec![Node::optional(qb)]).value(sort)
                }
            / kw("qb_name") "(" _ name:hint_qb_name() ")" _ { Node::new(Kind::QbName,vec![name]) }
            / kw("opt_param") "(" _ name:hint_string() ("," _)? value:(
                hint_string()
                / start:position!() sign:("-" _ { "-" } / "+" _ { "+" })? value:type_size() {?
                    match sign { Some(sign) => Node::unary(sign,value,start),None => Ok(value) }
                }
            ) ")" _ { Node::new(Kind::OptParam,vec![name,value]) }
        rule hints() -> Vec<Node> = "/*+" _ options:((value:hint_option() { value }
                / (&[b'\''|b'"'|b'`'] opaque_comment_token() / cont()+
                    / ![b'\''|b'"'|b'`'] !"*/" [_]) _ { Node::missing() })*) "*/" _ {
            options.into_iter().filter(|value| value.kind!=Kind::Missing).collect()
        }
        rule select(level: usize) -> Node = value:query_expression(level) {? Node::select(value) }
        rule with_clause(level: usize) -> Node = kw("with") recursive:kw("recursive")?
            tables:(common_table(level,recursive.is_some()) ++ ("," _)) {
                Node::new(Kind::WithClause,tables).value(u64::from(recursive.is_some()))
            }
        rule common_table(level: usize,recursive: bool) -> Node = name:ident()
            columns:("(" _ names:(ident() ++ ("," _)) ")" _ { names })? kw("as") "(" _ query:query_expression(level+1) ")" _ {?
                query.into_cte(name,columns.unwrap_or_default(),recursive)
            }
        rule set_operator() -> Kind
            = kw("union") modifier:(kw("all") { Kind::UnionAll } / distinct_keyword() { Kind::Union })? { modifier.unwrap_or(Kind::Union) }
            / kw("intersect") { Kind::Intersect }
            / (kw("except") / kw("minus")) { Kind::Except }
        rule query_expression(level: usize) -> Box<Select> = depth(level) with:with_clause(level)? first:query_term(level)
            rest:((kind:set_operator() right:query_term(level) { (kind,right) })*)
            order:(kw("order") kw("by") values:(order_item(level) ++ ("," _)) { values })?
            approximate:(start:position!() (kw("approx") / kw("approximate")) end:position!() { Node::new(Kind::Approximate,vec![]).at(start,end) })?
            limit:limit()? parameters:(enabled(approximate.is_some()) value:vector_parameters() { value })? lock:lock()? into:select_into(level)? {?
                let value=rest.into_iter().try_fold(first,|left,(kind,right)| left.combine(kind,right))?;
                let mut value=value.finish(order.unwrap_or_default(),limit,lock,into,approximate,parameters)?;
                value.with=with.or(value.with);Ok(value)
            }
        rule vector_parameter_value() -> Node
            = value:number() { value.with_kind(Kind::Number) }
            / value:bool_null() {? if value.kind==Kind::Bool { Ok(value) } else { Err("boolean vector query parameter") } }
            / name:ident() { Node::name(name) }
        rule vector_parameters() -> Node = kw("parameters") value:vector_parameter_list() { value }
        rule vector_parameter_list() -> Node = "(" _ start:position!()
            values:((name:ident() "=" _ value:vector_parameter_value() { Node::new(Kind::VectorParameter,vec![Node::name(name),value]) }) ++ ("," _)) end:position!() ")" _ {
                Node::new(Kind::VectorParameters,values).at(start,end)
            }
        rule query_term(level: usize) -> Box<Select>
            = "(" _ value:query_expression(level+1) ")" _ { let mut value=value;value.parenthesized=true;value }
            / select_query(level) / values_query(level)
        rule select_into(level: usize) -> Node = start:position!() kw("into") value:(
                kw("outfile") filename:load_string() options:output_options(level) {
                    options.into_node(Kind::IntoOutfile,filename)
                }
                / kw("dumpfile") filename:load_string() {
                    select_into::FileOutput::default().into_node(Kind::IntoDumpfile,filename)
                }
                / !kw("outfile") variables:((user_variable() / name:ident() { Node::name(name) }) ++ ("," _)) {
                    Node::new(Kind::IntoVariables,vec![Node::new(Kind::IntoVariableList,variables)])
                }) end:position!() { value.at(start,end) }
        rule output_options(level: usize) -> select_into::FileOutput
            = kw("format") ("=" _)? "(" _ values:(output_format_option(level) ++ (("," _)?)) ")" _ file_options:output_file_option()* {
                select_into::FileOutput { external_format:values,file_options,..Default::default() }
            }
            / charset:(charset_keyword() !kw("default") value:charset_name() { value })?
                fields:((kw("fields") / kw("columns")) values:load_field_option()+ { values })?
                lines:(kw("lines") values:load_line_option()+ { values })? file_options:output_file_option()* {
                    select_into::FileOutput { charset,fields:fields.unwrap_or_default(),lines:lines.unwrap_or_default(),file_options,..Default::default() }
                }
        rule output_bool() -> Node = value:bool_null() {? if value.kind==Kind::Bool { Ok(value) } else { Err("boolean file option") } }
        rule output_file_option() -> Node
            = kw("single") ("=" _)? value:output_bool() { Node::new(Kind::OutputSingle,vec![value]) }
            / kind:(kw("max_file_size") { Kind::OutputMaxFileSize } / kw("buffer_size") { Kind::OutputBufferSize })
                ("=" _)? value:(load_integer() / load_string()) { Node::new(kind,vec![value]) }
        rule output_format_option(level: usize) -> Node
            = kind:(kw("type") { Kind::OutputFormatType } / kw("encoding") { Kind::OutputEncoding }
                    / kw("file_extension") { Kind::OutputFileExtension }) "=" _ value:load_string() { Node::new(kind,vec![value]) }
            / kind:(kw("field_delimiter") { Kind::LoadFieldTerminated } / kw("line_delimiter") { Kind::LoadLineTerminated }
                    / kw("escape") { Kind::LoadEscaped } / kw("field_optionally_enclosed_by") { Kind::LoadOptionallyEnclosed }
                    / kw("field_enclosed_by") { Kind::LoadEnclosed }) "=" _ depth(level) start:position!() value:expr(level+1) end:position!() {
                        Node::new(kind,vec![value]).at(start,end)
                    }
            / kw("skip_header") "=" _ value:load_integer() { Node::new(Kind::OutputSkipHeader,vec![value]) }
            / kind:(kw("skip_blank_lines") { Kind::OutputSkipBlankLines } / kw("trim_space") { Kind::OutputTrimSpace }
                    / kw("empty_field_as_null") { Kind::OutputEmptyFieldAsNull } / kw("parse_header") { Kind::OutputParseHeader }
                    / kw("ignore_last_empty_column") { Kind::OutputIgnoreLastEmptyColumn }) "=" _ value:output_bool() { Node::new(kind,vec![value]) }
            / kw("null_if") "=" _ "(" _ depth(level) start:position!() values:(expr(level+1) ++ ("," _)) end:position!() ")" _ {
                Node::new(Kind::OutputNullIf,vec![Node::new(Kind::ValueList,values)]).at(start,end)
            }
            / kind:(kw("compression") { Kind::OutputCompression } / kw("binary_format") { Kind::OutputBinaryFormat })
                "=" _ value:(load_string() / name:ident() { Node::name(name) }) { Node::new(kind,vec![value]) }
        rule select_option() -> Node
            = kind:(distinct_keyword() { Kind::Distinct } / kw("all") { Kind::All }
                / kw("sql_calc_found_rows") { Kind::FoundRows } / kw("straight_join") { Kind::StraightJoin }) { Node::new(kind,vec![]) }
            / (kw("sql_no_cache") / kw("sql_cache") / kw("high_priority") / kw("sql_small_result")
                / kw("sql_big_result") / kw("sql_buffer_result")) { Node::missing() }
        rule group_by(level: usize) -> Node = kw("group") kw("by") keys:((value:expr(level)
                direction:(kw("asc") { Kind::Asc } / kw("desc") { Kind::Desc })? {
                    let mut children=vec![value];children.extend(direction.map(|kind| Node::new(kind,vec![])));
                    Node::new(Kind::SortKey,children)
                }) ++ ("," _)) rollup:(kw("with") kw("rollup") { Node::new(Kind::Rollup,vec![]) })? {
                    let mut children=vec![Node::new(Kind::SortList,keys)];children.extend(rollup);Node::new(Kind::GroupBy,children)
                }
        rule select_query(level: usize) -> Box<Select> = depth(level) keyword("select") hints:hint_slot() options:select_option()* projection:(item(level) ++ ("," _)) into:select_into(level)?
            from:(kw("from") tables:(kw("dual") { vec![] } / tables:(table(level) ++ ("," _)) { tables }) { tables })?
            filter:(kw("where") value:expr(level) { value })?
            group_by:group_by(level)?
            having:(kw("having") value:expr(level) { value })?
            windows:(kw("window") values:(named_window(level) ++ ("," _)) { values })? {
                let mut query=Box::<Select>::default();
                query.options=options.into_iter().filter(|value| value.kind!=Kind::Missing).collect();
                query.projection=projection;query.into=into;query.from=from.unwrap_or_default();
                query.filter=filter;query.group_by=group_by;query.having=having;
                query.hints=hints.unwrap_or_default();query.windows=windows.unwrap_or_default();query
            }
        rule values_query(level: usize) -> Box<Select> = depth(level) kw("values") rows:((kw("row") value:row() { value }) ++ ("," _)) {
                let values=Node::new(Kind::Values,vec![Node::new(Kind::ValuesRows,rows)]);
                let projection=Node::new(Kind::Projection,vec![Node::new(Kind::Star,vec![])]);
                let mut query=Box::<Select>::default();
                query.projection=vec![projection];query.from=vec![Node::new(Kind::Table,vec![values])];query
            }
        rule column() -> Node = start:position!() names:column_name() {? let end = names.last().unwrap().end; Node::column(names, false, start, end) }
        rule insert_column() -> Node = start:position!() names:column_name() wildcard:("." _ "*" end:position!() _ { end })? {?
            let end = wildcard.unwrap_or_else(|| names.last().unwrap().end); Node::column(names, wildcard.is_some(), start, end)
        }
        rule expr_or_default() -> Node = kw("default") !"(" { Node::new(Kind::Default, vec![]) } / expr(0)
        rule assignment() -> Node = column:column() "=" _ value:expr_or_default() { Node::new(Kind::Assignment, vec![column, value]) }
        rule assignments() -> Vec<Node> = values:(assignment() ++ ("," _)) { values }
        rule row() -> Node = "(" _ first:expr_or_default()? rest:("," _ value:expr_or_default() { value })* ")" _ {
            let mut values:Vec<Node>=first.into_iter().collect();
            if values.is_empty() && !rest.is_empty() { values.push(Node::missing()); }
            values.extend(rest);
            Node::new(Kind::ValueVector,values)
        }
        rule insert() -> Node = replace:(keyword("insert") { false } / keyword("replace") { true }) hints:hint_slot()
            (kw("low_priority") / kw("high_priority"))? ignore:(enabled(!replace) kw("ignore"))? kw("into")? table:relation_name() partition:use_partition()?
            columns:("(" _ columns:(insert_column() ** ("," _)) ")" _ { columns })?
            values:((kw("values") / kw("value")) rows:(row() ++ ("," _)) { Node::new(Kind::ValueList, rows) }
                / enabled(columns.is_none()) kw("set") values:assignments() { Node::new(Kind::AssignList, values) }
                / select(0))
            alias:(enabled(matches!(values.kind,Kind::ValueList|Kind::AssignList)) kw("as") name:ident()
                columns:("(" _ names:(ident() ++ ("," _)) ")" _ { names })? { (name,columns.unwrap_or_default()) })?
            duplicate:(enabled(!replace) kw("on") kw("duplicate") kw("key") kw("update") values:assignments() { values })? {?
                let mut children=vec![Node::relation(table)?];children.extend(partition);
                let table=Node::new(Kind::Table,children);
                Node::insert(replace, ignore.is_some(), table, columns.unwrap_or_default(), values, alias, duplicate.unwrap_or_default(), hints.unwrap_or_default())
            }
        rule update() -> Node = with:with_clause(0)? keyword("update") hints:hint_slot() kw("low_priority")? ignore:kw("ignore")?
            tables:(table(0) ++ ("," _)) kw("set") values:assignments()
            filter:(kw("where") value:expr(0) { value })?
            order:(kw("order") kw("by") values:(order_item(0) ++ ("," _)) { values })?
            limit:limit()? {
                let mut children=vec![Node::new(Kind::FromList,tables),Node::new(Kind::AssignList,values)];
                children.extend(with);
                if let Some(filter)=filter { children.push(Node::new(Kind::Where,vec![filter])); }
                if let Some(order)=order { children.push(Node::new(Kind::Order,order)); }
                children.extend(limit);
                if let Some(hints)=hints { children.push(Node::new(Kind::Hints,hints)); }
                if ignore.is_some() { children.push(Node::new(Kind::Ignore,vec![])); }
                Node::new(Kind::Update,children)
            }
        rule delete_target() -> Node = name:name() ("." _ "*" _)? {? Node::relation(name) }
        rule delete_tables() -> Node
            = targets:(delete_target() ++ ("," _)) kw("from") tables:(table(0) ++ ("," _)) {
                Node::new(Kind::DeleteTables,vec![Node::new(Kind::DeleteTargets,targets),Node::new(Kind::FromList,tables)])
            }
            / kw("from") targets:(delete_target() ++ ("," _)) kw("using") tables:(table(0) ++ ("," _)) {
                Node::new(Kind::DeleteTables,vec![Node::new(Kind::DeleteTargets,targets),Node::new(Kind::FromList,tables)])
            }
            / kw("from") start:position!() table:relation(0) end:position!() {?
                if table.kind!=Kind::Table || table.children[0].kind!=Kind::Relation
                    || table.children.iter().any(|child| child.kind==Kind::ParenthesizedTable) {
                    return Err(diagnostics.record("DELETE target must be a base table",crate::Span { start,end },end));
                }
                Ok(Node::new(Kind::DeleteTables,vec![Node::new(Kind::FromList,vec![table])]))
            }
        rule delete() -> Node = with:with_clause(0)? keyword("delete") hints:hint_slot() ignore:kw("ignore")? (kw("low_priority") / kw("quick"))*
            tables:delete_tables() filter:(kw("where") value:expr(0) { value })?
            order:(enabled(tables.children.len()==1) kw("order") kw("by") values:(order_item(0) ++ ("," _)) { values })?
            limit:(enabled(tables.children.len()==1) value:limit() { value })? {
                let mut children=tables.into_children();
                children.extend(with);
                if let Some(filter)=filter { children.push(Node::new(Kind::Where,vec![filter])); }
                if let Some(order)=order { children.push(Node::new(Kind::Order,order)); }
                children.extend(limit);
                if let Some(hints)=hints { children.push(Node::new(Kind::Hints,hints)); }
                if ignore.is_some() { children.push(Node::new(Kind::Ignore,vec![])); }
                Node::new(Kind::Delete,children)
            }
        rule read_access() -> Node
            = start:position!() kw("read") kind:(kw("only") { Kind::ReadOnly } / kw("write") { Kind::ReadWrite }) end:position!() {
                Node::new(kind,vec![]).at(start,end)
            }
        rule consistent_snapshot() -> Node
            = start:position!() kw("with") kw("consistent") kw("snapshot") end:position!() {
                Node::new(Kind::ConsistentSnapshot,vec![]).at(start,end)
            }
        rule transaction_options() -> Vec<Node>
            = snapshot:consistent_snapshot() access:("," _ value:read_access() { value })? {
                let mut options=vec![snapshot]; options.extend(access); options
            }
            / access:read_access() snapshot:("," _ value:consistent_snapshot() { value })? {
                let mut options=vec![access]; options.extend(snapshot); options
            }
        rule transaction() -> Node
            = kw("savepoint") name:ident() { Node::new(Kind::Savepoint,vec![]).text(name.text).at(name.start,name.end) }
            / kw("release") kw("savepoint") name:ident() { Node::new(Kind::ReleaseSavepoint,vec![]).text(name.text).at(name.start,name.end) }
            / kw("rollback") kw("work")? kw("to") kw("savepoint")? name:ident() {
                Node::new(Kind::RollbackSavepoint,vec![]).text(name.text).at(name.start,name.end)
            }
            / options:(kw("begin") kw("work")? { vec![] } / kw("start") kw("transaction") options:transaction_options()? { options.unwrap_or_default() }) {
                Node::new(Kind::Begin,options)
            }
            / kw("commit") kw("work")? { Node::new(Kind::Commit,vec![]) }
            / kw("rollback") kw("work")? { Node::new(Kind::Rollback,vec![]) }
        rule if_not_exists() -> Node = kw("if") kw("not") kw("exists") { Node::new(Kind::IfNotExists, vec![]) }
        rule if_exists() -> Node = kw("if") kw("exists") { Node::new(Kind::IfExists, vec![]) }
        rule temporary() -> Node = kw("temporary") { Node::new(Kind::Temporary,vec![]) }
        rule geometry_type() -> Node = start:position!() kind:(kw("geometry") { 0 }
            / kw("point") { 1 } / kw("linestring") { 2 } / kw("polygon") { 3 }
            / kw("multipoint") { 4 } / kw("multilinestring") { 5 } / kw("multipolygon") { 6 }
            / (kw("geometrycollection") / kw("geomcollection")) { 7 }) end:position!() {
            Node::new(Kind::DataType,vec![]).text("geometry".into()).value(kind).at(start,end)
        }
        rule type_name() -> &'static str
            = (kw("tinyint") / kw("int1")) { "tinyint" } / (kw("smallint") / kw("int2")) { "smallint" }
            / (kw("mediumint") / kw("int3") / kw("middleint")) { "mediumint" }
            / (kw("int") / kw("integer") / kw("int4")) { "int" } / (kw("bigint") / kw("int8")) { "bigint" }
            / (kw("float") / kw("float4")) { "float" } / ((kw("double") / kw("float8")) kw("precision")?) { "double" }
            / kw("real") kw("precision")? { if mode.real_as_float { "float" } else { "double" } }
            / (kw("decimal") / kw("dec") / kw("numeric") / kw("number") / kw("fixed")) { "decimal" }
            / kw("boolean") { "boolean" } / kw("bool") { "bool" }
            / (kw("varchar") / kw("varcharacter") / (kw("char") / kw("character")) kw("varying")) { "varchar" }
            / (kw("char") / kw("character")) { "char" }
            / (kw("long") / kw("mediumtext")) kw("varbinary") { "mediumblob" }
            / (kw("long") / kw("mediumtext")) (kw("varchar") / kw("varcharacter"))? { "mediumtext" }
            / kw("text") { "text" } / kw("blob") { "blob" }
            / kw("tinytext") { "tinytext" } / kw("tinyblob") { "tinyblob" }
            / kw("mediumblob") { "mediumblob" } / kw("string") { "string" }
            / kw("longtext") { "longtext" } / kw("longblob") { "longblob" }
            / kw("varbinary") { "varbinary" } / kw("binary") { "binary" }
            / kw("datetime") { "datetime" } / kw("timestamp") { "timestamp" }
            / kw("time") { "time" } / kw("date") { "date" } / kw("year") { "year" } / kw("json") { "json" }
            / kw("bit") { "bit" }
        rule type_size() -> Node = start:position!() text:syntax_text(<$([b'0'..=b'9']+)>) !cont() end:position!() _ {? Node::number(text, start, end) }
        rule fractional_type_size() -> Node
            = start:position!() text:syntax_text(<$([b'0'..=b'9']+ "." [b'0'..=b'9']* / "." [b'0'..=b'9']+)>) !cont() end:position!() _ {?
                Node::number(text,start,end)
            }
        rule string_length() -> Node = fractional_type_size() / type_size()
        rule charset_keyword() = kw("charset") / (kw("character") / kw("char")) kw("set")
        rule cast_type() -> Node
            = start:position!() (kw("nchar") / kw("national") (kw("char") / kw("character")))
                parameters:("(" _ value:string_length() ")" _ { vec![value] })? end:position!() {?
                let mut value=Node::data_type("char",parameters.unwrap_or_default(),None,false,start,end)?;
                value.kind=Kind::CastType;
                value.children.push(Node::new(Kind::Charset,vec![]).text("utf8mb4".into()));
                Ok(value)
            }
            / start:position!() name:(kw("signed") (kw("int") / kw("integer"))? { "signed" }
                / kw("unsigned") (kw("int") / kw("integer"))? { "unsigned" } / kw("binary") { "binary" }
                / (kw("char") / kw("character")) { "char" }
                / (kw("decimal") / kw("dec") / kw("numeric") / kw("number") / kw("fixed")) { "decimal" }
                / (kw("double") / kw("float8")) { "double" } / (kw("float") / kw("float4")) { "float" } / kw("json") { "json" }
                / kw("datetime") { "datetime" } / kw("date") { "date" } / kw("time") { "time" } / kw("year") { "year" })
            parameters:("(" _ values:(enabled(matches!(name,"char"|"binary")) value:string_length() { vec![value] }
                / values:(type_size() ++ ("," _)) { values }) ")" _ { values })?
            charset:(enabled(name=="char") charset_keyword() !kw("default") value:charset_name() {
                let literal=value.kind==Kind::String;value.with_kind(Kind::Charset).value(u64::from(literal))
            })? (enabled(name=="char" && charset.is_none()) kw("binary"))? end:position!() {?
                let mut parameters=parameters.unwrap_or_default();
                let max=match name { "signed"|"unsigned"|"double"|"json" => 0,"decimal" => 2,_ => 1 };
                if parameters.len()>max { Err("invalid cast type parameters") }
                else { parameters.extend(charset);Ok(Node::new(Kind::CastType,parameters).text(name.into()).at(start,end)) }
            }
            / !kw("geometry") value:geometry_type() { value.with_kind(Kind::CastType) }
        rule serial_type() -> Node = start:position!() kw("serial") end:position!() {?
            Node::data_type("bigint",vec![],Some(true),false,start,end)
        }
        rule national_type(routine: bool) -> Node = start:position!() name:(
                (kw("nvarchar") / kw("nchar") (kw("varchar") / kw("varcharacter"))
                    / kw("national") ((kw("char") / kw("character")) kw("varying") / kw("varchar") / kw("varcharacter"))) { "varchar" }
                / (kw("nchar") / kw("national") (kw("char") / kw("character"))) { "char" })
            parameters:("(" _ value:string_length() ")" _ { vec![value] })?
            binary:kw("binary")? collation:(enabled(routine) kw("collate") value:collation_name() { value.with_kind(Kind::Collation) })? end:position!() {?
                let parameters=parameters.unwrap_or_else(|| if routine { vec![Node::new(Kind::Int,vec![]).value(1)] } else { vec![] });
                let mut value=Node::data_type(name,parameters,None,false,start,end)?;
                value.children.push(Node::new(Kind::Charset,vec![]).text("utf8mb4".into()));
                if binary.is_some() { value.children.push(Node::new(Kind::BinaryAttribute,vec![]).value(1)); }
                value.children.extend(collation);
                Ok(value)
            }
        rule routine_data_type() -> Node = serial_type() / national_type(true)
            / value:data_type() collation:(enabled(matches!(value.kind,Kind::EnumType|Kind::SetType)
                    || matches!(value.text.as_str(),"char"|"varchar"|"tinytext"|"text"|"mediumtext"|"longtext"|"string"))
                kw("collate") value:collation_name() { value.with_kind(Kind::Collation) })? end:position!() {
                    let mut value=value;
                    value.children.extend(collation);
                    value.end=end;
                    value
                }
        rule data_type() -> Node = collection_type(0)
        rule collection_type(level: usize) -> Node = depth(level) start:position!() value:(
                kw("array") "(" _ value:collection_type(level+1) ")" end:position!() _ {
                    Node::new(Kind::CollectionType,vec![value]).at(start,end)
                }
                / kw("vector") "(" _ length:type_size() ")" end:position!() _ {
                    Node::new(Kind::CollectionType,vec![length]).value(1).at(start,end)
                }
                / kw("map") "(" _ key:collection_type(level+1) "," _ value:collection_type(level+1) ")" end:position!() _ {
                    Node::new(Kind::CollectionType,vec![key,value]).value(2).at(start,end)
                }
                / kw("sparsevector") { Node::new(Kind::CollectionType,vec![]).value(3).at(start,start+12) }
                / scalar_type()
            ) dimensions:("[" _ "]" end:position!() _ { end })* {?
                dimensions.into_iter().try_fold(value,|value,end| Node::new(Kind::CollectionType,vec![value]).expression(start,end))
            }
        rule scalar_type() -> Node
            = start:position!() kind:(kw("enum") { Kind::EnumType } / kw("set") { Kind::SetType })
                "(" _ values:((binary_literal() / string()) ++ ("," _)) ")" _ binary:(kw("binary") { Node::new(Kind::BinaryAttribute,vec![]).value(1) })?
                charset:(charset_keyword() !kw("default") value:charset_name() { value.with_kind(Kind::Charset) })? end:position!() {
                    Node::new(kind,vec![Node::optional(charset),Node::missing(),Node::optional(binary),Node::new(Kind::StringList,values)]).at(start,end)
                }
            / geometry_type()
            / national_type(false)
            / start:position!() name:type_name()
            parameters:("(" _ values:(enabled(matches!(name,"char"|"varchar"|"binary"|"varbinary"|"text"|"blob"))
                value:string_length() { vec![value] } / enabled(name=="float") value:fractional_type_size() { vec![value] }
                / values:(type_size() ++ ("," _)) { values }) ")" _ { values })?
            unsigned:(kw("unsigned") { true } / kw("signed") { false })? zerofill:kw("zerofill")? binary:kw("binary")?
            charset:(charset_keyword() !kw("default") value:charset_name() { value.with_kind(Kind::Charset) })? end:position!() {?
                let span=crate::Span { start,end };
                let mut value=Node::data_type(name, parameters.unwrap_or_default(), unsigned, zerofill.is_some(), start, end)
                    .map_err(|message| diagnostics.record(message,span,end))?;
                if (binary.is_some() || charset.is_some())
                    && !matches!(name,"char"|"varchar"|"tinytext"|"text"|"mediumtext"|"longtext"|"string") {
                    return Err(diagnostics.record("character type",span,end));
                }
                if binary.is_some() { value.children.push(Node::new(Kind::BinaryAttribute,vec![]).value(1)); }
                value.children.extend(charset);
                Ok(value)
            }
        rule generated_column_attribute() -> Node
            = kw("not") kw("null") { Node::new(Kind::NotNull, vec![]) }
            / kw("null") { Node::new(Kind::Nullable, vec![]) }
            / kw("visible") { Node::new(Kind::ColumnVisible, vec![]) }
            / kw("invisible") { Node::new(Kind::ColumnInvisible, vec![]) }
            / kw("primary")? kw("key") { Node::new(Kind::ColumnPrimaryKey, vec![]) }
            / kw("unique") kw("key")? { Node::new(Kind::UniqueKey, vec![]) }
            / kw("comment") value:string() { Node::new(Kind::ColumnComment, vec![value]) }
            / kw("srid") value:type_size() { Node::new(Kind::Srid,vec![value]) }
            / check_constraint()
        rule current_timestamp(message: &'static str) -> Node = value:atom(0) failure:position!() {?
            if value.kind==Kind::CurrentTimestamp { Ok(value) }
            else { Err(diagnostics.record(message,crate::Span { start:value.start,end:value.end },failure)) }
        }
        rule column_default_value(timestamp: bool) -> Node
            = start:position!() "(" _ value:expr(0) ")" end:position!() _ {?
                Node::new(Kind::Parenthesized,vec![value]).expression(start,end)
            }
            / signed_literal()
            / enabled(timestamp) value:current_timestamp("nonconstant DEFAULT expressions require parentheses") { value }
        rule column_attribute() -> Node
            = generated_column_attribute()
            / storage_chunk()
            / kw("auto_increment") { Node::new(Kind::AutoIncrement, vec![]) }
            / kw("default") value:column_default_value(true) { Node::new(Kind::ColumnDefault, vec![value]) }
            / kw("on") kw("update") value:current_timestamp("ON UPDATE requires CURRENT_TIMESTAMP") { Node::new(Kind::OnUpdate,vec![value]) }
            / kw("collate") value:collation_name() { value.with_kind(Kind::Collation) }
            / kw("column_format") (kw("default") / kw("fixed") / kw("dynamic")) { Node::missing() }
            / kw("storage") (kw("default") / kw("disk") / kw("memory")) { Node::missing() }
            / kw("skip_index") "(" _ ("," _ &skip_index_kind())? kinds:(skip_index_kind() ** ("," _)) ")" _ {
                Node::new(Kind::ColumnSkipIndex,kinds)
            }
        rule skip_index_kind() -> Node
            = kw("min_max") { Node::new(Kind::SkipIndexMinMax,vec![]) }
            / kw("sum") { Node::new(Kind::SkipIndexSum,vec![]) }
        rule primary_key_header()
            = kw("constraint") ident()? kw("primary") kw("key")
            / kw("primary") kw("key") ident()?
        rule table_element() -> Node
            = check_constraint()
            / foreign_key()
            / primary_key_header() method:index_method()? "(" _ names:(ident() ++ ("," _)) ")" _
                method_end:index_method()? comment:(kw("comment") value:string() { value })? {
                Node::new(Kind::PrimaryKey, vec![Node::new(Kind::ColumnList, names.into_iter().map(Node::name).collect()), Node::optional(method_end.or(method)), Node::optional(comment)])
            }
            / index_definition(false)
            / column_definition()
        rule constraint_name() -> Node = kw("constraint") name:ident()? {
            Node::new(Kind::CheckConstraint,vec![name.map(Node::name).unwrap_or_else(Node::missing)])
        }
        rule reference_option() -> Node = kw("on") delete:(kw("update") { false } / kw("delete") { true })
            action:(kw("restrict") { Kind::Restrict } / kw("cascade") { Kind::Cascade }
                / kw("set") kw("null") { Kind::SetNull } / kw("no") kw("action") { Kind::NoAction }
                / kw("set") kw("default") { Kind::SetDefault }) {
                Node::new(Kind::ReferenceOption,vec![Node::new(action,vec![])]).value(u64::from(delete))
            }
        rule column_reference() = kw("references") relation_name() "(" _ (ident() ++ ("," _)) ")" _
            (kw("match") (kw("simple") / kw("full") / kw("partial")))?
            (reference_option() / kw("check") "(" _ expr(0) ")" _)*
        rule foreign_key() -> Node = constraint:constraint_name()? kw("foreign") kw("key") index:ident()?
            "(" _ columns:(ident() ++ ("," _)) ")" _ kw("references") table:relation_name()
            "(" _ parent:(ident() ++ ("," _)) ")" _ (kw("match") (kw("simple") / kw("full") / kw("partial")))?
            options:reference_option()* {?
                Ok(ddl::ForeignKey {
                    columns:columns.into_iter().map(Node::name).collect(), parent:Node::relation(table)?,
                    parent_columns:parent.into_iter().map(Node::name).collect(), actions:options,
                    constraint, index_name:index.map(Node::name),
                }.into_node())
            }
        rule check_state() -> Kind
            = kw("not") kw("enforced") { Kind::NotEnforced } / kw("enforced") { Kind::Enforced }
        rule check_constraint() -> Node = name:constraint_name()?
            kw("check") "(" _ start:position!() value:expr(0) end:position!() ")" _
            state:(check_state() / !kw("not") { Kind::Enforced }) {
                Node::new(Kind::CheckConstraint,vec![Node::optional(name),value.at(start,end),Node::new(state,vec![])]).value(1)
            }
        rule column_definition() -> Node
            = name:column() data_type:serial_type() attributes:column_attribute()* position:column_position()? {
                let mut attributes=attributes;
                attributes.extend([Node::new(Kind::NotNull,vec![]),Node::new(Kind::AutoIncrement,vec![]),Node::new(Kind::UniqueKey,vec![])]);
                ddl::Column { name, data_type, tail:ddl::ColumnTail { attributes, position, ..Default::default() } }.into_node()
            }
            / name:column() data_type:data_type() tail:(
                collation:(kw("collate") value:collation_name() { value.with_kind(Kind::Collation) })?
                generated:(kw("generated") kw("always"))?
                kw("as") "(" _ start:position!() value:expr(0) end:position!() ")" _
                storage:(kind:(kw("virtual") { Kind::VirtualColumn } / kw("stored") { Kind::StoredColumn }) { Node::new(kind,vec![]) })?
                attributes:generated_column_attribute()* column_reference()? position:column_position()? {
                    ddl::ColumnTail {
                        attributes, position, collation,
                        generation:Some(ddl::GeneratedColumn { expression:value.at(start,end), storage, always:generated.is_some() }),
                    }
                }
                / attributes:column_attribute()* column_reference()? position:column_position()? {
                    ddl::ColumnTail { attributes, position, ..Default::default() }
                }) {
                ddl::Column { name, data_type, tail }.into_node()
            }
        rule column_position() -> Node
            = kw("first") { Node::new(Kind::ColumnFirst,vec![]) }
            / kind:(kw("before") { Kind::ColumnBefore } / kw("after") { Kind::ColumnAfter }) name:ident() { Node::new(kind,vec![Node::name(name)]) }
        rule parallel_option() -> Node
            = kw("parallel") ("=" _)? value:type_size() {
                Node::new(Kind::Parallel,vec![value])
            }
            / kw("noparallel") { Node::new(Kind::Parallel,vec![Node::new(Kind::Int,vec![]).value(1)]) }
        rule storage_chunk() -> Node = kw("chunk") value:(string() / type_size()) {
            Node::new(Kind::StorageChunk,vec![value])
        }
        rule json_storage() -> Node = kw("json") "(" _ column:ident() ")" _ kw("store") kw("as")
            "(" _ chunks:storage_chunk()+ ")" _ {
                let mut children=vec![Node::name(column)];children.extend(chunks);
                Node::new(Kind::JsonStorage,children)
            }
        rule table_option() -> Node
            = kw("organization") ("=" _)? kind:(kw("index") { Kind::OrganizationIndex } / kw("heap") { Kind::OrganizationHeap }) { Node::new(Kind::Organization,vec![Node::new(kind,vec![])]) }
            / kw("default")? charset_keyword() ("=" _)? value:charset_name() { Node::new(Kind::TableCharset,vec![value]) }
            / kw("default")? kw("collate") ("=" _)? value:collation_name() { value.with_kind(Kind::Collation) }
            / parallel_option()
            / read_access()
            / kw("engine") ("=" _)? value:collation_name() { Node::new(Kind::Engine,vec![value]) }
            / kind:(kw("progressive_merge_num") { Kind::ProgressiveMergeNum }
                / kw("auto_increment_cache_size") { Kind::AutoIncrementCacheSize }) ("=" _)? value:type_size() {
                Node::new(kind,vec![value])
            }
            / kw("delay_key_write") ("=" _)? kw("all") { Node::missing() }
            / kw("pack_keys") ("=" _)? kw("default") { Node::missing() }
            / (kw("checksum") / kw("avg_row_length") / kw("delay_key_write") / kw("pack_keys")
                / kw("key_block_size") / kw("max_rows") / kw("min_rows")) ("=" _)? type_size() { Node::missing() }
            / (kw("stats_auto_recalc") / kw("stats_persistent") / kw("stats_sample_pages"))
                ("=" _)? (kw("default") / type_size()) { Node::missing() }
            / (kw("password") / kw("connection") / (kw("data") / kw("index")) kw("directory"))
                ("=" _)? string() { Node::missing() }
            / kw("insert_method") ("=" _)? (kw("no") / kw("first") / kw("last")) { Node::missing() }
            / kw("union") ("=" _)? "(" _ (relation_name() ** ("," _)) ")" _ { Node::missing() }
            / kw("micro_index_clustered") ("=" _)? value:(kw("true") { 1 } / kw("false") { 0 }) {
                Node::new(Kind::MicroIndexClustered,vec![Node::new(Kind::Bool,vec![]).value(value)])
            }
            / json_storage()
            / kind:(kw("compression") { Kind::Compression } / kw("comment") { Kind::ColumnComment }
                / kw("semistruct_encoding_type") { Kind::SemistructEncodingType }
                / kw("table_mode") { Kind::TableMode }) ("=" _)? value:string() { Node::new(kind,vec![value]) }
            / kw("auto_increment") ("=" _)? value:(fractional_type_size() / type_size()) {
                Node::new(Kind::TableAutoIncrement,vec![value])
            }
            / kind:(kw("block_size") { Kind::BlockSize }
                / kw("tablet_size") { Kind::TabletSize } / kw("pctfree") { Kind::PctFree }
                / (kw("lob_inrow_threshold") / kw("default_lob_inrow_threshold")) { Kind::LobInrowThreshold }) ("=" _)? value:type_size() { Node::new(kind,vec![value]) }
            / kw("row_format") ("=" _)? value:(kw("redundant") { 1 } / kw("compact") { 2 }
                / (kw("dynamic") / kw("default")) { 3 } / kw("compressed") { 4 } / kw("condensed") { 5 }) {
                    Node::new(Kind::StoreFormat,vec![Node::new(Kind::HiddenInt,vec![]).value(value)])
                }
        rule database_option() -> Node
            = kw("default")? charset_keyword() ("=" _)? !kw("default") value:charset_name() { value.with_kind(Kind::Charset) }
            / kw("default")? kw("collate") ("=" _)? value:collation_name() { value.with_kind(Kind::Collation) }
            / read_access()
        rule index_method() -> Node = kw("using") value:(kw("btree") { Node::missing() } / kw("hash") { Node::new(Kind::UsingHash,vec![]) }) { value }
        rule index_column(fulltext: bool) -> Node = value:(name:ident() prefix:("(" _ value:type_size() ")" _ { value })? { (Node::name(name),Node::optional(prefix)) }
                / "(" _ start:position!() value:expr(0) end:position!() ")" _ { (value.at(start,end),Node::missing()) })
            direction:(start:position!() kind:(kw("asc") { Kind::Asc } / enabled(!fulltext) kw("desc") { Kind::Desc }) end:position!() { Node::new(kind,vec![]).at(start,end) })? {
                let direction=Node::optional(direction);
                Node::new(Kind::IndexColumn,vec![value.0,value.1,direction])
            }
        rule index_visibility() -> Node
            = visibility:(kw("visible") { 0 } / kw("invisible") { 1 }) { Node::new(Kind::IndexVisibility,vec![]).value(visibility) }
        rule index_option() -> Node
            = global:(kw("global") { true } / kw("local") { false }) {
                Node::new(Kind::IndexScope,vec![Node::new(Kind::HiddenInt,vec![]).value(u64::from(global)),Node::new(Kind::String,vec![]).text(if global { "GLOBAL" } else { "LOCAL" }.into())])
            }
            / kw("storing") "(" _ names:(ident() ++ ("," _)) ")" _ { Node::new(Kind::StoringColumns,names.into_iter().map(Node::name).collect()) }
            / index_visibility()
            / index_method()
            / parallel_option()
            / kw("comment") value:string() { Node::new(Kind::ColumnComment,vec![value]) }
            / kw("block_size") ("=" _)? value:type_size() { Node::new(Kind::BlockSize,vec![value]) }
            / kw("key_block_size") ("=" _)? type_size() { Node::missing() }
            / kw("with") kw("rowid") { Node::missing() }
            / kw("with") value:vector_parameter_list() { value }
        rule fulltext_property() -> Node
            = kind:(kw("min_token_size") { Kind::ParserMinTokenSize } / kw("max_token_size") { Kind::ParserMaxTokenSize }
                / kw("ngram_token_size") { Kind::ParserNgramTokenSize } / kw("min_ngram_size") { Kind::ParserMinNgramSize }
                / kw("max_ngram_size") { Kind::ParserMaxNgramSize }) "=" _ value:type_size() { Node::new(kind,vec![value]) }
            / kind:(kw("stopword_table") { Kind::ParserStopwordTable } / kw("dict_table") { Kind::ParserDictTable }
                / kw("quantifier_table") { Kind::ParserQuantifierTable } / kw("ik_mode") { Kind::ParserIkMode }) "=" _ value:string() { Node::new(kind,vec![value]) }
        rule fulltext_option() -> Node
            = kw("with") kw("parser") name:ident() { Node::new(Kind::ParserName,vec![Node::name(name)]) }
            / kw("parser_properties") ("=" _)? "(" _ values:(fulltext_property() ++ ("," _ / "")) ")" _ { Node::new(Kind::ParserProperties,values) }
        rule index_kind() -> u64 = kw("unique") { 1 } / kw("spatial") { 2 } / kw("fulltext") { 3 } / kw("vector") { 6 }
        rule index_definition(allow_constraint_partition: bool) -> Node
            = constraint:(kw("constraint") name:ident()? { name })?
            kind:(kind:index_kind() (enabled(kind!=6) (kw("key") / kw("index"))? / (kw("key") / kw("index"))) { kind }
                / (kw("key") / kw("index")) { 0 }) enabled(constraint.is_none() || kind==1)
            name:ident()? method:(enabled(kind!=6) value:index_method() { value })? "(" _ columns:(index_column(kind==3) ++ ("," _)) ")" _ options:index_options(kind==3)
            partition:(enabled(kind<2 && (constraint.is_none() || allow_constraint_partition)) value:partition_option() { value })? {
                ddl::Index {
                    kind, name:name.or(constraint.flatten()).map(Node::name), table:None, columns, options, method, partition,
                    if_not_exists:false, hints:vec![],
                }.into_node()
            }
        rule index_options(fulltext: bool) -> Node = values:((index_option() / enabled(fulltext) value:fulltext_option() { value })*) {
            Node::list(Kind::TableOptions,values.into_iter().filter(|value| value.kind!=Kind::Missing).collect())
        }
        rule partition_header(sub: bool) -> ddl::Partition = kind:(kw("key") { Kind::KeyPartition } / kw("hash") { Kind::HashPartition }
                / kw("range") columns:kw("columns")? { if columns.is_some() { Kind::RangeColumnsPartition } else { Kind::RangePartition } }
                / kw("list") columns:kw("columns")? { if columns.is_some() { Kind::ListColumnsPartition } else { Kind::ListPartition } })
            "(" _ start:position!() expressions:(enabled(kind==Kind::KeyPartition) names:(ident() ** ("," _)) { names.into_iter().map(Node::name).collect() }
                / enabled(matches!(kind,Kind::RangeColumnsPartition|Kind::ListColumnsPartition)) names:(ident() ++ ("," _)) { names.into_iter().map(Node::name).collect() }
                / enabled(matches!(kind,Kind::HashPartition|Kind::RangePartition|Kind::ListPartition)) value:expr(0) { vec![value] }) enabled(!(sub && kind==Kind::KeyPartition && expressions.is_empty())) ")" _ {?
                let end=expressions.last().map_or(start,|expression| expression.end);
                let hash=matches!(kind,Kind::KeyPartition|Kind::HashPartition);
                let expression=if hash {
                    Node::named_call(if kind==Kind::KeyPartition { Kind::PartitionKeyFunction } else { Kind::PartitionHashFunction },
                        Node::new(Kind::Ident,vec![]).text(if kind==Kind::KeyPartition { "partition_key" } else { "partition_hash" }.into()),expressions)
                } else { Node::new(Kind::ExprList,expressions) };
                Ok(ddl::Partition { kind, expression, start, end, count:None, elements:None, subpartition:None, template:false })
            }
        rule partition_count() -> Node = kw("partitions") value:type_size() { value }
        rule partition_options() -> (Option<Node>,Option<Node>)
            = count:partition_count() sub:subpartition_option()? { (Some(count),sub) }
            / sub:subpartition_option() count:partition_count()? { (count,Some(sub)) }
            / { (None,None) }
        rule partition_option() -> Node = kw("partition") kw("by") header:partition_header(false) options:partition_options()
            elements:partition_elements(header.kind,options.1.as_ref().map(|sub| sub.kind))? {?
                if matches!(header.kind,Kind::ListPartition|Kind::ListColumnsPartition) && elements.is_none() { return Err("list partitions"); }
                let partition=ddl::Partition { count:options.0, subpartition:options.1, elements, ..header };
                Ok(Node::new(Kind::PartitionOption,vec![partition.into_node()]))
            }
        rule subpartition_option() -> Node = kw("subpartition") kw("by") header:partition_header(true)
            template:(kw("subpartition") kw("template") elements:subpartition_elements(header.kind) { elements })?
            count:(enabled(template.is_none() && matches!(header.kind,Kind::KeyPartition|Kind::HashPartition)) kw("subpartitions") value:type_size() { value })? {
                ddl::Partition { template:template.is_some(), elements:template, count, ..header }.into_node()
            }
        rule partition_elements(kind: Kind,subkind: Option<Kind>) -> Node = "(" _
            elements:((kw("partition") value:partition_element(kind,subkind) { value }) ++ ("," _)) ")" _ {
                Node::new(Kind::Partitions,elements)
            }
        rule subpartition_elements(kind: Kind) -> Node = "(" _
            elements:((kw("subpartition") value:partition_element(kind,Some(Kind::Missing)) { value }) ++ ("," _)) ")" _ {
                let list=match kind {
                    Kind::KeyPartition|Kind::HashPartition => Kind::HashSubpartitions,
                    Kind::RangePartition|Kind::RangeColumnsPartition => Kind::RangeSubpartitions,
                    _ => Kind::ListSubpartitions,
                };
                Node::new(list,elements)
            }
        rule partition_element(kind: Kind,subkind: Option<Kind>) -> Node = name:relation_name()
            bound:(enabled(matches!(kind,Kind::RangePartition|Kind::RangeColumnsPartition)) kw("values") kw("less") kw("than")
                    values:("(" _ values:((kw("maxvalue") { Node::new(Kind::MaxValue,vec![]) } / expr(0)) ++ ("," _)) ")" _ { values }
                        / kw("maxvalue") { vec![Node::new(Kind::MaxValue,vec![])] }) { (Kind::RangePartitionElement,Node::new(Kind::ExprList,values)) }
                / enabled(matches!(kind,Kind::ListPartition|Kind::ListColumnsPartition)) kw("values") kw("in") "(" _
                    value:(kw("default") { Node::new(Kind::Default,vec![]) } / values:expr_list(0) { Node::new(Kind::ExprList,values) }) ")" _ { (Kind::ListPartitionElement,value) }
                / enabled(matches!(kind,Kind::KeyPartition|Kind::HashPartition)) { (Kind::HashPartitionElement,Node::missing()) })
            engine:(kw("engine") "=" _ value:collation_name() { Node::new(Kind::Engine,vec![value]) })?
            elements:(enabled(subkind!=Some(Kind::Missing)) values:(
                subpartition_elements(subkind.unwrap_or(Kind::RangePartition))
                / enabled(subkind.is_none()) value:(subpartition_elements(Kind::ListPartition)
                    / subpartition_elements(Kind::HashPartition)) { value }) { values })? {?
                Ok(ddl::PartitionEntry { kind:bound.0, name:Node::relation(name)?, values:bound.1, engine, subpartitions:elements }.into_node())
            }
        rule rename_table() -> Node = from:relation_name() kw("to") to:relation_name() {?
            Ok(Node::new(Kind::RenameTableAction,vec![Node::relation(from)?,Node::relation(to)?]))
        }
        rule alter_column() -> Node
            = kw("add") kw("column")? values:("(" _ values:(column_definition() ++ ("," _)) ")" _ storage:json_storage()? {
                    let mut values=values;values.extend(storage);values
                } / value:column_definition() { vec![value] }) { Node::new(Kind::ColumnAdd,values) }
            / kw("drop") kw("column")? name:column() behavior:(kw("cascade") { 2 } / kw("restrict") { 1 })? { Node::new(Kind::ColumnDrop,vec![name]).value(behavior.unwrap_or(0)) }
            / kw("modify") kw("column")? value:column_definition() { Node::new(Kind::ColumnModify,vec![value]) }
            / kw("change") kw("column")? name:column() value:column_definition() { Node::new(Kind::ColumnChange,vec![name,value]) }
            / kw("rename") kw("column") from:ident() kw("to") to:ident() {?
                let (start,end)=(from.start,from.end);
                Ok(Node::new(Kind::ColumnRename,vec![Node::column(vec![from],false,start,end)?,Node::name(to)]))
            }
            / kw("alter") kw("column")? name:column() value:(kw("set") kw("default") value:column_default_value(false) { Node::new(Kind::ColumnDefault,vec![value]) }
                / kw("drop") kw("default") { Node::new(Kind::Nullable,vec![]) }) { Node::new(Kind::ColumnAlter,vec![name,value]) }
        rule alter_key_definition() -> Node
            = !kw("vector") value:index_definition(true) { value.with_kind(Kind::IndexAdd) }
            / primary_key_header() "(" _ names:(ident() ++ ("," _)) ")" _ options:index_options(false) {
                Node::new(Kind::PrimaryKey,vec![Node::new(Kind::ColumnList,names.into_iter().map(Node::name).collect()),options])
            }
        rule alter_index() -> Node
            = kw("add") value:(alter_key_definition()
                / "(" _ value:alter_key_definition() ")" _ { value }) { value }
            / kw("drop") (kw("index") / kw("key")) name:ident() { Node::new(Kind::IndexDrop,vec![Node::name(name)]) }
            / kw("drop") kw("primary") kw("key") { Node::new(Kind::PrimaryKeyDrop,vec![]) }
            / kw("rename") (kw("index") / kw("key")) from:ident() kw("to") to:ident() { Node::new(Kind::IndexRename,vec![Node::name(from),Node::name(to)]) }
            / keyword("alter") hint_slot() kw("index") name:ident() value:(
                index_visibility()
                / value:parallel_option() { Node::new(Kind::IndexParallel,value.into_children()) }) {
                let (kind,flags)=(value.kind,value.value);
                let mut children=vec![Node::name(name)];children.extend(value.into_children());
                Node::new(kind,children).value(flags)
            }
        rule partition_names(spaced: bool) -> Node = names:(ident() ++ ("," _ / enabled(spaced))) !"," { Node::new(Kind::Names,names.into_iter().map(Node::name).collect()) }
        rule alter_partition() -> Node
            = value:partition_option() { Node::new(Kind::Repartition,value.into_children()) }
            / kw("exchange") kw("partition") name:ident() kw("with") kw("table") table:relation_name() kw("without") kw("validation") {?
                Ok(Node::new(Kind::PartitionExchange,vec![Node::name(name),Node::relation(table)?]))
            }
            / kw("add") kw("partition") start:position!()
                values:(value:partition_elements(Kind::RangePartition,None) { value.with_kind(Kind::RangePartitions) }
                    / partition_elements(Kind::ListPartition,None)) end:position!() {
                Node::new(Kind::PartitionAdd,values.into_children()).at(start,end)
            }
            / kw("modify") kw("partition") parent:relation_name() kw("add") kw("subpartition")
                elements:(subpartition_elements(Kind::RangePartition) / subpartition_elements(Kind::ListPartition)) {?
                Ok(Node::new(Kind::SubpartitionAdd,vec![Node::relation(parent)?,elements]))
            }
            / truncate:(kw("truncate") { true } / kw("drop") { false })
                sub:(kw("subpartition") { true } / kw("partition") { false })
                names:(partition_names(truncate) / enabled(!truncate) "(" _ names:partition_names(false) ")" _ { names }) {
                let kind=match (sub,truncate) {
                    (false,false)=>Kind::PartitionDrop,(false,true)=>Kind::PartitionTruncate,
                    (true,false)=>Kind::SubpartitionDrop,(true,true)=>Kind::SubpartitionTruncate,
                };
                Node::new(kind,vec![names,Node::missing()])
            }
        rule alter_action() -> Node
            = kw("add") value:check_constraint() { value }
            / kw("drop") (kw("check") / kw("constraint")) "(" _ names:(ident() ++ ("," _)) ")" _ {
                Node::new(Kind::CheckConstraintDrop,names.into_iter().map(Node::name).collect())
            }
            / kw("drop") kw("check") name:ident() { Node::new(Kind::CheckConstraintDrop,vec![Node::name(name)]) }
            / keyword("alter") hint_slot() check_only:(kw("check") { true } / kw("constraint") { false })
                name:ident() state:check_state() {
                Node::new(Kind::AlterCheckConstraint,vec![Node::name(name),Node::new(state,vec![])]).value(u64::from(check_only))
            }
            / value:(kw("add") value:foreign_key() { value }
                / kw("drop") kw("foreign") kw("key") name:ident() { Node::new(Kind::ForeignKeyDrop,vec![Node::name(name)]) }) {
                value
            }
            / kw("drop") kw("constraint") name:ident() { Node::new(Kind::DropConstraint,vec![Node::name(name)]) }
            / alter_index()
            / alter_column()
            / kw("force") { Node::new(Kind::TableForce,vec![]) }
            / kw("convert") kw("to") (kw("character") / kw("char")) kw("set") !kw("default")
                charset:charset_name() collation:(kw("collate") value:collation_name() { value })? {
                Node::new(Kind::ConvertTableCharset,vec![charset,Node::optional(collation)])
            }
            / kw("rename") kw("to")? name:relation_name() {? Ok(Node::new(Kind::TableRename,vec![Node::relation(name)?])) }
            / kw("set")? options:table_option()+ {
                Node::new(Kind::TableOptions,options.into_iter().filter(|option| option.kind!=Kind::Missing).collect())
            }
            / kw("algorithm") ("=" _)? (kw("default") / kw("inplace") / kw("copy") / kw("instant")) { Node::missing() }
            / kw("lock") ("=" _)? (kw("default") / kw("none") / kw("shared") / kw("exclusive")) { Node::missing() }
        rule view_definition() -> Node = view_mode:(keyword("create") hint_slot() replace:(kw("or") kw("replace"))? { (false,replace.is_some()) }
                / keyword("alter") hint_slot() { (true,false) })
            (kw("algorithm") "=" _ (kw("undefined") / kw("merge") / kw("temptable")))?
            definer:(kw("definer") "=" _ value:(kw("current_user") ("(" _ ")" _)? { Node::new(Kind::CurrentUser,vec![]) }
                / account()) { Node::new(Kind::Definer,vec![value]) })? (kw("sql") kw("security") kw("definer"))?
            kw("view") name:relation_name() columns:("(" _ names:(ident() ++ ("," _)) ")" _ { names })? kw("as")
            start:position!() query:select(0) end:position!()
            check:(kw("with") local:(kw("local") { true } / kw("cascaded") { false })? kw("check") kw("option") { if local==Some(true) { 1 } else { 2 } })? {?
                Ok(ddl::View {
                    relation:Node::relation(name)?, columns:columns.unwrap_or_default().into_iter().map(Node::name).collect(),
                    query:query.at(start,end), definer, replace:view_mode.1, alter:view_mode.0, check:check.unwrap_or(0),
                }.into_node())
            }
        rule outline_query_end(start: usize, end: usize) -> usize = #{|input: &[u8], pos| {
            let mut tokens = crate::prefix::Tokens::new(&input[start..end], mode);
            let mut last = 0;
            loop {
                match tokens.next() {
                    Ok(Some(token)) => last = token.end,
                    Ok(None) => break peg::RuleResult::Matched(pos, start + last),
                    Err(()) => break peg::RuleResult::Failed,
                }
            }
        }}
        rule outline_query() -> Node = start:position!() query:(select(0) / insert() / update() / delete())
            end:position!() end:outline_query_end(start,end) {
            query.at(start,end)
        }
        rule outline_body() -> ddl::OutlineBody
            = statement:outline_query() target:(kw("to") query:outline_query() { query })? {
                ddl::OutlineBody::Statement { statement,target }
            }
        rule outline_definition() -> Node
            = keyword("create") hint_slot() replace:(kw("or") kw("replace"))? format:kw("format")?
                kw("outline") name:ident() kw("on") body:(outline_body()
                    / value:string() kw("using") keyword("hint") hint_gap() start:position!() hints:hints() end:position!() {
                        ddl::OutlineBody::SqlId { value,hint:Node::new(Kind::Hints,hints).at(start,end) }
                    }) {?
                Ok(ddl::Outline { kind:Kind::CreateOutline,relation:Node::relation(vec![name])?,
                    body:Some(body),format:format.is_some(),replace:replace.is_some() }.into_node())
            }
            / kw("alter") format:kw("format")? kw("outline") name:ident() kw("add") body:outline_body() {?
                Ok(ddl::Outline { kind:Kind::AlterOutline,relation:Node::relation(vec![name])?,
                    body:Some(body),format:format.is_some(),replace:false }.into_node())
            }
            / kw("drop") format:kw("format")? kw("outline") name:relation_name() {?
                Ok(ddl::Outline { kind:Kind::DropOutline,relation:Node::relation(name)?,
                    body:None,format:format.is_some(),replace:false }.into_node())
            }
        rule ddl() -> Node
            = view_definition()
            / outline_definition()
            / kw("fork") kw("table") source:relation_name() kw("to") destination:relation_name() {?
                Ok(Node::new(Kind::ForkTable,vec![Node::relation(destination)?,Node::relation(source)?]))
            }
            / kw("fork") kw("database") source:ident() kw("to") destination:ident() {
                Node::new(Kind::ForkDatabase,vec![Node::name(destination),Node::name(source)])
            }
            / kw("diff") kw("table") incoming:relation_name() kw("against") current:relation_name() {?
                Ok(Node::new(Kind::DiffTable,vec![Node::relation(incoming)?,Node::relation(current)?]))
            }
            / kw("merge") kw("table") incoming:relation_name() kw("into") current:relation_name()
                strategy:(kw("strategy") value:(kw("fail") { 0 } / kw("theirs") { 1 } / kw("ours") { 2 }) { value })? {?
                Ok(Node::new(Kind::MergeTable,vec![Node::relation(incoming)?,Node::relation(current)?]).value(strategy.unwrap_or(0)))
            }
            / kw("drop") kw("view") exists:if_exists()? names:(relation_name() ++ ("," _)) (kw("cascade") / kw("restrict"))? {?
                let views=names.into_iter().map(Node::relation).collect::<Result<Vec<_>,_>>()?;
                Ok(Node::new(Kind::DropView,vec![Node::missing(),Node::optional(exists),Node::new(Kind::ViewList,views)]))
            }
            / kw("create") (kw("database") / kw("schema")) absent:if_not_exists()? name:ident() options:database_option()* {
                Node::new(Kind::CreateDatabase, vec![Node::optional(absent), Node::name(name), Node::list(Kind::DatabaseOptions,options)])
            }
            / keyword("alter") hint_slot() (kw("database") / kw("schema"))
                name:(!(kw("default") / kw("charset") / kw("character") / kw("collate") / kw("read") / kw("set")) name:ident() { Node::name(name) })?
                kw("set")? options:database_option()+ {
                Node::new(Kind::AlterDatabase,vec![Node::optional(name),Node::new(Kind::DatabaseOptions,options)])
            }
            / keyword("create") hint_slot() temporary:temporary()? kw("table") absent:if_not_exists()? name:relation_name()
                source:(kw("like") name:relation_name() { name } / "(" _ kw("like") name:relation_name() ")" _ { name }) {?
                Ok(ddl::TableLike { relation:Node::relation(name)?, source:Node::relation(source)?,
                    temporary:temporary.is_some(), if_not_exists:absent.is_some() }.into_node())
            }
            / keyword("create") hints:hint_slot() temporary:temporary()? kw("table") absent:if_not_exists()? name:relation_name()
                elements:("(" _ elements:(table_element() ++ ("," _)) ")" _ { elements })?
                options:((value:table_option() ("," _)? { value })*) partition:partition_option()?
                query:(modifier:(kw("ignore") { Kind::Ignore } / kw("replace") { Kind::Replace })? kw("as")? query:select(0) { (modifier,query) })? {?
                if elements.is_none() && query.is_none() { return Err("table columns or SELECT"); }
                Ok(ddl::CreateTable {
                    relation:Node::relation(name)?, elements:elements.unwrap_or_default(),
                    options:options.into_iter().filter(|option| option.kind!=Kind::Missing).collect(), partition,
                    query:query.map(|(modifier,query)| ddl::TableQuery { query, modifier, hints:hints.unwrap_or_default() }),
                    temporary:temporary.is_some(), if_not_exists:absent.is_some(),
                }.into_node())
            }
            / keyword("create") hints:hint_slot() kind:index_kind()? kw("index") absent:if_not_exists()? name:name() method:index_method()? kw("on") table:relation_name()
                "(" _ columns:(index_column(kind==Some(3)) ++ ("," _)) ")" _ options:index_options(kind==Some(3)) partition:partition_option()? {?
                Ok(ddl::Index {
                    kind:kind.unwrap_or(0), name:Some(Node::relation(name)?), table:Some(Node::relation(table)?),
                    columns, options, method, partition, if_not_exists:absent.is_some(), hints:hints.unwrap_or_default(),
                }.into_node())
            }
            / kw("drop") kw("index") name:ident() kw("on") table:relation_name() {?
                Ok(Node::new(Kind::DropIndex,vec![Node::name(name),Node::relation(table)?]))
            }
            / kw("drop") (kw("database") / kw("schema")) exists:if_exists()? name:ident() {
                Node::new(Kind::DropDatabase, vec![Node::optional(exists), Node::name(name)])
            }
            / kw("drop") temporary:temporary()? (kw("table") / kw("tables")) exists:if_exists()? names:(relation_name() ++ ("," _)) (kw("cascade") / kw("restrict"))? {?
                let tables=names.into_iter().map(Node::relation).collect::<Result<Vec<_>, _>>()?;
                Ok(Node::new(Kind::DropTable, vec![Node::optional(temporary), Node::optional(exists), Node::new(Kind::TableList, tables)]))
            }
            / kw("truncate") kw("table")? name:relation_name() {? Ok(Node::new(Kind::TruncateTable,vec![Node::relation(name)?])) }
            / kw("optimize") kw("table") names:(relation_name() ++ ("," _)) {?
                Ok(Node::new(Kind::OptimizeTable,vec![Node::new(Kind::TableList,names.into_iter().map(Node::relation).collect::<Result<Vec<_>,_>>()?)]))
            }
            / kw("rename") kw("table") values:(rename_table() ++ ("," _)) { Node::new(Kind::RenameTables,values) }
            / keyword("alter") hints:hint_slot() kw("table") name:relation_name() actions:(first:(alter_partition() / alter_action())? rest:("," _ value:alter_action() { value })* {
                let mut actions=vec![];actions.extend(first);actions.extend(rest);actions
            }) {?
                Ok(Node::new(Kind::AlterTable,vec![Node::relation(name)?,Node::list(Kind::AlterTableActions,actions.into_iter().filter(|value| value.kind!=Kind::Missing).collect()),Node::missing(),Node::list(Kind::Hints,hints.unwrap_or_default())]))
            }
            / enabled(!pl) kw("use") name:ident() { Node::new(Kind::UseDatabase, vec![Node::name(name)]) }
        rule setting_keyword() -> Node
            = start:position!() value:(kw("on") { "ON" } / kw("binary") { "BINARY" }) { Node::new(Kind::String,vec![]).text(value.into()).at(start,start+value.len()) }
            / kw("default") !"(" { Node::new(Kind::Default,vec![]) }
        rule setting_value() -> Node = setting_keyword() / expr(0)
        rule charset_name() -> Node = kw("default") { Node::new(Kind::Default,vec![]) }
            / start:position!() kw("binary") { Node::new(Kind::CharsetName,vec![]).text("binary".into()).at(start,start+6) }
            / string() / name:ident() { Node::name(name).with_kind(Kind::CharsetName) }
        rule collation_name() -> Node = string() / name:ident() { Node::name(name) }
        rule assignment_name(scoped: bool) -> Name
            = (enabled(scoped) / !kw("password")) name:ident() { name }
        rule assignment_scope() -> u64 = kw("global") { 1 } / kw("session") { 2 }
        rule variable_assignment() -> Node
            = kw("names") charset:charset_name() collation:(kw("collate") value:collation_name() { value })? {
                Node::new(Kind::SetNames,vec![charset,Node::optional(collation)])
            }
            / charset_keyword() charset:charset_name() { Node::new(Kind::SetCharset,vec![charset]) }
            / variable:user_variable() (":=" / "=" / kw("to")) _ value:expr(0) {
                Node::new(Kind::VariableValue,vec![variable,value]).value(2)
            }
            / scope:assignment_scope()? variable:(system_variable() / name:assignment_name(scope.is_some()) { Node::name(name) })
                (":=" / "=" / kw("to")) _ value:setting_value() {
                let assignment_scope=scope.unwrap_or(if variable.kind==Kind::SystemVariable && variable.value!=0 { variable.value } else { 2 });
                let value=Node::setting_value(&variable,value);
                Node::new(Kind::VariableValue,vec![variable,value]).value(assignment_scope)
            }
        rule isolation_level() -> Node = kw("isolation") kw("level") value:(kw("read") kw("uncommitted") { 0 }
                / kw("read") kw("committed") { 1 } / kw("repeatable") kw("read") { 2 } / kw("serializable") { 3 }) {
            Node::new(Kind::HiddenInt,vec![]).value(value)
        }
        rule transaction_characteristics() -> Node
            = access:read_access() isolation:("," _ value:isolation_level() { value })? {
                let access=if access.kind==Kind::ReadOnly { 2 } else { 4 };
                Node::new(Kind::TransactionCharacteristics,vec![Node::new(Kind::HiddenInt,vec![]).value(access),Node::optional(isolation)])
            }
            / isolation:isolation_level() access:("," _ value:read_access() { value })? {
                let access=access.map(|node| Node::new(Kind::HiddenInt,vec![]).value(if node.kind==Kind::ReadOnly { 2 } else { 4 }));
                Node::new(Kind::TransactionCharacteristics,vec![Node::optional(access),isolation])
            }
        rule setting() -> Node
            = kw("set") scope:scope()? kw("transaction") options:transaction_characteristics() {
                Node::new(Kind::SetTransaction,vec![Node::new(Kind::HiddenInt,vec![]).value(scope.unwrap_or(0)),options])
            }
            / kw("set") assignments:(variable_assignment() ++ ("," _)) { Node::new(Kind::VariableSet,assignments) }
        rule show_condition() -> Node
            = kw("like") value:string() escape:(kw("escape") value:string() { value })? {
                let escape=escape.unwrap_or_else(|| Node::new(Kind::String,vec![]).text("\\".into()));
                Node::new(Kind::LikeClause,vec![value,escape])
            }
            / kw("where") value:expr(0) { Node::new(Kind::Where,vec![value]) }
        rule show_database() -> Node = (kw("from") / kw("in")) name:ident() { Node::new(Kind::FromList,vec![Node::name(name)]) }
        rule show_limit() -> Node = kw("limit") first:type_size() second:("," _ value:type_size() { value })? {
            match second { Some(second) => Node::new(Kind::ShowLimit,vec![first,second]),
                None => Node::new(Kind::ShowLimit,vec![Node::missing(),first]) }
        }
        rule show_modifiers() -> u64 = extended:kw("extended")? full:kw("full")? {
            u64::from(full.is_some()) | (u64::from(extended.is_some()) << 1)
        }
        rule show_body() -> Node
            = kw("grants") user:(kw("for") value:(kw("current_user") ("(" _ ")" _)? { Node::missing() } / account()) { value })?
                roles:(enabled(user.as_ref().is_some_and(|user| user.kind!=Kind::Missing)) kw("using") values:roles() { values })? {
                    Node::new(Kind::ShowGrants,vec![Node::optional(user),Node::new(Kind::Roles,roles.unwrap_or_default())])
                }
            / kw("parameters") condition:show_condition()?
                selector:(marker:ident() ("=" _)? target:(string() / kw("all") { Node::new(Kind::String,vec![]).text("all".into()) } / name:ident() { Node::name(name) }) {
                    Node::new(Kind::RuntimeSelector,vec![target]).text(marker.text)
                })? { Node::new(Kind::ShowParameters,vec![Node::optional(condition),Node::optional(selector)]) }
            / kw("trace") format:(kw("format") "=" _ value:string() { value })? condition:show_condition()? {
                let format=format.unwrap_or_else(|| Node::new(Kind::String,vec![]).text("ROW".into()));
                Node::new(Kind::ShowTrace,vec![Node::optional(condition),format])
            }
            / full:kw("full")? kw("processlist") { Node::new(Kind::ShowProcesslist,vec![Node::new(Kind::HiddenInt,vec![]).value(u64::from(full.is_some()))]) }
            / kind:(charset_keyword() { Kind::ShowCharset } / kw("collation") { Kind::ShowCollation })
                condition:show_condition()? { Node::new(kind,vec![Node::optional(condition)]) }
            / extended:kw("extended")? (kw("index") / kw("indexes") / kw("keys")) (kw("from") / kw("in")) table:relation_name() database:show_database()? filter:(kw("where") value:expr(0) { value })? {?
                Ok(Node::new(Kind::ShowIndexes,vec![Node::relation(table)?,Node::optional(database),Node::where_clause(filter),Node::new(Kind::HiddenInt,vec![]).value(u64::from(extended.is_some()))]))
            }
            / (kw("databases") / kw("schemas")) status:kw("status")? condition:show_condition()? {
                Node::new(Kind::ShowDatabases,vec![Node::optional(condition),Node::missing()]).value(u64::from(status.is_some()))
            }
            / modifiers:show_modifiers() kw("tables") database:show_database()? condition:show_condition()? {
                Node::new(Kind::ShowTables,vec![Node::optional(database),Node::optional(condition),Node::new(Kind::Int,vec![]).value(modifiers)])
            }
            / kind:(kw("table") kw("status") { Kind::ShowTableStatus }
                / kw("procedure") kw("status") { Kind::ShowProcedureStatus }
                / kw("function") kw("status") { Kind::ShowFunctionStatus }
                / kw("triggers") { Kind::ShowTriggers }) database:show_database()? condition:show_condition()? {
                Node::new(kind,vec![Node::optional(database),Node::optional(condition)])
            }
            / (kw("profile") / kw("profiles")) { Node::new(Kind::ShowProfile,vec![]) }
            / kw("storage")? kw("engines") { Node::new(Kind::ShowEngines,vec![]) }
            / kw("engine") (string() / ident() / kw("all"))
                (kw("status") / kw("mutex")) { Node::new(Kind::ShowEngine,vec![]) }
            / kw("privileges") { Node::new(Kind::ShowPrivileges,vec![]) }
            / kw("recyclebin") { Node::new(Kind::ShowRecyclebin,vec![]) }
            / kw("open") kw("tables") show_database()? condition:show_condition()? {
                Node::new(Kind::ShowOpenTables,vec![Node::optional(condition)])
            }
            / modifiers:show_modifiers() (kw("columns") / kw("fields")) (kw("from") / kw("in")) table:relation_name() database:show_database()? condition:show_condition()? {?
                Ok(Node::new(Kind::ShowColumns,vec![Node::new(Kind::Int,vec![]).value(modifiers),Node::relation(table)?,Node::optional(database),Node::optional(condition)]))
            }
            / scope:scope()? kw("variables") condition:show_condition()? {
                Node::new(Kind::ShowVariables,vec![Node::new(Kind::Int,vec![]).value(scope.unwrap_or(0)),Node::optional(condition)])
            }
            / scope:scope()? kw("status") condition:show_condition()? {
                Node::new(Kind::ShowStatus,vec![Node::new(Kind::Int,vec![]).value(scope.unwrap_or(0)),Node::optional(condition)])
            }
            / kw("create") kind:(kw("table") { Kind::ShowCreateTable } / kw("view") { Kind::ShowCreateView }
                / kw("procedure") { Kind::ShowCreateProcedure } / kw("function") { Kind::ShowCreateFunction }
                / kw("trigger") { Kind::ShowCreateTrigger }) name:relation_name() {? Ok(Node::new(kind,vec![Node::relation(name)?])) }
            / kw("create") kw("user") user:user_spec(false) {
                Node::new(Kind::ShowCreateUser,vec![Node::new(Kind::Users,vec![user])])
            }
            / kw("create") (kw("database") / kw("schema")) absent:if_not_exists()? name:ident() { Node::new(Kind::ShowCreateDatabase,vec![Node::optional(absent),Node::name(name)]) }
            / kind:(kw("warnings") { Kind::ShowWarnings } / kw("errors") { Kind::ShowErrors }) limit:show_limit()? { Node::new(kind,vec![Node::optional(limit)]) }
            / kw("count") "(" _ "*" _ ")" _ kind:(kw("warnings") { Kind::ShowWarnings } / kw("errors") { Kind::ShowErrors }) {
                Node::new(kind,vec![Node::missing()]).value(1)
            }
        rule check_table_option() = kw("for") kw("upgrade") / kw("quick") / kw("fast")
            / kw("medium") / kw("extended") / kw("changed")
        rule show() -> Node
            = kw("show") value:show_body() { value }
            / enabled(!pl) kw("check") kw("table") tables:((name:relation_name() {? Node::relation(name) }) ++ ("," _)) check_table_option()* {
                Node::new(Kind::CheckTable,tables)
            }
            / (kw("describe") / kw("desc") / kw("explain")) table:relation_name()
                column:(value:(string() / name:ident() { Node::new(Kind::String,vec![]).text(name.text) }) {
                    Node::new(Kind::LikeClause,vec![value,Node::new(Kind::String,vec![]).text("\\".into())])
                })? {? Ok(Node::new(Kind::ShowColumns,vec![Node::new(Kind::Int,vec![]),Node::relation(table)?,Node::missing(),Node::optional(column)])) }
        rule config_scope() -> u64 = kw("scope") "=" _ value:(kw("memory") { 0 } / kw("spfile") { 1 } / kw("both") { 2 }) { value }
        rule config_value() -> Node = string() / temporal_literal() / bool_null() / system_variable() / number()
            / start:position!() "-" _ value:number() {? Node::unary("-",value,start) }
        rule config_assignment() -> Node = name:ident() "=" _ value:config_value()
            comment:(kw("comment") value:string() { value })? scope:config_scope()? {
                Node::new(Kind::ConfigAction,vec![Node::name(name),value,Node::optional(comment),Node::missing()]).value(scope.unwrap_or(2))
            }
        rule tracepoint_option() -> Node
            = kw("tp_name") ("=" _)? name:(string() / name:ident() { Node::name(name) }) { Node::new(Kind::TracepointName,vec![name]) }
            / kind:(kw("tp_no") { Kind::TracepointNumber } / kw("occur") { Kind::TracepointOccurrence }
                / kw("frequency") { Kind::TracepointFrequency } / kw("error_code") { Kind::TracepointError }
                / kw("match") { Kind::TracepointMatch }) ("=" _)? value:type_size() { Node::new(kind,vec![value]) }
        rule cache_type() -> Node = value:(kw("plan") { "plan" } / kw("pl") { "pl" } / kw("ps") { "ps" } / kw("lib") { "lib" }) {
            Node::new(Kind::CacheType,vec![]).text(value.into())
        }
        rule flush_privileges() -> Node = kw("flush") (kw("local") / kw("no_write_to_binlog"))? kw("privileges") {
            Node::new(Kind::FlushPrivileges,vec![])
        }
        rule flush_cache() -> Node = kw("flush") value:(
            cache:cache_type() kw("cache")
            namespace:(kw("namespace") ("=" _)? value:string() { Node::new(Kind::CacheNamespace,vec![value]) })?
            id:(kw("sql_id") ("=" _)? value:string() { Node::new(Kind::SqlId,vec![value]) }
                / kw("schema_id") ("=" _)? value:limit_value() { Node::new(Kind::SchemaId,vec![value]) })?
            databases:(kw("databases") ("=" _)? value:string() { Node::new(Kind::DatabaseList,vec![value]) })? kw("global")? {
                Node::new(Kind::FlushCache,vec![cache,Node::optional(namespace),Node::optional(id),Node::optional(databases)])
            }
            / kw("sql") cache:cache_type() kw("global")? { Node::new(Kind::FlushCache,vec![cache,Node::missing(),Node::missing(),Node::missing()]) }
            / kw("kvcache") name:(kw("cache") ("=" _)? value:collation_name() { Node::new(Kind::CacheName,vec![value]) })? { Node::new(Kind::FlushKvCache,vec![Node::optional(name)]) }
        ) { value }
        rule alter_system() -> Node = kw("alter") kw("system") value:(
            flush_cache()
            / kw("flush") kw("dag") kw("warnings") { Node::new(Kind::FlushDagWarnings,vec![]) }
            / kw("refresh") kw("io") kw("calibration")
                storage:(kw("storage") ("=" _)? value:string() { value })?
                entries:(kw("calibration_info") ("=" _)? "(" _ ("," _ &string())? values:(string() ** ("," _)) ")" _ {
                    Node::new(Kind::StringList,values)
                })? {
                    Node::new(Kind::RefreshIoCalibration,vec![Node::optional(storage),Node::optional(entries)])
                }
            / kind:(kw("switchover") kw("to") target:(kw("primary") { Kind::SwitchoverToPrimary } / kw("standby") { Kind::SwitchoverToStandby }) { target }
                / kw("activate") kw("standby") { Kind::ActivateStandby }) verify:kw("verify")? {
                    Node::new(kind,vec![]).value(u64::from(verify.is_some()))
                }
            / kind:(kw("suspend") { Kind::SuspendMerge } / kw("resume") { Kind::ResumeMerge }) kw("merge") { Node::new(kind,vec![]) }
            / kw("clear") kw("merge") kw("error") { Node::new(Kind::ClearMergeError,vec![]) }
            / kw("cancel") kw("task") id:string() { Node::new(Kind::CancelTask,vec![id]) }
            / kind:(kw("major") { 1 } / kw("minor") { 2 }) kw("freeze")
                tablet:(kw("tablet_id") "=" _ value:load_integer() { value })? {
                Node::new(Kind::Freeze,vec![Node::new(Kind::HiddenInt,vec![]).value(kind),Node::missing(),Node::optional(tablet)])
            }
            / kw("checkpoint") { Node::new(Kind::Freeze,vec![Node::new(Kind::HiddenInt,vec![]).value(1),Node::missing(),Node::missing()]) }
            / kw("set_tp") ("," _ &tracepoint_option())? options:(tracepoint_option() ** ("," _)) { Node::new(Kind::SetTracepoint,vec![Node::list(Kind::TracepointOptions,options),Node::missing()]) }
            / kw("reset") actions:((name:ident() scope:config_scope()? { Node::new(Kind::ConfigAction,vec![Node::name(name)]).value(scope.unwrap_or(2)) }) ++ ("," _)) {
                Node::new(Kind::ResetConfig,vec![Node::new(Kind::ConfigList,actions)])
            }
            / kw("set")? actions:(config_assignment() ++ ("," _)) { Node::new(Kind::SetConfig,vec![Node::new(Kind::ConfigList,actions)]) }
        ) { value }
        rule account_name() -> Node = string() / name:ident() { Node::name(name) }
        rule account() -> Node = name:account_name() host:user_variable()? {
            Node::new(Kind::UserHost,vec![name,Node::optional(host)])
        }
        rule account_principal() -> Node
            = kw("current_user") ("(" _ ")" _)? { Node::new(Kind::CurrentUser,vec![]) }
            / account()
        rule named_account_spec() -> Node = principal:account() { Node::new(Kind::UserSpec,vec![principal]) }
        rule password() -> Node = value:string() { value.with_kind(Kind::Password) }
        rule authentication() -> Node = kw("identified")
            plugin:(kw("with") value:account_name() { Node::new(Kind::AuthenticationPlugin,vec![value]) })?
            password:(kw("by") encoded:kw("password")? value:password() {
                    (if encoded.is_some() { Kind::EncodedAuthentication } else { Kind::PlaintextAuthentication },value)
                }
                / kw("as") value:password() { (Kind::EncodedAuthentication,value) }) {
                let mut children=vec![password.1];
                children.extend(plugin);
                Node::new(password.0,children)
            }
        rule user_spec(authenticated: bool) -> Node = principal:account_principal()
            auth:(enabled(authenticated) value:authentication() { value })? {
            let mut children=vec![principal];
            children.extend(auth);
            Node::new(Kind::UserSpec,children)
        }
        rule privilege_name() -> &'static str
            = kw("create") kw("ai") kw("model") { "create ai model" }
            / kw("alter") kw("ai") kw("model") { "alter ai model" }
            / kw("drop") kw("ai") kw("model") { "drop ai model" }
            / kw("access") kw("ai") kw("model") { "access ai model" }
            / kw("create") kw("user") { "create user" }
            / kw("grant") kw("option") { "grant option" }
            / kw("create") kw("view") { "create view" }
            / kw("show") kw("view") { "show view" }
            / kw("show") kw("databases") { "show databases" }
            / kw("replication") kw("slave") { "replication slave" }
            / kw("replication") kw("client") { "replication client" }
            / kw("alter") kw("system") { "alter system" }
            / kw("alter") kw("routine") { "alter routine" }
            / kw("create") kw("routine") { "create routine" }
            / kw("lock") kw("tables") { "lock tables" }
            / kw("create") kw("role") { "create role" }
            / kw("drop") kw("role") { "drop role" }
            / kw("alter") { "alter" }
            / kw("create") { "create" }
            / kw("delete") { "delete" }
            / kw("drop") { "drop" }
            / kw("insert") { "insert" }
            / kw("update") { "update" }
            / kw("select") { "select" }
            / kw("index") { "index" }
            / kw("super") { "super" }
            / kw("process") { "process" }
            / kw("file") { "file" }
            / kw("execute") { "execute" }
            / kw("shutdown") { "shutdown" }
            / kw("reload") { "reload" }
            / kw("references") { "references" }
            / kw("trigger") { "trigger" }
            / kw("read") { "read" }
            / kw("write") { "write" }
            / kw("usage") { "usage" }
        rule privilege() -> Node = name:privilege_name()
            columns:(enabled(matches!(name,"select"|"insert"|"update"|"references")) "(" _ names:(ident() ++ ("," _)) ")" _ { names })? {?
                let children=columns.map(|names| vec![Node::new(Kind::ColumnList,names.into_iter().map(Node::name).collect())]).unwrap_or_default();
                Ok(Node::new(Kind::Privilege,children).text(name.into()))
            }
        rule privileges() -> Vec<Node> = kw("all") kw("privileges")? { vec![Node::new(Kind::Privilege,vec![]).text("all".into())] }
            / values:((privilege() / named_account_spec()) ++ ("," _)) { values }
        rule privilege_target() -> Node = object:privilege_object()?
            first:("*" _ { Node::new(Kind::Star,vec![]) } / name:ident() { Node::name(name) })
            second:("." _ value:("*" _ { Node::new(Kind::Star,vec![]) } / name:ident() { Node::name(name) }) { value })? {
                access::privilege_target(object,first,second)
            }
        rule privilege_object() -> Node = kind:(kw("table") { Kind::TablePrivilegeObject }
            / kw("procedure") { Kind::ProcedurePrivilegeObject } / kw("function") { Kind::FunctionPrivilegeObject }) {
            Node::new(kind,vec![])
        }
        rule accounts() -> Node = values:(user_spec(false) ++ ("," _)) { Node::new(Kind::Users,values) }
        rule roles() -> Vec<Node> = values:(named_account_spec() ++ ("," _)) { values }
        rule granted_roles() -> Vec<Node> = values:((!privilege_name() value:named_account_spec() { value }) ++ ("," _)) { values }
        rule ignore_unknown_user() -> Node = kw("ignore") kw("unknown") kw("user") { Node::new(Kind::IgnoreUnknownUser,vec![]) }
        rule role_selection(session: bool) -> Node
            = kw("all") except:(enabled(session) kw("except") roles:granted_roles() { roles })? {
                match except {
                    Some(roles) => Node::new(Kind::AllExceptRoles,vec![Node::new(Kind::Roles,roles)]),
                    None => Node::new(Kind::AllRoles,vec![]),
                }
            }
            / kind:(kw("none") { Kind::NoRoles } / enabled(session) kw("default") { Kind::DefaultRoles }) {
                Node::new(kind,vec![])
            }
            / roles:roles() { Node::new(Kind::ListedRoles,vec![Node::new(Kind::Roles,roles)]) }
        rule role_statement() -> Node
            = kw("create") kw("role") absent:if_not_exists()? roles:roles() {
                let mut children=vec![Node::new(Kind::Users,roles)];
                children.extend(absent);
                Node::new(Kind::CreateRole,children)
            }
            / kw("drop") kw("role") exists:if_exists()? roles:accounts() {
                let mut children=vec![roles];
                children.extend(exists);
                Node::new(Kind::DropRole,children)
            }
            / kw("grant") roles:granted_roles() kw("to") users:accounts() option:(kw("with") kw("admin") kw("option") { Node::new(Kind::WithAdminOption,vec![]) })? {
                let mut children=vec![Node::new(Kind::Roles,roles),users];
                children.extend(option);
                Node::new(Kind::GrantRole,children)
            }
            / kw("revoke") exists:if_exists()? roles:granted_roles() kw("from") users:accounts()
                ignore:ignore_unknown_user()? {
                    let mut children=vec![Node::new(Kind::Roles,roles),users];
                    children.extend(exists);
                    children.extend(ignore);
                    Node::new(Kind::RevokeRole,children)
                }
            / kw("set") kw("role") selection:role_selection(true) { Node::new(Kind::SetRole,vec![selection]) }
            / kw("set") kw("default") kw("role") selection:role_selection(false) kw("to") users:accounts() { Node::new(Kind::SetDefaultRole,vec![users,selection]) }
            / kw("alter") kw("user") user:user_spec(false) kw("default") kw("role") selection:role_selection(false) { Node::new(Kind::AlterUserDefaultRole,vec![Node::new(Kind::Users,vec![user]),selection]) }
        rule lock_tables() -> Node
            = kw("lock") (kw("table") / kw("tables")) tables:(table_lock() ++ ("," _)) {
                Node::new(Kind::LockTables,tables)
            }
            / enabled(!pl) kw("unlock") (kw("table") / kw("tables")) { Node::new(Kind::UnlockTables,vec![]) }
        rule table_lock() -> Node = name:relation_name()
            alias:(kw("as") name:ident() { name })?
            write:(kw("read") kw("local")? { false } / kw("low_priority")? kw("write") { true }) {?
                Ok(Node::new(Kind::TableLock,vec![Node::table(Node::relation(name)?,alias)?])
                    .value(u64::from(write)))
            }
        rule kill() -> Node = kw("kill") query:(kw("query") { true } / kw("connection") { false })? target:expr(0) {
            Node::new(Kind::Kill,vec![Node::new(Kind::Bool,vec![]).value(u64::from(query.unwrap_or(false))),target])
        }
        rule restore_table() -> Node = kw("flashback") kw("table") source:relation_name()
            kw("to") kw("before") kw("drop") target:(kw("rename") kw("to") value:relation_name() { value })? {?
                let mut children=vec![Node::relation(source)?];
                if let Some(target)=target { children.push(Node::relation(target)?); }
                Ok(Node::new(Kind::RestoreTable,children))
            }
        rule restore_database() -> Node = kw("flashback") (kw("database") / kw("schema")) source:ident()
            kw("to") kw("before") kw("drop") target:(kw("rename") kw("to") name:ident() { name })? {
                let mut children=vec![Node::name(source)];children.extend(target.map(Node::name));
                Node::new(Kind::RestoreDatabase,children)
            }
        rule purge() -> Node = kw("purge") value:(kw("recyclebin") { Node::new(Kind::PurgeRecyclebin,vec![]) }
            / kind:(kw("table") { Kind::PurgeTable } / kw("index") { Kind::PurgeIndex }) name:relation_name() {? Ok(Node::new(kind,vec![Node::relation(name)?])) }
            / (kw("database") / kw("schema")) name:ident() { Node::new(Kind::PurgeDatabase,vec![Node::name(name)]) }) { value }
        rule account_tls() -> Node = kw("require") value:(
            kw("none") { Node::new(Kind::TlsNone,vec![]) }
            / kw("ssl") { Node::new(Kind::TlsSsl,vec![]) }
            / kw("x509") { Node::new(Kind::TlsX509,vec![]) }
            / options:(account_tls_option() ++ (kw("and")?)) { Node::new(Kind::TlsOptions,options) }
        ) { value }
        rule account_tls_option() -> Node = kind:(kw("cipher") { Kind::TlsCipher }
            / kw("issuer") { Kind::TlsIssuer } / kw("subject") { Kind::TlsSubject }) value:string() {
            value.with_kind(kind)
        }
        rule account_resource_limits() -> Vec<Node> = kw("with") limits:(
            kind:(kw("max_connections_per_hour") { Kind::MaxConnectionsPerHour }
                / kw("max_user_connections") { Kind::MaxUserConnections }) value:number() {
                Node::new(kind,vec![value])
            })+ { limits }
        rule account_statement() -> Node
            = kw("create") kw("user") absent:if_not_exists()? users:(user_spec(true) ++ ("," _)) tls:account_tls()? limits:account_resource_limits()? {
                let mut children=vec![Node::new(Kind::Users,users)];
                children.extend(absent);
                children.extend(tls);
                children.extend(limits.into_iter().flatten());
                Node::new(Kind::CreateUser,children)
            }
            / kw("rename") kw("user") pairs:(
                source:named_account_spec() kw("to") target:named_account_spec() {
                    Node::new(Kind::AccountRename,vec![source,target])
                }) ++ ("," _) { Node::new(Kind::RenameUser,pairs) }
            / kw("drop") kw("user") exists:if_exists()? users:accounts() {
                let mut children=vec![users];
                children.extend(exists);
                Node::new(Kind::DropUser,children)
            }
            / kw("alter") kw("user") users:accounts() kw("account") locked:(kw("lock") { true } / kw("unlock") { false }) {
                Node::new(Kind::LockUser,vec![users]).value(u64::from(locked))
            }
            / kw("alter") kw("user") user:user_spec(false) tls:account_tls() {
                Node::new(Kind::SetPassword,vec![Node::new(Kind::Users,vec![user]),tls])
            }
            / kw("alter") kw("user") user:user_spec(false) limits:account_resource_limits() {
                let mut children=vec![Node::new(Kind::Users,vec![user])];
                children.extend(limits);
                Node::new(Kind::SetPassword,children)
            }
            / kw("alter") kw("user") user:account_principal() auth:authentication() {
                Node::new(Kind::SetPassword,vec![Node::new(Kind::Users,vec![Node::new(Kind::UserSpec,vec![user,auth])])])
            }
            / kw("set") kw("password") user:(kw("for") user:account() { user })? "=" _
                auth:(kw("password") "(" _ password:password() ")" _ { Node::new(Kind::PlaintextAuthentication,vec![password]) }
                    / password:password() { Node::new(Kind::EncodedAuthentication,vec![password]) }) {
                    let user=user.unwrap_or_else(|| Node::new(Kind::CurrentUser,vec![]));
                    Node::new(Kind::SetPassword,vec![Node::new(Kind::Users,vec![Node::new(Kind::UserSpec,vec![user,auth])])])
                }
            / kw("grant") privileges:privileges() kw("on") target:privilege_target()
                kw("to") users:(user_spec(true) ++ ("," _)) option:(kw("with") kw("grant") kw("option") { Node::new(Kind::WithGrantOption,vec![]) })? {
                    let mut children=vec![Node::new(Kind::Privileges,privileges),target,Node::new(Kind::Users,users)];
                    children.extend(option);
                    Node::new(Kind::Grant,children)
                }
            / kw("revoke") exists:if_exists()? kw("all") kw("privileges")? "," _ kw("grant") kw("option") kw("from") users:accounts() ignore:ignore_unknown_user()? {
                let mut children=vec![users];
                children.extend(exists);
                children.extend(ignore);
                Node::new(Kind::RevokeAll,children)
            }
            / kw("revoke") exists:if_exists()? privileges:privileges() kw("on") target:privilege_target() kw("from") users:accounts() ignore:ignore_unknown_user()? {
                let mut children=vec![Node::new(Kind::Privileges,privileges),target,users];
                children.extend(exists);
                children.extend(ignore);
                Node::new(Kind::Revoke,children)
            }
        rule routine_reserved_word() = text:syntax_text(<$(word())>) {?
            if reserved_in_routine(text) { Ok(()) } else { Err("routine keyword") }
        }
        rule pl_ident() -> Name
            = !routine_reserved_word() value:(ident()
            / &reserved_word() start:position!() text:syntax_text(<$(word())>) end:position!() _
                { Name { text: text.to_owned(), quoted: false, start, end } }) { value }
        rule package_routine() -> Node = start:position!() function:(kw("procedure") { false } / kw("function") { true }) name:pl_ident() params:routine_parameters(false,true)
            result:(enabled(function) kw("return") value:routine_data_type() { value })? end:position!() {?
                if function && result.is_none() { return Err("function return type"); }
                let mut children=vec![Node::name(name),params];
                if function { children.extend([Node::optional(result),Node::missing()]); }
                children.push(Node::missing());
                Ok(Node::new(if function { Kind::SubFunctionDeclaration } else { Kind::SubProcedureDeclaration },children).at(start,end))
            }
        rule package_declaration(body: bool) -> Node
            = start:position!() declaration:package_routine() definition:(enabled(body) value:pl_statement(0) { value })? end:position!() {
                match definition { Some(value) => Node::new(if declaration.kind==Kind::SubFunctionDeclaration { Kind::SubFunctionDefinition } else { Kind::SubProcedureDefinition },vec![declaration,value]).at(start,end),None => declaration }
            }
            / enabled(body) kw("pragma") kw("interface") "(" _ kw("c") "," _ name:pl_ident() ")" _ { Node::new(Kind::InterfacePragma,vec![Node::name(name)]) }
            / pl_declaration(0)
        rule package_block() -> Node = start:position!() kw("package") body:kw("body")? name:routine_name()
            rights:(enabled(body.is_none()) kw("authid") value:(kw("current_user") { 1 } / kw("definer") { 0 }) { Node::new(Kind::InvokeRights,vec![]).value(value) })?
            contents_start:position!() declarations:((value:package_declaration(body.is_some()) ";" _ { value })*)
            statements:(enabled(body.is_some()) value:pl_statements(0) { value })? kw("end") tail:pl_ident()? end:position!() {
                let declarations=Node::list(if body.is_some() { Kind::PackageBodyStatements } else { Kind::PackageStatements },declarations).at(contents_start,end);
                let middle=if body.is_some() { vec![declarations,Node::optional(statements)] } else { vec![Node::list(Kind::RoutineClauses,rights.into_iter().collect()),declarations] };
                let mut children=vec![name];children.extend(middle);children.push(tail.map(Node::name).unwrap_or_else(Node::missing));
                Node::new(if body.is_some() { Kind::PackageBody } else { Kind::Package },children).at(start,end)
            }
        rule package_statement() -> Node
            = kw("create") replace:(kw("or") kw("replace"))? value:package_block() {
                Node::new(if value.kind==Kind::PackageBody { Kind::CreatePackageBody } else { Kind::CreatePackage },vec![value]).value(u64::from(replace.is_some()))
            }
            / kw("drop") kw("package") body:kw("body")? name:routine_name() { Node::new(Kind::DropPackage,vec![name]).value(if body.is_some() { 2 } else { 0 }) }
        rule routine_name() -> Node = parts:(pl_ident() ++ ("." _)) {?
            if parts.len()>2 { return Err("routine name has at most two parts"); }
            let mut names:Vec<_>=(parts.len()..2).map(|_| Node::missing()).collect();
            names.extend(parts.into_iter().map(Node::name));
            Ok(Node::new(Kind::RoutineName,names))
        }
        rule routine_default() -> Node = kw("default") start:position!() value:expr(0) end:position!() {
            Node::new(Kind::PlDefault,vec![value]).at(start,end)
        }
        rule row_type() -> Node = parts:(pl_ident() ++ ("." _)) "%" _ kw("rowtype") {?
            if parts.len()>3 { return Err("row type has at most three name parts"); }
            let mut names:Vec<_>=(parts.len()..3).map(|_| Node::missing()).collect();
            names.extend(parts.into_iter().map(Node::name));
            Ok(Node::new(Kind::RowType,vec![Node::new(Kind::PlTypeAccess,vec![Node::new(Kind::PlCallName,names),Node::missing()]),Node::missing()]))
        }
        rule routine_parameter(function: bool,row_types: bool) -> Node
            = direction:(enabled(!function) value:(kw("inout") { 2 } / kw("out") { 1 } / kw("in") { 0 }) { value })?
                name:pl_ident() data_type:(routine_data_type() / enabled(row_types) value:row_type() { value }) default:(enabled(!function) value:routine_default() { value })? {
                let mut children=vec![Node::name(name),data_type];
                if !function { children.push(Node::optional(default)); }
                Node::new(Kind::RoutineParameter,children).value(direction.unwrap_or(0))
            }
        rule routine_parameters(function: bool,row_types: bool) -> Node = "(" start:position!() _ values:(routine_parameter(function,row_types) ** ("," _)) end:position!() ")" _ {
            Node::list(Kind::RoutineParameters,values).at(start,end)
        }
        rule routine_characteristic(alter: bool) -> Node
            = kw("comment") value:string() { value.with_kind(Kind::ColumnComment) }
            / kw("language") kw("sql") { Node::missing() }
            / enabled(!alter) kw("not") kw("deterministic") { Node::missing() }
            / enabled(!alter) kw("deterministic") { Node::new(Kind::Deterministic,vec![]) }
            / value:(kw("contains") kw("sql") { 0 } / kw("no") kw("sql") { 1 }
                / kw("reads") kw("sql") kw("data") { 2 } / kw("modifies") kw("sql") kw("data") { 3 }) {
                Node::new(Kind::DataAccess,vec![]).value(value)
            }
            / kw("sql") kw("security") value:(kw("definer") { 0 } / kw("invoker") { 2 }) { Node::new(Kind::InvokeRights,vec![]).value(value) }
        rule routine_definition(internal: bool, level: usize) -> Node = start:position!() function:(kw("procedure") { false } / kw("function") { true })
            absent:if_not_exists()? name:routine_name() params:routine_parameters(function,internal)
            result:(enabled(function) kw("returns") value:routine_data_type() { value })? enabled(!function || result.is_some())
            clauses:routine_characteristic(false)* body:pl_statement(level) end:position!() {?
                let mut children=vec![Node::missing(),name,params];
                if function { children.push(Node::optional(result)); }
                children.push(Node::list(Kind::RoutineClauses,clauses.into_iter().filter(|node| node.kind!=Kind::Missing).collect()));
                children.push(body);
                Ok(Node::new(if function { Kind::CreateFunction } else { Kind::CreateProcedure },children).value(u64::from(absent.is_some())).at(start,end))
            }
        rule trigger_source(level: usize) -> Node = start:position!() kw("trigger") absent:if_not_exists()? name:routine_name()
            after:(kw("before") { false } / kw("after") { true })
            event:(kw("insert") { Kind::Insert } / kw("update") { Kind::Update } / kw("delete") { Kind::Delete })
            kw("on") table:routine_name() kw("for") kw("each") kw("row")
            order:(value:(kw("follows") { 1 } / kw("precedes") { 2 }) name:routine_name() { Node::new(Kind::TriggerOrder,vec![name]).value(value) })?
            body:pl_statement(level) end:position!() {
                Node::new(Kind::TriggerSource,vec![name,Node::new(Kind::TriggerDefinition,
                    vec![Node::new(event,vec![]),table,Node::optional(order),body]).value(u64::from(after)).at(start,end)])
                    .value(u64::from(absent.is_some()))
            }
        pub rule trigger() -> Node = _ value:trigger_source(0) (";" _)* { value }
        rule routine_statement(level: usize) -> Node
            = kw("create") definer:(kw("definer") "=" _ value:(kw("current_user") ("(" _ ")" _)? {
                Node::new(Kind::UserHost,vec![Node::new(Kind::Ident,vec![]).text("CURRENT_USER".into()),Node::missing()])
            } / name:account_name() start:position!() "@%" end:position!() _ {
                Node::new(Kind::UserHost,vec![name,Node::new(Kind::UserVariable,vec![]).text("%".into()).at(start,end)])
            } / account()) { value })? mut_value:(routine_definition(false,level) / source:trigger_source(level) { Node::new(Kind::CreateTrigger,vec![Node::missing(),source]) }) {
                let mut value=mut_value;value.children[0]=Node::optional(definer);value
            }
            / kw("drop") kind:(kw("procedure") { Kind::DropProcedure } / kw("function") { Kind::DropFunction } / kw("trigger") { Kind::DropTrigger }) exists:if_exists()? name:routine_name() {
                Node::new(kind,vec![name]).value(u64::from(exists.is_some()))
            }
            / kw("alter") kind:(kw("procedure") { Kind::AlterProcedure } / kw("function") { Kind::AlterFunction }) name:routine_name() clauses:routine_characteristic(true)* {
                Node::new(kind,vec![name,Node::list(Kind::RoutineClauses,clauses.into_iter().filter(|node| node.kind!=Kind::Missing).collect())])
            }
        rule pl_expression(level: usize) -> Node = value:expr(level) {? value.pl_expression() }
        rule pl_statements(level: usize) -> Node = values:((value:pl_statement(level) ";" _ { value })*) { Node::list(Kind::PlStatements,values) }
        rule pl_block(level: usize) -> Node = kw("begin")
            declarations:(values:((value:pl_declaration(level) ";" _ { value })+) (";" _)* { values })?
            statements:pl_statements(level) kw("end") {
                Node::new(Kind::PlBlock,vec![Node::list(Kind::PlDeclarations,declarations.unwrap_or_default()),statements])
            }
        rule sql_state() -> Node = kw("sqlstate") kw("value")? value:string() { Node::new(Kind::SqlState,vec![value]) }
        rule pl_condition() -> Node = value:(type_size() / sql_state()) { Node::new(Kind::PlCondition,vec![value]) }
        rule handler_condition() -> Node
            = kw("sqlexception") { Node::new(Kind::SqlException,vec![]) }
            / kw("sqlwarning") { Node::new(Kind::SqlWarning,vec![]) }
            / kw("not") kw("found") { Node::new(Kind::SqlNotFound,vec![]) }
            / pl_condition() / value:pl_ident() { Node::name(value) }
        rule pl_declaration(level: usize) -> Node = start:position!() kw("declare") value:(
            kind:(kw("continue") { 1 } / kw("exit") { 0 }) kw("handler") kw("for") conditions:(handler_condition() ++ ("," _)) body:pl_statement(level+1) {
                Node::new(Kind::PlHandler,vec![Node::new(Kind::PlHandlerConditions,conditions),Node::new(Kind::PlStatements,vec![body])]).value(kind)
            }
            / name:pl_ident() kw("condition") kw("for") value:pl_condition() { Node::new(Kind::PlConditionDeclaration,vec![Node::name(name),value]) }
            / name:pl_ident() kw("cursor") kw("for") start:position!() query:select(level+1) end:position!() {
                Node::new(Kind::PlCursor,vec![Node::name(name),Node::missing(),Node::missing(),Node::new(Kind::PlSql,vec![query]).at(start,end)])
            }
            / names:(pl_ident() ++ ("," _)) data_type:routine_data_type() default:routine_default()? {
                Node::new(Kind::PlDeclaration,vec![Node::new(Kind::PlIdentifiers,names.into_iter().map(Node::name).collect()),data_type,Node::optional(default)])
            }
        ) end:position!() { value.at(start,end) }
        rule pl_if(level: usize) -> Node = condition:pl_expression(level) kw("then") body:pl_statements(level) alternative:pl_else(level)? {
            Node::new(Kind::PlIf,vec![condition,body,Node::optional(alternative)])
        }
        rule pl_else(level: usize) -> Node
            = kw("elseif") value:pl_if(level) { Node::new(Kind::PlElse,vec![Node::new(Kind::PlStatements,vec![value])]) }
            / kw("else") body:pl_statements(level) { Node::new(Kind::PlElse,vec![body]) }
        rule pl_control(level: usize) -> Node
            = kw("loop") body:pl_statements(level) kw("end") kw("loop") { Node::new(Kind::PlLoop,vec![body]) }
            / kw("while") condition:pl_expression(level) kw("do") body:pl_statements(level) kw("end") kw("while") { Node::new(Kind::PlWhile,vec![condition,body]) }
            / kw("repeat") body:pl_statements(level) kw("until") condition:pl_expression(level) kw("end") kw("repeat") { Node::new(Kind::PlRepeat,vec![body,condition]) }
        rule pl_setting(level: usize) -> Node = kw("set") assignments:((
            variable:(start:position!() scope:assignment_scope() name:assignment_name(true) {
                    let end=name.end; Node::new(Kind::SystemVariable,vec![]).text(name.text).value(scope).at(start,end)
                } / user_variable() / system_variable()
                / start:position!() parts:qualified_name() enabled(parts.len()==2) end:position!() {? Node::column(parts,false,start,end) }
                / &quiet!{ident() "."} start:position!() first:ident() "." _ second:ident() end:position!() {? Node::column(vec![first,second],false,start,end) }
                / name:assignment_name(false) { Node::name(name) }) (":=" / "=" / kw("to")) _
                value:(enabled(variable.kind!=Kind::UserVariable) keyword:setting_keyword() { keyword } / expr(level)) {?
                let value=Node::routine_setting_value(&variable,value)?;
                let scope=if variable.kind==Kind::SystemVariable && variable.value!=0 { variable.value } else { 2 };
                Ok(Node::new(Kind::VariableValue,vec![variable,value]).value(scope))
            }
        ) ++ ("," _)) { Node::new(Kind::VariableSet,assignments) }
        rule call_argument() -> Node = start:position!() first:expr(0) second:("=>" _ value:expr(0) { value })? end:position!() {?
            match second { Some(value) => Node::new(Kind::PlNamedArgument,vec![first,value]).expression(start,end),None => Ok(first) }
        }
        rule pl_call() -> Node = start:position!() kw("call") names:(pl_ident() ++ ("." _))
            arguments:("(" _ values:(call_argument() ** ("," _)) ")" _ { values })? end:position!() {?
                if names.len()>3 { return Err("routine name has at most three parts"); }
                let mut parts:Vec<_>=(names.len()..3).map(|_| Node::missing()).collect();parts.extend(names.into_iter().map(Node::name));
                Ok(Node::new(Kind::PlCall,vec![Node::new(Kind::PlCallName,parts),Node::list(Kind::PlCallArguments,arguments.unwrap_or_default())]).at(start,end))
            }
        rule signal_item_name() -> (&'static str,u64)
            = kw("class_origin") { ("CLASS_ORIGIN",0) } / kw("subclass_origin") { ("SUBCLASS_ORIGIN",1) }
            / kw("constraint_catalog") { ("CONSTRAINT_CATALOG",2) } / kw("constraint_schema") { ("CONSTRAINT_SCHEMA",3) }
            / kw("constraint_name") { ("CONSTRAINT_NAME",4) } / kw("catalog_name") { ("CATALOG_NAME",5) }
            / kw("schema_name") { ("SCHEMA_NAME",6) } / kw("table_name") { ("TABLE_NAME",7) }
            / kw("column_name") { ("COLUMN_NAME",8) } / kw("cursor_name") { ("CURSOR_NAME",9) }
            / kw("message_text") { ("MESSAGE_TEXT",10) } / kw("mysql_errno") { ("MYSQL_ERRNO",11) }
        rule pl_signal() -> Node = kind:(kw("signal") { Kind::PlSignal } / kw("resignal") { Kind::PlResignal })
            condition:(sql_state() / !kw("set") value:pl_ident() { Node::name(value) })?
            items:(kw("set") values:((name:signal_item_name() "=" _ value:(string() / number() / bool_null() / user_variable() / system_variable() / names:(pl_ident() ++ ("." _)) {? Node::column(names,false,0,0) }) {
                Node::new(Kind::PlSignalItem,vec![value]).value(name.1)
            }) ++ ("," _)) { values })? {?
                if kind==Kind::PlSignal && condition.is_none() { Err("SIGNAL condition") }
                else { Ok(Node::new(kind,vec![Node::optional(condition),Node::list(Kind::PlSignalItems,items.unwrap_or_default())])) }
            }
        rule diagnostics_target() -> Node = user_variable() / parameter() / name:pl_ident() { Node::name(name) }
        rule diagnostics_item(condition: bool) -> Node = target:diagnostics_target() "=" _ name:(
            enabled(condition) name:(kw("returned_sqlstate") { "RETURNED_SQLSTATE" } / name:signal_item_name() { name.0 }) { name }
            / enabled(!condition) name:((kw("number") / kw("dec")) { "NUMBER" } / kw("row_count") { "ROW_COUNT" }) { name }
        ) { Node::binary_operands("=".into(),vec![target,Node::new(Kind::String,vec![]).text(name.into())]) }
        rule diagnostics() -> Node = kw("get") current:(kw("current") { 1 } / kw("stacked") { 0 })? kw("diagnostics")
            condition:(kw("condition") value:(user_variable() / parameter() / string() / type_size() / name:pl_ident() { Node::name(name) }) { value })?
            items:(diagnostics_item(condition.is_some()) ++ ("," _)) {
                Node::new(Kind::Diagnostics,vec![Node::new(Kind::Bool,vec![]).value(u64::from(condition.is_some())),Node::new(Kind::Bool,vec![]).value(current.unwrap_or(1)),Node::optional(condition),Node::new(Kind::Link,items)])
            }
        rule pl_do(level: usize) -> Node = kw("do") values:(pl_expression(level) ++ ("," _)) ("," _)* {
            Node::new(Kind::PlDo,values)
        }
        rule anonymous_statement() -> Node = start:position!() value:(pl_do(0) / pl_signal()) end:position!() {
            Node::new(Kind::PlBlock,vec![Node::new(Kind::PlDeclarations,vec![]),
                Node::new(Kind::PlStatements,vec![value.at(start,end)])]).at(start,end)
        }
        rule pl_statement(level: usize) -> Node = depth(level) start:position!() value:(
            label:(quiet!{ name:pl_ident() ":" _ { name } }) body:(pl_block(level+1) / pl_control(level+1)) tail:pl_ident()? {
                Node::new(if body.kind==Kind::PlBlock { Kind::PlLabeledBlock } else { Kind::PlLabeledControl },vec![Node::name(label),body,tail.map(Node::name).unwrap_or_else(Node::missing)])
            }
            / pl_block(level+1) / pl_control(level+1)
            / kw("if") body:pl_if(level+1) kw("end") kw("if") { body }
            / kw("case") value:pl_expression(level+1)?
                arms:((kw("when") condition:pl_expression(level+1) kw("then") body:pl_statements(level+1) { Node::new(Kind::PlWhen,vec![condition,body]) })+)
                alternative:pl_else(level+1)? kw("end") kw("case") { Node::new(Kind::PlCase,vec![Node::optional(value),Node::new(Kind::WhenList,arms),Node::optional(alternative)]) }
            / kw("return") value:pl_expression(level+1) { Node::new(Kind::PlReturn,vec![value]) }
            / kind:(kw("leave") { Kind::PlLeave } / kw("iterate") { Kind::PlIterate }) label:pl_ident() { Node::new(kind,vec![Node::name(label),Node::missing()]) }
            / kw("open") name:pl_ident() { Node::new(Kind::PlOpen,vec![Node::name(name),Node::missing(),Node::missing()]) }
            / kw("close") name:pl_ident() { Node::new(Kind::PlClose,vec![Node::name(name)]) }
            / kw("fetch") (kw("next") kw("from") / kw("from"))? name:pl_ident() kw("into") variables:((value:pl_expression(level+1) {
                crate::routine::cursor_fetch_target(value)
            }) ++ ("," _)) {
                Node::new(Kind::PlFetch,vec![Node::name(name),Node::new(Kind::IntoVariables,vec![Node::new(Kind::PlInto,variables)])])
            }
            / routine_statement(level+1) / pl_call() / pl_signal() / pl_do(level+1) / pl_setting(level+1)
            / !kw("begin") statement:sql_statement() {?
                if statement.kind==Kind::VariableSet && !(statement.children.len()==1
                    && matches!(statement.children[0].kind,Kind::SetNames | Kind::SetCharset)) {
                    Err("routine variable assignment")
                } else { Ok(Node::new(Kind::PlSql,vec![statement])) }
            }
        ) end:position!() { value.at(start,end) }
        pub rule routine_body() -> Node = _ value:(anonymous_statement() / package_block() / kw("create") value:routine_definition(true,0) { value }
            / value:routine_definition(true,0) { value.into_children().into_iter().last().unwrap() } / pl_statement(0)) (";" _)* { value }
        rule prepared_statement() -> Node
            = kw("prepare") name:ident() kw("from") source:(string() / quiet!{binary_literal()} / user_variable()) {
                Node::new(Kind::Prepare,vec![source]).text(name.text)
            }
            / kw("execute") name:ident() args:(kw("using") args:(user_variable() ++ ("," _)) { args })? {
                Node::new(Kind::Execute,args.unwrap_or_default()).text(name.text)
            }
            / (kw("deallocate") / kw("drop")) kw("prepare") name:ident() { Node::new(Kind::Deallocate,vec![]).text(name.text) }
        rule analyze() -> Node = kw("analyze") kw("table") value:(
                table:(names:relation_name() {? Node::relation(names) }) kw("update") kw("histogram") kw("on")
                columns:(ident() ++ ("," _)) buckets:(kw("with") value:type_size() kw("buckets") {
                    Node::new(Kind::StatsBucketCount,vec![value])
                })? {
                    let mut children=vec![Node::new(Kind::TableList,vec![table]),
                        Node::new(Kind::ColumnList,columns.into_iter().map(Node::name).collect())];
                    children.extend(buckets);
                    Node::new(Kind::Analyze,children).value(1)
                }
                / table:(names:relation_name() {? Node::relation(names) }) kw("drop") kw("histogram") kw("on")
                columns:(ident() ++ ("," _)) {
                    Node::new(Kind::Analyze,vec![Node::new(Kind::TableList,vec![table]),
                        Node::new(Kind::ColumnList,columns.into_iter().map(Node::name).collect())]).value(2)
                }
                / tables:((names:relation_name() {? Node::relation(names) }) ++ ("," _)) {
                    Node::new(Kind::Analyze,vec![Node::new(Kind::TableList,tables)])
                }
            ) { value }
        rule stats_size() -> Node = kw("size") value:(
                value:(kw("auto") { 0 } / kw("repeat") { 1 } / kw("skewonly") { 2 }) { Node::new(Kind::HiddenInt,vec![]).value(value) }
                / value:number() { Node::new(Kind::StatsBucketCount,vec![value]) }
            ) { value }
        rule stats_column() -> Node
            = column:("(" _ names:(ident() ++ ("," _)) ")" _ {
                    Node::new(Kind::StatsExtension,vec![Node::new(Kind::ColumnList,names.into_iter().map(Node::name).collect()),Node::missing()])
                } / !kw("size") !kw("for") name:ident() { Node::new(Kind::Column,vec![Node::missing(),Node::name(name),Node::missing()]) }) size:stats_size()? {
                Node::new(Kind::StatsColumn,vec![column,Node::optional(size)])
            }
            / size:stats_size() { Node::new(Kind::StatsColumn,vec![Node::missing(),size]) }
        rule stats_method() -> Node = kw("for") method:(
                kw("all") scope:(kw("indexed") { 1 } / kw("hidden") { 2 })? kw("columns") size:stats_size()? {
                    Node::new(Kind::StatsAll,vec![Node::new(Kind::HiddenInt,vec![]).value(scope.unwrap_or(0)),Node::optional(size)])
                }
                / kw("columns") columns:(first:stats_column() rest:(("," _)? value:stats_column() { value })* {
                    let mut columns=vec![first];columns.extend(rest);columns
                })? {
                    Node::new(Kind::StatsColumns,columns.unwrap_or_else(|| vec![Node::new(Kind::StatsColumn,vec![Node::missing(),Node::missing()])]))
                }
            ) { method }
        pub rule statistics_options() -> Node = _ methods:stats_method()+ (";" _)* { Node::new(Kind::StatsOptions,methods) }
        pub rule expression() -> Node = _ value:expr(0) { value }
        rule explain() -> Node = (kw("explain") / kw("describe") / kw("desc")) hints()?
            format:(kw("basic") { Kind::ExplainBasic } / kw("outline") { Kind::ExplainOutline }
                / kw("extended") { Kind::ExplainExtended } / kw("extended_noaddr") { Kind::ExplainExtendedNoAddr }
                / kw("partitions") { Kind::ExplainPartitions } / kw("planregress") { Kind::ExplainPlanRegress }
                / kw("format") "=" _ kind:(kw("traditional") { Kind::ExplainTraditional } / kw("json") { Kind::ExplainJson }) { kind })?
            display:(kw("pretty") { Kind::ExplainPretty } / kw("pretty_color") { Kind::ExplainPrettyColor })?
            into:(enabled(format.is_none() && display.is_none()) kw("into") name:ident() { Node::name(name) })?
            statement_id:(enabled(format.is_none() && display.is_none()) kw("set") kw("statement_id") "=" _ value:(string_expression() / number() / bool_null() / binary_literal() / temporal_literal()) {
                if value.kind==Kind::StringConcat {
                    let text=crate::expression::call_arguments(&value).iter().flat_map(|part| part.text.as_bytes().iter().copied()).collect();
                    Node::new(Kind::String,vec![]).bytes(text).at(value.start,value.end)
                } else { value }
            })?
            value:(select(0) / insert() / update() / delete()) {
                let mut children=vec![];
                children.extend(format.map(|kind| Node::new(kind,vec![])));
                children.extend(display.map(|kind| Node::new(kind,vec![])));
                if let Some(into)=into { children.push(Node::new(Kind::ExplainIntoTable,vec![into])); }
                if let Some(statement_id)=statement_id { children.push(Node::new(Kind::ExplainStatementId,vec![statement_id])); }
                children.push(value);
                Node::new(Kind::Explain,children)
        }
        rule load_string() -> Node = start:position!() text:(string_segment(<"'">)
                / enabled(!mode.ansi_quotes) value:string_segment(<"\"">) { value }) end:position!() _ {
            Node::new(Kind::String,vec![]).bytes(decode(text,mode.no_backslash_escapes)).at(start,end)
        }
        rule load_text() -> Node = load_string() / binary_literal()
        rule load_integer() -> Node = type_size()
        rule hint_integer() -> Node = type_size()
        rule load_field_option() -> Node
            = kw("terminated") kw("by") value:load_text() { Node::new(Kind::LoadFieldTerminated,vec![value]) }
            / optional:kw("optionally")? kw("enclosed") kw("by") value:load_text() {
                Node::new(if optional.is_some() { Kind::LoadOptionallyEnclosed } else { Kind::LoadEnclosed },vec![value])
            }
            / kw("escaped") kw("by") value:load_text() { Node::new(Kind::LoadEscaped,vec![value]) }
        rule load_line_option() -> Node
            = kw("starting") kw("by") value:load_text() { Node::new(Kind::LoadLineStarting,vec![value]) }
            / kw("terminated") kw("by") value:load_text() { Node::new(Kind::LoadLineTerminated,vec![value]) }
        rule load_prefix() -> Option<Vec<Node>>
            = keyword("load") space()+ keyword("data") hints:hint_slot() { hints }
            / kw("load") kw("data") { None }
        rule load_extended_option()
            = (kw("logfile") / kw("badfile")) ("=" _)? load_string()
            / kw("reject") kw("limit") ("=" _)? load_integer()
        rule load_data() -> Node = hints:load_prefix() local:kw("local")? kw("infile") filename:load_string()
            duplicate:(kw("ignore") { crate::LoadDuplicate::Ignore } / kw("replace") { crate::LoadDuplicate::Replace })?
            kw("into") kw("table") table:relation_name()
            partitions:(kw("partition") "(" _ names:(ident() ++ ("," _)) ")" _ { names.into_iter().map(Node::name).collect() })?
            compression:(kw("compression") ("=" _)? value:(load_string() / name:ident() { Node::name(name) }) { value })?
            charset:((kw("character") / kw("char")) kw("set") value:charset_name() { value })?
            fields:((kw("fields") / kw("columns")) values:load_field_option()+ { values })?
            lines:(kw("lines") values:load_line_option()+ { values })?
            ignore_rows:((kw("ignore") / kw("generated")) value:load_integer() (kw("lines") / kw("rows")) { value })?
            input_fields:("(" _ values:((column() / user_variable()) ** ("," _)) ")" _ { values })?
            assignments:(kw("set") values:assignments() { values })? load_extended_option()* {?
                let mut formats=fields.unwrap_or_default();formats.extend(lines.unwrap_or_default());
                Ok(load_data::LoadData {
                    table:Node::relation(table)?,filename,local:local.is_some(),
                    duplicate:duplicate.unwrap_or(crate::LoadDuplicate::Stop),compression,charset,ignore_rows,
                    formats,fields:input_fields.unwrap_or_default(),assignments:assignments.unwrap_or_default(),
                    partitions:partitions.unwrap_or_default(),hints:hints.unwrap_or_default(),
                }.into_node())
            }
        rule sql_statement() -> Node = select(0) / insert() / update() / delete() / transaction() / ddl() / account_statement() / role_statement() / setting() / explain() / show() / flush_privileges() / alter_system() / prepared_statement() / diagnostics() / analyze() / lock_tables() / load_data() / kill() / purge() / restore_table() / restore_database()
        rule statement() -> Node = _ value:(package_statement() / routine_statement(0) / pl_call() / anonymous_statement() / sql_statement()) { value }
        #[no_eof]
        pub rule statement_end() -> usize = (statement() / _) (";" / ![_]) end:position!() { end }
        pub rule query() -> Node = value:(
                value:statement() (";" _)* { value }
                / text:$(_ (";" _)*) { Node::new(Kind::EmptyQuery,vec![]).value(u64::from(text.iter().any(|byte| !byte.is_ascii_whitespace() && *byte!=b';'))) }
            ) { value }
    }
}
