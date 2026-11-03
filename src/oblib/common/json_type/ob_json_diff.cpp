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
#define USING_LOG_PREFIX LIB
#include "ob_json_diff.h"
#include "ob_json_bin.h"
#include "rust/json.h"

namespace oceanbase {
namespace common {

class DeltaState
{
public:
  DeltaState() : value_(json::new_delta()) {}
  rust::Box<json::Delta> value_;
};

namespace json {
uint64_t BinarySource::binary_length() const noexcept
{
  return cursor_.get_length();
}

rust::Slice<const uint8_t> BinarySource::binary_view() const noexcept
{
  ObString data;
  if (cursor_.is_full_mode() && cursor_.get_data(data) == OB_SUCCESS
      && data.length() == cursor_.get_length() && data.length() > 0) {
    return {reinterpret_cast<const uint8_t *>(data.ptr()), static_cast<size_t>(data.length())};
  }
  return {};
}

int32_t BinarySource::binary_read(uint64_t offset, rust::Slice<uint8_t> output) const noexcept
{
  int ret = OB_SUCCESS;
  ObString data;
  if (offset > static_cast<uint64_t>(INT64_MAX)
      || output.size() > static_cast<uint64_t>(INT64_MAX) - offset
      || offset + output.size() > static_cast<uint64_t>(cursor_.get_length())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(cursor_.get(offset, output.size(), data))) {
  } else if (data.length() != output.size()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!output.empty()) {
    MEMCPY(output.data(), data.ptr(), output.size());
  }
  return ret;
}
} // namespace json


namespace {
int delta_status(int32_t status)
{
  switch (status) {
    case 0: return OB_SUCCESS;
    case 1: return OB_ALLOCATE_MEMORY_FAILED;
    case 2: return OB_NOT_SUPPORTED;
    case 3: return OB_DESERIALIZE_ERROR;
    default: return OB_INVALID_ARGUMENT;
  }
}

rust::Slice<const uint8_t> delta_bytes(const ObString &value)
{
  static const uint8_t empty = 0;
  return rust::Slice<const uint8_t>(
      value.empty() ? &empty : reinterpret_cast<const uint8_t *>(value.ptr()),
      value.length());
}
} // namespace

ObJsonBinUpdateCtx::ObJsonBinUpdateCtx(ObIAllocator *allocator)
  : allocator_(allocator), cursor_(nullptr), tmp_buffer_(allocator),
    delta_(OB_NEWx(DeltaState, allocator_))
{}

ObJsonBinUpdateCtx::~ObJsonBinUpdateCtx()
{
  if (delta_ != nullptr) {
    delta_->~DeltaState();
    allocator_->free(delta_);
    delta_ = nullptr;
  }
  if (nullptr != cursor_) {
    cursor_->~ObILobCursor();
    cursor_ = nullptr;
  }
}

bool ObJsonBinUpdateCtx::is_no_update() const
{
  return binary_diff_count() == 0;
}

bool ObJsonBinUpdateCtx::is_rebuild_all() const
{
  return delta_ != nullptr && delta_->value_->delta_is_rebuilt();
}

void ObJsonBinUpdateCtx::mark_rebuild_all()
{
  if (delta_ != nullptr) {
    delta_->value_->delta_mark_rebuilt();
  }
}

int64_t ObJsonBinUpdateCtx::logical_diff_count() const
{
  return delta_ == nullptr ? 0 : delta_->value_->delta_logical_count();
}

int64_t ObJsonBinUpdateCtx::binary_diff_count() const
{
  return delta_ == nullptr ? 0 : delta_->value_->delta_physical_count();
}

BinaryDiff ObJsonBinUpdateCtx::binary_diff(int64_t index) const
{
  const json::DeltaRange range = delta_->value_->delta_physical(index);
  return BinaryDiff{range.offset, range.length};
}

bool ObJsonBinUpdateCtx::intersects(uint64_t offset, uint64_t length) const
{
  return delta_ != nullptr && delta_->value_->delta_intersects(offset, length);
}

int64_t ObJsonBinUpdateCtx::logical_serialize_size() const
{
  return delta_ == nullptr ? 2 : delta_->value_->delta_encoded_size();
}

int ObJsonBinUpdateCtx::record_inline_diff(ObJsonDiffOp op, uint8_t value_type,
    const ObString &path, uint8_t entry_var_type, const ObString &value)
{
  return delta_ == nullptr ? OB_ALLOCATE_MEMORY_FAILED
      : delta_status(delta_->value_->delta_record(static_cast<uint8_t>(op), value_type,
          entry_var_type & 3, delta_bytes(path), delta_bytes(value)));
}

int ObJsonBinUpdateCtx::record_diff(ObJsonDiffOp op, uint8_t value_type,
    const ObString &path, const ObString &value)
{
  return record_inline_diff(op, value_type, path, 0, value);
}

int ObJsonBinUpdateCtx::record_remove_diff(const ObString &path)
{
  return record_diff(ObJsonDiffOp::REMOVE, 0, path, ObString());
}

int ObJsonBinUpdateCtx::record_binary_diff(int64_t offset, int64_t len)
{
  return delta_ == nullptr ? OB_ALLOCATE_MEMORY_FAILED
      : delta_status(delta_->value_->delta_record_physical(offset, len));
}

int ObJsonBinUpdateCtx::merge_edit(BinaryEdit &edit)
{
  return delta_ == nullptr ? OB_ALLOCATE_MEMORY_FAILED
      : delta_status(delta_->value_->merge_edit(edit));
}

int ObJsonBinUpdateCtx::should_pack_diff(uint64_t total, uint64_t extension,
    uint64_t root_offset, bool &pack) const
{
  int ret = init_status();
  pack = false;
  if (OB_SUCC(ret)) {
    const int32_t decision = delta_->value_->delta_should_pack(total, extension, root_offset);
    if (decision < 0) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      pack = decision == 1;
    }
  }
  return ret;
}

int ObJsonBinUpdateCtx::serialize_logical(char *buf, int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  const int64_t length = logical_serialize_size();
  if (delta_ == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(buf) || pos < 0 || pos > buf_len || length > buf_len - pos) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(delta_status(delta_->value_->delta_encode(rust::Slice<uint8_t>(
                 reinterpret_cast<uint8_t *>(buf + pos), length))))) {
  } else {
    pos += length;
  }
  return ret;
}

int ObJsonBinUpdateCtx::restore_delta(const char *buf, int64_t buf_len,
    const ObIArray<BinaryDiff> &physical)
{
  int ret = OB_SUCCESS;
  ObSEArray<json::DeltaRange, 10> ranges;
  if (delta_ == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(buf) || buf_len < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < physical.count(); ++i) {
      ret = ranges.push_back(json::DeltaRange{physical.at(i).offset, physical.at(i).length});
    }
    if (OB_SUCC(ret)) {
      ret = delta_status(delta_->value_->delta_restore(rust::Slice<const uint8_t>(
          reinterpret_cast<const uint8_t *>(buf), buf_len),
          rust::Slice<const json::DeltaRange>(ranges.get_data(), ranges.count())));
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
