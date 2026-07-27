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

#include "ob_trans_id_service.h"

namespace oceanbase
{

namespace transaction
{

int ObTransIDService::server_module_init(ObTransIDService *&trans_id_service)
{
  return trans_id_service->init();
}

int ObTransIDService::init()
{
  service_type_ = ServiceType::TransIDService;
  pre_allocated_range_ = TRANS_ID_PREALLOCATED_RANGE;
  return OB_SUCCESS;
}

}
}
