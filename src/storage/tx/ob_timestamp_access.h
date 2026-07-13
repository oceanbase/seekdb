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

#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{

namespace transaction
{
class ObTimestampAccess
{
public:
  ObTimestampAccess() {}
  ~ObTimestampAccess() {}
  static int mtl_init(ObTimestampAccess *&timestamp_access)
  {
    timestamp_access->reset();
    return OB_SUCCESS;
  }
  void destroy() { reset();}
  void reset() {}
  int get_number(int64_t &gts);
  void get_virtual_info(int64_t &ts_value);
};


}
}
#endif
