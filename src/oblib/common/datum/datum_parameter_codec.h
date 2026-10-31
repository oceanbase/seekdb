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
  static int import_schema_native_value(const ObDatum &value, const SqlTypeMeta &meta,
                                       ObIAllocator &allocator, Datum &output);
  static int clone_schema_native_value(const Datum &value, const SqlTypeMeta &meta,
                                      ObIAllocator &allocator, Datum &output);
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
  static int parameter_size(const DatumParameter &value, int64_t &size);
  static int encode_parameter(const DatumParameter &value, char *buf,
                              int64_t length, int64_t &pos);
  static int decode_parameter(const char *buf, int64_t length, int64_t &pos,
                              ObIAllocator &allocator, DatumParameter &value);
};

} // namespace common
} // namespace oceanbase
