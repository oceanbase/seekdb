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
#include "common/datum/datum_parameter_codec.h"

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
// The generic physical wrapper selects inline versus reference by metadata.
// Pointer identity is meaningful only in the latter domain, including empty
// references; never inspect an inline integer's inactive reference union.
bool package_has_reference(const DatumParameter &value)
{
  if (!value.get_datum().is_present() || value.get_datum().is_inline_extended()) { return false; }
  switch (value.get_type()) {
    case ObTinyIntType: case ObSmallIntType: case ObMediumIntType:
    case ObInt32Type: case ObIntType: case ObDateTimeType: case ObTimestampType:
    case ObDateType: case ObTimeType: case ObMySQLDateType: case ObMySQLDateTimeType:
    case ObUTinyIntType: case ObUSmallIntType: case ObUMediumIntType:
    case ObUInt32Type: case ObUInt64Type: case ObYearType: case ObBitType:
    case ObEnumType: case ObSetType: case ObFloatType: case ObUFloatType:
    case ObDoubleType: case ObUDoubleType: return false;
    default: return true;
  }
}
int package_reusable_bytes(const DatumParameter &value, int64_t &bytes)
{
  int ret = OB_SUCCESS;
  bytes = 0;
  if (!value.is_null() && value.get_meta().is_valid()) {
    int64_t span = 0;
    if (OB_FAIL(DatumParameterCodec::native_span_size(value.get_datum(), value.get_meta(), span))) {
    } else if (ob_is_number_tc(value.get_type()) || value.is_decimal_int()) {
      // Only actual Number/Decimal reference bytes reuse a package buffer.
      // Native scalar assignment carries its bits directly in the descriptor.
      bytes = span;
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
      if (!value.get_datum().is_inline_extended()) {
        inner_allocator_.free(const_cast<void *>(value.get_datum().get_reference()));
      }
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
      && old.get_type() == value.get_type() && package_has_reference(old)
      && old.get_datum().get_reference() != nullptr && old.get_datum().length() == reusable_bytes) {
    // This slot already owns a buffer of exactly the required size. Readers
    // snapshot expression results before another assignment can change it.
    void *buffer = const_cast<void *>(old.get_datum().get_reference());
    Datum updated;
    if (OB_FAIL(updated.set_reference(buffer, static_cast<uint32_t>(reusable_bytes),
        value.get_datum().reference_flags()))) { return ret; }
    MEMMOVE(buffer, value.get_datum().get_reference(), reusable_bytes);
    old.datum_ = updated;
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
      // Reference spans/complex values retain their existing package clone policy;
      // ordinary scalars copy inline and need no payload allocation.
      OZ (ObUserDefinedType::clone_parameter(inner_allocator_, value, replacement));
    }
    if (OB_SUCC(ret)) {
      vars_.at(var_idx) = replacement.value_view();
      const bool same_span = package_has_reference(old) && package_has_reference(replacement)
          && old.get_datum().get_reference() == replacement.get_datum().get_reference();
      if (!same_span) {
        const bool same_handle = old.is_pl_extend() && replacement.is_pl_extend()
            && old.get_ext() == replacement.get_ext();
        if (same_handle) {
          // Replacing a descriptor must not destroy the live aliased pointee.
          if (!old.get_datum().is_inline_extended()) {
            inner_allocator_.free(const_cast<void *>(old.get_datum().get_reference()));
          }
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
