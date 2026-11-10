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

#define USING_LOG_PREFIX SHARE

#include "share/session/ob_local_session_var.h"
#include "share/datum/typed_datum_compare.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include <algorithm>

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{

namespace {
int64_t owned_native_session_value_size(const DatumValue &value, int depth = 0)
{
  alignas(8) char scratch[64];
  ObDatum physical;
  if (value.export_legacy(physical, scratch, sizeof(scratch)) != OB_SUCCESS) { return INT64_MAX; }
  // Inline scalar/state values own no separate bytes. A Reference still owns
  // at least one allocated byte when its actual physical span is empty.
  int64_t bytes = value.is_null() || physical.ptr_ == scratch
      ? 0 : std::max<int64_t>(1, physical.get_deep_copy_size());
  if (!value.is_null() && value.is_ext_sql_array()) {
    if (depth > 64) { return INT64_MAX; }
    const auto *array = reinterpret_cast<const DatumParameterArray *>(value.get_ext());
    if (array == nullptr || array->count_ < 0 || (array->count_ > 0 && array->data_ == nullptr)
        || array->count_ > (INT64_MAX - sizeof(DatumParameterArray)) / sizeof(DatumParameter)) {
      return INT64_MAX;
    }
    const int64_t descriptors = sizeof(DatumParameterArray) + sizeof(DatumParameter) * array->count_;
    if (bytes > INT64_MAX - descriptors) { return INT64_MAX; }
    bytes += descriptors;
    for (int64_t i = 0; i < array->count_; ++i) {
      const int64_t element = owned_native_session_value_size(array->data_[i].value_view(), depth + 1);
      if (bytes > INT64_MAX - element) { return INT64_MAX; }
      bytes += element;
    }
  }
  return bytes;
}
} // namespace

int ObLocalSessionVar::add_local_var(const ObSessionSysVar *var)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(var)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(add_local_var_native(var->type_, var->get_value_datum(), var->get_value_meta()))) {
  }
  return ret;
}

int ObLocalSessionVar::add_local_var(ObSysVarClassType var_type, const DatumValue &value)
{
  Datum native;
  alignas(8) char scratch[64];
  ObDatum physical;
  int ret = value.export_legacy(physical, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::borrow_schema_native_value(physical, value.meta_, native); }
  if (OB_SUCC(ret)) { ret = add_local_var_native(var_type, native, value.meta_); }
  return ret;
}

