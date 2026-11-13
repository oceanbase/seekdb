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

#include "json_builder.h"
#include "ob_json_base.h"
#include "common/timezone/ob_time_convert.h"
#include "rust/json.h"
#include "lib/ob_errno.h"
#include <optional>

namespace oceanbase {
namespace common {

static int build_status_error(int status)
{
  switch (status) {
    case 0: return OB_SUCCESS;
    case 1: return OB_ALLOCATE_MEMORY_FAILED;
    case 3: return OB_ERR_DUPLICATE_KEY;
    case 4: return OB_ERR_JSON_OUT_OF_DEPTH;
    case 5: return OB_ERR_JSON_KEY_NOT_FOUND;
    case 6: return OB_NOT_SUPPORTED;
    default: return OB_ERR_UNEXPECTED;
  }
}

int copy_json_binary(ObIAllocator &allocator, const ObString &binary, ObIJsonBase *&value)
{
  int ret = OB_SUCCESS;
  char *bytes = static_cast<char *>(allocator.alloc(binary.length()));
  if (bytes == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(bytes, binary.ptr(), binary.length());
    ret = ObJsonBaseFactory::get_json_base(&allocator, bytes, binary.length(),
        ObJsonInType::JSON_BIN, value);
  }
  return ret;
}

int make_json_integer(ObIAllocator &allocator, int64_t integer, ObIJsonBase *&value)
{
  auto encoded = json::encode_scalar(2, integer, 0, 0, rust::Slice<const uint8_t>(), 0);
  const int ret = build_status_error(encoded.status);
  return ret != OB_SUCCESS ? ret : copy_json_binary(allocator,
      ObString(encoded.binary.size(), reinterpret_cast<const char *>(encoded.binary.data())), value);
}

int make_json_string(ObIAllocator &allocator, const ObString &text, ObIJsonBase *&value)
{
  auto encoded = json::encode_scalar(5, 0, 0, 0,
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(text.ptr()), text.length()), 0);
  const int ret = build_status_error(encoded.status);
  return ret != OB_SUCCESS ? ret : copy_json_binary(allocator,
      ObString(encoded.binary.size(), reinterpret_cast<const char *>(encoded.binary.data())), value);
}

static int encode_database_scalar(ObIAllocator &allocator, uint8_t kind, int64_t integer,
    uint64_t unsigned_integer, double real, const ObString &payload, uint16_t field_type,
    ObString &binary)
{
  auto encoded = json::encode_scalar(kind, integer, unsigned_integer, real,
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(payload.ptr()), payload.length()), field_type);
  int ret = encoded.status == 1 ? OB_ALLOCATE_MEMORY_FAILED : encoded.status == 0
      ? OB_SUCCESS : kind == 4 ? OB_INVALID_NUMERIC : OB_ERR_UNEXPECTED;
  if (ret == OB_SUCCESS) {
    ret = deep_copy_ob_string(allocator, ObString(encoded.binary.size(),
        reinterpret_cast<const char *>(encoded.binary.data())), binary);
  }
  return ret;
}

int encode_json_null(ObIAllocator &a, ObString &out)
{ return encode_database_scalar(a, 0, 0, 0, 0, ObString(), 0, out); }
int encode_json_integer(ObIAllocator &a, int64_t v, ObString &out)
{ return encode_database_scalar(a, 2, v, 0, 0, ObString(), 0, out); }
int encode_json_unsigned(ObIAllocator &a, uint64_t v, ObString &out)
{ return encode_database_scalar(a, 3, 0, v, 0, ObString(), 0, out); }
int encode_json_real(ObIAllocator &a, double v, ObString &out)
{ return encode_database_scalar(a, 4, 0, 0, v, ObString(), 0, out); }
int encode_json_boolean(ObIAllocator &a, bool v, ObString &out)
{ return encode_database_scalar(a, 8, v, 0, 0, ObString(), 0, out); }
int encode_json_text(ObIAllocator &a, const ObString &v, ObString &out)
{ return encode_database_scalar(a, 5, 0, 0, 0, v, 0, out); }
int encode_json_opaque(ObIAllocator &a, const ObString &v, uint16_t type, ObString &out)
{ return encode_database_scalar(a, 13, 0, 0, 0, v, type, out); }

int encode_json_decimal(ObIAllocator &allocator, const number::ObNumber &value,
                        int16_t precision, int16_t scale, ObString &out)
{
  int ret = OB_SUCCESS;
  ObJsonBuffer payload(&allocator);
  int64_t pos = 0;
  if (OB_FAIL(payload.reserve(value.get_serialize_size() + 4))) {
  } else if (OB_FAIL(serialization::encode_i16(payload.ptr(), payload.capacity(), pos, precision))) {
  } else if (OB_FAIL(serialization::encode_i16(payload.ptr(), payload.capacity(), pos, scale))) {
  } else if (OB_FAIL(value.serialize(payload.ptr(), payload.capacity(), pos))) {
  } else if (OB_FAIL(payload.set_length(pos))) {
  } else {
    ret = encode_database_scalar(allocator, 1, 0, 0, 0, payload.string(), 0, out);
  }
  return ret;
}

