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

#include "ob_timestamp_access.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_timestamp_service.h"
 
namespace oceanbase
{
namespace transaction
{

int ObTimestampAccess::get_number(int64_t &gts)
{
  int ret = OB_SUCCESS;
  ObTimestampProvider provider = provider_.load(std::memory_order_acquire);
  if (nullptr != provider) {
    ret = provider(gts);
  } else {
    ret = ::oceanbase::share::server_service<ObTimestampService>()->get_timestamp(gts);
  }
  return ret;
}

int ObTimestampAccess::get_virtual_info(int64_t &ts_value)
{
  int ret = OB_SUCCESS;
  ObTimestampProvider provider = provider_.load(std::memory_order_acquire);
  if (nullptr != provider) {
    if (OB_FAIL(provider(ts_value))) {
      ts_value = 0;
    }
  } else {
    ret = ::oceanbase::share::server_service<ObTimestampService>()->get_virtual_info(ts_value);
  }
  return ret;
}

}
}
