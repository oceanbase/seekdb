# JSON domain

The `rust/json` crate owns JSON text parsing, binary representation, paths, queries, comparison, formatting, schema validation, construction, merging and mutation. C++ retains SQL evaluation, database scalar conversion, LOB access and database error mapping. The existing stored binary representation and mutation delta encoding are preserved.

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

C++ callers use CXX adapters over the native domain interfaces. The C++ mutable tree, parser event stream and aggregate serializer have been removed. `ObJsonBin` is a read adapter over a Rust-owned stored-value handle; it no longer owns JSON metadata or mutation algorithms. `ObJsonBase`, path and schema classes retain the interfaces used by SQL and database callers.

The ARRAY/MAP SQL conversion boundary retains its existing case-insensitive unquoted `NULL` collection literal. A quote-aware normalization there does not change the JSON parser's strict syntax.

## Native interfaces and ownership

| Interface | Responsibility and ownership |
| --- | --- |
| `BinaryDocument<B>`, `BinaryValue<'_>` | Stored bytes and borrowed scalar/container views; reads check offsets and lengths. |
| `Query`, `Path`, `Value`, `Selection` | Query execution and output policy. Selections share immutable document ownership and carry stable occurrence identities for JSON_TABLE scans. |
| `compare`, `format`, `hash` | Complete domain traversal; database number/time formatting, ordering and hashing use narrow synchronous capabilities. |
| `schema::Schema`, `schema::Cache` | Compiled validation rules, normalized encoding, reports and per-expression cache entries. Pattern compilation is injected separately. |
| `construction::Scalar`, `ContainerBuilder` | Scalar encoding and owned array/object construction, with explicit duplicate and aggregate policies. |
| `writer::BinaryWriter` | Container framing and widths for streaming producers such as GeoJSON. Open-container handles belong to one writer. |
| `mutation` | Sparse binary editing, prepared SQL mutations, merge policies and the authoritative logical/physical delta state. |

Native Rust callers use these interfaces directly without constructing CXX objects. CXX translates representations and database errors. Database-specific number/time payloads remain opaque to domain algorithms when their semantics require database conversion.

The LOB source capability and number, time, hash and pattern capabilities execute synchronously on the caller's thread. They do not spawn work, retain borrowed inputs or outlive their SQL expression. C++ owns the supplied allocator, LOB reader and database scalar objects. Rust owns returned binary buffers, compiled schemas, selections, editors and deltas. SQL contexts destroy handles before their backing allocator and capability objects.

Prepared mutations record the editor generation and reject stale application. Values are evaluated only when the SQL operation requires them; a failed later argument is propagated before publishing the final SQL result. Delta restore validates into temporary state before replacing the current delta. Immutable construction and merge results become visible only after successful completion. Storage writes and transaction rollback remain the database caller's responsibility.

## Production consumers

JSON SQL query, read, schema, update, merge, constructor and aggregate functions use the native core. JSON_TABLE retains SQL column conversion and scan orchestration while native selections own matches and their identities. Full and partial LOB updates use the same editor and delta contracts.

GeoJSON uses the binary writer with the existing coordinate conversion and geometry policies in C++. AI request/response assembly, full-text parser properties and token output, telemetry and vector metadata use immutable binary values and container builders. Bulk array producers keep one builder until completion to avoid copying every previously appended element.

## Validation

The established `expr.json_standard_syntax` mysqltest suite covers strict syntax and SQL regressions. Native tests in `rust/json/src/tests.rs` cover the domain interfaces, malformed binary inputs, deep binary traversal, scalar width boundaries, mutation failure state and ownership. `test_json_parser` exercises CXX integration and frozen C++ mutation/hash results; `test_json_direct_benchmark differential` checks parser binary output against frozen legacy results.

The differential drivers in `unittest/oblib/json_*_differential.py` compare exact results, storage sizes and SQL errors against a separate baseline instance. `json_ai_local.py` validates request and response behavior with a local HTTP fixture. `json_domain_benchmark.py` copies a small-document fixture into identical task-owned tables on both instances, checks exact results and alternates warm SQL measurements. Optional profiling runs after timed trials; process RSS is reported separately from latency and is not a measure of JSON allocation alone.
