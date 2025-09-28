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
 */

#define USING_LOG_PREFIX SHARE
#include "ob_resource_plan_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

ObString oceanbase::share::get_io_function_name(ObFunctionType function_type)
{
  ObString ret_name;
  switch (function_type) {
#define OB_RESOURCE_FUNCTION_TYPE_DEF(function_type_string) \
  case ObFunctionType::PRIO_##function_type_string:         \
    ret_name = ObString(#function_type_string);             \
    break;
#include "ob_resource_plan_info.h"
#undef OB_RESOURCE_FUNCTION_TYPE_DEF
    default:
      ret_name = ObString("OTHER_GROUPS");
      break;
  }
  return ret_name;
}