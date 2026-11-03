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
#include "sql/pl/ob_pl_resolver.h"
#include "sql/pl/parser/parse_stmt_item_type.h"

namespace oceanbase { namespace sql {
// DDL names declared inputs exactly. Never use call overload resolution here:
// casts, omitted defaults and expanded variadic arguments identify no object.
struct NativeRoutineDdl final
{
  static int resolve_signature(const ParseNode &node, common::ObIAllocator &allocator,
      ObSQLSessionInfo &session, std::string &identity)
  {
    using namespace common;
    using namespace share::schema;
    identity.clear();
    if (node.type_ != T_SP_PARAM_LIST || node.num_child_ < 0 ||
        node.num_child_ > NativeRoutineSignature::MAX_EXPANDED_ARGUMENTS ||
        (node.num_child_ != 0 && !node.children_)) return OB_INVALID_ARGUMENT;
    try {
      std::string value(NativeRoutineSignature::INPUT_IDENTITY_PREFIX);
      for (int64_t i = 0; i < node.num_child_; ++i) {
        const auto *input = node.children_[i];
        if (!input || input->type_ != T_SP_PARAM || input->num_child_ != 1 ||
            !input->children_ || !input->children_[0] || (input->value_ != 0 && input->value_ != 1))
          return OB_INVALID_ARGUMENT;
        const auto *type = input->children_[0];
        if (type->type_ == T_SP_OBJ_ACCESS_REF || type->type_ == T_SP_ROWTYPE) return OB_NOT_SUPPORTED;
        if (input->value_ && i + 1 != node.num_child_) return OB_NOT_SUPPORTED;
        pl::ObPLDataType data_type;
        pl::ObPLEnumSetCtx enum_set(allocator);
        data_type.set_enum_set_ctx(&enum_set);
        int ret = pl::ObPLResolver::resolve_sp_scalar_type(allocator, type, ObString(), session, data_type);
        if (ret != OB_SUCCESS) return ret;
        if (!data_type.get_data_type()) return OB_ERR_UNEXPECTED;
        ObRoutineParam parameter(&allocator);
        parameter.set_param_type(*data_type.get_data_type());
        ObIArray<ObString> *extended = nullptr;
        if ((ret = data_type.get_type_info(extended)) != OB_SUCCESS) return ret;
        if (extended && !extended->empty()) return OB_NOT_SUPPORTED;
        if (input->value_) parameter.set_native_variadic();
        if ((ret = NativeRoutineSignature::append_input_type(parameter, value)) != OB_SUCCESS) return ret;
      }
      identity = std::move(value);
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }

  static int find(share::schema::ObSchemaGetterGuard &guard, uint64_t database,
      const common::ObString &name, const std::string *signature,
      const share::schema::ObRoutineInfo *&result)
  {
    using namespace common;
    using namespace share::schema;
    result = nullptr;
    ObSEArray<const ObRoutineInfo *, 4> candidates;
    int ret = guard.get_standalone_function_infos(database, name, candidates);
    for (int64_t i = 0; OB_SUCC(ret) && i < candidates.count(); ++i) {
      const auto *candidate = candidates.at(i);
      if (!candidate) { ret = OB_ERR_UNEXPECTED; break; }
      if (signature) {
        if (!candidate->is_native()) continue; // Typed references are native-only for now.
        std::string identity;
        if (OB_FAIL(NativeRoutineSignature::input_identity(*candidate, identity))) break;
        if (identity != *signature) continue;
      }
      if (result) ret = OB_ERR_FUNC_DUP;
      else result = candidate;
    }
    if (OB_FAIL(ret)) result = nullptr;
    return ret;
  }
};
} }
