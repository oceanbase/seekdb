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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_expr_strcmp.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{


ObExprStrcmp::ObExprStrcmp(ObIAllocator &alloc)
    : ObRelationalExprOperator::ObRelationalExprOperator(alloc, T_FUN_SYS_STRCMP, N_STRCMP, 2, NOT_ROW_DIMENSION)
{
}

int ObExprStrcmp::calc_result_type2(ObExprResType &type,
                                    ObExprResType &type1,
                                    ObExprResType &type2,
                                    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  type.set_int();
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
  ObCollationType coll_type = CS_TYPE_INVALID;
  ObCollationLevel coll_level = CS_LEVEL_EXPLICIT;
  ObExprResTypes res_types;
  if (OB_FAIL(res_types.push_back(type1))) {
    LOG_WARN("fail to push back res type", K(ret));
  } else if (OB_FAIL(res_types.push_back(type2))) {
    LOG_WARN("fail to push back res type", K(ret));
  } else if (OB_FAIL(aggregate_charsets_for_comparison(type, &res_types.at(0), 2, type_ctx))) {
    LOG_WARN("failed to aggregate_charsets_for_comparison", K(ret));
  } else {
    type.set_calc_type(ObVarcharType);
    type1.set_calc_type(ObVarcharType);
    type2.set_calc_type(ObVarcharType);
    type1.set_calc_collation(type);
    type2.set_calc_collation(type);
  }
  return ret;
}

}//end sql namespace
}//end oceanbase namespace

