# JSON text handling

seekdb uses the Rust `json` crate and `serde_json` to parse and validate JSON text on every supported platform. The C++ `ObJsonParser` interface remains the adapter for the existing binary JSON tree, SQL JSON functions, and JSON Schema implementation.

## Standard syntax

All platforms accept standard RFC 8259 JSON syntax. The former relaxed syntax extensions are intentionally not supported. In particular, input is rejected when it contains:

- Unquoted or single-quoted object keys.
- Comments or trailing commas.
- Uppercase or mixed-case `true`, `false`, or `null` literals.
- A leading plus sign, leading zeros, hexadecimal numbers, a missing integer part such as `.5`, or a missing fraction part such as `1.`.
- `NaN`, `Infinity`, or other nonstandard numeric values.

The parser consumes one complete JSON document. Strings must be valid UTF-8, escape sequences must be valid JSON escapes, and UTF-16 surrogate pairs must be well formed. Reported error offsets are zero-based byte offsets into the original input, not character, line, or column numbers.

## Independent policies

Duplicate object keys and JSON Schema behavior are separate from text syntax. Callers can request last-value overwrite, duplicate preservation, duplicate rejection, or schema-compatible first-value retention. Signed integers, unsigned integers, and floating-point values retain their binary JSON classifications, including negative zero. Nesting limits and JSON Schema reference errors retain their existing database error mappings.

Integer `-0` remains integer zero. Floating-point forms such as `-0.0` and `-0e0` preserve the negative floating-point sign.

Native Rust callers use `json::parse`, `json::parse_binary`, or `json::validate` with byte slices and `ParseOptions`. Parsing returns an owned `Document`; binary parsing returns an owned seekdb binary JSON buffer; validation avoids constructing a document when no duplicate or schema policy requires one.

CXX is used only by C++ adapters and transfers each complete document in one call. Callers that need binary JSON use the Rust binary representation directly, without constructing `ObJsonNode` or running a second C++ serializer. Callers that explicitly require a mutable JSON tree still receive a complete event stream and construct `ObJsonNode` in the adapter.
