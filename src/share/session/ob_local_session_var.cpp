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
  // ObDatum's arena clone reserves one byte even for a non-NULL empty string.
  int64_t bytes = value.is_null() ? 0 : std::max<int64_t>(1, value.datum_.get_deep_copy_size());
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
  } else if (OB_FAIL(add_local_var(var->type_, var->val_))) {
  }
  return ret;
}

int ObLocalSessionVar::add_local_var(ObSysVarClassType var_type, const DatumValue &value)
{
  int ret = OB_SUCCESS;
  ObSessionSysVar *cur_var = NULL;
  if (OB_ISNULL(alloc_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_local_var(var_type, cur_var))) {
  } else if (NULL == cur_var) {
    ObSessionSysVar *new_var = OB_NEWx(ObSessionSysVar, alloc_);
    if (OB_ISNULL(new_var)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new_var->type_ = var_type;
      if (OB_FAIL(clone_typed_value(value, new_var->val_, *alloc_))) {
      } else if (OB_FAIL(local_session_vars_.push_back(new_var))) {
      }
      if (OB_FAIL(ret)) { new_var->~ObSessionSysVar(); alloc_->free(new_var); }
    }
  } else if (!cur_var->is_equal(value)) {
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
        is_equal = var->is_equal(other_val->val_);
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

bool ObSessionSysVar::is_equal(const DatumValue &other) const
{
  bool bool_ret = false;
  if (val_.get_meta() != other.get_meta()) {
    bool_ret = false;
    if (ob_is_string_type(val_.get_type())
        && val_.get_type() == other.get_type()
        && val_.get_collation_type() != other.get_collation_type()) {
      // The collation type of string system variables is rewritten by session collation on update.
      bool_ret = common::ObCharset::case_sensitive_equal(val_.get_string(), other.get_string());
    }
  } else if (binary_equal_datum_values(val_, other)) {
    bool_ret = true;
  }
  return bool_ret;
}

OB_DEF_SERIALIZE(ObSessionSysVar)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(type_);
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_value(val_.datum_, val_.meta_, buf, buf_len, pos); }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSessionSysVar)
{
  int64_t len = 0, value_size = 0;
  if (DatumParameterCodec::value_size(val_.datum_, val_.meta_, value_size) != OB_SUCCESS) { return 0; }
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
    DatumValue candidate;
    if (body_pos < len) { ret = serialization::decode(buf + body_start, len, body_pos, type); }
    if (OB_SUCC(ret) && body_pos < len) {
      ret = DatumParameterCodec::decode_unis_value(buf + body_start, len, body_pos,
                                                  owner, candidate.datum_, candidate.meta_);
    } else if (OB_SUCC(ret)) {
      // A legal omitted final value retains the record's previous value, and
      // the result still has a caller-owned lifetime after this decode.
      ret = clone_typed_value(val_, candidate, owner);
    }
    if (OB_SUCC(ret)) { type_ = type; val_ = candidate; }
    pos = body_start + len;
  }
  return ret;
}

int64_t ObSessionSysVar::get_deep_copy_size() const
{
  const int64_t payload = owned_native_session_value_size(val_);
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
            "val", local_session_vars_.at(i)->val_);
    }
  }
  J_OBJ_END();
  return pos;
}

} // namespace share
} // namespace oceanbase
