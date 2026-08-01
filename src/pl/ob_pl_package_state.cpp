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

#define USING_LOG_PREFIX PL

#include "ob_pl_package_state.h"
#include "pl/ob_pl_package.h"
#include "pl/ob_pl_dependency_util.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
namespace pl
{

ObPackageStateVersion::ObPackageStateVersion(const ObPackageStateVersion &other)
{
  *this = other;
}

ObPackageStateVersion &ObPackageStateVersion::operator =(const ObPackageStateVersion &other)
{
  if (this != &other) {
    package_version_ = other.package_version_;
    package_body_version_ = other.package_body_version_;
    header_merge_version_ = other.header_merge_version_;
    body_merge_version_ = other.body_merge_version_;
    reserved_header_count_ = other.reserved_header_count_;
    reserved_body_count_ = other.reserved_body_count_;
  }
  return *this;
}

bool ObPackageStateVersion::equal(const ObPackageStateVersion &other)
{
  return package_version_ == other.package_version_
      && package_body_version_ == other.package_body_version_;
}

bool ObPackageStateVersion::operator ==(const ObPackageStateVersion &other) const
{
  return package_version_ == other.package_version_
      && package_body_version_ == other.package_body_version_
      && header_merge_version_ == other.header_merge_version_
      && body_merge_version_ == other.body_merge_version_;
}

void ObPackageStateVersion::set_merge_versions(const ObPLPackage &head, const ObPLPackage *body)
{
  header_merge_version_ = head.get_sys_schema_version();
  reserved_header_count_ = 0;
  if (OB_NOT_NULL(body)) {
    body_merge_version_ = body->get_sys_schema_version();
    reserved_body_count_ = 0;
  }
}

int ObPLPackageState::init()
{
  return inner_allocator_.init(nullptr);
}

int ObPLPackageState::add_package_var_val(const common::ObObj &value, ObPLType type)
{
  int ret = OB_SUCCESS;
  OZ (types_.push_back(type));
  if (OB_SUCC(ret) && OB_FAIL(vars_.push_back(value))) {
    types_.pop_back();
    LOG_WARN("failed to push back", K(ret), K(value), K(type));
  }
  return ret;
}

void ObPLPackageState::reset(ObSQLSessionInfo *session_info)
{
  for (int64_t i = 0; i < types_.count(); ++i) {
    if (!vars_.at(i).is_ext()) {
      void *ptr = vars_.at(i).get_deep_copy_obj_ptr();
      if (nullptr != ptr) {
        inner_allocator_.free(ptr);
      }
    } else if (PL_RECORD_TYPE == types_.at(i)
               || PL_NESTED_TABLE_TYPE == types_.at(i)
               || PL_ASSOCIATIVE_ARRAY_TYPE == types_.at(i)
               || PL_VARRAY_TYPE == types_.at(i)
               || PL_OPAQUE_TYPE == types_.at(i)) {
      int ret = OB_SUCCESS;
      if (OB_FAIL(ObUserDefinedType::destruct_objparam(inner_allocator_, vars_.at(i), session_info))) {
        LOG_WARN("failed to destruct composte obj", K(ret));
      }
    } else if (PL_CURSOR_TYPE == types_.at(i)) {
      ObPLCursorInfo *cursor = reinterpret_cast<ObPLCursorInfo *>(vars_.at(i).get_ext());
      if (OB_NOT_NULL(cursor)) {
        cursor->close(*session_info);
        cursor->~ObPLCursorInfo();
      }
    }
  }
  types_.reset();
  vars_.reset();
  inner_allocator_.reset();
  cursor_allocator_.reset();
  package_id_ = common::OB_INVALID_ID;
}

int ObPLPackageState::set_package_var_val(const int64_t var_idx,
                                          const ObObj &value,
                                          bool deep_copy_complex)
{
  int ret = OB_SUCCESS;
  if (var_idx < 0 || var_idx >= vars_.count()) {
    ret = OB_ARRAY_OUT_OF_RANGE;
    LOG_WARN("invalid var index", K(var_idx), K(vars_.count()), K(ret));
  } else if (value.need_deep_copy() && deep_copy_complex) {
    int64_t pos = 0;
    char *buf = static_cast<char *>(inner_allocator_.alloc(value.get_deep_copy_size()));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for pacakge var", K(ret), K(buf));
    }
    OZ (vars_.at(var_idx).deep_copy(value, buf, value.get_deep_copy_size(), pos));
  } else if (value.is_pl_extend()
             && value.get_meta().get_extend_type() != PL_CURSOR_TYPE
             && deep_copy_complex) {
    ObObj copy;
    OZ (ObUserDefinedType::deep_copy_obj(inner_allocator_, value, copy));
    OX (vars_.at(var_idx) = copy);
  } else if (value.is_null()
             && vars_.at(var_idx).is_pl_extend()
             && types_.at(var_idx) != PL_CURSOR_TYPE) {
    CK (vars_.at(var_idx).get_ext() != 0);
    OZ (ObUserDefinedType::reset_composite(vars_.at(var_idx), NULL));
  } else {
    vars_.at(var_idx) = value;
  }
  return ret;
}

int ObPLPackageState::get_package_var_val(const int64_t var_idx, ObObj &value)
{
  int ret = OB_SUCCESS;
  if (var_idx < 0 || var_idx >= vars_.count()) {
    ret = OB_ARRAY_OUT_OF_RANGE;
    LOG_WARN("invalid var index", K(var_idx), K(vars_.count()), K(ret));
  } else {
    value = vars_.at(var_idx);
  }
  return ret;
}

int ObPLPackageState::check_version(const ObPackageStateVersion &state_version,
                                    const ObPackageStateVersion &cur_state_version,
                                    ObSchemaGetterGuard &schema_guard,
                                    const ObPLPackage &spec,
                                    const ObPLPackage *body,
                                    bool &match)
{
  int ret = OB_SUCCESS;
  match = true;

  if (cur_state_version == state_version) {
    match = true;
  } else {
    if (cur_state_version.header_merge_version_ != state_version.header_merge_version_) {
      if (OB_FAIL(ObPLDependencyUtil::check_dep_schema(schema_guard,
                                                       spec.get_dependency_table(),
                                                       cur_state_version.header_merge_version_,
                                                       match))) {
        LOG_WARN("fail to check dep schema", K(ret), K(cur_state_version), K(state_version));
      }
    }
    if (OB_SUCC(ret) && match
        && cur_state_version.body_merge_version_ != state_version.body_merge_version_
        && OB_NOT_NULL(body)) {
      if (OB_FAIL(ObPLDependencyUtil::check_dep_schema(schema_guard,
                                                       body->get_dependency_table(),
                                                       cur_state_version.body_merge_version_,
                                                       match))) {
        LOG_WARN("fail to check dep schema", K(ret), K(cur_state_version), K(state_version));
      }
    }
  }

  return ret;
}

} // namespace pl
} // namespace oceanbase
