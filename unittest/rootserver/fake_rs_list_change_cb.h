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

#include "share/partition_table/ob_partition_table_operator.h"
namespace oceanbase
{
namespace share
{
using namespace common;
class FakeRsListChangeCb
{
public:
  int submit_update_rslist_task(const bool force_update)
  {
    UNUSED(force_update);
    return OB_SUCCESS;
  }
};
}
}