int encode_json_time(ObIAllocator &allocator, ObJsonNodeType type, const ObTime &input, ObString &out)
{
  ObTime time = input;
  int ret = OB_SUCCESS;
  uint8_t kind = static_cast<uint8_t>(type);
  int64_t value = 0;
  int32_t length = 8;
  switch (type) {
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_JSON_DATE_EXT:
      value = ObTimeConverter::ob_time_to_date(time); length = 4; break;
    case ObJsonNodeType::J_MYSQL_DATE: {
      kind = 32;
      ObMySQLDate date = ObTimeConverter::ob_time_to_mdate(time);
      MEMCPY(&value, &date, sizeof(date)); length = 4; break;
    }
    case ObJsonNodeType::J_TIME:
      value = ObTimeConverter::ob_time_to_time(time); break;
    case ObJsonNodeType::J_MYSQL_DATETIME: {
      kind = 33;
      ObMySQLDateTime datetime;
      ret = ObTimeConverter::ob_time_to_mdatetime(time, datetime);
      if (ret == OB_SUCCESS) { MEMCPY(&value, &datetime, sizeof(datetime)); }
      break;
    }
    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_TIMESTAMP:
    case ObJsonNodeType::J_ODATE:
    case ObJsonNodeType::J_OTIMESTAMP:
    case ObJsonNodeType::J_OTIMESTAMPTZ: {
      ObTimeConvertCtx context(nullptr, false);
      ret = ObTimeConverter::ob_time_to_datetime(time, context, value); break;
    }
    default: ret = OB_INVALID_ARGUMENT; break;
  }
  return ret != OB_SUCCESS ? ret : encode_database_scalar(allocator, kind, 0, 0, 0,
      ObString(length, reinterpret_cast<const char *>(&value)), 0, out);
}

class JsonContainerBuilder::State {
public:
  explicit State(bool object) : builder_(json::new_container_builder(object)) {}
  rust::Box<json::ContainerBuilder> builder_;
  std::optional<json::BuildResult> output_;
  std::optional<json::FormatResult> text_;
};

JsonContainerBuilder::JsonContainerBuilder(bool object) : state_(new (std::nothrow) State(object)) {}
JsonContainerBuilder::~JsonContainerBuilder() = default;

int JsonContainerBuilder::append(const ObString &key, const ObString &binary)
{
  return !state_ ? OB_ALLOCATE_MEMORY_FAILED : build_status_error(state_->builder_->append_binary(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(key.ptr()), key.length()),
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(binary.ptr()), binary.length())));
}

int JsonContainerBuilder::append_encoded(const ObString &key, json::BuildResult encoded)
{
  const int ret = build_status_error(encoded.status);
  if (ret != OB_SUCCESS) { return ret; }
  return !state_ ? OB_ALLOCATE_MEMORY_FAILED : build_status_error(state_->builder_->append_owned_binary(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(key.ptr()), key.length()), std::move(encoded.binary)));
}

int JsonContainerBuilder::append_aggregate(const ObString &key, const ObString &binary)
{
  return !state_ ? OB_ALLOCATE_MEMORY_FAILED : build_status_error(state_->builder_->append_aggregate_binary(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(key.ptr()), key.length()),
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(binary.ptr()), binary.length())));
}

int JsonContainerBuilder::append_integer(const ObString &key, int64_t integer)
{
  return append_encoded(key, json::encode_scalar(2, integer, 0, 0, rust::Slice<const uint8_t>(), 0));
}

int JsonContainerBuilder::append_string(const ObString &key, const ObString &text)
{
  return append_encoded(key, json::encode_scalar(5, 0, 0, 0,
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(text.ptr()), text.length()), 0));
}

int JsonContainerBuilder::append_container(const ObString &key, JsonContainerBuilder &value)
{
  ObString binary;
  const int ret = value.finish(binary);
  return ret != OB_SUCCESS ? ret : append(key, binary);
}

int JsonContainerBuilder::extend(const ObString &binary)
{
  return !state_ ? OB_ALLOCATE_MEMORY_FAILED : build_status_error(state_->builder_->extend_binary(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(binary.ptr()), binary.length())));
}

int JsonContainerBuilder::rename_key(const ObString &old_key, const ObString &new_key)
{
  return !state_ ? OB_ALLOCATE_MEMORY_FAILED : build_status_error(state_->builder_->rename_key(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(old_key.ptr()), old_key.length()),
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(new_key.ptr()), new_key.length())));
}

int JsonContainerBuilder::finish(ObString &binary, bool aggregate, bool sort_keys,
                                 bool unique_keys, uint32_t max_depth)
{
  if (!state_) { return OB_ALLOCATE_MEMORY_FAILED; }
  state_->output_.emplace(state_->builder_->build_container(
      unique_keys ? json::Duplicate::Overwrite : json::Duplicate::Preserve,
      sort_keys, aggregate, max_depth));
  const int ret = build_status_error(state_->output_->status);
  if (ret == OB_SUCCESS) {
    binary.assign_ptr(reinterpret_cast<char *>(state_->output_->binary.data()),
                      static_cast<ObString::obstr_size_t>(state_->output_->binary.size()));
  }
  return ret;
}

int JsonContainerBuilder::format(ObString &text)
{
  if (!state_) { return OB_ALLOCATE_MEMORY_FAILED; }
  if (!state_->output_) { return OB_ERR_UNEXPECTED; }
  state_->text_.emplace(json::format_binary(
      rust::Slice<const uint8_t>(state_->output_->binary.data(), state_->output_->binary.size()), true, false, 0));
  int ret = state_->text_->platform_error;
  if (ret == OB_SUCCESS && state_->text_->status != json::Status::Ok) { ret = OB_ERR_UNEXPECTED; }
  if (ret == OB_SUCCESS) {
    text.assign_ptr(reinterpret_cast<char *>(state_->text_->text.data()),
                    static_cast<ObString::obstr_size_t>(state_->text_->text.size()));
  }
  return ret;
}

uint64_t JsonContainerBuilder::approximate_length() const { return state_ ? state_->builder_->approximate_length() : 0; }
uint64_t JsonContainerBuilder::input_count() const { return state_ && state_->output_ ? state_->output_->input_count : 0; }
uint64_t JsonContainerBuilder::output_count() const { return state_ && state_->output_ ? state_->output_->output_count : 0; }

} // namespace common
} // namespace oceanbase
