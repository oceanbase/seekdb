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

namespace {
DatumParameter package_value_view(const DatumValue &value)
{
  DatumParameter parameter;
  parameter.datum_ = value.datum_;
  parameter.set_type_meta(value.meta_);
  return parameter;
}
int package_reusable_bytes(const DatumParameter &value, int64_t &bytes)
{
  int ret = OB_SUCCESS;
  bytes = 0;
  if (!value.is_null() && value.get_meta().is_valid()) {
    switch (ObDatum::get_sql_value_datum_map_type(value.get_type())) {
      case OBJ_DATUM_8BYTE_DATA: bytes = 8; break;
      case OBJ_DATUM_4BYTE_DATA: bytes = 4; break;
      case OBJ_DATUM_1BYTE_DATA: bytes = 1; break;
      case OBJ_DATUM_NUMBER:
        if (value.datum_.ptr_ == nullptr || value.datum_.len_ < sizeof(number::ObNumber::Desc)
            || value.datum_.len_ != sizeof(number::ObNumber::Desc)
                + sizeof(uint32_t) * value.datum_.get_number_desc().len_) {
          ret = OB_INVALID_ARGUMENT;
        } else { bytes = value.datum_.len_; }
        break;
      case OBJ_DATUM_DECIMALINT:
        if (value.datum_.len_ != 0 && value.datum_.len_ != 4 && value.datum_.len_ != 8
            && value.datum_.len_ != 16 && value.datum_.len_ != 32 && value.datum_.len_ != 64) {
          ret = OB_INVALID_ARGUMENT;
        } else { bytes = value.datum_.len_; }
        break;
      default: break;
    }
    if (bytes > 0 && (value.datum_.ptr_ == nullptr || value.datum_.len_ != bytes)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}
}

int ObPLPackageState::add_package_var_val(const DatumParameter &value, ObPLType type)
{
  int ret = OB_SUCCESS;
  OZ (types_.push_back(type));
  if (OB_SUCC(ret) && OB_FAIL(vars_.push_back(value.value_view()))) { types_.pop_back(); }
  return ret;
}

void ObPLPackageState::reset(ObSQLSessionInfo *session_info)
{
  for (int64_t i = 0; i < vars_.count(); ++i) {
    DatumParameter value = package_value_view(vars_.at(i));
    // Cursor pointees use cursor_allocator_; descriptor bytes use the package
    // allocator. Record destruction follows its own nested PL allocators.
    if (PL_CURSOR_TYPE == types_.at(i) && !value.is_null()) {
      ObPLCursorInfo *cursor = reinterpret_cast<ObPLCursorInfo *>(value.get_ext());
      if (cursor != nullptr) {
        if (session_info != nullptr) { cursor->close(*session_info); }
        cursor->~ObPLCursorInfo();
      }
      inner_allocator_.free(const_cast<char *>(value.datum_.ptr_));
    } else {
      int ret = ObUserDefinedType::destruct_objparam(inner_allocator_, value, session_info);
      if (OB_SUCCESS != ret) { LOG_WARN("release native package value failed", K(ret), K(i)); }
    }
  }
  types_.reset();
  vars_.reset();
  inner_allocator_.reset();
  cursor_allocator_.reset();
  package_id_ = common::OB_INVALID_ID;
}

int ObPLPackageState::set_package_var_val(const int64_t var_idx,
                                          const DatumParameter &value,
                                          bool deep_copy_complex,
                                          ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  if (var_idx < 0 || var_idx >= vars_.count()) { return OB_ARRAY_OUT_OF_RANGE; }
  DatumParameter old = package_value_view(vars_.at(var_idx));
  int64_t reusable_bytes = 0;
  if (deep_copy_complex && OB_FAIL(package_reusable_bytes(value, reusable_bytes))) { return ret; }
  if (deep_copy_complex && reusable_bytes > 0 && !old.is_null()
      && old.get_type() == value.get_type() && old.datum_.ptr_ != nullptr
      && old.datum_.len_ == reusable_bytes) {
    // This slot already owns a buffer of exactly the required size. Readers
    // snapshot expression results before another assignment can change it.
    MEMMOVE(const_cast<char *>(old.datum_.ptr_), value.datum_.ptr_, reusable_bytes);
    old.datum_.pack_ = value.datum_.pack_;
    old.set_type_meta(value.get_meta());
    vars_.at(var_idx) = old.value_view();
    return OB_SUCCESS;
  }
  if (value.is_null() && old.is_pl_extend() && types_.at(var_idx) != PL_CURSOR_TYPE) {
    CK (old.get_ext() != 0);
    OZ (ObUserDefinedType::reset_composite(old, session));
    if (OB_SUCC(ret)) { vars_.at(var_idx) = old.value_view(); }
  } else {
    DatumParameter replacement;
    if (value.is_pl_extend() && value.get_extend_type() == PL_CURSOR_TYPE) {
      OZ (replacement.deep_copy(value, inner_allocator_));
    } else if (!deep_copy_complex) {
      // Initialization adopts a value already allocated by this package.
      replacement.assign_value(value);
    } else {
      // Even scalars need package-owned bytes: a Datum may borrow a SQL arena.
      OZ (ObUserDefinedType::clone_parameter(inner_allocator_, value, replacement));
    }
    if (OB_SUCC(ret)) {
      vars_.at(var_idx) = replacement.value_view();
      if (old.datum_.ptr_ != replacement.datum_.ptr_) {
        const bool same_handle = old.is_pl_extend() && replacement.is_pl_extend()
            && old.get_ext() == replacement.get_ext();
        if (same_handle) {
          // Replacing a descriptor must not destroy the live aliased pointee.
          inner_allocator_.free(const_cast<char *>(old.datum_.ptr_));
        } else {
          const int cleanup_ret = ObUserDefinedType::destruct_objparam(inner_allocator_, old, session);
          if (OB_SUCCESS != cleanup_ret) { LOG_WARN("release replaced package value failed", K(cleanup_ret), K(var_idx)); }
        }
      }
    }
  }
  return ret;
}

int ObPLPackageState::get_package_var_val(const int64_t var_idx, DatumParameter &value)
{
  int ret = OB_SUCCESS;
  if (var_idx < 0 || var_idx >= vars_.count()) { ret = OB_ARRAY_OUT_OF_RANGE; }
  else { value.assign_value(package_value_view(vars_.at(var_idx))); }
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
      }
    }
    if (OB_SUCC(ret) && match
        && cur_state_version.body_merge_version_ != state_version.body_merge_version_
        && OB_NOT_NULL(body)) {
      if (OB_FAIL(ObPLDependencyUtil::check_dep_schema(schema_guard,
                                                       body->get_dependency_table(),
                                                       cur_state_version.body_merge_version_,
                                                       match))) {
      }
    }
  }

  return ret;
}

} // namespace pl
} // namespace oceanbase
