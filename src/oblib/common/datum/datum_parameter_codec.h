/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "common/datum/datum.h"
#include "common/datum/datum_parameter_array.h"
#include "common/datum/datum_param_store.h"

namespace oceanbase
{
namespace common
{

// Legacy value/parameter wire format implemented directly over native Datum.
// Parameter wire contains value + accuracy + result flags only. Runtime flags,
// source text positions and param_meta are intentionally absent from that format.
// SQL-array nesting is bounded at 64; PL pointer values remain unsupported by
// the legacy format. Extend transient size is not present in that wire format.
// Decoding owns every payload (including nested array elements) in allocator.
// Single-value decode failures preserve output and caller position; arena bytes
// allocated before an error remain owned by the caller until arena reset.
class DatumParameterCodec
{
public:
  // Store wire is the existing count + parameter sequence. A failed store
  // decode leaves the store empty and the caller position unchanged; payload
  // allocations remain in allocator, as for the single-value decoder.
  static int array_size(const DatumParameterArray &array, int64_t &size);
  static int encode_array(const DatumParameterArray &array, char *buf, int64_t length, int64_t &pos);
  static int decode_array(const char *buf, int64_t length, int64_t &pos,
                         ObIAllocator &allocator, DatumParameterArray *&array);
  // Forward boundary for the old value decoder's deferred SQL-array body.
  // This entry consumes the entire declared body, owns the array recursively
  // in allocator, and preserves its original value length. Failure frees every
  // allocation made here and leaves value unchanged.
  static int decode_deferred_array(const char *buf, int64_t length,
                         ObIAllocator &allocator, ObDatum &value);

  static int store_size(const DatumParamStore &store, int64_t &size);
  static int encode_store(const DatumParamStore &store, char *buf, int64_t length, int64_t &pos);
  static int decode_store(const char *buf, int64_t length, int64_t &pos,
                          ObIAllocator &allocator, DatumParamStore &store);

  // Generic wrapper physical domain. Ordinary scalars are inline; Unknown
  // remains Reference8 and Decimal preserves the actual coefficient span.
  // Span copy/import owns only bytes, not an array/PL/LOB pointee. Schema graph
  // ownership stays explicit in clone_schema_native_value below.
  static int borrow_native_value(const ObDatum &, const SqlTypeMeta &, Datum &);
  static int import_native_value(const ObDatum &, const SqlTypeMeta &, ObIAllocator &, Datum &);
  static int copy_native_value_span(const Datum &, const SqlTypeMeta &, ObIAllocator &, Datum &);
  // Bounded contiguous-buffer copy of the generic physical span. Inline and
  // special states need zero bytes; references need their actual length (also
  // zero for an empty reference). No pointee/array graph is retained. No output
  // or cursor is published on failure; buffer bytes may have been overwritten.
  static int native_span_size(const Datum &, const SqlTypeMeta &, int64_t &);
  static int copy_native_span(const Datum &, const SqlTypeMeta &, char *buffer,
                             int64_t length, int64_t &pos, Datum &output);
  static int export_native_value(const Datum &, const SqlTypeMeta &, ObDatum &, void *, int64_t);
  static int copy_legacy_value(const Datum &, const SqlTypeMeta &, ObIAllocator &, ObDatum &);
  static int native_value_size(const Datum &, const SqlTypeMeta &, int64_t &);
  static int encode_native_value(const Datum &, const SqlTypeMeta &, char *, int64_t, int64_t &);
  static int decode_native_value(const char *, int64_t, int64_t &, ObIAllocator &, Datum &, SqlTypeMeta &);

