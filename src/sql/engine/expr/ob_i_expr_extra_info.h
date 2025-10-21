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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_I_EXPR_EXTRA_INFO_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_I_EXPR_EXTRA_INFO_
#include <stdint.h>
#include "sql/engine/expr/ob_expr_extra_info_factory.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
namespace sql
{
struct ObIExprExtraInfo
{
  ObIExprExtraInfo(common::ObIAllocator &alloc, const ObExprOperatorType &type)
    : type_(type)
  {
    UNUSED(alloc);
  }

public:
  virtual int serialize(char *buf, const int64_t len, int64_t &pos) const = 0;

  virtual int deserialize(const char *buf, const int64_t len, int64_t &pos) = 0;

  virtual int64_t get_serialize_size() const = 0;

  virtual int deep_copy(common::ObIAllocator &allocator,
                        const ObExprOperatorType type,
                        ObIExprExtraInfo *&copied_info) const = 0;
public:
  ObExprOperatorType type_;
  TO_STRING_KV(K(type_));
};

} // end namespace sql
} // end namespace oceanbase
#endif
