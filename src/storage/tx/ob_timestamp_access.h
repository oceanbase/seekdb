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

#ifndef OCEANBASE_TRANSACTION_OB_TIMESTAMP_ACCESS_
#define OCEANBASE_TRANSACTION_OB_TIMESTAMP_ACCESS_

#include <atomic>

#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{

namespace transaction
{
typedef int (*ObTimestampProvider)(int64_t &timestamp);

class ObTimestampAccess
{
public:
  ObTimestampAccess() : provider_(nullptr) {}
  ~ObTimestampAccess() {}
  static int server_module_init(ObTimestampAccess *&timestamp_access)
  {
    timestamp_access->reset();
    return OB_SUCCESS;
  }
  void destroy() { reset();}
  void reset() { provider_.store(nullptr, std::memory_order_release); }
  void set_provider(ObTimestampProvider provider)
  {
    provider_.store(provider, std::memory_order_release);
  }
  int get_number(int64_t &gts);
  int get_virtual_info(int64_t &ts_value);

private:
  std::atomic<ObTimestampProvider> provider_;
};


}
}
#endif
