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

#ifndef OCEANBASE_SHARE_AI_SERVICE_OB_AI_SERVICE_EXECUTOR_H_
#define OCEANBASE_SHARE_AI_SERVICE_OB_AI_SERVICE_EXECUTOR_H_

#include "share/ai_service/ob_ai_service_struct.h"

namespace oceanbase
{
namespace share
{

class ObAiServiceExecutor
{
public:
  ObAiServiceExecutor() = default;
  ~ObAiServiceExecutor() = default;

  // ai endpoint
  static int create_ai_model_endpoint(common::ObArenaAllocator &allocator, const ObString &endpoint_name, const ObIJsonBase &create_jbase);
  static int alter_ai_model_endpoint(ObArenaAllocator &allocator, const ObString &endpoint_name, const ObIJsonBase &alter_jbase);
  static int drop_ai_model_endpoint(const ObString &endpoint_name);
  static int read_ai_endpoint(ObArenaAllocator &allocator, const ObString &endpoint_name, ObAiModelEndpointInfo &endpoint_info);
  static int read_ai_endpoint_by_ai_model_name(ObArenaAllocator &allocator, const ObString &ai_model_name, ObAiModelEndpointInfo &endpoint_info);

private:
  static const int64_t SPECIAL_ENDPOINT_ID_FOR_VERSION;
  static const int64_t INIT_ENDPOINT_VERSION;
  static const char *SPECIAL_ENDPOINT_SCOPE_FOR_VERSION;
  static int construct_new_endpoint(common::ObArenaAllocator &allocator,
                                    const ObAiModelEndpointInfo &old_endpoint,
                                    const ObIJsonBase &alter_jbase,
                                    ObAiModelEndpointInfo &new_endpoint);
  static int fetch_new_ai_model_endpoint_id(const uint64_t tenant_id, uint64_t &new_ai_model_endpoint_id);
  static int lock_and_fetch_endpoint_version(ObMySQLTransaction &trans, const uint64_t tenant_id, int64_t &endpoint_version);
  static int insert_special_endpoint_for_version(ObMySQLTransaction &trans, const uint64_t tenant_id);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_AI_SERVICE_OB_AI_SERVICE_EXECUTOR_H_