int ObLocalSessionVar::add_local_var_native(ObSysVarClassType var_type, const Datum &value,
                                           const SqlTypeMeta &meta)
{
  int ret = OB_SUCCESS;
  ObSessionSysVar *cur_var = NULL;
  ObDatum checked;
  alignas(8) char scratch[64];
  if (OB_ISNULL(alloc_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(DatumParameterCodec::export_schema_legacy_value(
                 value, meta, checked, scratch, sizeof(scratch)))) {
  } else if (OB_FAIL(get_local_var(var_type, cur_var))) {
  } else if (NULL == cur_var) {
    ObSessionSysVar *new_var = OB_NEWx(ObSessionSysVar, alloc_);
    if (OB_ISNULL(new_var)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new_var->type_ = var_type;
      if (OB_FAIL(new_var->set_native_value(value, meta, *alloc_))) {
      } else if (OB_FAIL(local_session_vars_.push_back(new_var))) {
      }
      if (OB_FAIL(ret)) { new_var->~ObSessionSysVar(); alloc_->free(new_var); }
    }
  } else if (!cur_var->is_equal(value, meta)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObLocalSessionVar::get_local_var(ObSysVarClassType var_type, ObSessionSysVar *&sys_var) const
{
  int ret = OB_SUCCESS;
  sys_var = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && NULL == sys_var && i < local_session_vars_.count(); ++i) {
    if (OB_ISNULL(local_session_vars_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (local_session_vars_.at(i)->type_ == var_type) {
      sys_var = local_session_vars_.at(i);
    }
  }
  return ret;
}

int ObLocalSessionVar::get_local_vars(ObIArray<const ObSessionSysVar *> &var_array) const
{
  int ret = OB_SUCCESS;
  var_array.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < local_session_vars_.count(); ++i){
    if (OB_FAIL(var_array.push_back(local_session_vars_.at(i)))) {
    }
  }
  return ret;
}

int ObLocalSessionVar::gen_local_session_var_str(ObIAllocator &allocator,
                                                 ObString &local_session_var_str) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t buf_len = get_serialize_size();
  char *binary_str = NULL;
  char *hex_str = NULL;
  int64_t hex_pos = 0;
  ObArenaAllocator tmp_allocator(ObModIds::OB_TEMP_VARIABLES);
  if (OB_ISNULL(binary_str = static_cast<char *>(tmp_allocator.alloc(buf_len)))
      || OB_ISNULL(hex_str = static_cast<char *>(allocator.alloc(buf_len * 2)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(serialize_(binary_str, buf_len, pos))) {
  } else if (OB_FAIL(common::hex_print(binary_str, pos, hex_str, buf_len * 2, hex_pos))) {
  } else {
    local_session_var_str.assign(hex_str, hex_pos);
  }
  return ret;
}

int ObLocalSessionVar::fill_local_session_var_from_str(const ObString &local_session_var_str)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator(ObModIds::OB_TEMP_VARIABLES);
  char *value_buf = NULL;
  ObLength len = 0;
  int64_t pos = 0;
  if (OB_UNLIKELY(local_session_var_str.empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(value_buf = static_cast<char*>(tmp_allocator.alloc(local_session_var_str.length())))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FALSE_IT(len = common::str_to_hex(local_session_var_str.ptr(), local_session_var_str.length(),
                                                  value_buf, local_session_var_str.length()))) {
  } else if (OB_FAIL(deserialize_(value_buf, static_cast<int64_t>(len), pos))) {
  }
  return ret;
}

int ObLocalSessionVar::deep_copy(const ObLocalSessionVar &other)
{
  int ret = OB_SUCCESS;
  local_session_vars_.reset();
  if (this == &other) {
    // do nothing
  } else if (NULL != other.alloc_) {
    if (NULL == alloc_) {
      alloc_ = other.alloc_;
      local_session_vars_.set_allocator(other.alloc_);
    }
  }
  if (OB_FAIL(local_session_vars_.reserve(other.local_session_vars_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < other.local_session_vars_.count(); ++i) {
    if (OB_FAIL(add_local_var(other.local_session_vars_.at(i)))) {
    }
  }
  return ret;
}

int ObLocalSessionVar::assign(const ObLocalSessionVar &other)
{
  int ret = OB_SUCCESS;
  local_session_vars_.reset();
  if (NULL != other.alloc_) {
    if (NULL == alloc_) {
      alloc_ = other.alloc_;
      local_session_vars_.set_allocator(other.alloc_);
    }
    if (OB_FAIL(local_session_vars_.reserve(other.local_session_vars_.count()))) {
    } else if (OB_FAIL(local_session_vars_.assign(other.local_session_vars_))) {
    }
  } else {
    // do nothing, other is not inited
  }
  return ret;
}

void ObLocalSessionVar::reset()
{
  local_session_vars_.reset();
}

int ObLocalSessionVar::set_local_var_capacity(int64_t sz)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(local_session_vars_.reserve(sz))) {
  }
  return ret;
}

bool ObLocalSessionVar::operator == (const ObLocalSessionVar& other) const
{
  bool is_equal = local_session_vars_.count() == other.local_session_vars_.count();
  if (is_equal) {
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; is_equal && i < local_session_vars_.count(); ++i) {
      ObSessionSysVar *var = local_session_vars_.at(i);
      ObSessionSysVar *other_val = NULL;
      if (OB_ISNULL(var)) {
       is_equal = false;
      } else if ((tmp_ret = other.get_local_var(var->type_, other_val)) != OB_SUCCESS) {
        is_equal = false;
      } else if (other_val == NULL) {
        is_equal = false;
      } else {
        is_equal = var->is_equal(other_val->get_value_datum(), other_val->get_value_meta());
      }
    }
  }
  return is_equal;
}

int64_t ObLocalSessionVar::get_deep_copy_size() const
{
  int64_t sz = sizeof(*this) + local_session_vars_.count() * sizeof(ObSessionSysVar *);
  for (int64_t i = 0; i < local_session_vars_.count(); ++i) {
    if (OB_NOT_NULL(local_session_vars_.at(i))) {
      const int64_t child = local_session_vars_.at(i)->get_deep_copy_size();
      if (child > INT64_MAX - sz) { return INT64_MAX; }
      sz += child;
    }
  }
  return sz;
}

int ObSessionSysVar::set_value(const DatumValue &value, ObIAllocator &owner)
{
  Datum candidate;
  const int ret = DatumParameterCodec::clone_schema_native_value(
      value.datum_, value.meta_, owner, candidate);
  if (OB_SUCCESS == ret) { value_ = candidate; value_meta_ = value.meta_; }
  return ret;
}

int ObSessionSysVar::set_native_value(const Datum &value, const SqlTypeMeta &meta,
                                      ObIAllocator &owner)
{
  Datum candidate;
  const int ret = DatumParameterCodec::clone_schema_native_value(value, meta, owner, candidate);
  if (OB_SUCCESS == ret) { value_ = candidate; value_meta_ = meta; }
  return ret;
}

int ObSessionSysVar::export_value(DatumValue &output, void *scratch, int64_t capacity) const
{
  DatumValue candidate;
  ObDatum physical;
  int ret = DatumParameterCodec::export_schema_legacy_value(
      value_, value_meta_, physical, scratch, capacity);
  if (OB_SUCCESS == ret) { ret = candidate.assign_legacy_borrowed(physical, value_meta_); }
  if (OB_SUCCESS == ret) { output = candidate; }
  return ret;
}

int ObSessionSysVar::copy_value(DatumValue &output, ObIAllocator &owner) const
{
  DatumValue view;
  alignas(8) char scratch[64];
  int ret = export_value(view, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = clone_typed_value(view, output, owner); }
  return ret;
}

bool ObSessionSysVar::is_equal(const DatumValue &other) const
{
  DatumValue value;
  alignas(8) char scratch[64];
  const int ret = export_value(value, scratch, sizeof(scratch));
  OB_ASSERT(OB_SUCCESS == ret);
  bool bool_ret = false;
  if (value.get_meta() != other.get_meta()) {
    if (ob_is_string_type(value.get_type())
        && value.get_type() == other.get_type()
        && value.get_collation_type() != other.get_collation_type()) {
      // Session updates rewrite the collation of string system variables.
      bool_ret = ObCharset::case_sensitive_equal(value.get_string(), other.get_string());
    }
  } else if (binary_equal_datum_values(value, other)) {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObSessionSysVar::is_equal(const Datum &other, const SqlTypeMeta &meta) const
{
  DatumValue view;
  alignas(8) char scratch[64];
  ObDatum physical;
  int ret = DatumParameterCodec::export_schema_legacy_value(
      other, meta, physical, scratch, sizeof(scratch));
  if (OB_SUCCESS == ret) { ret = view.assign_legacy_borrowed(physical, meta); }
  OB_ASSERT(OB_SUCCESS == ret);
  return is_equal(view);
}

OB_DEF_SERIALIZE(ObSessionSysVar)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(type_);
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_schema_native_value(value_, value_meta_, buf, buf_len, pos); }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSessionSysVar)
{
  int64_t len = 0, value_size = 0;
  if (DatumParameterCodec::schema_native_value_size(value_, value_meta_, value_size) != OB_SUCCESS) { return 0; }
  OB_UNIS_ADD_LEN(type_);
  len += value_size;
  return len;
}

int ObSessionSysVar::deserialize(const char *buf, int64_t data_len, int64_t &pos,
                                 ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  int64_t version = 0, len = 0;
  OB_UNIS_DECODE(version);
  OB_UNIS_DECODE(len);
  CHECK_VERSION_LENGTH(ObSessionSysVar, version, len);
  if (OB_SUCC(ret)) {
    const int64_t body_start = pos;
    int64_t body_pos = 0;
    auto type = type_;
    Datum candidate;
    SqlTypeMeta candidate_meta;
    if (body_pos < len) { ret = serialization::decode(buf + body_start, len, body_pos, type); }
    if (OB_SUCC(ret) && body_pos < len) {
      ret = DatumParameterCodec::decode_unis_native_value(buf + body_start, len, body_pos,
                                                  owner, candidate, candidate_meta);
    } else if (OB_SUCC(ret)) {
      // A legal omitted final value retains the record's previous value, and
      // the result still has a caller-owned lifetime after this decode.
      ret = DatumParameterCodec::clone_schema_native_value(value_, value_meta_, owner, candidate);
      candidate_meta = value_meta_;
    }
    if (OB_SUCC(ret)) { type_ = type; value_ = candidate; value_meta_ = candidate_meta; }
    pos = body_start + len;
  }
  return ret;
}

int64_t ObSessionSysVar::get_deep_copy_size() const
{
  DatumValue view;
  alignas(8) char scratch[64];
  const int ret = export_value(view, scratch, sizeof(scratch));
  OB_ASSERT(OB_SUCCESS == ret);
  // Only inline scalar/state exports use this local scratch. Reference spans,
  // including empty spans and array wrappers, retain the actual stored owner.
  const int64_t payload = owned_native_session_value_size(view);
  int64_t sz = payload > INT64_MAX - sizeof(*this) ? INT64_MAX : sizeof(*this) + payload;
  return sz;
}

OB_DEF_SERIALIZE(ObLocalSessionVar)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, local_session_vars_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < local_session_vars_.count(); ++i) {
    if (OB_ISNULL(local_session_vars_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      LST_DO_CODE(OB_UNIS_ENCODE, *local_session_vars_.at(i));
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLocalSessionVar)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, local_session_vars_.count());
  for (int64_t i = 0; i < local_session_vars_.count(); ++i) {
    if (OB_NOT_NULL(local_session_vars_.at(i))) {
      LST_DO_CODE(OB_UNIS_ADD_LEN, *local_session_vars_.at(i));
    }
  }
  return len;
}

OB_DEF_DESERIALIZE(ObLocalSessionVar)
{
  int ret = OB_SUCCESS;
  int64_t cnt = 0;
  ObArenaAllocator decode_owner(ObModIds::OB_TEMP_VARIABLES);
  OB_UNIS_DECODE(cnt);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(local_session_vars_.reserve(cnt))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
    ObSessionSysVar var;
    if (pos < data_len) {
      ret = var.deserialize(buf, data_len, pos, alloc_ == nullptr
          ? static_cast<ObIAllocator &>(decode_owner) : *alloc_);
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(add_local_var(&var))) {
      }
    }
  }
  return ret;
}

DEF_TO_STRING(ObLocalSessionVar)
{
  int64_t pos = 0;
  J_OBJ_START();
  for (int64_t i = 0; i < local_session_vars_.count(); ++i) {
    if (i > 0) {
      J_COMMA();
    }
    if (OB_NOT_NULL(local_session_vars_.at(i))) {
      J_KV("type", local_session_vars_.at(i)->type_,
            "val", local_session_vars_.at(i)->get_value_datum(),
            "meta", local_session_vars_.at(i)->get_value_meta());
    }
  }
  J_OBJ_END();
  return pos;
}

} // namespace share
} // namespace oceanbase
