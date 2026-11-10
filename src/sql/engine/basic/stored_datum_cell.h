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

#include "common/datum/datum_copy_plan.h"

namespace oceanbase
{
namespace sql
{

// Version-one row-store cell. This format is independent of execution Datum:
// the reference is either a payload pointer or a row-relative spill offset.
// The following word preserves the existing 29-bit length, 2-bit flags and
// NULL bit. Neither types nor execution-only ownership objects belong here.
struct StoredDatumCell
{
  StoredDatumCell() : ptr_(nullptr), pack_(0) {}

  const char *ptr_;
#if defined(__EMSCRIPTEN__) && __SIZEOF_POINTER__ == 4
  uint32_t reserved_ptr_ = 0;
#endif
  union {
    struct {
      uint32_t len_ : 29;
      uint32_t flag_ : 2;
      uint32_t null_ : 1;
    };
    uint32_t pack_;
  };

  bool is_null() const { return null_ != 0; }
  void set_null() { len_ = 0; flag_ = 0; null_ = 1; }
  TO_STRING_KV(KP_(ptr), K_(len), K_(flag), K_(null));
} __attribute__((packed));

static_assert(sizeof(StoredDatumCell) == 12, "Version-one stored cells are exactly 12 bytes");
static_assert(alignof(StoredDatumCell) == 1, "Stored cells may start at any byte offset");

class StoredDatumCodec
{
public:
  // Explicit legacy adapters accept only the old pointer Datum. The native
  // overloads use a typed codec; these legacy input/output boundaries still
  // require their carrier contract until execution callers migrate.
  static_assert(sizeof(common::ObDatum) == 12,
      "Legacy pointer adapters require the version-one ObDatum layout");
  static_assert(sizeof(common::ObDatumPtr) == 8 && sizeof(common::ObDatumDesc) == 4,
      "Legacy pointer Datum fields must match the version-one storage boundary");

  static StoredDatumCell encode_legacy_view(const common::ObDatum &source)
  {
    StoredDatumCell destination;
    destination.ptr_ = source.ptr_;
    destination.pack_ = source.pack_;
    return destination;
  }

  static common::ObDatum decode_legacy_view(const StoredDatumCell &source)
  {
    common::ObDatum destination;
    destination.ptr_ = source.ptr_;
    destination.pack_ = source.pack_;
    return destination;
  }

  // The execution value is materialized according to its prepared type. An
  // integer is emitted as legacy payload bytes only at this storage boundary;
  // no pointer to the native Datum's inline member escapes into the row.
  static int encode_native_copy(const common::Datum &source, StoredDatumCell &destination,
                                const common::DatumCopyPlan &plan,
                                char *buffer, int64_t capacity, int64_t &position,
                                bool offsets = false)
  {
    int64_t size = 0;
    int ret = plan.legacy_payload_size(source, size);
    if (ret == common::OB_SUCCESS && size > ((uint64_t(1) << 29) - 1)) {
      ret = common::OB_SIZE_OVERFLOW;
    }
    if (ret == common::OB_SUCCESS) {
      common::ObDatum encoded;
      const int64_t start = position;
      ret = plan.export_legacy(source, encoded, buffer, capacity, position);
      if (ret == common::OB_SUCCESS) {
        destination = encode_legacy_view(encoded);
        if (offsets && !encoded.is_null()) {
          destination.ptr_ = reinterpret_cast<const char *>(start);
        }
      }
    }
    return ret;
  }

  // References borrow the already swizzled row's payload. Keeping this Datum
  // object does not extend the lifetime of the row or prevent its reuse.
  static int decode_native(const StoredDatumCell &source, const common::DatumCopyPlan &plan,
                           common::Datum &destination)
  {
    return plan.import_legacy(decode_legacy_view(source), destination);
  }

  static int copy_stored_payload(const StoredDatumCell &source, StoredDatumCell &destination,
                                  char *buffer, int64_t capacity, int64_t &position)
  {
    int ret = common::OB_SUCCESS;
    StoredDatumCell copy = source;
    const int64_t size = source.is_null() ? 0 : source.len_;
    if (position < 0 || position > capacity || (size > 0 && source.ptr_ == nullptr)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if (size > capacity - position) {
      ret = common::OB_BUF_NOT_ENOUGH;
    } else if (!source.is_null()) {
      if (buffer == nullptr) {
        ret = common::OB_INVALID_ARGUMENT;
      } else {
        if (size > 0) { MEMMOVE(buffer + position, source.ptr_, size); }
        copy.ptr_ = buffer + position;
      }
    }
    if (common::OB_SUCCESS == ret) {
      destination = copy;
      position += size;
    }
    return ret;
  }

  static int encode_legacy_copy(const common::ObDatum &source, StoredDatumCell &destination,
                                const common::DatumCopyPlan &plan,
                                char *buffer, int64_t capacity, int64_t &position,
                                bool offsets = false)
  {
    common::ObDatum owned;
    const int64_t payload_position = position;
    const int ret = plan.deep_copy(source, owned, buffer, capacity, position);
    if (common::OB_SUCCESS == ret) {
      destination = encode_legacy_view(owned);
      if (offsets && !owned.is_null()) {
        destination.ptr_ = reinterpret_cast<const char *>(payload_position);
      }
    }
    return ret;
  }

  // An explicitly legacy-only adapter for callers that still pass raw Datum
  // arrays. It must not become an untyped entry for a future inline Datum.
  static int encode_legacy_copy(const common::ObDatum &source, StoredDatumCell &destination,
                                char *buffer, int64_t capacity, int64_t &position,
                                bool offsets = false)
  {
    const common::DatumCopyPlan plan(common::SqlTypeMeta{}, common::ObAccuracy{});
    return encode_legacy_copy(source, destination, plan, buffer, capacity, position, offsets);
  }
};

} // namespace sql
} // namespace oceanbase
