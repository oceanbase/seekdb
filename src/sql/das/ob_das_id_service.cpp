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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_id_service.h"
namespace oceanbase
{
namespace sql
{
int ObDASIDService::mtl_init(ObDASIDService *&das_id_service)
{
  return das_id_service->init();
}

int ObDASIDService::init()
{
  self_ = GCTX.self_addr();
  service_type_ = ServiceType::DASIDService;
  pre_allocated_range_ = DAS_ID_PREALLOCATED_RANGE;
  return OB_SUCCESS;
}

int ObDASIDService::handle_request(const ObDASIDRequest &request, obcall::ObDASIDRpcResult &result)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!request.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(request));
  } else {
    
    const int64_t range = request.get_range();
    int64_t start_id = 0;
    int64_t end_id = 0;
    if (OB_FAIL(get_number(range, 0, start_id, end_id))) {
    }
    // overwrite ret
    if (OB_FAIL(result.init(ret, start_id, end_id))) {
    }
  }
  // overwrite ret
  return OB_SUCCESS;
}
} // namespace sql
} // namespace oceanbase
