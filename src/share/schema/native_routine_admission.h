/*
 * Copyright (c) 2026 OceanBase.
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

#include "share/schema/ob_routine_info.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include <initializer_list>

namespace oceanbase { namespace share { namespace schema {
class NativeRoutineAdmission final
{
public:
  // Probe the actual SQL catalog through the caller's transaction, not the
  // current binary's generated table templates. Never upgrade tables here or
  // assume that installing a new binary has added the columns to an instance.
  static int check_catalog(common::ObMySQLTransaction &transaction)
  {
    using namespace common;
    if (!transaction.is_started()) return OB_STATE_NOT_MATCH;
    for (const char *sql : {
        "SELECT native_module_id,native_implementation_id,native_abi_version FROM oceanbase.__all_routine WHERE 1=0",
        "SELECT native_module_id,native_implementation_id,native_abi_version FROM oceanbase.__all_routine_history WHERE 1=0"}) {
      ObISQLClient::ReadResult result;
      int ret = transaction.read(result, sql);
      if (ret == OB_SUCCESS) {
        if (result.get_result() == nullptr) ret = OB_ERR_UNEXPECTED;
        else {
          ret = result.get_result()->next();
          ret = ret == OB_ITER_END ? OB_SUCCESS : ret == OB_SUCCESS ? OB_ERR_UNEXPECTED : ret;
        }
      }
      const int close_ret = result.close();
      if (ret != OB_SUCCESS) return ret;
      if (close_ret != OB_SUCCESS) return close_ret;
    }
    return OB_SUCCESS;
  }

  static bool same_binding(const ObRoutineInfo &left, const ObRoutineInfo &right)
  {
    return left.is_native() && right.is_native() &&
        left.get_native_abi_version() == right.get_native_abi_version() &&
        left.get_native_module_id() == right.get_native_module_id() &&
        left.get_native_implementation_id() == right.get_native_implementation_id();
  }

  // Only attribute changes can reuse a dormant module's existing binding.
  // A changed signature or determinism claim must be validated again, even
  // when AS names the same implementation. Ignore assigned ID/schema versions.
  static bool same_signature(const ObRoutineInfo &left, const ObRoutineInfo &right)
  {
    if (!same_binding(left, right) || !left.is_native_binding_valid() || !right.is_native_binding_valid() ||
        !left.get_ret_type() || !right.get_ret_type() ||
        left.is_deterministic() != right.is_deterministic()) return false;
    const auto &a = left.get_routine_params();
    const auto &b = right.get_routine_params();
    if (a.count() != b.count()) return false;
    for (int64_t i = 0; i < a.count(); ++i) {
      if (!a.at(i) || !b.at(i)) return false;
      const auto &left_type = a.at(i)->get_param_type();
      const auto &right_type = b.at(i)->get_param_type();
      // Match the semantic type carried by the existing ObDataType codec.
      // charset_ and is_binary_collation_ are declaration hints omitted by
      // that codec; actual text/binary identity is in meta_'s collation.
      if (!(left_type.get_meta_type() == right_type.get_meta_type()) ||
          !(left_type.get_accuracy() == right_type.get_accuracy()) ||
          left_type.is_zero_fill() != right_type.is_zero_fill() ||
          a.at(i)->get_flag() != b.at(i)->get_flag() || a.at(i)->get_param_position() != b.at(i)->get_param_position() ||
          a.at(i)->get_param_name() != b.at(i)->get_param_name() ||
          a.at(i)->get_default_value() != b.at(i)->get_default_value() ||
          a.at(i)->get_extended_type_info().count() != 0 || b.at(i)->get_extended_type_info().count() != 0)
        return false;
    }
    return true;
  }
};
} } }
