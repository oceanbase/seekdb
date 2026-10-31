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

#include "common/datum/datum_core.h"

namespace oceanbase
{
namespace common
{

// A caller prepares this once from its existing column/parameter metadata and
// can reuse it for values with that metadata. Both endpoints currently use the
// legacy pointer Datum representation; this is not an inline-value codec.
// Existing producers remain responsible for type/length validation. The byte
// copier retains that context without introducing a second semantic validator.
//
// The plan owns no payload. Copying an extended address or a locator copies
// its bytes only, not the object/resource behind it. Recursive SQL-array and
// PL/resource ownership remains the responsibility of their dedicated cloners.
class DatumCopyPlan
{
public:
  DatumCopyPlan(const SqlTypeMeta &meta, const ObAccuracy &accuracy)
    : meta_(meta), accuracy_(accuracy) {}

  const SqlTypeMeta &get_type_meta() const { return meta_; }
  const ObAccuracy &get_accuracy() const { return accuracy_; }

  // This reports external payload bytes, excluding the Datum descriptor and
  // any allocator padding. Unknown Decimal precision stays unknown: its
  // existing coefficient width is preserved instead of inferred or expanded.
  int payload_copy_size(const ObDatum &source, int64_t &size) const
  {
    int ret = OB_SUCCESS;
    if (source.is_null()) {
      size = 0;
    } else if (source.len_ > 0 && source.ptr_ == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      size = source.len_;
    }
    return ret;
  }

  // Failure leaves destination unchanged, including when it aliases source.
  int deep_copy(const ObDatum &source, ObDatum &destination, ObIAllocator &allocator) const
  {
    int ret = OB_SUCCESS;
    int64_t size = 0;
    ObDatum copy = source;
    if (OB_FAIL(payload_copy_size(source, size))) {
    } else if (!source.is_null()) {
      char *bytes = static_cast<char *>(allocator.alloc(size > 0 ? size : 1));
      if (bytes == nullptr) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (size > 0) { MEMCPY(bytes, source.ptr_, size); }
        copy.ptr_ = bytes;
      }
    }
    if (OB_SUCC(ret)) { destination = copy; }
    return ret;
  }

  // The buffer is owned by the caller. Capacity is checked before bytes or
  // descriptors are published; only a successful copy advances position.
  int deep_copy(const ObDatum &source, ObDatum &destination,
                char *buffer, int64_t capacity, int64_t &position) const
  {
    int ret = OB_SUCCESS;
    int64_t size = 0;
    ObDatum copy = source;
    if (position < 0 || capacity < position) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(payload_copy_size(source, size))) {
    } else if (size > capacity - position) {
      ret = OB_BUF_NOT_ENOUGH;
    } else if (!source.is_null()) {
      if (buffer == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        if (size > 0) { MEMMOVE(buffer + position, source.ptr_, size); }
        copy.ptr_ = buffer + position;
      }
    }
    if (OB_SUCC(ret)) {
      destination = copy;
      position += size;
    }
    return ret;
  }

private:
  SqlTypeMeta meta_;
  ObAccuracy accuracy_;
};

} // namespace common
} // namespace oceanbase
