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

#define USING_LOG_PREFIX SHARE
#include "share/rc/ob_context.h"

using namespace oceanbase::common;
using namespace oceanbase::lib;
namespace oceanbase
{
namespace lib
{
uint64_t current_resource_owner_id()
{
  return CURRENT_ENTITY(RESOURCE_OWNER)->get_owner_id();
}
} // end of namespace lib

namespace share
{


ObResourceOwner &ObResourceOwner::root()
{
  static ObResourceOwner *root = nullptr;
  if (OB_UNLIKELY(nullptr == root)) {
    static lib::ObMutex mutex;
    lib::ObMutexGuard guard(mutex);
    if (nullptr == root) {
      static ObResourceOwner tmp(common::OB_SERVER_RUNTIME_ID);
      int ret = tmp.init();
      abort_unless(OB_SUCCESS == ret);
      root = &tmp;
    }
  }
  return *root;
}

} // end of namespace share
} // end of namespace oceanbase
