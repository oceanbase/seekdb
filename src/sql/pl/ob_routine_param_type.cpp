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

#include "sql/pl/ob_pl_type.h"

#include "share/schema/ob_routine_info.h"
#include "sql/pl/ob_pl_stmt.h"

namespace oceanbase
{
namespace pl
{

using share::schema::ObIRoutineParam;
using share::schema::ObRoutineParam;

ObPLDataType get_pl_data_type(const ObIRoutineParam &routine_param)
{
  ObPLDataType type;
  if (ObIRoutineParam::Kind::PL_VARIABLE == routine_param.get_kind()) {
    type = static_cast<const ObPLVar &>(routine_param).get_pl_data_type();
  } else if (ObIRoutineParam::Kind::PL_ROUTINE == routine_param.get_kind()) {
    type = static_cast<const ObPLRoutineParam &>(routine_param).get_pl_data_type();
  } else {
    const ObRoutineParam &schema_param =
        static_cast<const ObRoutineParam &>(routine_param);
    if (schema_param.is_pl_integer_type()) {
      type.set_pl_integer_type(schema_param.get_pl_integer_type(),
                               schema_param.get_param_type());
      const ObPLIntegerType pls_type = type.get_pl_integer_type();
      switch (pls_type) {
        case PL_PLS_INTEGER:
        case PL_BINARY_INTEGER:
        case PL_SIMPLE_INTEGER: {
          type.set_range(-2147483648, 2147483647);
          type.set_not_null(PL_SIMPLE_INTEGER == pls_type);
          break;
        }
        case PL_NATURAL:
        case PL_NATURALN: {
          type.set_range(0, 2147483647);
          type.set_not_null(PL_NATURALN == pls_type);
          break;
        }
        case PL_POSITIVE:
        case PL_POSITIVEN: {
          type.set_range(1, 2147483647);
          type.set_not_null(PL_POSITIVEN == pls_type);
          break;
        }
        case PL_SIGNTYPE: {
          type.set_range(-1, 1);
          break;
        }
        default: {
          break;
        }
      }
    } else {
      type.set_data_type(schema_param.get_param_type());
    }
  }
  return type;
}

}  // namespace pl
}  // namespace oceanbase
