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

#ifndef OCEANBASE_SRC_PL_OB_PL_OBJECT_ID_UTIL_H_
#define OCEANBASE_SRC_PL_OB_PL_OBJECT_ID_UTIL_H_

#include "lib/ob_define.h"
#include "share/rc/ob_tenant_base.h"  // MTL_ID

namespace oceanbase
{
namespace pl
{

OB_INLINE uint64_t get_tenant_id_by_object_id(uint64_t object_id)
{
  object_id = object_id & ~(OB_MOCK_TRIGGER_PACKAGE_ID_MASK);
  object_id = object_id & ~(OB_MOCK_OBJECT_PACAKGE_ID_MASK);
  object_id = object_id & ~(OB_MOCK_PACKAGE_BODY_ID_MASK);
  return is_inner_pl_object_id(object_id) ? OB_SERVER_TENANT_ID : MTL_CTX()->id();
}

}  // namespace pl
}  // namespace oceanbase

#endif /* OCEANBASE_SRC_PL_OB_PL_OBJECT_ID_UTIL_H_ */
