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

#include "share/ob_json_access_utils.h"
#include "share/ob_cluster_version.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/json_type/ob_json_base.h"
#include "common/ob_smart_call.h"
namespace oceanbase
{
using namespace common;
namespace share
{

int ObJsonWrapper::get_raw_binary(ObIJsonBase *j_base, ObString &result, ObIAllocator *allocator)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(allocator) || OB_ISNULL(j_base)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("allocator or j_base is null", K(ret), KP(allocator), KP(j_base));
  } else if (OB_FAIL(SMART_CALL(j_base->get_raw_binary(result, allocator)))) {
    LOG_WARN("get raw binary fail", K(ret));
  }
  return ret;
}


} // end namespace share
} // end namespace oceanbase
