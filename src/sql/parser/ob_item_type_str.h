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

#ifndef _OB_ITEM_TYPE_STR_H
#define _OB_ITEM_TYPE_STR_H 1
#include "objit/common/ob_item_type.h"

namespace oceanbase
{
namespace sql
{
inline const char *ob_aggr_func_str(ObItemType aggr_func)
{
  const char *ret = "UNKNOWN_AGGR";
  switch (aggr_func) {
    case T_FUN_MAX:
      ret = "MAX";
      break;
    case T_FUN_MIN:
      ret = "MIN";
      break;
    case T_FUN_COUNT_SUM:
      ret = "COUNT_SUM";
      break;
    case T_FUN_SUM:
      ret = "SUM";
      break;
    case T_FUN_COUNT:
      ret = "COUNT";
      break;
    case T_FUN_AVG:
      ret = "AVG";
      break;
    case T_FUN_GROUPING:
    	ret = "GROUPING";
    	break;
    case T_FUN_GROUPING_ID:
      ret = "GROUPING_ID";
      break;
    case T_FUN_GROUP_ID:
      ret = "GROUP_ID";
      break;
    case T_FUN_GROUP_CONCAT:
      ret = "GROUP_CONCAT";
      break;
    case T_FUN_APPROX_COUNT_DISTINCT:
      ret = "APPROX_COUNT_DISTINCT";
      break;
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS:
      ret = "APPROX_COUNT_DISTINCT_SYNOPSIS";
      break;
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE:
      ret = "APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE";
      break;
    case T_FUN_VARIANCE:
      ret = "VARIANCE";
      break;
    case T_FUN_STDDEV:
      ret = "STDDEV";
      break;
    case T_FUN_SYS_BIT_AND:
      ret = "BIT_AND";
      break;
    case T_FUN_SYS_BIT_OR:
      ret = "BIT_OR";
      break;
    case T_FUN_SYS_BIT_XOR:
      ret = "BIT_XOR";
      break;
    default:
      break;
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_ITEM_TYPE_STR_H */
