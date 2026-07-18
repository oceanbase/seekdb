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

#define USING_LOG_PREFIX PL

#include "ob_pl_package_guard.h"
#include "src/pl/ob_pl_package_state.h"

namespace oceanbase
{
namespace pl
{
ObPLPackageGuard::~ObPLPackageGuard()
{
  if (map_.created()) {
    FOREACH(it, map_) {
      if (OB_ISNULL(it->second)) {
      } else {
        it->second->~ObCacheObjGuard();
      }
    }
    map_.destroy();
  }
}

int ObPLPackageGuard::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(map_.create(
        common::hash::cal_next_prime(256),
        common::ObModIds::OB_HASH_BUCKET,
        common::ObModIds::OB_HASH_NODE))) {
    LOG_WARN("failed to create package guard map!", K(ret));
  }
  return ret;
}

} // end namespace pl
} // end namespace oceanbase
