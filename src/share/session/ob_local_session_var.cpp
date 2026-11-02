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

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{

int ObLocalSessionVar::add_local_var(const ObSessionSysVar *var)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(var)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(var));
  } else if (OB_FAIL(add_local_var(var->type_, var->val_))) {
    LOG_WARN("fail to add local session var", K(ret));
  }
  return ret;
}

int ObLocalSessionVar::add_local_var(ObSysVarClassType var_type, const ObObj &value)
{
  int ret = OB_SUCCESS;
  ObSessionSysVar *cur_var = NULL;
  if (OB_ISNULL(alloc_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(alloc_));
  } else if (OB_FAIL(get_local_var(var_type, cur_var))) {
    LOG_WARN("get local var failed", K(ret));
  } else if (NULL == cur_var) {
    ObSessionSysVar *new_var = OB_NEWx(ObSessionSysVar, alloc_);
    if (OB_ISNULL(new_var)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc new var failed.", K(ret));
    } else if (OB_FAIL(local_session_vars_.push_back(new_var))) {
      LOG_WARN("push back new var failed", K(ret));
    } else if (OB_FAIL(deep_copy_obj(*alloc_, value, new_var->val_))) {
      LOG_WARN("fail to deep copy obj", K(ret));
    } else {
      new_var->type_ = var_type;
    }
  } else if (!cur_var->is_equal(value)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local session var added before is not equal to the new var", K(ret), KPC(cur_var), K(value));
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
      LOG_WARN("unexpected null", K(ret), K(local_session_vars_));
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
      LOG_WARN("push back local session vars failed", K(ret));
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
    LOG_WARN("allocate memory for local_session_var failed", K(ret), KP(binary_str), KP(hex_str));
  } else if (OB_FAIL(serialize_(binary_str, buf_len, pos))) {
    LOG_WARN("fail to serialize local_session_var", K(ret));
  } else if (OB_FAIL(common::hex_print(binary_str, pos, hex_str, buf_len * 2, hex_pos))) {
    LOG_WARN("print hex string failed", K(ret));
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
    LOG_WARN("unexpected empty str", K(ret), K(local_session_var_str));
  } else if (OB_ISNULL(value_buf = static_cast<char*>(tmp_allocator.alloc(local_session_var_str.length())))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FALSE_IT(len = common::str_to_hex(local_session_var_str.ptr(), local_session_var_str.length(),
                                                  value_buf, local_session_var_str.length()))) {
  } else if (OB_FAIL(deserialize_(value_buf, static_cast<int64_t>(len), pos))) {
    LOG_WARN("fail to deserialize local_session_var", K(ret));
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
    LOG_WARN("fail to reserve local session vars", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < other.local_session_vars_.count(); ++i) {
    if (OB_FAIL(add_local_var(other.local_session_vars_.at(i)))) {
      LOG_WARN("fail to add session var", K(ret));
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
      LOG_WARN("reserve failed", K(ret));
    } else if (OB_FAIL(local_session_vars_.assign(other.local_session_vars_))) {
      LOG_WARN("fail to push back local var", K(ret));
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
    LOG_WARN("reserve failed", K(ret), K(sz));
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
      sz += local_session_vars_.at(i)->get_deep_copy_size();
    }
  }
  return sz;
}

bool ObSessionSysVar::is_equal(const ObObj &other) const
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
  } else if (val_.is_equal(other, CS_TYPE_BINARY)) {
    bool_ret = true;
  }
  return bool_ret;
}

OB_DEF_SERIALIZE(ObSessionSysVar)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, type_, val_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSessionSysVar)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, type_, val_);
  return len;
}

OB_DEF_DESERIALIZE(ObSessionSysVar)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, type_, val_);
  return ret;
}

int64_t ObSessionSysVar::get_deep_copy_size() const
{
  int64_t sz = sizeof(*this) + val_.get_deep_copy_size();
  return sz;
}

OB_DEF_SERIALIZE(ObLocalSessionVar)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, local_session_vars_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < local_session_vars_.count(); ++i) {
    if (OB_ISNULL(local_session_vars_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
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
  OB_UNIS_DECODE(cnt);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(local_session_vars_.reserve(cnt))) {
      LOG_WARN("reserve failed", K(ret));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
    ObSessionSysVar var;
    LST_DO_CODE(OB_UNIS_DECODE, var);
    if (OB_SUCC(ret)) {
      if (OB_FAIL(add_local_var(&var))) {
        LOG_WARN("fail to add local session var", K(ret));
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
