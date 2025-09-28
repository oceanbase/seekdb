/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 * This file contains implementation for lob_access_utils.
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
