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
#include <string>
#include <new>

namespace oceanbase { namespace share { namespace schema {

// Catalog arity is the number of declarations, never rewritten per call.
// Expanded positional VARIADIC calls repeat the final element type. The native
// C ABI receives the resulting argument vector, not a PG in-process ArrayType.
struct NativeRoutineSignature final
{
  static constexpr int64_t MAX_EXPANDED_ARGUMENTS = 1024;
  static constexpr const char *INPUT_IDENTITY_PREFIX = "native-input-v1";
  // Shared by bound catalog routines and type-only DDL references. Appends
  // one declared input type, never an expanded call argument or a return type.
  static int append_input_type(const ObRoutineParam &parameter, std::string &value)
  {
    using namespace common;
    const auto &data_type = parameter.get_param_type();
    const auto type = data_type.get_obj_type();
    if (parameter.get_extended_type_info().count() != 0 ||
        !(ob_is_geometry(type) || ob_is_integer_type(type) || ob_is_number_tc(type) ||
          ob_is_decimal_int(type) || ob_is_float_type(type) || ob_is_double_type(type) ||
          ob_is_string_or_lob_type(type))) return OB_NOT_SUPPORTED;
    try {
      const auto logical_type = ob_is_decimal_int(type) ? ObNumberType : type;
      std::string part = '/' + std::to_string(static_cast<int>(logical_type));
      part += ob_is_string_or_lob_type(type) && data_type.get_collation_type() == CS_TYPE_BINARY ? 'b' : 't';
      part += parameter.is_native_variadic() ? 'a' : 's';
      value += part;
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }
  // An in-memory input signature, not a persisted hash or a routine ID.
  // Names, defaults, result type, implementation and type modifiers do not
  // distinguish overloads. Binary and character SQL types do distinguish them
  // even where MySQL uses the same ObObjType (BLOB/TEXT, VARBINARY/VARCHAR).
  static int input_identity(const ObRoutineInfo &routine, std::string &identity)
  {
    using namespace common;
    identity.clear();
    if (!routine.is_native() || !routine.is_native_binding_valid() ||
        routine.get_param_count() < 0 || routine.get_param_count() > MAX_EXPANDED_ARGUMENTS)
      return OB_INVALID_ARGUMENT;
    try {
      std::string value(INPUT_IDENTITY_PREFIX);
      for (int64_t i = 0; i < routine.get_param_count(); ++i) {
        ObRoutineParam *parameter = nullptr;
        int ret = routine.get_routine_param(i, parameter);
        if (ret != OB_SUCCESS) return ret;
        if (!parameter || !parameter->is_in_param() || parameter->is_ret_param()) return OB_INVALID_ARGUMENT;
        if ((ret = append_input_type(*parameter, value)) != OB_SUCCESS) return ret;
      }
      identity = std::move(value);
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }
  static bool variadic(const ObIRoutineInfo &routine)
  {
    const auto *native = dynamic_cast<const ObRoutineInfo *>(&routine);
    if (!native || !native->is_native() || native->get_param_count() <= 0) return false;
    ObRoutineParam *last = nullptr;
    return native->get_routine_param(native->get_param_count() - 1, last) == common::OB_SUCCESS &&
           last != nullptr && last->is_native_variadic();
  }

  static int call_count(const ObIRoutineInfo &routine, int64_t supplied, int64_t &count)
  {
    count = routine.get_param_count();
    if (variadic(routine)) {
      // An expanded call supplies at least one element. Passing an explicit
      // array (including an empty one) is a separate, not-yet-enabled path.
      if (supplied < count || supplied > MAX_EXPANDED_ARGUMENTS) return common::OB_ERR_SP_WRONG_ARG_NUM;
      count = supplied;
    }
    return common::OB_SUCCESS;
  }

  static int64_t parameter_index(const ObIRoutineInfo &routine, int64_t index)
  {
    return variadic(routine) && index >= routine.get_param_count()
        ? routine.get_param_count() - 1 : index;
  }
};

} } }
