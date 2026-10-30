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

#include "common/datum/datum_parameter_codec.h"

namespace oceanbase
{
namespace sql
{

// The statement clock is fixed-schema execution state, not a dynamically typed
// SQL value. Keep its timestamp inline and retain only the metadata bytes needed
// for the existing execution-context wire (including the initial NULL state).
class StatementTime
{
public:
  StatementTime() : microseconds_(0), meta_() {}
  void set_timestamp(int64_t timestamp)
  {
    microseconds_ = timestamp;
    meta_.set_timestamp();
  }
  int64_t get_timestamp() const { return microseconds_; }
  int64_t get_datetime() const { return microseconds_; }
  bool has_value() const { return meta_.is_timestamp(); }

  int serialize(char *buf, int64_t length, int64_t &pos) const
  {
    common::ObDatum view;
    view.set_string(reinterpret_cast<const char *>(&microseconds_), sizeof(microseconds_));
    if (meta_.is_null()) { view.set_null(); }
    return common::DatumParameterCodec::encode_value(view, meta_, buf, length, pos);
  }
  int64_t get_serialize_size() const
  {
    return meta_.get_serialize_size() + (has_value() ? common::serialization::encoded_length(microseconds_) : 0);
  }
  int deserialize(const char *buf, int64_t length, int64_t &pos)
  {
    using namespace common;
    if (buf == nullptr || pos < 0 || length < pos) { return OB_INVALID_ARGUMENT; }
    if (length - pos < sizeof(SqlTypeMeta)) { return OB_DESERIALIZE_ERROR; }
    int64_t cursor = pos;
    SqlTypeMeta meta;
    int64_t timestamp = microseconds_;
    int ret = meta.deserialize(buf, length, cursor);
    if (ret == OB_SUCCESS) {
      if (meta.is_timestamp()) {
        ret = serialization::decode(buf, length, cursor, timestamp);
      } else if (!meta.is_null()) {
        ret = OB_OBJ_TYPE_ERROR;
      }
    }
    if (ret == OB_SUCCESS) {
      // The old NULL decoder changed metadata without overwriting the inline
      // payload. Retain that behavior when an execution context is reused.
      microseconds_ = timestamp;
      meta_ = meta;
      pos = cursor;
    }
    return ret;
  }
  TO_STRING_KV("timestamp", microseconds_, "present", has_value());
private:
  int64_t microseconds_;
  common::SqlTypeMeta meta_;
};

} // namespace sql
} // namespace oceanbase
