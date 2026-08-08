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

#ifndef OCEANBASE_QUERY_API_ENGINE_EXPR_OB_AI_MODEL_RESOLVER_H_
#define OCEANBASE_QUERY_API_ENGINE_EXPR_OB_AI_MODEL_RESOLVER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace query
{

// Resolves query-owned AI model metadata without exposing expression objects
// or the SQL schema-resolution implementation to callers.
class ObAIModelResolver
{
public:
  static int resolve_model_name(
      common::ObIAllocator &allocator,
      const common::ObString &model_id,
      common::ObString &model_name);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_EXPR_OB_AI_MODEL_RESOLVER_H_