  static int value_size(const ObDatum &value, const SqlTypeMeta &meta, int64_t &size);
  static int encode_value(const ObDatum &value, const SqlTypeMeta &meta,
                          char *buf, int64_t length, int64_t &pos);
  static int decode_value(const char *buf, int64_t length, int64_t &pos,
                          ObIAllocator &allocator, ObDatum &value, SqlTypeMeta &meta);
  // Persisted version-1 schema fields historically permit omitted trailing
  // metadata/scalar fields at the end of a body. Protocol/parameter decoding
  // continues to require complete fields through decode_value().
  static int decode_schema_value(const char *buf, int64_t length, int64_t &pos,
                          ObIAllocator &allocator, ObDatum &value, SqlTypeMeta &meta);
  // Column defaults have a historical value domain independent of the column
  // declaration (literal/SQL text/NOW/unknown-precision Decimal). These ports
  // move that physical domain without inventing a complete SqlType. Ordinary
  // scalars are inline; Decimal keeps its actual reference width. References
  // are owned on import/clone/decode, including the existing SQL-array graph.
  // PL/LOB wrapper copying retains its old domain meaning, not external-resource
  // ownership. Every output, and decode's position, is published only on success.
  // Checked physical borrow: scalars become inline, references keep the source
  // owner. Generic Unknown remains an eight-byte reference, unlike Raw indices.
  static int borrow_schema_native_value(const ObDatum &value, const SqlTypeMeta &meta,
                                       Datum &output);
  static int import_schema_native_value(const ObDatum &value, const SqlTypeMeta &meta,
                                       ObIAllocator &allocator, Datum &output);
  static int clone_schema_native_value(const Datum &value, const SqlTypeMeta &meta,
                                      ObIAllocator &allocator, Datum &output);
  // Only for values owned by successful schema native import/clone/decode in
  // this allocator (or an explicit ownership transfer). Never release a borrowed
  // reference/export view. Inline values and bare states allocate nothing;
  // references include zero-length allocations and recursive SQL-array graphs.
  // The value is reset; metadata is left to the owning record.
  static void release_owned_schema_native_value(Datum &value, const SqlTypeMeta &meta,
                                                ObIAllocator &allocator);
  // Scalar/special-state views borrow caller scratch; reference views borrow
  // value's owner. Neither may outlive that storage. A 64-byte aligned scratch
  // covers every scalar view. No column precision/label normalization occurs.
  static int export_schema_legacy_value(const Datum &value, const SqlTypeMeta &meta,
                                       ObDatum &output, void *scratch, int64_t capacity);
  static int schema_native_value_size(const Datum &value, const SqlTypeMeta &meta, int64_t &size);
  static int encode_schema_native_value(const Datum &value, const SqlTypeMeta &meta,
                                       char *buf, int64_t length, int64_t &pos);
  static int decode_schema_native_value(const char *buf, int64_t length, int64_t &pos,
                                       ObIAllocator &allocator, Datum &output, SqlTypeMeta &meta);
  // Old UNIS member records also accepted a final string without its unused
  // terminator. Payload bytes remain bounded; only the record cursor may end
  // one byte past the body, as the enclosing UNIS reader historically allowed.
  static int decode_unis_value(const char *buf, int64_t length, int64_t &pos,
                          ObIAllocator &allocator, ObDatum &value, SqlTypeMeta &meta);
  // The same UNIS omission and metadata-reuse contract with native inline
  // scalars. Explicit-free owners must retain their decode allocation transaction
  // so failures also reclaim unpublished nested-array allocations.
  static int decode_unis_native_value(const char *buf, int64_t length, int64_t &pos,
                          ObIAllocator &allocator, Datum &output, SqlTypeMeta &meta);
  // AST constants retain their historical value metadata independently of the
  // inferred expression type. This physical domain shares the schema rules,
  // except that Unknown is an inline structural question-mark index. These
  // ports never prepare a semantic SqlType or normalize Decimal precision.
  // Borrow never allocates; clone owns reference bytes/the old SQL-array graph,
  // while PL/LOB pointees keep their existing borrowed-resource contract.
  // Scalar/state exports borrow caller scratch; reference exports borrow the
  // input owner. All outputs are published only on success.
  static int borrow_raw_constant_value(const ObDatum &value, const SqlTypeMeta &meta,
                                       Datum &output);
  static int export_raw_constant_value(const Datum &value, const SqlTypeMeta &meta,
                                       ObDatum &output, void *scratch, int64_t capacity);
  static int clone_raw_constant_value(const Datum &value, const SqlTypeMeta &meta,
                                      ObIAllocator &allocator, Datum &output);
  static int parameter_size(const DatumParameter &value, int64_t &size);
  static int encode_parameter(const DatumParameter &value, char *buf,
                              int64_t length, int64_t &pos);
  static int decode_parameter(const char *buf, int64_t length, int64_t &pos,
                              ObIAllocator &allocator, DatumParameter &value);
};

} // namespace common
} // namespace oceanbase
