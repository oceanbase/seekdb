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

#ifndef OCEANBASE_QUERY_AI_OB_AI_ENDPOINT_RESOLVER_H_
#define OCEANBASE_QUERY_AI_OB_AI_ENDPOINT_RESOLVER_H_

#include "share/ai_service/ob_ai_service_struct.h"

namespace oceanbase
{
namespace query
{

class ObIAiEndpointResolver
{
public:
  virtual ~ObIAiEndpointResolver() = default;
  virtual int resolve_by_model_name(
      const common::ObString &model_name,
      common::ObIAllocator &allocator,
      share::ObAiModelEndpointInfo &endpoint,
      bool check_access = true) const = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_AI_OB_AI_ENDPOINT_RESOLVER_H_
