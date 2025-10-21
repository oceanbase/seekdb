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

#ifndef OCEANBASE_SHARE_AI_SERVICE_OB_AI_SERVICE_PROXY_H_
#define OCEANBASE_SHARE_AI_SERVICE_OB_AI_SERVICE_PROXY_H_

#include "share/ai_service/ob_ai_service_struct.h"

namespace oceanbase
{
namespace share
{

class ObAiServiceProxy
{
public:
  ObAiServiceProxy() = default;
  ~ObAiServiceProxy() = default;
  // ai endpoint
  static int insert_ai_endpoint(const uint64_t tenant_id, ObMySQLTransaction &trans, const int64_t new_endpoint_version, const ObAiModelEndpointInfo &endpoint);
  static int select_ai_endpoint(const uint64_t tenant_id, common::ObArenaAllocator &allocator, ObISQLClient &sql_proxy,
                                const ObString &name, ObAiModelEndpointInfo &endpoint, bool for_update = false);
  static int drop_ai_model_endpoint(const uint64_t tenant_id, ObMySQLTransaction &trans, const ObString &name);
  static int update_ai_endpoint(const uint64_t tenant_id, ObMySQLTransaction &trans, const int64_t new_endpoint_version, const ObAiModelEndpointInfo &endpoint);
  static int select_ai_endpoint_by_ai_model_name(const uint64_t tenant_id, common::ObArenaAllocator &allocator, ObISQLClient &sql_proxy,
                                                 const ObString &ai_model_name, common::ObNameCaseMode name_case_mode, ObAiModelEndpointInfo &endpoint);
  static int check_ai_endpoint_exists(const uint64_t tenant_id, common::ObArenaAllocator &allocator, ObISQLClient &sql_proxy, const ObString &name, bool &is_exists);

private:
  // ai endpoint
  static int build_ai_endpoint_(common::ObArenaAllocator &allocator, common::sqlclient::ObMySQLResult &result, ObAiModelEndpointInfo &endpoint);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_AI_SERVICE_OB_AI_SERVICE_PROXY_H_
