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

#ifndef OCEANBASE_SHARE_OBJECT_OB_OBJ_CAST_RUNTIME_H_
#define OCEANBASE_SHARE_OBJECT_OB_OBJ_CAST_RUNTIME_H_

#include <stdint.h>
#include "common/object/ob_object.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{

struct ObObjCastParams;

// Runtime semantics that cannot be implemented from Share value types alone.
// Callers own the Adapter and keep it alive for the complete cast operation.
class ObIObjCastRuntime
{
public:
  virtual ~ObIObjCastRuntime() = default;

  virtual int get_enum_set_values(
      uint16_t subschema_id,
      const ObIArray<ObString> *&values,
      ObCollationType &collation_type) const = 0;

  virtual int cast_collection(
      ObObjCastParams &params,
      const ObObj &input,
      ObObj &output,
      uint64_t cast_mode) const = 0;

  virtual void report_warning(
      int64_t code,
      const ObString &type_name,
      const ObString &input,
      uint64_t cast_mode) const = 0;
};

}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_OBJECT_OB_OBJ_CAST_RUNTIME_H_
