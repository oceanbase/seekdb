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

#define USING_LOG_PREFIX SERVER

#include "ob_table_load_rpc_processor.h"
#include "observer/table_load/ob_table_load_service.h"

namespace oceanbase
{
namespace observer
{

int ObDirectLoadControlP::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableLoadService::direct_load_control(arg_, result_, allocator_))) {
    LOG_WARN("fail to direct load control", KR(ret), K(arg_));
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
