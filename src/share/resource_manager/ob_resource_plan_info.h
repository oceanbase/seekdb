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

// Define a function type of resource manager
// Examples:
//   Defination: 
//     DAG_SCHEDULER_DAG_PRIO_DEF(TEST_FUNCTION)
//   Use this function type: 
//     ObFunctionType func_type = ObFunctionType::PRIO_TEST_FUNCTION
//   Get function name: 
//     ObString functiono_name = get_io_function_name(ObFunctionType::PRIO_TEST_FUNCTION)
#ifdef OB_RESOURCE_FUNCTION_TYPE_DEF
// DAG_SCHEDULER_DAG_PRIO_DEF(function_type_string)
OB_RESOURCE_FUNCTION_TYPE_DEF(COMPACTION_HIGH)
OB_RESOURCE_FUNCTION_TYPE_DEF(HA_HIGH)
OB_RESOURCE_FUNCTION_TYPE_DEF(COMPACTION_MID)
OB_RESOURCE_FUNCTION_TYPE_DEF(HA_MID)
OB_RESOURCE_FUNCTION_TYPE_DEF(COMPACTION_LOW)
OB_RESOURCE_FUNCTION_TYPE_DEF(HA_LOW)
OB_RESOURCE_FUNCTION_TYPE_DEF(DDL)
OB_RESOURCE_FUNCTION_TYPE_DEF(DDL_HIGH)
OB_RESOURCE_FUNCTION_TYPE_DEF(GC_MACRO_BLOCK)
OB_RESOURCE_FUNCTION_TYPE_DEF(CLOG_LOW)
OB_RESOURCE_FUNCTION_TYPE_DEF(CLOG_MID)
OB_RESOURCE_FUNCTION_TYPE_DEF(CLOG_HIGH)
OB_RESOURCE_FUNCTION_TYPE_DEF(OPT_STATS)
OB_RESOURCE_FUNCTION_TYPE_DEF(IMPORT)
OB_RESOURCE_FUNCTION_TYPE_DEF(EXPORT)
OB_RESOURCE_FUNCTION_TYPE_DEF(SQL_AUDIT)
OB_RESOURCE_FUNCTION_TYPE_DEF(MICRO_MINI_MERGE)
OB_RESOURCE_FUNCTION_TYPE_DEF(MVIEW)
OB_RESOURCE_FUNCTION_TYPE_DEF(PL_RECOMPILE)
OB_RESOURCE_FUNCTION_TYPE_DEF(REPLAY_HIGH)
#endif

#ifndef OB_SHARE_RESOURCE_MANAGER_OB_PLAN_INFO_H_
#define OB_SHARE_RESOURCE_MANAGER_OB_PLAN_INFO_H_

#include "lib/utility/ob_macro_utils.h"
#include "common/data_buffer.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace share
{
enum ObFunctionType : uint8_t // FARM COMPAT WHITELIST: refine ObFuncType interface
{
  DEFAULT_FUNCTION = 0,
#define OB_RESOURCE_FUNCTION_TYPE_DEF(function_type_string) PRIO_##function_type_string,
#include "ob_resource_plan_info.h"
#undef OB_RESOURCE_FUNCTION_TYPE_DEF
  MAX_FUNCTION_NUM
};
ObString get_io_function_name(ObFunctionType function_type);

}
}
#endif /* OB_SHARE_RESOURCE_MANAGER_OB_PLAN_INFO_H_ */
//// end of header file
