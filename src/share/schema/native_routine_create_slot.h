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
#include "share/schema/native_routine_signature.h"
#include "share/schema/ob_schema_getter_guard.h"
#include <set>

namespace oceanbase { namespace share { namespace schema {

// Call under host DDL admission, before allocating ID/version or staging.
// Slots are layout, not callable identity. A plugin or parsed CREATE cannot
// choose one; routine ID and input signature identify each independent object.
struct NativeRoutineCreateSlot final
{
  static constexpr int64_t MAX_FAMILY = 4096;
  static int select(const ObRoutineInfo &incoming,
      const common::ObIArray<const ObRoutineInfo *> &family, int64_t &slot)
  {
    using namespace common;
    slot = -1;
    if (incoming.get_database_id() == 0 || incoming.get_database_id() > INT64_MAX ||
        incoming.get_owner_id() == 0 || incoming.get_owner_id() > INT64_MAX ||
        incoming.get_routine_name().empty() || incoming.get_routine_name().length() > OB_MAX_ROUTINE_NAME_BINARY_LENGTH)
      return OB_INVALID_ARGUMENT;
    if (family.count() >= MAX_FAMILY) return OB_SIZE_OVERFLOW;
    std::string input;
    int ret = NativeRoutineSignature::input_identity(incoming, input);
    if (ret != OB_SUCCESS) return ret;
    try {
      int64_t next = 0;
      std::set<int64_t> slots;
      std::set<uint64_t> ids;
      std::set<std::string> signatures;
      for (int64_t i = 0; i < family.count(); ++i) {
        const auto *existing = family.at(i);
        if (!existing || existing->get_database_id() != incoming.get_database_id() ||
            existing->get_routine_name().case_compare(incoming.get_routine_name()) != 0 ||
            existing->get_package_id() != OB_INVALID_ID || existing->get_routine_type() != ROUTINE_FUNCTION_TYPE ||
            existing->get_routine_id() == 0 || existing->get_routine_id() > INT64_MAX ||
            existing->get_schema_version() <= 0 || existing->get_overload() < 0) return OB_STATE_NOT_MATCH;
        if (!existing->is_native()) return OB_ERR_SP_ALREADY_EXISTS;
        if (!slots.insert(existing->get_overload()).second || !ids.insert(existing->get_routine_id()).second)
          return OB_STATE_NOT_MATCH;
        std::string signature;
        if ((ret = NativeRoutineSignature::input_identity(*existing, signature)) != OB_SUCCESS) return ret;
        if (!signatures.insert(signature).second) return OB_STATE_NOT_MATCH;
        if (signature == input) return OB_ERR_SP_ALREADY_EXISTS;
        if (existing->get_overload() == INT64_MAX) return OB_SIZE_OVERFLOW;
        next = std::max(next, existing->get_overload() + 1);
      }
      slot = next;
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }
  static int select(ObSchemaGetterGuard &guard, const ObRoutineInfo &incoming, int64_t &slot)
  {
    common::ObSEArray<const ObRoutineInfo *, 4> family;
    slot = -1;
    const int ret = guard.get_standalone_function_infos(incoming.get_database_id(), incoming.get_routine_name(), family);
    return ret == common::OB_SUCCESS ? select(incoming, family, slot) : ret;
  }
  static int assign(ObSchemaGetterGuard &guard, ObRoutineInfo &incoming)
  {
    int64_t slot = -1;
    const int ret = select(guard, incoming, slot);
    if (ret == common::OB_SUCCESS) incoming.set_overload(slot);
    return ret;
  }
};
} } }
